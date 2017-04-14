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
"""Test cases for IDL parser."""

from __future__ import absolute_import, print_function, unicode_literals

import os
import unittest

# import package so that it works regardless of whether we run as a module or file
if __package__ is None:
    import sys
    sys.path.append(os.path.dirname(os.path.abspath(__file__)))
    from context import idl
    import testcase
else:
    from .context import idl
    from . import testcase


class TestGenerator(testcase.IDLTestcase):
    """Test the IDL Generator."""

    def test_compile(self):
        # type: () -> None
        """Exercise the code generator so code coverage can be measured."""
        base_dir = os.path.dirname(
            os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
        idl_dir = os.path.join(base_dir, 'src', 'mongo', 'idl')

        args = idl.compiler.CompilerArgs()
        args.output_suffix = "_codecoverage_gen"
        # TODO: after rebase on import support
        #args.input_file = os.path.join(idl_dir, 'unittest_import.idl')
        #idl.compiler.compile_idl(args)
        args.input_file = os.path.join(idl_dir, 'unittest.idl')
        self.assertTrue(idl.compiler.compile_idl(args))


if __name__ == '__main__':

    unittest.main()
