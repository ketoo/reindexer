#include "listener.h"
#include <fcntl.h>
#include <chrono>
#include <cstdlib>
#include <thread>
#include "core/type_consts.h"
#include "net/http/serverconnection.h"
#include "tools/logger.h"

#if REINDEX_WITH_GPERFTOOLS
#include <gperftools/heap-profiler.h>
#include <gperftools/malloc_extension.h>
#include <gperftools/profiler.h>
#else
static void ProfilerRegisterThread(){};
#endif

namespace reindexer {

namespace net {

static atomic<int> counter_;

Listener::Listener(ev::dynamic_loop &loop, std::shared_ptr<Shared> shared) : loop_(loop), shared_(shared), id_(counter_++) {
	io_.set<Listener, &Listener::io_accept>(this);
	io_.set(loop);
	timer_.set<Listener, &Listener::timeout_cb>(this);
	timer_.set(loop);
	timer_.start(5., 5.);
	async_.set<Listener, &Listener::async_cb>(this);
	async_.set(loop);
	async_.start();
	std::lock_guard<std::mutex> lck(shared_->lck_);
	shared_->listeners_.push_back(this);
}

Listener::Listener(ev::dynamic_loop &loop, ConnectionFactory connFactory, int maxListeners)
	: Listener(loop, std::make_shared<Shared>(connFactory, maxListeners ? maxListeners : std::thread::hardware_concurrency())) {}

Listener::~Listener() {
	io_.stop();
	std::lock_guard<std::mutex> lck(shared_->lck_);
	auto it = std::find(shared_->listeners_.begin(), shared_->listeners_.end(), this);
	assert(it != shared_->listeners_.end());
	shared_->listeners_.erase(it);
	shared_->count_--;
}

bool Listener::Bind(string addr) {
	if (shared_->sock_.valid()) {
		return false;
	}

	shared_->addr_ = addr;

	if (shared_->sock_.bind(addr.c_str()) < 0) {
		return false;
	}

	if (shared_->sock_.listen(500) < 0) {
		perror("listen error");
		return false;
	}

	io_.start(shared_->sock_.fd(), ev::READ);
	reserveStack();
	return true;
}

void Listener::io_accept(ev::io & /*watcher*/, int revents) {
	if (ev::ERROR & revents) {
		perror("got invalid event");
		return;
	}

	auto client = shared_->sock_.accept();

	if (!client.valid()) {
		return;
	}

	std::unique_lock<mutex> lck(shared_->lck_);
	if (shared_->idle_.size()) {
		auto conn = std::move(shared_->idle_.back());
		shared_->idle_.pop_back();
		conn->Attach(loop_);
		conn->Restart(client.fd());
		connections_.push_back(std::move(conn));
	} else {
		connections_.push_back(std::unique_ptr<IServerConnection>(shared_->connFactory_(loop_, client.fd())));
	}
	if (shared_->count_ < shared_->maxListeners_) {
		shared_->count_++;
		std::thread th(&Listener::clone, shared_);
		th.detach();
	}
}

void Listener::timeout_cb(ev::periodic &, int) {
	std::unique_lock<mutex> lck(shared_->lck_);

	// Move finished connections to idle connections pool
	for (unsigned i = 0; i < connections_.size();) {
		if (connections_[i]->IsFinished()) {
			connections_[i]->Detach();
			shared_->idle_.push_back(std::move(connections_[i]));
			if (i != connections_.size() - 1) std::swap(connections_[i], connections_.back());
			connections_.pop_back();
			shared_->ts_ = std::chrono::steady_clock::now();
		} else {
			i++;
		}
	}

	// Clear all idle connections, after 300 sec
	if (shared_->idle_.size() && std::chrono::steady_clock::now() - shared_->ts_ > std::chrono::seconds(300)) {
		logPrintf(LogInfo, "Cleanup idle connections. %d cleared", int(shared_->idle_.size()));
		shared_->idle_.clear();
	}

	int curConnCount = connections_.size();

	if (!std::getenv("REINDEXER_NOREBALANCE")) {
		// Try to rebalance
		int minConnCount = INT_MAX;
		auto minIt = shared_->listeners_.begin();

		for (auto it = minIt; it != shared_->listeners_.end(); it++) {
			int connCount = (*it)->connections_.size();
			if (connCount < minConnCount) {
				minIt = it;
				minConnCount = connCount;
			}
		}

		if (minConnCount + 1 < curConnCount) {
			logPrintf(LogInfo, "Rebalance connection from listener %d to %d", id_, (*minIt)->id_);
			auto conn = std::move(connections_.back());
			conn->Detach();
			(*minIt)->connections_.push_back(std::move(conn));
			(*minIt)->async_.send();
			connections_.pop_back();
		}
	}
	if (curConnCount != 0) {
		logPrintf(LogTrace, "Listener(%s) %d stats: %d connections", shared_->addr_.c_str(), id_, curConnCount);
	}
}

void Listener::async_cb(ev::async &watcher) {
	logPrintf(LogInfo, "Listener(%s) %d async received", shared_->addr_.c_str(), id_);
	std::unique_lock<mutex> lck(shared_->lck_);
	for (auto &it : connections_) {
		if (!it->IsFinished()) it->Attach(loop_);
	}
	watcher.loop.break_loop();
}

void Listener::Stop() {
	std::lock_guard<std::mutex> lck(shared_->lck_);
	shared_->terminating_ = true;
	for (auto listener : shared_->listeners_) {
		listener->async_.send();
	}
	if (shared_->listeners_.size() && this == shared_->listeners_.front()) {
		while (shared_->listeners_.size() != 1) {
			shared_->lck_.unlock();
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			shared_->lck_.lock();
		}
	}
}

void Listener::Fork(int clones) {
	for (int i = 0; i < clones; i++) {
		std::thread th(&Listener::clone, shared_);
		th.detach();
		shared_->count_++;
	}
}

void Listener::clone(std::shared_ptr<Shared> shared) {
	ev::dynamic_loop loop;
	Listener listener(loop, shared);
	ProfilerRegisterThread();
	listener.io_.start(listener.shared_->sock_.fd(), ev::READ);
	while (!listener.shared_->terminating_) {
		loop.run();
	}
}

void Listener::reserveStack() {
	char placeholder[0x8000];
	for (size_t i = 0; i < sizeof(placeholder); i += 4096) placeholder[i] = i & 0xFF;
}

Listener::Shared::Shared(ConnectionFactory connFactory, int maxListeners)
	: maxListeners_(maxListeners), count_(1), connFactory_(connFactory), terminating_(false) {}

Listener::Shared::~Shared() { sock_.close(); }

}  // namespace net
}  // namespace reindexer
