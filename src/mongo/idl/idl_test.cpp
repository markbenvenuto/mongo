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

template<
    typename ParserT,
    typename TestT,
    BSONType Test_bson_type
> 
void TestLoopback(TestT test_value) {
    IDLParserErrorContext ctxt("root");

    auto testDoc = BSON("value" << test_value);

    auto element = testDoc.firstElement();
    ASSERT_EQUALS(element.type(), Test_bson_type);

    auto testStruct = ParserT::parse(ctxt, testDoc);
    ASSERT_EQUALS(testStruct.getValue(), test_value);

    // Positive: Test we can roundtrip from the just parsed document
    {
        BSONObjBuilder builder;
        testStruct.serialize(&builder);
        auto loopbackDoc = builder.obj();

        ASSERT_BSONOBJ_EQ(testDoc, loopbackDoc);
    }

    // Positive: Test we can serialize from nothing the same document
    {
        BSONObjBuilder builder;
        ParserT one_new;
        one_new.setValue(test_value);
        testStruct.serialize(&builder);

        auto serializedDoc = builder.obj();
        ASSERT_BSONOBJ_EQ(testDoc, serializedDoc);
    }
}

/// Type tests:
// Positive: Test we can serialize the type out and back again
TEST(IDLOneTypeTests, TestLoopbackTest) {
    TestLoopback<One_string, StringData, String>("test_value");
    TestLoopback<One_int, std::int32_t, NumberInt>(123);
    TestLoopback<One_long, std::int64_t, NumberLong>(456);
    TestLoopback<One_double, double, NumberDouble>(3.14159);
    //TestLoopback<One_string, Decimal, String>("test_value");
    TestLoopback<One_bool, bool, Bool>(true);
    TestLoopback<One_objectid, OID, jstOID>(OID::max());
    TestLoopback<One_date, Date_t, Date>(Date_t::now());
    TestLoopback<One_timestamp, Timestamp, bsonTimestamp>(Timestamp::max());
}

// Test if a given value for a given bson document parses successfully or fails if the bson types
// mismatch.
template<
    typename ParserT,
    BSONType Parser_bson_type,
    typename TestT,
    BSONType Test_bson_type
>
void TestParse(TestT test_value) {
    IDLParserErrorContext ctxt("root");

    auto testDoc = BSON("value" << test_value);

    auto element = testDoc.firstElement();
    ASSERT_EQUALS(element.type(), Test_bson_type);

    if (Parser_bson_type != Test_bson_type) {
        ASSERT_THROWS(ParserT::parse(ctxt, testDoc), UserException);
    } else {
        (void)ParserT::parse(ctxt, testDoc);
    }
}

// Test each of types either fail or succeeded based on the parser's bson type
template<
    typename ParserT,
    BSONType Parser_bson_type
>
void TestParsers() {
    TestParse<ParserT, Parser_bson_type, StringData, String>("test_value");
    TestParse<ParserT, Parser_bson_type, std::int32_t, NumberInt>(123);
    TestParse<ParserT, Parser_bson_type, std::int64_t, NumberLong>(456);
    TestParse<ParserT, Parser_bson_type, double, NumberDouble>(3.14159);
    //TestParse<ParserT, Parser_bson_type, Decimal,String>("test_value");
    TestParse<ParserT, Parser_bson_type, bool, Bool>(true);
    TestParse<ParserT, Parser_bson_type, OID, jstOID>(OID::max());
    TestParse<ParserT, Parser_bson_type, Date_t, Date>(Date_t::now());
    TestParse<ParserT, Parser_bson_type, Timestamp, bsonTimestamp>(Timestamp::max());
}

// Negative: document with wrong types for required field
TEST(IDLOneTypeTests, TestNegativeWrongTypes) {
    TestParsers<One_string, String>();
    TestParsers<One_int, NumberInt>();
    TestParsers<One_long, NumberLong>();
    TestParsers<One_double, NumberDouble>();
    //TestParsers<One_string, String>();
    TestParsers<One_bool, Bool>();
    TestParsers<One_objectid, jstOID>();
    TestParsers<One_date, Date>();
    TestParsers<One_timestamp, bsonTimestamp>();
}

