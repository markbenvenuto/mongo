
/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include <vector>

#include <string>

#include "mongo/base/data_range.h"
#include "mongo/bson/bsonelement.h"
#include "mongo/bson/bsonobj.h"
#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/uuid.h"
#include "mongo/idl/idl_parser.h"

namespace mongo {

/**
 * Simple class that demonstrates the contract a class must implement to parse an IDL "any" type.
 */
class KeyId {
public:
    enum class Type {
        kUUIDs,
        kJSONPointer,
    };

    KeyId() = default;
    KeyId(StringData key) : _strKeyId(key), _type(Type::kJSONPointer)
    {}

    static KeyId parseFromBSON(const BSONElement& element) {
        KeyId keyid;

        if (element.type() == String) {
            keyid._type = Type::kJSONPointer;
            keyid._strKeyId = element.String();
        } else if (element.type() == Array) {
            std::uint32_t expectedFieldNumber{0};
            const IDLParserErrorContext arrayCtxt("key"_sd);

            const BSONObj arrayObject = element.Obj();
            for (const auto& arrayElement : arrayObject) {
                const auto arrayFieldName = arrayElement.fieldNameStringData();
                std::uint32_t fieldNumber;

                Status status = parseNumberFromString(arrayFieldName, &fieldNumber);
                if (status.isOK()) {
                    if (fieldNumber != expectedFieldNumber) {
                        arrayCtxt.throwBadArrayFieldNumberSequence(fieldNumber, expectedFieldNumber);
                    }

                    if (arrayCtxt.checkAndAssertBinDataType(arrayElement, bdtUUID)) {
                        IDLParserErrorContext tempContext("key"_sd, &arrayCtxt);
                        const auto localObject = arrayElement.Obj();
                        keyid._uuids.emplace_back( UUID::parse(localObject));
                    }
                }
                else {
                    arrayCtxt.throwBadArrayFieldNumberValue(arrayFieldName);
                }
                ++expectedFieldNumber;
            }
        } else {
            // TODO - make better error
            uassertStatusOK(
                Status(ErrorCodes::BadValue, "Expected either string or array of UUID for KeyId"));
        }
        return keyid;
    }

    /**
     * Serialize this class as a field in a document.
     */
    void serializeToBSON(StringData fieldName, BSONObjBuilder* builder) const {
        builder->append(fieldName, _strKeyId);
    }

    Type type() const {
        return _type;
    }
    const std::vector<UUID>& UUIDs() const {
        dassert(_type == Type::kUUIDs);
        return _uuids;
    }
    StringData jsonPointer() const {
        dassert(_type == Type::kJSONPointer);
        return _strKeyId;
    }

private:
    std::string _strKeyId;
    std::vector<UUID> _uuids;

    Type _type;
};

class NormalizedKeyId {
    friend class EncryptionInfoNormalized;

public:
    enum class Type {
        kUUID,
        kValue,
    };

    // TODO: - remove ::gen
    NormalizedKeyId() : _uuid(UUID::gen()) {}

    NormalizedKeyId(UUID uuid) : _uuid(uuid), _type(Type::kUUID) {}
    NormalizedKeyId(BSONElement value) : _value(value), _uuid(UUID::gen()),  _type(Type::kValue) {}

    static NormalizedKeyId parseFromBSON(const BSONElement& element) {
        // Not supported - DO NOT CALL
        invariant(false);
        return NormalizedKeyId();
    }

    /**
     * Serialize this class as a field in a document.
     */
    void serializeToBSON(StringData fieldName, BSONObjBuilder* builder) const {
        if (_type == Type::kUUID) {
            _uuid.appendToBuilder(builder, fieldName);
        } else {
            builder->appendAs(_value, fieldName);
        }
    }

private:
    BSONElement _value;

    UUID _uuid;

    Type _type;
};


}  // namespace mongo
