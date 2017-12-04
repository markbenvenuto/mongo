/**
 * WARNING: This is a generated file. Do not modify.
 *
 * Source: .\idlc.py --include d:\mongo\src --base_dir d:\mongo\buildscripts --header
 * .\sample\sample.h -o .\sample\sample.cpp .\sample\sample.idl
 */

#pragma once

#include <algorithm>
#include <boost/optional.hpp>
#include <cstdint>
#include <string>
#include <vector>

#include "mongo/base/data_range.h"
#include "mongo/base/string_data.h"
#include "mongo/bson/bsonobj.h"
#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/db/namespace_string.h"
#include "mongo/idl/idl_parser.h"
#include "mongo/util/net/op_msg.h"

namespace mongo {

/**
 * UnitTest for a single safeInt32
 */
class Default_values {
public:
    static constexpr auto kBinDataFieldFieldName = "binDataField"_sd;
    static constexpr auto kIntfieldFieldName = "intfield"_sd;
    static constexpr auto kNsfieldFieldName = "nsfield"_sd;
    static constexpr auto kNumericfieldFieldName = "numericfield"_sd;
    static constexpr auto kOptionalFieldFieldName = "optionalField"_sd;
    static constexpr auto kStringfieldFieldName = "stringfield"_sd;
    static constexpr auto kUuidFieldFieldName = "uuidField"_sd;
    static constexpr auto kVectorFieldFieldName = "vectorField"_sd;

    Default_values();

    static Default_values parse(const IDLParserErrorContext& ctxt, const BSONObj& bsonObject);
    void serialize(BSONObjBuilder* builder) const;
    BSONObj toBSON() const;

    /**
     * An example string field with default value
     */
    const StringData getStringfield() const& {
        return _stringfield;
    }
    void getStringfield() && = delete;
    void setStringfield(StringData value) & {
        _stringfield = value.toString();
    }

    /**
     * An example int field with default value
     */
    std::int32_t getIntfield() const {
        return _intfield;
    }
    void setIntfield(std::int32_t value) & {
        _intfield = std::move(value);
    }

    /**
     * A numeric type that supports multiple types
     */
    std::int64_t getNumericfield() const {
        return _numericfield;
    }
    void setNumericfield(std::int64_t value) & {
        _numericfield = std::move(value);
        _hasNumericfield = true;
    }

    /**
     * A namespace string type
     */
    const mongo::NamespaceString& getNsfield() const {
        return _nsfield;
    }
    void setNsfield(mongo::NamespaceString value) & {
        _nsfield = std::move(value);
        _hasNsfield = true;
    }

    /**
     * An optional string
     */
    const boost::optional<StringData> getOptionalField() const& {
        return boost::optional<StringData>{_optionalField};
    }
    void getOptionalField() && = delete;
    void setOptionalField(boost::optional<StringData> value) & {
        if (value.is_initialized()) {
            _optionalField = value.get().toString();
        } else {
            _optionalField = boost::none;
        }
    }

    /**
     * An example int array field with default value
     */
    const std::vector<std::int32_t>& getVectorField() const& {
        return _vectorField;
    }
    void getVectorField() && = delete;
    void setVectorField(std::vector<std::int32_t> value) & {
        _vectorField = std::move(value);
        _hasVectorField = true;
    }

    /**
     * A binData of generic subtype
     */
    const ConstDataRange getBinDataField() const& {
        return ConstDataRange(reinterpret_cast<const char*>(_binDataField.data()),
                              _binDataField.size());
    }
    void getBinDataField() && = delete;
    void setBinDataField(ConstDataRange value) & {
        _binDataField = std::vector<std::uint8_t>(reinterpret_cast<const uint8_t*>(value.data()),
                                                  reinterpret_cast<const uint8_t*>(value.data()) +
                                                      value.length());
        _hasBinDataField = true;
    }

    /**
     * A binData of uuid subtype
     */
    const std::array<std::uint8_t, 16> getUuidField() const {
        return _uuidField;
    }
    void setUuidField(std::array<std::uint8_t, 16> value)& {
        _uuidField = std::move(value);
        _hasUuidField = true;
    }

protected:
    void parseProtected(const IDLParserErrorContext& ctxt, const BSONObj& bsonObject);

private:
    std::string _stringfield{"a default"};
    std::int32_t _intfield{42};
    std::int64_t _numericfield;
    mongo::NamespaceString _nsfield;
    boost::optional<std::string> _optionalField;
    std::vector<std::int32_t> _vectorField;
    std::vector<std::uint8_t> _binDataField;
    std::array<std::uint8_t, 16> _uuidField;
    bool _hasNumericfield : 1;
    bool _hasNsfield : 1;
    bool _hasVectorField : 1;
    bool _hasBinDataField : 1;
    bool _hasUuidField : 1;
};

}  // namespace mongo
