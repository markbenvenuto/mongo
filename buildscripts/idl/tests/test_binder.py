#!/usr/bin/env python2
# Copyright (C) 2017 MongoDB Inc.
#
# This program is free software: you can redistribute it and/or  modify
# it under the terms of the GNU Affero General Public License, version 3,
# as published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
"""Test cases for IDL binder."""

from __future__ import absolute_import, print_function, unicode_literals

import textwrap
import unittest

# import package so that it works regardless of whether we run as a module or file
if __package__ is None:
    import sys
    from os import path
    sys.path.append(path.dirname(path.dirname(path.abspath(__file__))))
    from context import idl
    import testcase
else:
    from .context import idl
    from . import testcase

# All YAML tests assume 4 space indent
INDENT_SPACE_COUNT = 4


def fill_spaces(count):
    # type: (int) -> unicode
    """Fill a string full of spaces."""
    fill = ''
    for _ in range(count * INDENT_SPACE_COUNT):
        fill += ' '

    return fill


def indent_text(count, unindented_text):
    # type: (int, unicode) -> unicode
    """Indent each line of a multi-line string."""
    lines = unindented_text.splitlines()
    fill = fill_spaces(count)
    return '\n'.join(fill + line for line in lines)


class TestBinder(testcase.IDLTestcase):
    """Test cases for the IDL binder."""

    def test_empty(self):
        # type: () -> None
        """Test an empty document works."""
        self.assert_bind("")

    def test_global_positive(self):
        # type: () -> None
        """Postive global tests."""
        spec = self.assert_bind(
            textwrap.dedent("""
        global:
            cpp_namespace: 'something'
            cpp_includes: 
                - 'bar'
                - 'foo'"""))
        self.assertEquals(spec.globals.cpp_namespace, "something")
        self.assertListEqual(spec.globals.cpp_includes, ['bar', 'foo'])

    def test_type_positive(self):
        # type: () -> None
        """Positive type tests."""
        self.assert_bind(
            textwrap.dedent("""
        type:
            name: foo
            description: foo
            cpp_type: foo
            bson_serialization_type: string
            serializer: foo
            deserializer: foo
            default: foo
            """))

        # Test supported types
        for bson_type in [
                "bool", "date", "null", "decimal", "double", "int", "long", "objectid", "regex",
                "string", "timestamp", "undefined"
        ]:
            self.assert_bind(
                textwrap.dedent("""
        type:
            name: foofoo
            description: foo
            cpp_type: foo
            bson_serialization_type: %s
            default: foo
            """ % (bson_type)))

        # Test object
        self.assert_bind(
            textwrap.dedent("""
        type:
            name: foofoo
            description: foo
            cpp_type: foo
            bson_serialization_type: object
            serializer: foo
            deserializer: foo
            default: foo
            """))

        # Test object
        self.assert_bind(
            textwrap.dedent("""
        type:
            name: foofoo
            description: foo
            cpp_type: foo
            bson_serialization_type: any
            serializer: foo
            deserializer: foo
            default: foo
            """))

        # Test supported bindata_subtype
        for bindata_subtype in ["generic", "function", "binary", "uuid_old", "uuid", "md5"]:
            self.assert_bind(
                textwrap.dedent("""
            type:
                name: foofoo
                description: foo
                cpp_type: foo
                bson_serialization_type: bindata
                bindata_subtype: %s
                default: foo
                """ % (bindata_subtype)))

    def test_type_negative(self):
        # type: () -> None
        """Negative type tests for properties that types and fields share."""

        # Test bad bson type name
        self.assert_bind_fail(
            textwrap.dedent("""
            type:
                name: foofoo
                description: foo
                cpp_type: foo
                bson_serialization_type: foo
            """), idl.errors.ERROR_ID_BAD_BSON_TYPE)

        # Test bad cpp_type name
        self.assert_bind_fail(
            textwrap.dedent("""
            type:
                name: foofoo
                description: foo
                cpp_type: StringData
                bson_serialization_type: string
            """), idl.errors.ERROR_ID_NO_STRINGDATA)

        # Test bindata_subtype missing
        self.assert_bind_fail(
            textwrap.dedent("""
            type:
                name: foofoo
                description: foo
                cpp_type: foo
                bson_serialization_type: bindata
            """), idl.errors.ERROR_ID_BAD_BSON_BINDATA_SUBTYPE_VALUE)

        # Test bindata_subtype wrong
        self.assert_bind_fail(
            textwrap.dedent("""
            type:
                name: foofoo
                description: foo
                cpp_type: foo
                bson_serialization_type: bindata
                bindata_subtype: foo
            """), idl.errors.ERROR_ID_BAD_BSON_BINDATA_SUBTYPE_VALUE)

        # Test bindata_subtype on wrong type
        self.assert_bind_fail(
            textwrap.dedent("""
            type:
                name: foofoo
                description: foo
                cpp_type: foo
                bson_serialization_type: string
                bindata_subtype: generic
            """), idl.errors.ERROR_ID_BAD_BSON_BINDATA_SUBTYPE_TYPE)

        # Test bindata in list of types
        self.assert_bind_fail(
            textwrap.dedent("""
            type:
                name: foofoo
                description: foo
                cpp_type: foo
                bson_serialization_type: 
                            - bindata
                            - string
            """), idl.errors.ERROR_ID_BAD_BSON_TYPE)

        # Test bindata in list of types
        self.assert_bind_fail(
            textwrap.dedent("""
            type:
                name: foofoo
                description: foo
                cpp_type: StringData
                bson_serialization_type: 
                            - bindata
                            - string
            """), idl.errors.ERROR_ID_BAD_BSON_TYPE)

        # Test unsupported serialization
        for bson_type in [
                "bool", "date", "null", "decimal", "double", "int", "long", "objectid", "regex",
                "timestamp", "undefined"
        ]:
            self.assert_bind_fail(
                textwrap.dedent("""
                type:
                    name: foofoo
                    description: foo
                    cpp_type: std::string
                    bson_serialization_type: %s
                    serializer: foo
                    """ % (bson_type)),
                idl.errors.ERROR_ID_CUSTOM_SCALAR_SERIALIZATION_NOT_SUPPORTED)

            self.assert_bind_fail(
                textwrap.dedent("""
                type:
                    name: foofoo
                    description: foo
                    cpp_type: std::string
                    bson_serialization_type: %s
                    deserializer: foo
                    """ % (bson_type)),
                idl.errors.ERROR_ID_CUSTOM_SCALAR_SERIALIZATION_NOT_SUPPORTED)

        # Test object serialization needs deserializer & serializer
        self.assert_bind_fail(
            textwrap.dedent("""
            type:
                name: foofoo
                description: foo
                cpp_type: foo
                bson_serialization_type: object
                serializer: foo
            """), idl.errors.ERROR_ID_MISSING_AST_REQUIRED_FIELD)

        self.assert_bind_fail(
            textwrap.dedent("""
            type:
                name: foofoo
                description: foo
                cpp_type: foo
                bson_serialization_type: object
                deserializer: foo
            """), idl.errors.ERROR_ID_MISSING_AST_REQUIRED_FIELD)

        # Test any serialization needs deserializer
        self.assert_bind_fail(
            textwrap.dedent("""
            type:
                name: foofoo
                description: foo
                cpp_type: foo
                bson_serialization_type: any
            """), idl.errors.ERROR_ID_MISSING_AST_REQUIRED_FIELD)

        # Test list of bson types needs deserializer
        self.assert_bind_fail(
            textwrap.dedent("""
            type:
                name: foofoo
                description: foo
                cpp_type: int
                bson_serialization_type: 
                            - int
                            - string
            """), idl.errors.ERROR_ID_MISSING_AST_REQUIRED_FIELD)

        # Test array as name
        self.assert_bind_fail(
            textwrap.dedent("""
            type: 
                name: array<foo>
                description: foo
                cpp_type: foo
                bson_serialization_type: string
            """), idl.errors.ERROR_ID_ARRAY_NOT_VALID_TYPE)

    def test_struct_positive(self):
        # type: () -> None
        """Positive struct tests."""

        # Setup some common types
        test_preamble = textwrap.dedent("""
        type:
            name: string
            description: foo
            cpp_type: foo
            bson_serialization_type: string
            serializer: foo
            deserializer: foo
            default: foo
        """)

        self.assert_bind(test_preamble + textwrap.dedent("""
            struct: 
                name: foo
                description: foo
                strict: true
                fields:
                    foo: string
            """))

    def test_struct_negative(self):
        # type: () -> None
        """Negative struct tests."""

        # Setup some common types
        test_preamble = textwrap.dedent("""
        type:
            name: string
            description: foo
            cpp_type: foo
            bson_serialization_type: string
            serializer: foo
            deserializer: foo
            default: foo
        """)

        # Test array as name
        self.assert_bind_fail(test_preamble + textwrap.dedent("""
            struct: 
                name: array<foo>
                description: foo
                strict: true
                fields:
                    foo: string
            """), idl.errors.ERROR_ID_ARRAY_NOT_VALID_TYPE)

    def test_field_positive(self):
        # type: () -> None
        """Positive test cases for field."""

        # Setup some common types
        test_preamble = textwrap.dedent("""
        type:
            name: string
            description: foo
            cpp_type: foo
            bson_serialization_type: string
            serializer: foo
            deserializer: foo
            default: foo
        """)

        # Short type
        self.assert_bind(test_preamble + textwrap.dedent("""
        struct:
            name: bar
            description: foo
            strict: false
            fields:
                foo: string
            """))

        # Long type
        self.assert_bind(test_preamble + textwrap.dedent("""
        struct:
            name: bar
            description: foo
            strict: false
            fields:
                foo: 
                    type: string
            """))

        # Long type with default
        self.assert_bind(test_preamble + textwrap.dedent("""
        struct:
            name: bar
            description: foo
            strict: false
            fields:
                foo: 
                    type: string
                    default: bar
            """))

    def test_field_negative(self):
        # type: () -> None
        """Negative field tests."""

        # Setup some common types
        test_preamble = textwrap.dedent("""
        type:
            name: string
            description: foo
            cpp_type: foo
            bson_serialization_type: string
            serializer: foo
            deserializer: foo
            default: foo
        """)

        # Test array as field name
        self.assert_bind_fail(test_preamble + textwrap.dedent("""
            struct: 
                name: foo
                description: foo
                strict: true
                fields:
                    array<foo>: string
            """), idl.errors.ERROR_ID_ARRAY_NOT_VALID_TYPE)

    def test_ignored_field_negative(self):
        # type: () -> None
        """Test that if a field is marked as ignored, no other properties are set."""
        for test_value in [
                "optional: true",
        ]:
            self.assert_bind_fail(
                textwrap.dedent("""
            struct:
                name: foo
                description: foo
                strict: false
                fields:
                    foo:
                        type: string
                        ignore: true
                        %s
                """ % (test_value)), idl.errors.ERROR_ID_FIELD_MUST_BE_EMPTY_FOR_IGNORED)


if __name__ == '__main__':

    unittest.main()
