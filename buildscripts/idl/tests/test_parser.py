from __future__ import absolute_import

from .context import idl

import unittest

class Test_test_parser(unittest.TestCase):
    def test_A(self):
        idl.parser.parse("import: 'foo'")
        self.fail("Not implemented")

if __name__ == '__main__':
    print dir(idl)
    print dir(idl.parser)
    unittest.main()
