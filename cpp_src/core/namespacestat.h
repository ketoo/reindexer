#pragma once

#include <stdlib.h>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>

namespace reindexer {

class WrSerializer;

struct LRUCacheMemStat {
	void GetJSON(WrSerializer &ser);

	size_t totalSize = 0;
	size_t itemsCount = 0;
	size_t emptyCount = 0;
	size_t hitCountLimit = 0;
};

struct IndexMemStat {
	void GetJSON(WrSerializer &ser);
	std::string name;
	size_t uniqKeysCount = 0;
	size_t dataSize = 0;
	size_t idsetBTreeSize = 0;
	size_t idsetPlainSize = 0;
	size_t sortOrdersSize = 0;
	size_t fulltextSize = 0;
	size_t columnSize = 0;
	LRUCacheMemStat idsetCache;
};

struct NamespaceMemStat {
	void GetJSON(WrSerializer &ser);

	std::string name;
	std::string storagePath;
	bool storageOK = false;
	unsigned long long updatedUnixNano = 0;
	size_t itemsCount = 0;
	size_t emptyItemsCount = 0;
	size_t dataSize = 0;
	struct {
		size_t dataSize = 0;
		size_t indexesSize = 0;
		size_t cacheSize = 0;
	} Total;
	LRUCacheMemStat joinCache;
	LRUCacheMemStat queryCache;
	std::vector<IndexMemStat> indexes;
};

struct PerfStat {
	void GetJSON(WrSerializer &ser);
	size_t totalHitCount;
	size_t totalTimeUs;
	size_t totalLockTimeUs;
	size_t avgHitCount;
	size_t avgTimeUs;
	size_t avgLockTimeUs;
};

struct NamespacePerfStat {
	void GetJSON(WrSerializer &ser);
	std::string name;
	PerfStat updates;
	PerfStat selects;
};

}  // namespace reindexer
