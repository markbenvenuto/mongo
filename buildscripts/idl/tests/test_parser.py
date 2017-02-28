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


class Test_parser(testcase.IDLTestcase):
    def test_empty(self):
        # type: () -> None
        self.assert_parse("")

    def test_global_positive(self):
        # type: () -> None
        self.assert_parse(textwrap.dedent("""
        global:
            cpp_namespace: 'foo'"""))
        self.assert_parse(textwrap.dedent("""
        global:
            cpp_includes: 'foo'"""))
        self.assert_parse(
            textwrap.dedent("""
        global:
            cpp_includes: 
                - 'bar'
                - 'foo'"""))
        self.assert_parse(textwrap.dedent("""
        global:
            cpp_includes: 'bar'"""))

    def test_global_negative(self):
        # type: () -> None
        self.assert_parse_fail(
            textwrap.dedent("""
        global: foo
            """), idl.errors.ERROR_ID_IS_NODE_TYPE)

        self.assert_parse_fail(
            textwrap.dedent("""
        global:
            cpp_namespace: 'foo'
        global:
            cpp_namespace: 'bar'
            """), idl.errors.ERROR_ID_DUPLICATE_NODE)
        self.assert_parse_fail(
            textwrap.dedent("""
        global:
            cpp_namespace: 'foo'
            cpp_namespace: 'foo'"""), idl.errors.ERROR_ID_DUPLICATE_NODE)
        self.assert_parse_fail(
            textwrap.dedent("""
        global:
            cpp_includes: 'foo'
            cpp_includes: 'foo'"""), idl.errors.ERROR_ID_DUPLICATE_NODE)

        self.assert_parse_fail(
            textwrap.dedent("""
        global:
            cpp_includes:
                inc1: 'foo'"""), idl.errors.ERROR_ID_IS_NODE_TYPE_SCALAR_OR_SEQUENCE)

        self.assert_parse_fail(
            textwrap.dedent("""
        global:
            bar: 'foo'
            """), idl.errors.ERROR_ID_UNKNOWN_NODE)

    def test_root_negative(self):
        # type: () -> None
        self.assert_parse_fail(
            textwrap.dedent("""
        fake:
            cpp_namespace: 'foo'
            """), idl.errors.ERROR_ID_UNKNOWN_ROOT)

    def test_type_positive(self):
        # type: () -> None
        self.assert_parse(
            textwrap.dedent("""
        type:
            name: foo
            description: foo
            cpp_type: foo
            bson_serialization_type: foo
            serializer: foo
            deserializer: foo
            default: foo
            bindata_subtype: foo
            """))

    def test_type_negative(self):
        # type: () -> None
        self.assert_parse_fail(
            textwrap.dedent("""
            type: 'foo'"""), idl.errors.ERROR_ID_IS_NODE_TYPE)
        self.assert_parse_fail(
            textwrap.dedent("""
        type:
            name: foo
            bogus: foo
            """), idl.errors.ERROR_ID_UNKNOWN_NODE)
        self.assert_parse_fail(
            textwrap.dedent("""
        type:
            name: foo
            name: foo
            """), idl.errors.ERROR_ID_DUPLICATE_NODE)
        self.assert_parse_fail(
            textwrap.dedent("""
        type:
            name:
                - foo
            """), idl.errors.ERROR_ID_IS_NODE_TYPE)
        self.assert_parse_fail(
            textwrap.dedent("""
        type:
            name:
                foo: bar
            """), idl.errors.ERROR_ID_IS_NODE_TYPE)
        self.assert_parse_fail(
            textwrap.dedent("""
        type:
            description: foo
            cpp_type: foo
            bson_serialization_type: foo
            """), idl.errors.ERROR_ID_MISSING_REQUIRED_FIELD)
        self.assert_parse_fail(
            textwrap.dedent("""
        type:
            name: foo
            description: foo
            cpp_type: foo
            """), idl.errors.ERROR_ID_MISSING_REQUIRED_FIELD)
        self.assert_parse_fail(
            textwrap.dedent("""
        type:
            name: foo
            description: foo
            bson_serialization_type: foo
            """), idl.errors.ERROR_ID_MISSING_REQUIRED_FIELD)

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

    print(dir(idl))
    print(dir(idl.parser))

    unittest.main()
