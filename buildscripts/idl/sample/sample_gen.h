/**
 * WARNING: This is a generated file. Do not modify.
 *
 * Source: idl/idlc.py idl/sample/sample.idl
 */

#pragma once

#include <algorithm>
#include <boost/optional.hpp>
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

#include "mongo/base/data_range.h"
#include "mongo/base/string_data.h"
#include "mongo/bson/bsonobj.h"
#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/bson/simple_bsonobj_comparator.h"
#include "mongo/db/namespace_string.h"
#include "mongo/idl/idl_parser.h"
#include "mongo/rpc/op_msg.h"

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
    Default_values(std::int64_t numericfield, mongo::NamespaceString nsfield, std::vector<std::int32_t> vectorField, std::vector<std::uint8_t> binDataField, std::array<std::uint8_t, 16> uuidField);

    static Default_values parse(const IDLParserErrorContext& ctxt, const BSONObj& bsonObject);
    void serialize(BSONObjBuilder* builder) const;
    BSONObj toBSON() const;

    /**
     * An example string field with default value
     */
    const StringData getStringfield() const& { return _stringfield; }
    void getStringfield() && = delete;
    void setStringfield(StringData value) & { auto _tmpValue = value.toString();  _stringfield = std::move(_tmpValue);  }

    /**
     * An example int field with default value
     */
    std::int32_t getIntfield() const { return _intfield; }
    void setIntfield(std::int32_t value) & {  _intfield = std::move(value);  }

    /**
     * A numeric type that supports multiple types
     */
    std::int64_t getNumericfield() const { return _numericfield; }
    void setNumericfield(std::int64_t value) & {  _numericfield = std::move(value); _hasNumericfield = true; }

    /**
     * A namespace string type
     */
    const mongo::NamespaceString& getNsfield() const { return _nsfield; }
    void setNsfield(mongo::NamespaceString value) & {  _nsfield = std::move(value); _hasNsfield = true; }

    /**
     * An optional string
     */
    const boost::optional<StringData> getOptionalField() const& { return boost::optional<StringData>{_optionalField}; }
    void getOptionalField() && = delete;
    void setOptionalField(boost::optional<StringData> value) & { if (value.is_initialized()) {
        auto _tmpValue = value.get().toString();
        
        _optionalField = std::move(_tmpValue);
    } else {
        _optionalField = boost::none;
    }
      }

    /**
     * An example int array field with default value
     */
    const std::vector<std::int32_t>& getVectorField() const& { return _vectorField; }
    void getVectorField() && = delete;
    void setVectorField(std::vector<std::int32_t> value) & {  _vectorField = std::move(value); _hasVectorField = true; }

    /**
     * A binData of generic subtype
     */
    const ConstDataRange getBinDataField() const& { return ConstDataRange(reinterpret_cast<const char*>(_binDataField.data()), _binDataField.size()); }
    void getBinDataField() && = delete;
    void setBinDataField(ConstDataRange value) & { auto _tmpValue = std::vector<std::uint8_t>(reinterpret_cast<const uint8_t*>(value.data()), reinterpret_cast<const uint8_t*>(value.data()) + value.length());  _binDataField = std::move(_tmpValue); _hasBinDataField = true; }

    /**
     * A binData of uuid subtype
     */
    const std::array<std::uint8_t, 16> getUuidField() const { return _uuidField; }
    void setUuidField(std::array<std::uint8_t, 16> value) & {  _uuidField = std::move(value); _hasUuidField = true; }

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

/**
 * UnitTest for a basic concatenate_with_db_or_uuid command
 */
class BasicConcatenateWithDbOrUUIDCommand {
public:
    static constexpr auto kDbNameFieldName = "$db"_sd;
    static constexpr auto kField1FieldName = "field1"_sd;
    static constexpr auto kField2FieldName = "field2"_sd;
    static constexpr auto kCommandName = "BasicConcatenateWithDbOrUUIDCommand"_sd;

    explicit BasicConcatenateWithDbOrUUIDCommand(const NamespaceStringOrUUID nssOrUUID);
    BasicConcatenateWithDbOrUUIDCommand(const NamespaceStringOrUUID nssOrUUID, std::int32_t field1, std::string field2);

    static BasicConcatenateWithDbOrUUIDCommand parse(const IDLParserErrorContext& ctxt, const BSONObj& bsonObject);
    static BasicConcatenateWithDbOrUUIDCommand parse(const IDLParserErrorContext& ctxt, const OpMsgRequest& request);
    void serialize(const BSONObj& commandPassthroughFields, BSONObjBuilder* builder) const;
    OpMsgRequest serialize(const BSONObj& commandPassthroughFields) const;
    BSONObj toBSON(const BSONObj& commandPassthroughFields) const;

    const NamespaceStringOrUUID& getNamespaceOrUUID() const { return _nssOrUUID; }

    std::int32_t getField1() const { return _field1; }
    void setField1(std::int32_t value) & {  _field1 = std::move(value); _hasField1 = true; }

    const StringData getField2() const& { return _field2; }
    void getField2() && = delete;
    void setField2(StringData value) & { auto _tmpValue = value.toString();  _field2 = std::move(_tmpValue); _hasField2 = true; }

    const StringData getDbName() const& { return _dbName; }
    void getDbName() && = delete;
    void setDbName(StringData value) & { auto _tmpValue = value.toString();  _dbName = std::move(_tmpValue); _hasDbName = true; }

protected:
    void parseProtected(const IDLParserErrorContext& ctxt, const BSONObj& bsonObject);
    void parseProtected(const IDLParserErrorContext& ctxt, const OpMsgRequest& request);

private:
    static const std::vector<StringData> _knownBSONFields;
    static const std::vector<StringData> _knownOP_MSGFields;


    NamespaceStringOrUUID _nssOrUUID;

    std::int32_t _field1;
    std::string _field2;
    std::string _dbName;
    bool _hasField1 : 1;
    bool _hasField2 : 1;
    bool _hasDbName : 1;
};

}  // namespace mongo
