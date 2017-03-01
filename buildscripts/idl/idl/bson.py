"""
BSON Type Information
"""
from __future__ import absolute_import, print_function, unicode_literals

BSON_TYPE_INFORMATION = {
    #"minkey": True,
    #"eoo": True,
    "numberdouble": True,
    "string": True,
    "object": False,
    #"array": False,
    "bindata": True,
    "undefined": True,
    "objectid": True,
    "bool": True,
    "date": True,
    "null": True,
    "regex": True,
    #"dbref": True,
    #"code": True,
    #"symbol": True,
    #"codewscope": True,
    "numberint": True,
    "timestamp": True,
    "numberlong": True,
    "numberdecimal": True,
    #"jstypemax": True,
    #"maxkey": True,
}

BINDATA_SUBTYPE = {
    "generic": True,
    "function": True,
    "binary": False,
    "uuid_old": False,
    "uuid": True,
    "md5": True,
    # "user_defined" : True  
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


def list_valid_types():
    # type: () -> List[unicode]
    """Returns a list of supported bson types."""
    # TODO: improve
    return [a for a in BSON_TYPE_INFORMATION.iterkeys()]


def is_valid_bindata_subtype(name):
    # type: (unicode) -> bool
    """Returns True if this bindata subtype is valid."""
    return name in BINDATA_SUBTYPE
