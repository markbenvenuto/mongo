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

        # Test supported types
        for bson_type in [
                "bool", "date", "null", "numberdecimal", "numberdouble", "numberint", "numberlong",
                "object", "objectid", "regex", "string", "timestamp", "undefined"
        ]:
            self.assert_bind(
                textwrap.dedent("""
            type:
                name: foo
                description: foo
                cpp_type: foo
                bson_serialization_type: %s
                serializer: foo
                deserializer: foo
                default: foo
                """ % (bson_type)))

        # Test supported bindata_subtype
        for bindata_subtype in ["generic", "function", "binary", "uuid_old", "uuid", "md5"]:
            self.assert_bind(
                textwrap.dedent("""
            type:
                name: foo
                description: foo
                cpp_type: foo
                bson_serialization_type: bindata
                bindata_subtype: %s
                serializer: foo
                deserializer: foo
                default: foo
                """ % (bindata_subtype)))

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
                bson_serialization_type: foo
            """), idl.errors.ERROR_ID_ARRAY_NOT_VALID_TYPE)

        # Test bad type name
        self.assert_bind_fail(
            textwrap.dedent("""
            type: 
                name: foo
                description: foo
                cpp_type: foo
                bson_serialization_type: foo
            """), idl.errors.ERROR_ID_BAD_BSON_TYPE)

        # Test bindata_subtype missing
        self.assert_bind_fail(
            textwrap.dedent("""
            type: 
                name: foo
                description: foo
                cpp_type: foo
                bson_serialization_type: bindata
            """), idl.errors.ERROR_ID_BAD_BSON_BINDATA_SUBTYPE_VALUE)

        # Test bindata_subtype wrong
        self.assert_bind_fail(
            textwrap.dedent("""
            type: 
                name: foo
                description: foo
                cpp_type: foo
                bson_serialization_type: bindata
                bindata_subtype: foo
            """), idl.errors.ERROR_ID_BAD_BSON_BINDATA_SUBTYPE_VALUE)

        # Test bindata_subtype on wrong type
        self.assert_bind_fail(
            textwrap.dedent("""
            type: 
                name: foo
                description: foo
                cpp_type: foo
                bson_serialization_type: string
                bindata_subtype: generic
            """), idl.errors.ERROR_ID_BAD_BSON_BINDATA_SUBTYPE_TYPE)

        # Test bindata in list of types
        self.assert_bind_fail(
            textwrap.dedent("""
            type: 
                name: foo
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
                name: foo
                description: foo
                cpp_type: StringData
                bson_serialization_type: 
                            - bindata
                            - string
            """), idl.errors.ERROR_ID_BAD_BSON_TYPE)

    def test_struct_positive(self):
        # type: () -> None
        self.assert_parse(
            textwrap.dedent("""
        struct:
            name: foo
            description: foo
            strict: true
            fields:
                foo: bar
            """))
        self.assert_parse(
            textwrap.dedent("""
        struct:
            name: foo
            description: foo
            strict: false
            fields:
                foo: bar
            """))

    def test_struct_negative(self):
        # type: () -> None
        self.assert_parse_fail(
            textwrap.dedent("""
        struct:
            name: foo
            description: foo
            strict: true
            """), idl.errors.ERROR_ID_EMPTY_FIELDS)
        self.assert_parse_fail(
            textwrap.dedent("""
        struct:
            name: foo
            description: foo
            strict: bar
            fields:
                foo: bar
            """), idl.errors.ERROR_ID_IS_NODE_VALID_BOOL)

    def test_field_positive(self):
        # type: () -> None
        self.assert_parse(
            textwrap.dedent("""
        struct:
            name: foo
            description: foo
            strict: false
            fields:
                foo: short
            """))
        self.assert_parse(
            textwrap.dedent("""
        struct:
            name: foo
            description: foo
            strict: false
            fields:
                foo:
                    type: foo
                    description: foo
                    cpp_type: foo
                    bson_serialization_type: foo
                    serializer: foo
                    deserializer: foo
                    default: foo
                    bindata_subtype: foo
                    optional: true
                    ignore: true
            """))
        self.assert_parse(
            textwrap.dedent("""
        struct:
            name: foo
            description: foo
            strict: false
            fields:
                foo:
                    optional: false
                    ignore: false
            """))

    def test_field_negative(self):
        # type: () -> None
        self.assert_parse_fail(
            textwrap.dedent("""
        struct:
            name: foo
            description: foo
            strict: false
            fields:
                foo:
                    optional: bar
            """), idl.errors.ERROR_ID_IS_NODE_VALID_BOOL)
        self.assert_parse_fail(
            textwrap.dedent("""
        struct:
            name: foo
            description: foo
            strict: false
            fields:
                foo:
                    ignore: bar
            """), idl.errors.ERROR_ID_IS_NODE_VALID_BOOL)


if __name__ == '__main__':

    unittest.main()
