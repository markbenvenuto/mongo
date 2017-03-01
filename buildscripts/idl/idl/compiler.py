"""
IDL compiler driver.
"""
from __future__ import absolute_import, print_function, unicode_literals

from typing import Any
import os
import io

from . import parser
from . import binder
from . import generator

class CompilerArgs(object):
    """Set of compiler arguments."""
    def __init__(self):
        # type: () -> None
        self.import_directories = None # type: List[unicode]
        self.input_file = None # type: unicode

        self.output_prefix = None # type: unicode
        self.output_suffix = None # type: unicode


def compile_idl(args):
    # type: (CompilerArgs) -> bool
    """Compile an IDL file into C++ code."""
    # Named compile_idl to avoid naming conflict
    if not os.path.exists(args.input_file):
        print("ERROR: File '%s' not found" % (args.input_file))
    
    # TODO: resolve the paths, and log if they do not exist
    #for import_dir in args.import_directories:
    #    if not os.path.exists(args.input_file):

    error_file_name = os.path.basename(args.input_file)

    with io.open(args.input_file) as file_stream:
        parsed_doc = parser.parse(file_stream, error_file_name=error_file_name)

        if not parsed_doc.errors:
            bound_doc = binder.bind(parsed_doc.spec)
            if not bound_doc.errors:
                generator.generate_code(bound_doc.spec, "example_gen")

                return True
            else:
                bound_doc.errors.dump_errors()
        else:
            parsed_doc.errors.dump_errors()

        return False
    # TODO: bind and validate the tree

    #spec.dump()

    # Dump code for all the generated files
    # 1. Generate Header file
    # 2. Generate C++ file stuff    

    # Create series of classes to generate code for each type
