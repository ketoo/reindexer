#pragma once

#include <string>
#include <vector>
#include "core/indexopts.h"
#include "core/type_consts.h"
#include "tools/errors.h"

union JsonValue;

namespace reindexer {

using std::string;
using std::vector;

class string_view;
class WrSerializer;

class JsonPaths : public vector<string> {
public:
	using vector::vector;
	JsonPaths();
	JsonPaths(const string_view &jsonPath);
	void Set(const string_view &other);
	void Set(const vector<string> &other);
	string AsSerializedString() const;
};

struct IndexDef {
	IndexDef();
	IndexDef(const string &name, const string_view &jsonPaths, const string &indexType, const string &fieldType, const IndexOpts opts);
	bool operator==(const IndexDef &) const;
	IndexType Type() const;
	string getCollateMode() const;
	const vector<string> &Conditions() const;
	void FromType(IndexType type);
	Error FromJSON(char *json);
	Error FromJSON(JsonValue &jvalue);
	void GetJSON(WrSerializer &ser, bool describeCompat = false) const;

public:
	string name_;
	JsonPaths jsonPaths_;
	string indexType_;
	string fieldType_;
	IndexOpts opts_;
};

bool isComposite(IndexType type);
bool isFullText(IndexType type);
bool isSortable(IndexType type);

}  // namespace reindexer
