
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

#include <algorithm>
#include <functional>
#include <numeric>
#include <unordered_map>

#include "mongo/db/matcher/expression.h"
#include "mongo/db/matcher/expression_tree.h"
#include "mongo/db/matcher/expression_type.h"
#include "mongo/util/mongoutils/str.h"
#include "mongo/db/matcher/schema/encrypt_schema_gen.h"

namespace mongo {
struct EncryptionPath {
    std::vector<std::string> path;
};

inline bool operator==(const EncryptionPath& lhs, const EncryptionPath& rhs) {
    return lhs.path == rhs.path;
}
}

namespace std {
template <>
struct hash<mongo::EncryptionPath> {
    typedef mongo::EncryptionPath argument_type;
    typedef std::size_t result_type;
    result_type operator()(argument_type const& s) const noexcept {
        return std::accumulate<decltype(argument_type::path)::const_iterator, std::size_t>(
            cbegin(s.path), cend(s.path), 0, [](size_t s, std::string a) {
                return s ^ std::hash<std::string>()(a);
            });
    }
};
}

namespace mongo {

template <typename T>
std::string vectorToString2(const std::vector<T>& list) {
    StringBuilder str;
    for (const auto& entry : list) {
        str << "[" << entry << "]";
    }

    return str.str();
}


// class EncryptionInfo {
// public:
// enum class Algorithm {
//     TBD_Deterministic = 0,
//     TBD_Random = 1,
// };
//     std::string name;
//     std::vector<std::string> path;

//     Algorithm algo{Algorithm::TBD_Deterministic};
//     bool deterministic{false};
//     std::vector<uint8_t> iv;

//     std::string keyVault;

//     std::string keyId;

//     std::string toString() const {
//         std::string s1 = str::stream() << "name: " << name << ", path: " << vectorToString2(path) << ", algo: " << algo
//                                        << ", deterministic: " << deterministic << ", keyVault: "
//                                        << keyVault << ", keyId: " << keyId;

//         return s1;
//     }

//     BSONObj toBSON() const {
//         BSONObjBuilder builder;
//         builder.append("name", name);
//         builder.append("path", path);
//         builder.append("algo", algo);


// builder.append("deeterministic", deterministic  );
// builder.append("iv", iv);
// builder.append("keyVault", keyVault);
// builder.append("keyId", keyId);

// return builder.obj();
//     }
// };


class JSONSchemaContext {
public:
    // TODO: create a struct of properties
    void addEncryptionInformation(EncryptionInfoNormalized ei);
    void pushPath(StringData path, bool isArray);
    void popPath();

    boost::optional<EncryptionInfoNormalized> findField(const FieldRef& path);

    std::unordered_map<EncryptionPath, EncryptionInfoNormalized>& keys() {
        return _map;
    };

    std::vector<std::string> paths() const {  return _paths; }

private:
    std::vector<std::string> _paths;

    std::unordered_map<EncryptionPath, EncryptionInfoNormalized> _map;
};

class JSONSchemaParser {
public:
    // Primitive type name constants.
    static constexpr StringData kSchemaTypeArray = "array"_sd;
    static constexpr StringData kSchemaTypeBoolean = "boolean"_sd;
    static constexpr StringData kSchemaTypeNull = "null"_sd;
    static constexpr StringData kSchemaTypeObject = "object"_sd;
    static constexpr StringData kSchemaTypeString = "string"_sd;

    // Explicitly unsupported type name constants.
    static constexpr StringData kSchemaTypeInteger = "integer"_sd;

    /**
     * Converts a JSON schema, represented as BSON, into a semantically equivalent match expression
     * tree. Returns a non-OK status if the schema is invalid or cannot be parsed.
     */
    static StatusWithMatchExpression parse(BSONObj schema,
                                           bool ignoreUnknownKeywords = false,
                                           JSONSchemaContext* encryptionPaths = nullptr);
};

}  // namespace mongo
