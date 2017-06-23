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

namespace mongo {

/**
 * UnitTest for a single string
 */
class One_string {
public:
    static constexpr auto kValueFieldName = "value"_sd;

    static One_string parse(const IDLParserErrorContext& ctxt, const BSONObj& bsonObject);
    void serialize(BSONObjBuilder* builder) const;
    BSONObj toBSON() const;

    const StringData getValue() const& {
        return _value;
    }
    void getValue() && = delete;
    void setValue(StringData value) & {
        _value = value.toString();
    }

protected:
    void parseProtected(const IDLParserErrorContext& ctxt, const BSONObj& bsonObject);

private:
    std::string _value;
};

/**
 * UnitTest for a single safeInt32
 */
class Default_values {
public:
    static constexpr auto kBinDataFieldFieldName = "binDataField"_sd;
    static constexpr auto kIntfieldFieldName = "intfield"_sd;
    static constexpr auto kNsfieldFieldName = "nsfield"_sd;
    static constexpr auto kNumericfieldFieldName = "numericfield"_sd;
    static constexpr auto kObjectsFieldName = "objects"_sd;
    static constexpr auto kOptionalFieldFieldName = "optionalField"_sd;
    static constexpr auto kStringfieldFieldName = "stringfield"_sd;
    static constexpr auto kStructsFieldName = "structs"_sd;
    static constexpr auto kUuidFieldFieldName = "uuidField"_sd;
    static constexpr auto kVectorFieldFieldName = "vectorField"_sd;

    static Default_values parse(const IDLParserErrorContext& ctxt,
                                StringData dbName,
                                const BSONObj& bsonObject);
    void serialize(const NamespaceString& ns, BSONObjBuilder* builder) const;
    BSONObj toBSON(const NamespaceString& ns) const;

    const NamespaceString& getNamespace() const {
        return _ns;
    }

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
    }

    /**
     * A namespace string type
     */
    const mongo::NamespaceString& getNsfield() const {
        return _nsfield;
    }
    void setNsfield(mongo::NamespaceString value) & {
        _nsfield = std::move(value);
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
    }

    /**
     * A binData of uuid subtype
     */
    const std::array<std::uint8_t, 16> getUuidField() const {
        return _uuidField;
    }
    void setUuidField(std::array<std::uint8_t, 16> value)& {
        _uuidField = std::move(value);
    }

    const std::vector<One_string>& getStructs() const& {
        return _structs;
    }
    void getStructs() && = delete;
    void setStructs(std::vector<One_string> value) & {
        _structs = std::move(value);
    }

    const std::vector<mongo::BSONObj>& getObjects() const& {
        return _objects;
    }
    void getObjects() && = delete;
    void setObjects(std::vector<mongo::BSONObj> value) & {
        _objects = std::move(value);
    }

protected:
    void parseProtected(const IDLParserErrorContext& ctxt,
                        StringData dbName,
                        const BSONObj& bsonObject);

private:
    NamespaceString _ns;

    std::string _stringfield{"a default"};
    std::int32_t _intfield{42};
    std::int64_t _numericfield;
    mongo::NamespaceString _nsfield;
    boost::optional<std::string> _optionalField;
    std::vector<std::int32_t> _vectorField;
    std::vector<std::uint8_t> _binDataField;
    std::array<std::uint8_t, 16> _uuidField;
    std::vector<One_string> _structs;
    std::vector<mongo::BSONObj> _objects;
};

}  // namespace mongo
