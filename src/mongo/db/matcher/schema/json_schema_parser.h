
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

#include <unordered_map>
#include <algorithm>
#include <functional>
#include <numeric>

#include "mongo/db/matcher/expression.h"
#include "mongo/db/matcher/expression_tree.h"
#include "mongo/db/matcher/expression_type.h"


namespace mongo {
    struct EncryptionPath {
            std::vector<std::string> path;

    };

    inline bool operator==(const EncryptionPath& lhs, const EncryptionPath rhs) {
        return lhs.path == rhs.path;
    }
}

namespace std
{
    template<> struct hash<mongo::EncryptionPath>
    {
        typedef mongo::EncryptionPath argument_type;
        typedef std::size_t result_type;
        result_type operator()(argument_type const& s) const noexcept
        {
            return std::accumulate<decltype(argument_type::path)::const_iterator, std::size_t>(cbegin(s.path), cend(s.path), 0,
            []( size_t s, std::string a) { return s ^ std::hash<std::string>()(a); });
        }
    };

}

namespace mongo {

    class EncryptionInfo {
        public:
        std::string placeholder;

        std::string toString() const {  return placeholder; }
    };



    class JSONSchemaContext {
        public:
            // TODO: create a struct of properties
            void addEncryptionInformation(StringData name);
            void pushPath(StringData path, bool isArray);
            void popPath();

            std::unordered_map<EncryptionPath, EncryptionInfo>& keys() { return _map; };
        private:
            std::vector<std::string> _paths;

            std::unordered_map<EncryptionPath, EncryptionInfo> _map;
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
    static StatusWithMatchExpression parse(BSONObj schema, bool ignoreUnknownKeywords = false, JSONSchemaContext* encryptionPaths = nullptr);
};

}  // namespace mongo
