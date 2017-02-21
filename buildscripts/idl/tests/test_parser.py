from __future__ import absolute_import

import textwrap
from .context import idl

import unittest

class Test_test_parser(unittest.TestCase):
    def test_global_positive(self):
        self.assertIsNotNone(idl.parser.parse(textwrap.dedent("""
        global:
            cpp_namespace: 'foo'""")))
        self.assertIsNotNone(idl.parser.parse(textwrap.dedent("""
        global:
            cpp_includes: 'foo'""")))
        self.assertIsNotNone(idl.parser.parse(textwrap.dedent("""
        global:
            cpp_includes: 
                - 'bar'
                - 'foo'""")))
        #self.assertIsNotNone(idl.parser.parse(textwrap.dedent("""
        #global:
        ##    cpp_namespace: 'foo'
        #    cpp_namespace: 'foo'""")))
        #idl.parser.parse("global: cpp_namespace: 'foo'")
        #idl.parser.parse("global: cpp_namespace: 'foo'")

if __name__ == '__main__':
    print dir(idl)
    print dir(idl.parser)
    unittest.main()
