"""
Common error handling code for IDL compiler.

- Common Exceptions
- Error codes used by the IDL compiler
"""
from __future__ import absolute_import, print_function, unicode_literals

import yaml
from yaml import nodes
from typing import List, Union, Any

from . import common

# Error Codes used by IDL Compiler
#
ERROR_ID_UNKNOWN_ROOT = "ID0001"
ERROR_ID_DUPLICATE_SYMBOL = "ID0002"
ERROR_ID_IS_NODE_TYPE = "ID0003"
ERROR_ID_IS_NODE_TYPE_SCALAR_OR_SEQUENCE = "ID0004"
ERROR_ID_DUPLICATE_NODE = "ID0005"
ERROR_ID_UNKNOWN_TYPE = "ID0006"
ERROR_ID_IS_NODE_VALID_BOOL = "ID0007"
ERROR_ID_UNKNOWN_NODE = "ID0008"
ERROR_ID_EMPTY_FIELDS = "ID0009"
ERROR_ID_MISSING_REQUIRED_FIELD = "ID0010"


class IDLError(Exception):
    """Base class for all resmoke.py exceptions."""

    pass


class ParserError(common.SourceLocation):
    """
    Parser error represents a error from the IDL compiler.

    A Parser error consists of
    - error_id - IDxxxx where xxxx is a 0 leading number
    - msg - a string describing an error
    """

    def __init__(self, error_id, msg, file_name, line, column, *args, **kwargs):
        # type: (unicode, unicode, unicode, int, int, *str, **bool) -> None
        """"Construct a parser error with source location information."""
        self.error_id = error_id
        self.msg = msg
        super(ParserError, self).__init__(file_name, line, column)


class ParserErrorCollection(object):
    """A collection of parser errors with line & context information."""

    def __init__(self):
        # type: () -> None
        """Default constructor."""
        self._errors = []  # type: List[ParserError]

    def add(self, location, error_id, msg):
        # type: (common.SourceLocation, unicode, unicode) -> None
        """Add an error message with file (line, column) information."""
        self._errors.append(
            ParserError(error_id, msg, location.file_name, location.line, location.column))

    def has_errors(self):
        # type: () -> bool
        """Have any errors been added to the collection?."""
        return len(self._errors) > 0

    def contains(self, error_id):
        # type: (unicode) -> bool
        """Check if the error collection has at least one message of a given error_id."""
        return len([a for a in self._errors if a.error_id == error_id]) > 0

    def _to_list(self):
        # type: () -> List[unicode]
        return [
            "%s: (%d, %d): %s: %s" % (error.file_name, error.line, error.column, error.error_id,
                                      error.msg) for error in self._errors
        ]

    def dump_errors(self):
        # type: () -> None
        """Print the list of errors."""
        ', '.join(self._to_list())
        for error_msg in self._to_list():
            print("%s\n\n" % error_msg)

    def __str__(self):
        # type: () -> str
        return ', '.join(self._to_list())  # type: ignore


