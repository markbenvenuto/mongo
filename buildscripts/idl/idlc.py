#!/usr/bin/env python2
"""
IDL Compiler Driver
"""
from __future__ import absolute_import, print_function

import argparse
import sys

import idl.compiler


def main():
    # type: () -> None
    """Main Entry point"""
    parser = argparse.ArgumentParser(description='MongoDB IDL Compiler.')

    parser.add_argument('file', type=str, help="IDL input file")

    parser.add_argument('-o', '--output', type=str, help="IDL output source file")

    parser.add_argument('--header', type=str, help="IDL output header file")

    parser.add_argument(
        '-i',
        '--include',
        type=str,
        action="append",
        help="Directory to search for IDL import files")

    parser.add_argument('-v', '--verbose', action='count', help="Enable verbose tracing")

    args = parser.parse_args()

    compiler_args = idl.compiler.CompilerArgs()

    compiler_args.input_file = args.file
    compiler_args.import_directories = args.include

    compiler_args.output_source = args.output
    compiler_args.output_header = args.header
    compiler_args.output_suffix = "_gen"

    if (args.output is not None and args.header is None) or \
        (args.output is  None and args.header is not None):
        print("ERROR: Either both --header and --output must be specified or neither.")
        sys.exit(1)

    # Compile the IDL document the user specified
    success = idl.compiler.compile_idl(compiler_args)

    if not success:
        sys.exit(1)

if __name__ == '__main__':
    main()
