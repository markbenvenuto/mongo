"""
Exceptions raised by resmoke.py.
"""

ERROR_ID_UNKNOWN_ROOT = "ID0001"
ERROR_ID_DUPLICATE_SYMBOL = "ID0002"
ERROR_ID_IS_NODE_TYPE = "ID0003"

ERROR_ID_IS_NODE_TYPE_SCALAR_OR_SEQUENCE = "ID0004"

ERROR_ID_DUPLICATE_NODE = "ID0005"

class IDLError(Exception):
    """
    Base class for all resmoke.py exceptions.
    """
    pass
