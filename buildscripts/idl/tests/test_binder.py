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
    fill = ''
    for _ in range(count * INDENT_SPACE_COUNT):
        fill += ' '

    return fill


def indent_text(count, unindented_text):
    # type: (int, unicode) -> unicode
    lines = unindented_text.splitlines()
    fill = fill_spaces(count)
    return '\n'.join(fill + line for line in lines)


class Test_Binder(testcase.IDLTestcase):
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

        # TODO: validate namespace
        # TODO: validate includes prefixed with mongo???

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

    def assert_bind_type_properties(self, doc_str):
        # type: (unicode) -> None
        """Test type properties for both types an fields."""
        t1 = indent_text(1, doc_str)
        self.assert_bind(
            textwrap.dedent("""
            type:
                name: foofoo
                """) + indent_text(1, doc_str))

        self.assert_bind(
            textwrap.dedent("""
            type:
                name: string
                description: foo
                cpp_type: foo
                bson_serialization_type: string
                serializer: foo
                deserializer: foo
                default: foo

            struct:
                name: foofoo
                description: test
                fields:
                    bar:
                        type: string
                """) + indent_text(3, doc_str))

    def assert_bind_type_properties_fail(self, doc_str, error_id):
        # type: (unicode, unicode) -> None
        """Test type properties for both types an fields."""
        t1 = indent_text(1, doc_str)
        self.assert_bind_fail(
            textwrap.dedent("""
            type:
                name: foofoo
                """) + indent_text(1, doc_str), error_id)

        self.assert_bind_fail(
            textwrap.dedent("""
            type:
                name: string
                description: foo
                cpp_type: foo
                bson_serialization_type: string
                serializer: foo
                deserializer: foo
                default: foo

            struct:
                name: foofoo
                description: test
                fields:
                    bar:
                        type: string
                """) + indent_text(3, doc_str), error_id)

    def test_type_and_field_common_positive(self):
        # type: () -> None
        """Positive type tests for properties that types and fields share."""

        # Test supported types
        for bson_type in [
                "bool", "date", "null", "decimal", "double", "int", "long",
                "object", "objectid", "regex", "string", "timestamp", "undefined"
        ]:
            self.assert_bind_type_properties(
                textwrap.dedent("""
                description: foo
                cpp_type: foo
                bson_serialization_type: %s
                serializer: foo
                deserializer: foo
                default: foo
                """ % (bson_type)))

        # Test supported bindata_subtype
        for bindata_subtype in ["generic", "function", "binary", "uuid_old", "uuid", "md5"]:
            self.assert_bind_type_properties(
                textwrap.dedent("""
                description: foo
                cpp_type: foo
                bson_serialization_type: bindata
                bindata_subtype: %s
                serializer: foo
                deserializer: foo
                default: foo
                """ % (bindata_subtype)))

    def test_type_and_field_common_negative(self):
        # type: () -> None
        """Negative type tests for properties that types and fields share."""

        # Test bad bson type name
        self.assert_bind_type_properties_fail(
            textwrap.dedent("""
                description: foo
                cpp_type: foo
                bson_serialization_type: foo
            """), idl.errors.ERROR_ID_BAD_BSON_TYPE)

        # Test bad cpp_type name
        self.assert_bind_type_properties_fail(
            textwrap.dedent("""
                description: foo
                cpp_type: StringData
                bson_serialization_type: string
            """), idl.errors.ERROR_ID_NO_STRINGDATA)

        # Test bindata_subtype missing
        self.assert_bind_type_properties_fail(
            textwrap.dedent("""
                description: foo
                cpp_type: foo
                bson_serialization_type: bindata
            """), idl.errors.ERROR_ID_BAD_BSON_BINDATA_SUBTYPE_VALUE)

        # Test bindata_subtype wrong
        self.assert_bind_type_properties_fail(
            textwrap.dedent("""
                description: foo
                cpp_type: foo
                bson_serialization_type: bindata
                bindata_subtype: foo
            """), idl.errors.ERROR_ID_BAD_BSON_BINDATA_SUBTYPE_VALUE)

        # Test bindata_subtype on wrong type
        self.assert_bind_type_properties_fail(
            textwrap.dedent("""
                description: foo
                cpp_type: foo
                bson_serialization_type: string
                bindata_subtype: generic
            """), idl.errors.ERROR_ID_BAD_BSON_BINDATA_SUBTYPE_TYPE)

        # Test bindata in list of types
        self.assert_bind_type_properties_fail(
            textwrap.dedent("""
                description: foo
                cpp_type: foo
                bson_serialization_type: 
                            - bindata
                            - string
            """), idl.errors.ERROR_ID_BAD_BSON_TYPE)

        # Test bindata in list of types
        self.assert_bind_type_properties_fail(
            textwrap.dedent("""
                description: foo
                cpp_type: StringData
                bson_serialization_type: 
                            - bindata
                            - string
            """), idl.errors.ERROR_ID_BAD_BSON_TYPE)

    def test_type_negative(self):
        # type: () -> None
        """Negative type tests."""

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

    def test_field_negative(self):
        # type: () -> None

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
                "cpp_type: foo",
                "bson_serialization_type: string",
                "bindata_subtype: string",
                "serializer: foo",
                "deserializer: foo",
                "default: foo",
        ]:
            self.assert_bind_fail(
                textwrap.dedent("""
            struct:
                name: foo
                description: foo
                strict: false
                fields:
                    foo:
                        ignore: true
                        %s
                """ % (test_value)), idl.errors.ERROR_ID_FIELD_MUST_BE_EMPTY_FOR_IGNORED)

    def test_field_of_type_struct_negative(self):
        # type: () -> None
        """Test that if a field is of type struct, no other properties are set."""

        for test_value in [
                "cpp_type: foo",
                "bson_serialization_type: string",
                "bindata_subtype: string",
                "serializer: foo",
                "deserializer: foo",
                "default: foo",
        ]:
            self.assert_bind_fail(
                textwrap.dedent("""
            type:
                name: string
                description: foo
                cpp_type: foo
                bson_serialization_type: string

            struct:
                name: bar
                description: foo
                strict: false
                fields:
                    foo: string

            struct:
                name: foo
                description: foo
                strict: false
                fields:
                    foo:
                        type: bar
                        %s
                """ % (test_value)), idl.errors.ERROR_ID_FIELD_MUST_BE_EMPTY_FOR_IGNORED)


if __name__ == '__main__':

    unittest.main()