// Mixed: test a type that accepts multiple bson types
TEST(IDLOneTypeTests, TestSafeInt32) {
    TestParse<One_safeint32, NumberInt, StringData, String>("test_value");
    TestParse<One_safeint32, NumberInt, std::int32_t, NumberInt>(123);
    TestParse<One_safeint32, NumberLong, std::int64_t, NumberLong>(456);
    TestParse<One_safeint32, NumberDouble, double, NumberDouble>(3.14159);
    //TestParse<One_safeint32, Decimal, Decimal,String>("test_value");
    TestParse<One_safeint32, NumberInt, bool, Bool>(true);
    TestParse<One_safeint32, NumberInt, OID, jstOID>(OID::max());
    TestParse<One_safeint32, NumberInt, Date_t, Date>(Date_t::now());
    TestParse<One_safeint32, NumberInt, Timestamp, bsonTimestamp>(Timestamp::max());
}

// Mixed: test a type that accepts NamespaceString
TEST(IDLOneTypeTests, TestNamespaceString) {
    IDLParserErrorContext ctxt("root");

    auto testDoc = BSON("value" << "foo.bar");

    auto element = testDoc.firstElement();
    ASSERT_EQUALS(element.type(), String);

    auto testStruct = One_namespacestring::parse(ctxt, testDoc);
    ASSERT_EQUALS(testStruct.getValue(), NamespaceString("foo.bar"));

    // Positive: Test we can roundtrip from the just parsed document
    {
        BSONObjBuilder builder;
        testStruct.serialize(&builder);
        auto loopbackDoc = builder.obj();

        ASSERT_BSONOBJ_EQ(testDoc, loopbackDoc);
    }

    // Positive: Test we can serialize from nothing the same document
    {
        BSONObjBuilder builder;
        One_namespacestring one_new;
        one_new.setValue(NamespaceString("foo.bar"));
        testStruct.serialize(&builder);

        auto serializedDoc = builder.obj();
        ASSERT_BSONOBJ_EQ(testDoc, serializedDoc);
    }

    // Negative: invalid namespace
    {
        auto testBadDoc = BSON("value" << StringData("foo\0bar", 7));

        ASSERT_THROWS(One_namespacestring::parse(ctxt, testBadDoc), UserException);
    }
}

// Positive
// Validate default works

// Negative
// Check value mismatch for some types listed in “Built-in BSON Types” (like bad NamespaceString)


