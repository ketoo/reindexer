#include "payloadtype.h"
#include "core/keyvalue/keyvalue.h"

namespace reindexer {

size_t PayloadTypeImpl::TotalSize() const {
	if (fields_.size()) {
		return fields_.back().Offset() + fields_.back().Sizeof();
	}
	return 0;
}

string PayloadTypeImpl::ToString() const {
	string ret;

	for (auto &f : fields_) {
		ret += string(KeyValue::TypeName(f.Type())) + (f.IsArray() ? "[]" : "") + " '" + f.Name() + "'" + " json:\"";
		for (auto &jp : f.JsonPaths()) ret += jp + ";";
		ret += "\"\n";
	}
	return ret;
}

void PayloadTypeImpl::Add(PayloadFieldType f) {
	auto it = fieldsByName_.find(f.Name());
	if (it != fieldsByName_.end()) {
		// Non unique name -> check type, and upgrade to array if types are the same
		auto &oldf = fields_[it->second];
		if (oldf.Type() != f.Type())
			throw Error(errLogic, "Can't add field with name '%s' and type '%s' to namespace '%s'. It already exists with type '%s'",
						f.Name().c_str(), KeyValue::TypeName(f.Type()), Name().c_str(), KeyValue::TypeName(oldf.Type()));
		// Add json paths
		for (auto &jp : f.JsonPaths()) {
			if (!jp.length()) continue;
			auto res = fieldsByJsonPath_.emplace(jp, it->second);
			if (!res.second && res.first->second != it->second) {
				throw Error(errLogic, "Can't add field with name '%s' to namespace '%s'. Json path '%s' already used in field '%s'",
							f.Name().c_str(), Name().c_str(), jp.c_str(), Field(res.first->second).Name().c_str());
			}
			oldf.AddJsonPath(jp);
		}
		// Upgrade to array
		oldf.SetArray();
		// Move offsets of followed fields
		for (size_t i = it->second + 1; i < fields_.size(); ++i) {
			fields_[i].SetOffset(fields_[i - 1].Offset() + fields_[i - 1].Sizeof());
		}
	} else {
		// Unique name -> just add field
		f.SetOffset(TotalSize());
		for (auto &jp : f.JsonPaths()) {
			if (!jp.length()) continue;
			auto res = fieldsByJsonPath_.emplace(jp, int(fields_.size()));
			if (!res.second && res.first->second != int(fields_.size())) {
				throw Error(errLogic, "Can't add field with name '%s' to namespace '%s'. Json path '%s' already used in field '%s'",
							f.Name().c_str(), Name().c_str(), jp.c_str(), Field(res.first->second).Name().c_str());
			}
		}
		fieldsByName_.emplace(f.Name(), int(fields_.size()));
		if (f.Type() == KeyValueString) {
			strFields_.push_back(int(fields_.size()));
		}
		fields_.push_back(f);
	}
}

bool PayloadTypeImpl::Drop(const string &field) {
	auto it = fieldsByName_.find(field);
	if (it == fieldsByName_.end()) return false;

	int fieldIdx = it->second;
	for (auto &it : fieldsByName_) {
		if (it.second > fieldIdx) --it.second;
	}
	for (auto &it : fieldsByJsonPath_) {
		if (it.second > fieldIdx) --it.second;
	}

	KeyValueType fieldType = fields_[fieldIdx].Type();
	for (auto it = strFields_.begin(); it != strFields_.end();) {
		if ((*it == fieldIdx) && (fieldType == KeyValueString)) {
			it = strFields_.erase(it);
			continue;
		} else if (*it > fieldIdx)
			--(*it);
		++it;
	}

	fieldsByJsonPath_.erase(field);
	fieldsByName_.erase(field);

	fields_.erase(fields_.begin() + fieldIdx);
	for (size_t idx = static_cast<size_t>(fieldIdx); idx < fields_.size(); ++idx) {
		const PayloadFieldType &plTypePrev(fields_[idx - 1]);
		fields_[idx].SetOffset(plTypePrev.Offset() + plTypePrev.Sizeof());
	}

	return true;
}

bool PayloadTypeImpl::Contains(const string &field) const { return fieldsByName_.find(field) != fieldsByName_.end(); }

int PayloadTypeImpl::FieldByName(const string &field) const {
	auto it = fieldsByName_.find(field);
	if (it == fieldsByName_.end()) throw Error(errLogic, "Field '%s' not found in namespace '%s'", field.c_str(), Name().c_str());
	return it->second;
}

bool PayloadTypeImpl::FieldByName(const string &name, int &field) const {
	auto it = fieldsByName_.find(name);
	if (it == fieldsByName_.end()) return false;
	field = it->second;
	return true;
}

int PayloadTypeImpl::FieldByJsonPath(const string &jsonPath) const {
	auto it = fieldsByJsonPath_.find(jsonPath);
	if (it == fieldsByJsonPath_.end()) return -1;
	return it->second;
}

void PayloadTypeImpl::serialize(WrSerializer &ser) const {
	ser.PutVarUint(base_key_string::export_hdr_offset());
	ser.PutVarUint(NumFields());
	for (int i = 0; i < NumFields(); i++) {
		ser.PutVarUint(Field(i).Type());
		ser.PutVString(Field(i).Name());
		ser.PutVarUint(Field(i).Offset());
		ser.PutVarUint(Field(i).ElemSizeof());
		ser.PutVarUint(Field(i).IsArray());
	}
}

void PayloadTypeImpl::deserialize(Serializer &ser) {
	fields_.clear();
	fieldsByName_.clear();
	fieldsByJsonPath_.clear();
	strFields_.clear();

	ser.GetVarUint();

	int count = ser.GetVarUint();

	for (int i = 0; i < count; i++) {
		KeyValueType t = KeyValueType(ser.GetVarUint());
		string name = ser.GetVString().ToString();
		int offset = ser.GetVarUint();
		int elemSizeof = ser.GetVarUint();
		bool isArray = ser.GetVarUint();
		(void)elemSizeof;

		PayloadFieldType ft(t, name, name, isArray);
		if (isArray) ft.SetArray();
		ft.SetOffset(offset);
		fieldsByName_.emplace(name, fields_.size());
		if (t == KeyValueString) strFields_.push_back(fields_.size());
		fields_.push_back(ft);
	}
}

}  // namespace reindexer
