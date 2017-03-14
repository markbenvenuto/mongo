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
"""
IDL compiler driver.

Orchestrates the 3 passes (parser, binder, and generator) together.
"""

from __future__ import absolute_import, print_function, unicode_literals

import io
import os
from typing import Any, List

from . import binder
from . import errors
from . import generator
from . import parser


class CompilerArgs(object):
    """Set of compiler arguments."""

    def __init__(self):
        # type: () -> None
        """Create a container for compiler arguments."""
        self.import_directories = None  # type: List[unicode]
        self.input_file = None  # type: unicode

        self.output_source = None  # type: unicode
        self.output_header = None  # type: unicode
        self.output_suffix = None  # type: unicode


def compile_idl(args):
    # type: (CompilerArgs) -> bool
    """Compile an IDL file into C++ code."""
    # Named compile_idl to avoid naming conflict
    if not os.path.exists(args.input_file):
        print("ERROR: File '%s' not found" % (args.input_file))

    # TODO: resolve the paths, and log if they do not exist under verbose when import supported is added
    #for import_dir in args.import_directories:
    #    if not os.path.exists(args.input_file):

    error_file_name = os.path.basename(args.input_file)

    if args.output_source is None:
        if not '.' in error_file_name:
            raise errors.IDLError("File name '%s' must be contain a period" % error_file_name)

        file_name_prefix = error_file_name.split('.')[0]
        file_name_prefix += args.output_suffix

        source_file_name = file_name_prefix + ".cc"
        header_file_name = file_name_prefix + ".hpp"
    else:
        source_file_name = args.output_source
        header_file_name = args.output_header

    with io.open(args.input_file) as file_stream:
        parsed_doc = parser.parse(file_stream, error_file_name=error_file_name)

        if not parsed_doc.errors:
            bound_doc = binder.bind(parsed_doc.spec)
            if not bound_doc.errors:
                generator.generate_code(bound_doc.spec, header_file_name, source_file_name)

                return True
            else:
                bound_doc.errors.dump_errors()
        else:
            parsed_doc.errors.dump_errors()

        return False
