#!/usr/bin/env python2
"""
IDL Compiler Driver
"""
from __future__ import absolute_import, print_function, unicode_literals

import argparse
import idl.parser
import idl.generator

def main():
    """Main Entry point"""
    parser = argparse.ArgumentParser(description='MongoDB IDL Compiler.')

    parser.add_argument(
        '--input',
        type=str,
        help="IDL input file")

    parser.add_argument(
        '--output',
        type=str,
        help="IDL output file prefix")

    parser.add_argument(
        '--include',
        type=str,
        help="Directory to search for IDL import files")

    args = parser.parse_args()

    print("Hello")

    parsed_doc = idl.parser.parse("""
# TODO: write the code for this
global:
    cpp_namespace: "mongo::acme::"
    cpp_includes:
        - "mongo/db/foo.h"
# 3 levels of deserialization
# - 1 readString/toString
# - 2 BSONelemtn
# - 3 - full on kitchen

type:
    name: string
    bson_serialization_type: String 
    cpp_type: "mongo::StringData"
    deserializer: "mongo::BSONElement::str"
    

type:
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
    name: WriteConcern
    description: A Fake Write Concern for a command
    fields:
        w: 
          type: object
          cpp_type: WriteConcernWriteField
          serialization_type: any
          serializer: "WriteConcernWriteField::deserializeWField"
        j: bool
        wtimeout: 
          type: safeInt32
          min: 0
          max: 50
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
        ns: NamespaceString
        writeConcern: WriteConcern
""")

    if not parsed_doc.errors:
        idl.generator.generate_code(parsed_doc.spec, "fooobar")

if __name__ == '__main__':
    main()
