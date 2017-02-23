"""
Exceptions raised by resmoke.py.
"""
import yaml
from yaml import nodes
from typing import List, Union, Any

from . import common

ERROR_ID_UNKNOWN_ROOT = "ID0001"
ERROR_ID_DUPLICATE_SYMBOL = "ID0002"
ERROR_ID_IS_NODE_TYPE = "ID0003"
ERROR_ID_IS_NODE_TYPE_SCALAR_OR_SEQUENCE = "ID0004"
ERROR_ID_DUPLICATE_NODE = "ID0005"
ERROR_ID_UNKNOWN_TYPE = "ID0006"

class IDLError(Exception):
    """
    Base class for all resmoke.py exceptions.
    """
    pass

class ParserError(common.SourceLocation):
    def __init__(self, error_id, msg, file_name, line, column, *args, **kwargs):
        # type: (unicode, unicode, unicode, int, int, *str, **bool) -> None
        self.error_id = error_id
        self.msg = msg
        super(ParserError, self).__init__(file_name, line, column)

class ParserErrorCollection(object):
    """A collection of errors with line & context information"""
    def __init__(self):
        self._errors = [] # List[ParserError]

    def add(self, location, error_id, msg):
        # type: (common.SourceLocation, unicode, unicode) -> None
        """Add an error message with file (line, column) information"""
        self._errors.append(ParserError(error_id, msg, location.file_name, location.line, location.column))

    def has_errors(self):
        # type: () -> bool
        """Have any errors been added to the collection?"""
        return len(self._errors) > 0

    def contains(self, error_id):
        # type: (unicode) -> bool
        """Check if the error collection has at least one message of a given error_id"""
        return len([a for a in self._errors if a.error_id == error_id]) > 0

    def dump_errors(self):
        # type: () -> None
        """Print the list of errors"""
        for error in self._errors:
            error_msg = "%s: (%d, %d): %s: %s" % (error.file_name, error.line, error.column, error.error_id, error.msg)
            print ("%s\n\n" % error_msg)


class ParserContext(object):
    """IDL parser context

        Responsible for:
        - keeping track of current file
        - generating error messages
    """
    def __init__(self, file_name, errors, *args, **kwargs):
        # type: (unicode, ParserErrorCollection, *str, **bool) -> None
        self.errors = errors
        self.file_name = file_name
        #super(ParserContext, self).__init__(*args, **kwargs)

    def _add_error(self, location, error_id, msg):
        # type: (common.SourceLocation, unicode, unicode) -> None
        self.errors.add(location, error_id, msg)

    def _add_node_error(self, node, error_id, msg):
        # type: (yaml.nodes.Node, unicode, unicode) -> None
        self.errors.add(common.SourceLocation(self.file_name, node.start_mark.line, node.start_mark.column), error_id, msg)

    def add_unknown_root_node_error(self, node):
        # type: (yaml.nodes.Node) -> None
        """Add an error about an unknown root node"""
        self._add_node_error(node, ERROR_ID_UNKNOWN_ROOT, "Unrecognized IDL specification root level node '%s' only (global, import, type, command, and struct) are accepted" % (node.value))

    def add_duplicate_symbol_error(self, location, name, duplicate_class_name, original_class_name):
        # type: (common.SourceLocation, unicode, unicode, unicode) -> None
        """Add an error about a duplicate symbol"""
        self._add_error(location, ERROR_ID_DUPLICATE_SYMBOL, "%s '%s' is a duplicate symbol of an existing %s" % (duplicate_class_name, name, original_class_name))

    def add_unknown_type_error(self, location, field_name, type_name):
        # type: (common.SourceLocation, unicode, unicode) -> None
        """Add an error about a duplicate symbol"""
        self._add_error(location, ERROR_ID_UNKNOWN_TYPE, "'%s' is an unknown type for field '%s'" % (type_name, field_name))

    def _is_node_type(self, node, node_name, expected_node_type):
        # type: (Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode], unicode, unicode) -> bool
        if not node.id == expected_node_type:
            self._add_node_error(node, ERROR_ID_IS_NODE_TYPE, "Illegal node type '%s' for '%s', expected node type '%s'" % (node.id, node_name, expected_node_type))
            return False
        return True

    def is_mapping_node(self, node, node_name):
        # type: (Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode], unicode) -> bool
        """Is this YAML node a Map?"""
        return self._is_node_type(node, node_name, "mapping")

    def is_scalar_node(self, node, node_name):
        # type: (Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode], unicode) -> bool
        """Is this YAML node a Scalar?"""
        return self._is_node_type(node, node_name, "scalar")

    def is_sequence_node(self, node, node_name):
        # type: (Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode], unicode) -> bool
        """Is this YAML node a Sequence?"""
        return self._is_node_type(node, node_name, "sequence")

    def is_sequence_or_scalar_node(self, node, node_name):
        # type: (Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode], unicode) -> bool
        """Is this YAML node a Scalar or Sequence?"""
        if not node.id == "scalar" and not node.id == "sequence":
            self._add_node_error(node, ERROR_ID_IS_NODE_TYPE_SCALAR_OR_SEQUENCE, "Illegal node type '%s' for '%s', expected node type 'scalar' or 'sequence'" % (node.id, node_name))
            return False
        return True

    def get_list(self, node):
        # type: (Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode]) -> List[unicode]
        """Get a YAML scalar or sequence node as a list of strings"""
        assert self.is_sequence_or_scalar_node(node, "unknown")
        if node.id == "scalar":
            return [node.value]
        elif node.id == "sequence":
            # Unzip the list of ScalarNode
            return [v.value for v in node.value]

    def is_duplicate(self, node, value, node_name):
        # type: (yaml.nodes.Node, Any, unicode) -> bool
        """Is this a duplicate node, check to see if value is not None"""
        if value is not None:
            self._add_node_error(node, ERROR_ID_DUPLICATE_NODE, "Duplicate node found for '%s'" % (node_name))
            return True

        return False

    def add_duplicate(self, node, node_name):
        # type: (yaml.nodes.Node, unicode) -> None
        """Is this a duplicate node, check to see if value is not None"""
        self._add_node_error(node, ERROR_ID_DUPLICATE_NODE, "Duplicate node found for '%s'" % (node_name))

    def is_empty_list(self, node, value, node_name):
        # type: (yaml.nodes.Node, List, unicode) -> bool
        """Is this a duplicate node, check to see if value is not an empty list"""
        if len(value) == 0:
            return True

        self._add_node_error(node, ERROR_ID_DUPLICATE_NODE, "Duplicate node found for '%s'" % (node_name))
        return False