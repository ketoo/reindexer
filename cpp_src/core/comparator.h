#pragma once

#include <functional>
#include <type_traits>
#include <unordered_set>
#include "core/index/payload_map.h"
#include "core/indexopts.h"
#include "core/keyvalue/keyvalue.h"
#include "core/payload/fieldsset.h"
#include "core/type_consts.h"
#include "tools/stringstools.h"

namespace reindexer {

using reindexer::lower;
using std::unordered_set;
using std::reference_wrapper;
using std::shared_ptr;

template <class T>
class ComparatorImpl {
public:
	ComparatorImpl() {}
	void SetValues(CondType cond, const KeyValues &values) {
		if (cond == CondSet) {
			valuesS_.reset(new unordered_set<T>());
		}

		convertedStrings_.clear();
		KeyValueType thisType = type();

		for (const KeyValue &key : values) {
			if (thisType == key.Type()) {
				addValue(cond, static_cast<T>(static_cast<KeyRef>(key)));
			} else {
				if ((key.Type() == KeyValueString) && (!is_number(static_cast<p_string>(key).toString()))) {
					addValue(cond, T());
				} else {
					switch (thisType) {
						case KeyValueString: {
							convertedStrings_.push_back(key.As<string>());
							p_string value(convertedStrings_.back().c_str());
							addValue(cond, static_cast<T>(KeyRef(value)));
							break;
						}
						case KeyValueInt:
							addValue(cond, static_cast<T>(static_cast<KeyRef>(key.As<int>())));
							break;
						case KeyValueInt64:
							addValue(cond, static_cast<T>(KeyRef(key.As<int64_t>())));
							break;
						case KeyValueDouble:
							addValue(cond, static_cast<T>(KeyRef(key.As<double>())));
							break;
						default:
							std::abort();
					}
				}
			}
		}
	}

	bool Compare(CondType cond, const T &lhs) {
		const T &rhs = values_[0];
		switch (cond) {
			case CondEq:
				return lhs == rhs;
			case CondGe:
				return lhs >= rhs;
			case CondLe:
				return lhs <= rhs;
			case CondLt:
				return lhs < rhs;
			case CondGt:
				return lhs > rhs;
			case CondRange:
				return lhs >= rhs && lhs <= values_[1];
			case CondSet:
				return valuesS_->find(lhs) != valuesS_->end();
			default:
				abort();
		}
	}

	bool Compare(CondType cond, const p_string &lhs, const CollateOpts &collateOpts) {
		const p_string &rhs = values_[0];
		switch (cond) {
			case CondEq:
				return collateCompare(string_view(lhs), string_view(rhs), collateOpts) == 0;
			case CondGe:
				return collateCompare(string_view(lhs), string_view(rhs), collateOpts) >= 0;
			case CondLe:
				return collateCompare(string_view(lhs), string_view(rhs), collateOpts) <= 0;
			case CondLt:
				return collateCompare(string_view(lhs), string_view(rhs), collateOpts) < 0;
			case CondGt:
				return collateCompare(string_view(lhs), string_view(rhs), collateOpts) > 0;
			case CondRange:
				return collateCompare(string_view(lhs), string_view(rhs), collateOpts) >= 0 &&
					   collateCompare(string_view(lhs), string_view(values_[1]), collateOpts) <= 0;
			case CondSet:
				if (collateOpts.mode == CollateNone) return valuesS_->find(lhs) != valuesS_->end();
				for (auto it : *valuesS_) {
					if (!collateCompare(string_view(lhs), string_view(it), collateOpts)) return true;
				}
				return false;
			default:
				abort();
		}
	}

	h_vector<T, 2> values_;
	shared_ptr<unordered_set<T>> valuesS_;
	h_vector<string> convertedStrings_;

private:
	KeyValueType type() {
		if (std::is_same<T, p_string>::value) return KeyValueString;
		if (std::is_same<T, int>::value) return KeyValueInt;
		if (std::is_same<T, int64_t>::value) return KeyValueInt64;
		if (std::is_same<T, double>::value) return KeyValueDouble;
		std::abort();
	}

	void addValue(CondType cond, const T &value) {
		if (cond == CondSet) {
			valuesS_->emplace(value);
		} else {
			values_.push_back(value);
		}
	}
};

template <>
class ComparatorImpl<PayloadValue> {
public:
	ComparatorImpl(const PayloadType &payloadType, const FieldsSet &fields) : payloadType_(payloadType), fields_(fields) {}

