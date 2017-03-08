/**
 * Copyright (C) 2017 MongoDB Inc.
 *
 * This program is free software: you can redistribute it and/or  modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, the copyright holders give permission to link the
 * code of portions of this program with the OpenSSL library under certain
 * conditions as described in each individual source file and distribute
 * linked combinations including the program with the OpenSSL library. You
 * must comply with the GNU Affero General Public License in all respects
 * for all of the code used other than as permitted herein. If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so. If you do not
 * wish to do so, delete this exception statement from your version. If you
 * delete this exception statement from all source files in the program,
 * then also delete it in the license file.
 */

#include "mongo/platform/basic.h"

#include <map>

#include "mongo/unittest/unittest.h"
#include "mongo/idl/unittest_gen.hpp"

namespace mongo {

/// Type tests:
// Positive: Test we can serialize the type out and back again
TEST(ClientMetadatTest, TestLoopbackTest) {
    IDLParserErrorContext ctxt("root");

    StringData test_value = "test_value";
    auto testDoc = BSON("value" << test_value);
    // TODO: assert type of field in this generated document
    auto testStruct = One_string::parse(ctxt, testDoc);
    ASSERT_EQUALS(testStruct.getValue(), test_value);

    {
    BSONObjBuilder builder;
    testStruct.serialize(&builder);
    auto loopbackDoc = builder.obj();

    ASSERT_BSONOBJ_EQ(testDoc, loopbackDoc);    
    }

    {
    BSONObjBuilder builder;
    One_string one_new;
    one_new.setValue(test_value);
    testStruct.serialize(&builder);

    auto serializedDoc = builder.obj();
    ASSERT_BSONOBJ_EQ(testDoc, serializedDoc);    
    }
}

// Negative: document with wrong types for required fields
TEST(ClientMetadatTest, TestNegativeWrongTypes) {
    
}

// Positive
// Validate types listed in “Built-in BSON Types” are accepted
// Validate default works

// Negative
// Check type mismatch for types listed in “Built-in BSON Types”
// Check value mismatch for some types listed in “Built-in BSON Types” (like bad NamespaceString)

/// Field tests
// Positive: check if field is missing, it gets a default value
// Positive: struct strict, and ignored field works

/// Struct tests:
// Positive: strict, 3 required fields
// Positive: non-strict, ensure extra fields work
// Negative: strict, ensure extra fields fail
// Negative: strict, duplicate fields
// Negative: non-strict, duplicate fields

// Positive: test empty optional nested struct

/// Array tests
// Validate array parsing
// Check array vs non-array

}  // namespace mongo
