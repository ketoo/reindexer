#include <csignal>
#include <limits>
#include "args/args.hpp"
#include "client/reindexer.h"
#include "core/reindexer.h"
#include "dbwrapper.h"
#include "debug/backtrace.h"
#include "reindexer_version.h"
#include "tools/logger.h"

using args::Options;
using namespace reindexer_tool;

int llevel;

void InstallLogLevel(const vector<string>& args) {
	try {
		llevel = std::stoi(args.back());
		if ((llevel < 1) || (llevel > 5)) {
			throw std::out_of_range("value must be in range 1..5");
		}
	} catch (std::invalid_argument&) {
		throw args::UsageError("Value must be integer.");
	} catch (std::out_of_range& exc) {
		std::cout << "WARNING: " << exc.what() << std::endl << "Logging level set to 3" << std::endl;
		llevel = 3;
	}

	reindexer::logInstallWriter([](int level, char* buf) {
		if (level <= llevel) {
			std::cout << buf << std::endl;
		}
	});
}

int main(int argc, char* argv[]) {
	backtrace_init();

	args::ArgumentParser parser("Reindexer client tool");
	args::HelpFlag help(parser, "help", "show this message", {'h', "help"});

	args::Group progOptions("options");
	args::ValueFlag<string> dbDsn(progOptions, "DSN", "DSN to 'reindexer'. Can be 'cproto://<ip>:<port>/<dbname>' or 'builtin://<path>'",
								  {'d', "dsn"}, "", Options::Single | Options::Global | Options::Required);
	args::ValueFlag<string> fileName(progOptions, "FILENAME", "execute commands from file, then exit", {'f', "filename"}, "",
									 Options::Single | Options::Global);
	args::ValueFlag<string> command(progOptions, "COMMAND", "run only single command (SQL or internal) and exit'", {'c', "command"}, "",
									Options::Single | Options::Global);
	args::ValueFlag<string> outFileName(progOptions, "FILENAME", "send query results to file", {'o', "output"}, "",
										Options::Single | Options::Global);

	args::ActionFlag logLevel(progOptions, "INT=1..5", "reindexer logging level", {'l', "log"}, 1, &InstallLogLevel,
							  Options::Single | Options::Global);

	args::GlobalOptions globals(parser, progOptions);

	try {
		parser.ParseCLI(argc, argv);
	} catch (const args::Help&) {
		std::cout << parser;
	} catch (const args::Error& e) {
		std::cerr << "ERROR: " << e.what() << std::endl;
		std::cout << parser.Help() << std::endl;
		return 1;
	} catch (reindexer::Error& re) {
		std::cerr << "ERROR: " << re.what() << std::endl;
		return 1;
	}

	string dsn = args::get(dbDsn);
	bool ok = false;
	Error err;
#ifndef _WIN32
	signal(SIGPIPE, SIG_IGN);
#endif
	if (!args::get(command).length() && !args::get(fileName).length())
		std::cout << "Reindexer command line tool version " << REINDEX_VERSION << std::endl;

	if (dsn.compare(0, 9, "cproto://") == 0) {
		reindexer::client::ReindexerConfig config;
		config.ConnPoolSize = 1;
		DBWrapper<reindexer::client::Reindexer> db(args::get(outFileName), args::get(fileName), args::get(command), config);
		err = db.Connect(dsn);
		if (err.ok()) ok = db.Run();
	} else if (dsn.compare(0, 10, "builtin://") == 0) {
		DBWrapper<reindexer::Reindexer> db(args::get(outFileName), args::get(fileName), args::get(command));
		err = db.Connect(dsn);
		if (err.ok()) ok = db.Run();
	} else {
		std::cerr << "Invalid DSN formt: " << dsn << " Must begin from  cproto:// or builtin://" << std::endl;
	}
	if (!err.ok()) {
		std::cerr << "ERROR: " << err.what() << std::endl;
	}

	return ok ? 0 : 2;
}
