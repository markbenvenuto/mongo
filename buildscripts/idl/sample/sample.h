/**
 * WARNING: This is a generated file. Do not modify.
 *
 * Source: .\idlc.py --base_dir d:\mongo\buildscripts --header .\sample\sample.h -o
 * .\sample\sample.cpp .\sample\sample.idl
 */

#pragma once

#include <algorithm>
#include <boost/optional.hpp>
#include <cstdint>
#include <string>
#include <vector>

#include "mongo/base/string_data.h"
#include "mongo/bson/bsonobj.h"
#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/db/namespace_string.h"
#include "mongo/idl/idl_parser.h"

namespace mongo {

/**
 * UnitTest for a single safeInt32
 */
class Default_values {
public:
    static Default_values parse(const IDLParserErrorContext& ctxt, const BSONObj& object);
    void serialize(BSONObjBuilder* builder) const;

    /**
     * An example string field with default value
     */
    const StringData getStringfield() const& {
        return _stringfield;
    }
    const StringData getStringfield() && = delete;
    void setStringfield(StringData value) & {
        _stringfield = value.toString();
    }

    /**
     * An example int field with default value
     */
    const std::int32_t getIntfield() const {
        return _intfield;
    }
    void setIntfield(std::int32_t value) & {
        _intfield = std::move(value);
    }

    /**
     * A numeric type that supports multiple types
     */
    const std::int64_t getNumericfield() const {
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
    const boost::optional<StringData> getOptionalField() && = delete;
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
    const std::vector<std::int32_t>& getVectorField() && = delete;
    void setVectorField(std::vector<std::int32_t> value) & {
        _vectorField = std::move(value);
    }

protected:
    void parseProtected(const IDLParserErrorContext& ctxt, const BSONObj& object);

private:
    std::string _stringfield;
    std::int32_t _intfield;
    std::int64_t _numericfield;
    mongo::NamespaceString _nsfield;
    boost::optional<std::string> _optionalField;
    std::vector<std::int32_t> _vectorField;
};

/**
 * UnitTest for a basic ignored command
 */
class BasicIgnoredCommand {
public:
    static BasicIgnoredCommand parse(const IDLParserErrorContext& ctxt, const BSONObj& object);
    void serialize(BSONObjBuilder* builder) const;

    const std::int32_t getField1() const {
        return _field1;
    }
    void setField1(std::int32_t value) & {
        _field1 = std::move(value);
    }

    const StringData getField2() const& {
        return _field2;
    }
    const StringData getField2() && = delete;
    void setField2(StringData value) & {
        _field2 = value.toString();
    }

protected:
    void parseProtected(const IDLParserErrorContext& ctxt, const BSONObj& object);

private:
    std::int32_t _field1;
    std::string _field2;
};

/**
 * UnitTest for a basic concatenate_with_db command
 */
class BasicConcatenateWithDbCommand {
public:
    static BasicConcatenateWithDbCommand parse(const IDLParserErrorContext& ctxt,
                                               StringData dbName,
                                               const BSONObj& object);
    void serialize(const NamespaceString ns, BSONObjBuilder* builder) const;

    const NamespaceString& getNamespace() const {
        return _ns;
    }

    const std::int32_t getField1() const {
        return _field1;
    }
    void setField1(std::int32_t value) & {
        _field1 = std::move(value);
    }

    const StringData getField2() const& {
        return _field2;
    }
    const StringData getField2() && = delete;
    void setField2(StringData value) & {
        _field2 = value.toString();
    }

protected:
    void parseProtected(const IDLParserErrorContext& ctxt,
                        StringData dbName,
                        const BSONObj& object);

private:
    NamespaceString _ns;

    std::int32_t _field1;
    std::string _field2;
};

}  // namespace mongo