/// Struct tests:
// Positive: strict, 3 required fields
// Negative: strict, ensure extra fields fail
// Negative: strict, duplicate fields
TEST(IDLStructTests, TestStrictStruct) {
    IDLParserErrorContext ctxt("root");

    // Positive: Just 3 required fields
    {
        auto testDoc = BSON("field1" << 12 << "field2" << 123 << "field3" << 1234);
        auto testStruct = RequiredStrictField3::parse(ctxt, testDoc);
    }

    // Negative: Missing 1 required field
    {
        auto testDoc = BSON("field2" << 123 << "field3" << 1234);
        ASSERT_THROWS(RequiredStrictField3::parse(ctxt, testDoc), UserException);
    }
    {
        auto testDoc = BSON("field1" << 12 << "field3" << 1234);
        ASSERT_THROWS(RequiredStrictField3::parse(ctxt, testDoc), UserException);
    }
    {
        auto testDoc = BSON("field1" << 12 << "field2" << 123);
        ASSERT_THROWS(RequiredStrictField3::parse(ctxt, testDoc), UserException);
    }

    // Negative: Extra field
    {
        auto testDoc = BSON("field1" << 12 << "field2" << 123 << "field3" << 1234 << "field4" << 1234);
        ASSERT_THROWS(RequiredStrictField3::parse(ctxt, testDoc), UserException);
    }

    // Negative: Duplicate field
    {
        auto testDoc = BSON("field1" << 12 << "field2" << 123 << "field3" << 1234 << "field2" << 12345);
        ASSERT_THROWS(RequiredStrictField3::parse(ctxt, testDoc), UserException);
    }
}
// Positive: non-strict, ensure extra fields work
// Negative: non-strict, duplicate fields
TEST(IDLStructTests, TestNonStrictStruct) {
    IDLParserErrorContext ctxt("root");

    // Positive: Just 3 required fields
    {
        auto testDoc = BSON("field1" << 12 << "field2" << 123 << "field3" << 1234);
        auto testStruct = RequiredNonStrictField3::parse(ctxt, testDoc);
    }

    // Negative: Missing 1 required field
    {
        auto testDoc = BSON("field2" << 123 << "field3" << 1234);
        ASSERT_THROWS(RequiredNonStrictField3::parse(ctxt, testDoc), UserException);
    }
    {
        auto testDoc = BSON("field1" << 12 << "field3" << 1234);
        ASSERT_THROWS(RequiredNonStrictField3::parse(ctxt, testDoc), UserException);
    }
    {
        auto testDoc = BSON("field1" << 12 << "field2" << 123);
        ASSERT_THROWS(RequiredNonStrictField3::parse(ctxt, testDoc), UserException);
    }

    // Positive: Extra field
    {
        auto testDoc = BSON("field1" << 12 << "field2" << 123 << "field3" << 1234 << "field4" << 1234);
        auto testStruct = RequiredNonStrictField3::parse(ctxt, testDoc);
    }

    // Negative: Duplicate field
    {
        auto testDoc = BSON("field1" << 12 << "field2" << 123 << "field3" << 1234 << "field2" << 12345);
        ASSERT_THROWS(RequiredNonStrictField3::parse(ctxt, testDoc), UserException);
    }

    // Negative: Duplicate extra field
    {
        auto testDoc = BSON("field4" << 1234 << "field1" << 12 << "field2" << 123 << "field3" << 1234 << "field4" << 1234);
        ASSERT_THROWS(RequiredNonStrictField3::parse(ctxt, testDoc), UserException);
    }
}

// TODO: Positive: test empty optional nested struct

/// Field tests
// TODO: Positive: check if field is missing, it gets a default value

// Mixed: struct strict, and ignored field works
TEST(IDLFieldTests, TestStrictStructIgnoredField) {
    IDLParserErrorContext ctxt("root");

    // Positive: Just a required field
    {
        auto testDoc = BSON("required_field" << 12);
        auto testStruct = IgnoredField::parse(ctxt, testDoc);
        ASSERT_EQUALS(testStruct.getRequired_field(), 12);
    }

    // Positive: required field + ignored_field
    {
        auto testDoc = BSON("required_field" << 123 << "ignored_field" << 1234);
        auto testStruct = IgnoredField::parse(ctxt, testDoc);
        ASSERT_EQUALS(testStruct.getRequired_field(), 123);
    }

    // Negative: just ignored_field
    {
        auto testDoc = BSON("ignored_field" << 1234);
        ASSERT_THROWS(IgnoredField::parse(ctxt, testDoc), UserException);
    }

    // Negative: required field + ignored_field misspelled
    {
        auto testDoc = BSON("required_field" << 123 << "ignored_field_wrong" << 1234);
        ASSERT_THROWS(IgnoredField::parse(ctxt, testDoc), UserException);
    }

    // Negative: required field + duplicate ignored_field
    {
        auto testDoc = BSON("required_field" << 123 << "ignored_field" << 1234 << "ignored_field" << 1234);
        ASSERT_THROWS(IgnoredField::parse(ctxt, testDoc), UserException);
    }
}


/// TODO:  Array tests
// Validate array parsing
// Check array vs non-array

}  // namespace mongo