class ParserContext(object):
    """
    IDL parser current file and error context.

    Responsible for:
    - keeping track of current file during imports
    - tracking error messages
    """

    def __init__(self, file_name, errors, *args, **kwargs):
        # type: (unicode, ParserErrorCollection, *str, **bool) -> None
        """Constructor."""
        self.errors = errors
        self.file_name = file_name
        #super(ParserContext, self).__init__(*args, **kwargs)

    def _add_error(self, location, error_id, msg):
        # type: (common.SourceLocation, unicode, unicode) -> None
        self.errors.add(location, error_id, msg)

    def _add_node_error(self, node, error_id, msg):
        # type: (yaml.nodes.Node, unicode, unicode) -> None
        self.errors.add(
            common.SourceLocation(self.file_name, node.start_mark.line, node.start_mark.column),
            error_id, msg)

    def add_unknown_root_node_error(self, node):
        # type: (yaml.nodes.Node) -> None
        """Add an error about an unknown root node."""
        self._add_node_error(node, ERROR_ID_UNKNOWN_ROOT, (
            "Unrecognized IDL specification root level node '%s' only " +
            " (global, import, type, command, and struct) are accepted") % (node.value))

    def add_unknown_node_error(self, node, name):
        # type: (yaml.nodes.Node, unicode) -> None
        """Add an error about an unknown node."""
        self._add_node_error(node, ERROR_ID_UNKNOWN_NODE,
                             "Unknown IDL node '%s' for YAML entity '%s'." % (node.value, name))

    def add_duplicate_symbol_error(self, location, name, duplicate_class_name, original_class_name):
        # type: (common.SourceLocation, unicode, unicode, unicode) -> None
        """Add an error about a duplicate symbol."""
        self._add_error(location, ERROR_ID_DUPLICATE_SYMBOL,
                        "%s '%s' is a duplicate symbol of an existing %s" %
                        (duplicate_class_name, name, original_class_name))

    def add_unknown_type_error(self, location, field_name, type_name):
        # type: (common.SourceLocation, unicode, unicode) -> None
        """Add an error about a duplicate symbol."""
        self._add_error(location, ERROR_ID_UNKNOWN_TYPE,
                        "'%s' is an unknown type for field '%s'" % (type_name, field_name))

    def _is_node_type(self, node, node_name, expected_node_type):
        # type: (Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode], unicode, unicode) -> bool
        if not node.id == expected_node_type:
            self._add_node_error(node, ERROR_ID_IS_NODE_TYPE,
                                 "Illegal node type '%s' for '%s', expected node type '%s'" %
                                 (node.id, node_name, expected_node_type))
            return False
        return True

    def is_mapping_node(self, node, node_name):
        # type: (Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode], unicode) -> bool
        """Return True if this YAML node is a Map."""
        return self._is_node_type(node, node_name, "mapping")

    def is_scalar_node(self, node, node_name):
        # type: (Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode], unicode) -> bool
        """Return True if this YAML node is a Scalar."""
        return self._is_node_type(node, node_name, "scalar")

    def is_sequence_node(self, node, node_name):
        # type: (Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode], unicode) -> bool
        """Return True if this YAML node is a Sequence."""
        return self._is_node_type(node, node_name, "sequence")

    def is_sequence_or_scalar_node(self, node, node_name):
        # type: (Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode], unicode) -> bool
        """Return True if this YAML node is a Scalar or Sequence."""
        if not node.id == "scalar" and not node.id == "sequence":
            self._add_node_error(
                node, ERROR_ID_IS_NODE_TYPE_SCALAR_OR_SEQUENCE,
                "Illegal node type '%s' for '%s', expected node type 'scalar' or 'sequence'" %
                (node.id, node_name))
            return False
        return True

    def is_scalar_bool_node(self, node, node_name):
        # type: (Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode], unicode) -> bool
        """Return True if this YAML node is a Scalar and a valid boolean"""
        if not self._is_node_type(node, node_name, "scalar"):
            return False
        if not (node.value == "true" or node.value == "false"):
            self._add_node_error(node, ERROR_ID_IS_NODE_VALID_BOOL,
                                 "Illegal bool value for '%s', expected either 'true' or 'false'." %
                                 node_name)
        return True

    def get_list(self, node):
        # type: (Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode]) -> List[unicode]
        """Get a YAML scalar or sequence node as a list of strings."""
        assert self.is_sequence_or_scalar_node(node, "unknown")
        if node.id == "scalar":
            return [node.value]
        elif node.id == "sequence":
            # Unzip the list of ScalarNode
            return [v.value for v in node.value]

    def add_duplicate(self, node, node_name):
        # type: (yaml.nodes.Node, unicode) -> None
        """Add an error about a duplicate node."""
        self._add_node_error(node, ERROR_ID_DUPLICATE_NODE,
                             "Duplicate node found for '%s'" % (node_name))

    def add_empty_struct(self, node, name):
        # type: (yaml.nodes.Node, unicode) -> None
        """Add an error about a struct without fields."""
        self._add_node_error(node, ERROR_ID_EMPTY_FIELDS,
                             "Struct '%s' must have fields specified but none found" % (name))

    def add_missing_required_field(self, node, node_parent, node_name):
        # type: (yaml.nodes.Node, unicode, unicode) -> None
        """Add an error about a YAML node missing a required child."""
        self._add_node_error(node, ERROR_ID_MISSING_REQUIRED_FIELD,
                             "IDL node '%s' is missing required scalar '%s'" %
                             (node_parent, node_name))
