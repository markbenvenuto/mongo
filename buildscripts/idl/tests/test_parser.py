from __future__ import absolute_import

import textwrap
#from .context import idl

import unittest

class Test_parser(unittest.TestCase):
    def assertParseOk(self, str):
        parsed_doc = idl.parser.parse(str)

        self.assertIsNone(parsed_doc.errors)
        self.assertIsNotNone(parsed_doc.spec)

    def assertParseNotOk(self, str, id):
        parsed_doc = idl.parser.parse(str)

        self.assertIsNone(parsed_doc.spec)
        self.assertIsNotNone(parsed_doc.errors)
        self.assertTrue(parsed_doc.errors.contains(id))

    def test_global_positive(self):
        self.assertParseOk(textwrap.dedent("""
        global:
            cpp_namespace: 'foo'"""))
        self.assertParseOk(textwrap.dedent("""
        global:
            cpp_includes: 'foo'"""))
        self.assertParseOk(textwrap.dedent("""
        global:
            cpp_includes: 
                - 'bar'
                - 'foo'"""))
        self.assertParseOk(textwrap.dedent("""
        global:
            cpp_includes: 'bar'"""))

    def test_global_negative(self):
        self.assertParseNotOk(textwrap.dedent("""
        global:
            cpp_namespace: 'foo'
        global:
            cpp_namespace: 'bar'
            """), idl.errors.ERROR_ID_DUPLICATE_NODE)
        self.assertParseNotOk(textwrap.dedent("""
        global:
            cpp_namespace: 'foo'
            cpp_namespace: 'foo'"""), idl.errors.ERROR_ID_DUPLICATE_NODE)
        self.assertParseNotOk(textwrap.dedent("""
        global:
            cpp_includes: 'foo'
            cpp_includes: 'foo'"""), idl.errors.ERROR_ID_DUPLICATE_NODE)

    def test_root_negative(self):
        self.assertParseNotOk(textwrap.dedent("""
        fake:
            cpp_namespace: 'foo'
            """), idl.errors.ERROR_ID_UNKNOWN_ROOT)


if __name__ == '__main__':

    # import package so that it works regardless of whether we run as a module or file
    if __package__ is None:
        import sys
        from os import path
        sys.path.append( path.dirname( path.dirname( path.abspath(__file__) ) ) )
        from context import idl
    else:
        from .context import idl

    print dir(idl)
    print dir(idl.parser)

    unittest.main()