	void SetValues(CondType cond, const KeyValues &values) {
		if (cond == CondSet) {
			valuesSet_.reset(new unordered_payload_set(0, hash_composite(payloadType_, fields_), equal_composite(payloadType_, fields_)));
		}
		for (const KeyValue &kv : values) {
			if (kv.Type() == KeyValueComposite) {
				const PayloadValue &pv(kv);
				addValue(cond, pv);
			} else {
				partOfCjsonFieldSelect_ = true;
				break;
			}
		}
	}

	bool Compare(CondType cond, PayloadValue &leftValue, const CollateOpts &collateOpts) {
		if (partOfCjsonFieldSelect_) return false;
		assert(!values_.empty() || !valuesSet_->empty());
		assert(fields_.size() > 0);
		PayloadValue *rightValue(&values_[0]);
		Payload lhs(payloadType_, leftValue);
		switch (cond) {
			case CondEq:
				return (lhs.Compare(*rightValue, fields_, collateOpts) == 0);
			case CondGe:
				return (lhs.Compare(*rightValue, fields_, collateOpts) >= 0);
			case CondGt:
				return (lhs.Compare(*rightValue, fields_, collateOpts) > 0);
			case CondLe:
				return (lhs.Compare(*rightValue, fields_, collateOpts) <= 0);
			case CondLt: {
				return (lhs.Compare(*rightValue, fields_, collateOpts) < 0);
			}
			case CondRange: {
				PayloadValue *upperValue(&values_[1]);
				return (lhs.Compare(*rightValue, fields_, collateOpts) >= 0) && (lhs.Compare(*upperValue, fields_, collateOpts) <= 0);
			}
			case CondSet:
				return valuesSet_->find(leftValue) != valuesSet_->end();
			default:
				abort();
		}
	}

	bool partOfCjsonFieldSelect_ = false;

	PayloadType payloadType_;
	FieldsSet fields_;
	h_vector<PayloadValue, 2> values_;
	shared_ptr<unordered_payload_set> valuesSet_;

private:
	void addValue(CondType cond, const PayloadValue &pv) {
		if (cond == CondSet) {
			valuesSet_->emplace(pv);
		} else {
			values_.push_back(pv);
		}
	}
};

class Comparator {
public:
	Comparator();
	Comparator(CondType cond, KeyValueType type, const KeyValues &values, bool isArray, PayloadType payloadType, const FieldsSet &fields,
			   void *rawData = nullptr, const CollateOpts &collateOpts = CollateOpts());
	~Comparator();

	bool Compare(const PayloadValue &lhs, int rowId);
	void Bind(PayloadType type, int field);

protected:
	bool compare(const KeyRef &kr) {
		switch (kr.Type()) {
			case KeyValueInt:
				return cmpInt.Compare(cond_, static_cast<int>(kr));
			case KeyValueInt64:
				return cmpInt64.Compare(cond_, static_cast<int64_t>(kr));
			case KeyValueDouble:
				return cmpDouble.Compare(cond_, static_cast<double>(kr));
			case KeyValueString:
				return cmpString.Compare(cond_, static_cast<p_string>(kr), collateOpts_);
			case KeyValueComposite: {
				const PayloadValue &pl = static_cast<const PayloadValue &>(kr);
				return cmpComposite.Compare(cond_, const_cast<PayloadValue &>(pl), collateOpts_);
			}
			default:
				abort();
		}
	}

	bool compare(void *ptr) {
		switch (type_) {
			case KeyValueInt:
				return cmpInt.Compare(cond_, *static_cast<int *>(ptr));
			case KeyValueInt64:
				return cmpInt64.Compare(cond_, *static_cast<int64_t *>(ptr));
			case KeyValueDouble:
				return cmpDouble.Compare(cond_, *static_cast<double *>(ptr));
			case KeyValueString:
				return cmpString.Compare(cond_, *static_cast<p_string *>(ptr), collateOpts_);
			case KeyValueComposite:
				return cmpComposite.Compare(cond_, *static_cast<PayloadValue *>(ptr), collateOpts_);
			default:
				abort();
		}
	}

	void setValues(const KeyValues &values);

	ComparatorImpl<int> cmpInt;
	ComparatorImpl<int64_t> cmpInt64;
	ComparatorImpl<double> cmpDouble;
	ComparatorImpl<p_string> cmpString;

	CondType cond_ = CondEq;
	KeyValueType type_ = KeyValueUndefined;
	size_t offset_ = 0;
	size_t sizeof_ = 0;
	bool isArray_ = false;
	uint8_t *rawData_ = nullptr;
	CollateOpts collateOpts_;

	PayloadType payloadType_;
	FieldsSet fields_;
	ComparatorImpl<PayloadValue> cmpComposite;
};

}  // namespace reindexer
