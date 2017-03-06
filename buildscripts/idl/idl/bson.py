"""
BSON Type Information.

Utilities for validating bson types, etc.
"""
from __future__ import absolute_import, print_function, unicode_literals

from typing import Dict

# TODO: update with this list:
# https://docs.mongodb.com/manual/reference/bson-types/

BSON_TYPE_INFORMATION = {
    #"minkey" : { 'scalar' :  True, 'cpp_type' : 'MinKey'},
    #"eoo" : { 'scalar' :  True, 'cpp_type' : 'EOO'},
    "double": {
        'scalar': True,
        'cpp_type': 'NumberDouble'
    },
    "string": {
        'scalar': True,
        'cpp_type': 'String'
    },
    "object": {
        'scalar': False,
        'cpp_type': 'Object'
    },
    #"array" : { 'scalar' :  False, 'cpp_type' : 'Array'},
    "bindata": {
        'scalar': True,
        'cpp_type': 'BinData'
    },
    "undefined": {
        'scalar': True,
        'cpp_type': 'Undefined'
    },
    "objectid": {
        'scalar': True,
        'cpp_type': 'jstOID'
    },
    "bool": {
        'scalar': True,
        'cpp_type': 'Bool'
    },
    "date": {
        'scalar': True,
        'cpp_type': 'Date'
    },
    "null": {
        'scalar': True,
        'cpp_type': 'jstNULL'
    },
    "regex": {
        'scalar': True,
        'cpp_type': 'RegEx'
    },
    #"dbPointer" : { 'scalar' :  True, 'cpp_type' : 'DBRef'},
    #"code" : { 'scalar' :  True, 'cpp_type' : 'Code'},
    #"symbol" : { 'scalar' :  True, 'cpp_type' : 'Symbol'},
    #"javascriptwithscope" : { 'scalar' :  True, 'cpp_type' : 'CodeWScope'},
    "int": {
        'scalar': True,
        'cpp_type': 'NumberInt'
    },
    "timestamp": {
        'scalar': True,
        'cpp_type': 'bsonTimestamp'
    },
    "long": {
        'scalar': True,
        'cpp_type': 'NumberLong'
    },
    "decimal": {
        'scalar': True,
        'cpp_type': 'NumberDecimal'
    },
    #"maxkey" : { 'scalar' :  True, 'cpp_type' : 'MaxKey'},
    #"jstypemax" : { 'scalar' :  True, 'cpp_type' : 'JSTypeMax'},
}

BINDATA_SUBTYPE = {
    "generic": {
        'scalar': True,
        'cpp_type': 'BinDataGeneral'
    },
    "function": {
        'scalar': True,
        'cpp_type': 'Function'
    },
    "binary": {
        'scalar': False,
        'cpp_type': 'ByteArrayDeprecated'
    },
    "uuid_old": {
        'scalar': False,
        'cpp_type': 'bdtUUID'
    },
    "uuid": {
        'scalar': True,
        'cpp_type': 'newUUID'
    },
    "md5": {
        'scalar': True,
        'cpp_type': 'MD5Type'
    },
    # "user_defined" : True
}


def is_valid_bson_type(name):
    # type: (unicode) -> bool
    """Return True if this is a valid bson type."""
    return name in BSON_TYPE_INFORMATION


def is_scalar_bson_type(name):
    # type: (unicode) -> bool
    """Return True if this bson type is a scalar."""
    assert is_valid_bson_type(name)
    return BSON_TYPE_INFORMATION[name]['scalar']  # type: ignore


def cpp_bson_type_name(name):
    # type: (unicode) -> unicode
    """Return the C++ type name for a bson type."""
    assert is_valid_bson_type(name)
    return BSON_TYPE_INFORMATION[name]['cpp_type']  # type: ignore


def list_valid_types():
    # type: () -> List[unicode]
    """Return a list of supported bson types."""
    # TODO: improve
    return [a for a in BSON_TYPE_INFORMATION.iterkeys()]


def is_valid_bindata_subtype(name):
    # type: (unicode) -> bool
    """Return True if this bindata subtype is valid."""
    return name in BINDATA_SUBTYPE


def cpp_bindata_subtype_type_name(name):
    # type: (unicode) -> unicode
    """Return the C++ type name for a bindata subtype."""
    assert is_valid_bindata_subtype(name)
    return BINDATA_SUBTYPE[name]['cpp_type']  # type: ignore
