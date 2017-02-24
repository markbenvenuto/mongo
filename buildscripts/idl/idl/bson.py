"""
BSON Type Information
"""
from __future__ import absolute_import, print_function, unicode_literals

BSON_TYPE_INFORMATION = {
    "MinKey" : True,
    "EOO" : True,
    "NumberDouble" : True,
    "String" : True,
    "Object" : False,
    "Array" : False,
    "BinData" : True,
    "Undefined" : True,
    "ObjectId" : True,
    "Bool" : True,
    "Date" : True,
    "Null" : True,
    "RegEx" : True,
    "DBRef" : True,
    "Code" : True,
    "Symbol" : True,
    "CodeWScope" : True,
    "NumberInt" : True,
    "Timestamp" : True,
    "NumberLong" : True,
    "NumberDecimal" : True,
    "JSTypeMax" : True,
    "MaxKey" : True,
    }

def is_valid_bson_type(name):
    # type: (unicode) -> bool
    """Returns True if this is a valid bson type."""
    return name in BSON_TYPE_INFORMATION

def is_scalar_bson_type(name):
    # type: (unicode) -> bool
    """Returns True if this bson type is a scalar."""
    assert is_valid_bson_type(name)
    return BSON_TYPE_INFORMATION[name]
