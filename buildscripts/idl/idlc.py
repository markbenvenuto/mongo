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

    print("Hello")

    compiler_args = idl.compiler.CompilerArgs()

    compiler_args.input_file = args.file
    compiler_args.import_directories = args.include

    compiler_args.output_source = args.output
    compiler_args.output_header = args.header
    compiler_args.output_suffix = "_gen"

    # TODO require both header and source or none

    # Compile the IDL document the user specified
    success = idl.compiler.compile_idl(compiler_args)

    if not success:
        sys.exit(1)

    # idl.compiler.compile(compiler_args,
    foo = """
# TODO: write the code for this
global:
    cpp_namespace: "mongo::acme::"
#    cpp_includes:
#        - "mongo/db/foo.h"
# 3 levels of deserialization
# - 1 readString/toString
# - 2 BSONelemtn
# - 3 - full on kitchen

type:
    name: string
    bson_serialization_type: String 
    cpp_type: "std::string"
    deserializer: "mongo::BSONElement::str"
    
type:
    name: int32
    bson_serialization_type: int32
    cpp_type: "std::int32_t"
    deserializer: "mongo::BSONElement::numberInt"

type:
    name: NamespaceString
    name: NamespaceString
    cpp_type: NamepaceString
    bson_serialization_type: String 
    serializer: "mongo::NamespaceString::toBSON"
    deserializer: "mongo::NamepaceString::parseBSON"

type:
    name: safeInt32
    description: Accepts any numerical type within int32 range
    cpp_type: int64_t
    bson_serialization_type: "BSONElement::isNumber"
    serializer: "mongo::BSONElement::numberInt??"
    deserializer: "mongo::BSONElement::numberInt"

struct:
    name: FakeWriteConcern
    description: A Fake Write Concern for a command
    fields:
        j: int32
        wtimeout: 
          type: safeInt32
          default: 42
          required: true
        wOptime:
          type: void
          # A legacy field to ignore now
          ignore: true

struct:
    name: bikeShedCmd
    description: An example command
    fields:
        color:
          type: string
          description: The Bike Shed's color
          required: true
        nameplate:
          type: string
          description: The Bike Shed's nameplate
        writeConcern: FakeWriteConcern
"""


if __name__ == '__main__':
    main()
