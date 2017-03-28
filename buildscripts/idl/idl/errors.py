# Copyright (C) 2017 MongoDB Inc.
#
# This program is free software: you can redistribute it and/or  modify
# it under the terms of the GNU Affero General Public License, version 3,
# as published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
"""
Common error handling code for IDL compiler.

- Common Exceptions used by IDL compiler.
- Error codes used by the IDL compiler.
"""

from __future__ import absolute_import, print_function, unicode_literals

import inspect
import sys
# from typing import List, Union, Any
from yaml import nodes
import yaml

from . import common

# Public error Codes used by IDL Compiler.
# Used by tests cases to validate expected errors are thrown in negative tests.
# Error codes must be unique, validated  _assert_unique_error_messages on file load.
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
ERROR_ID_ARRAY_NOT_VALID_TYPE = "ID0011"
ERROR_ID_MISSING_AST_REQUIRED_FIELD = "ID0012"
ERROR_ID_BAD_BSON_TYPE = "ID0013"
ERROR_ID_BAD_BSON_TYPE_LIST = "ID0014"
ERROR_ID_BAD_BSON_BINDATA_SUBTYPE_TYPE = "ID0015"
ERROR_ID_BAD_BSON_BINDATA_SUBTYPE_VALUE = "ID0016"
ERROR_ID_NO_STRINGDATA = "ID0017"
ERROR_ID_FIELD_MUST_BE_EMPTY_FOR_IGNORED = "ID0018"
ERROR_ID_FIELD_MUST_BE_EMPTY_FOR_STRUCT = "ID0019"
ERROR_ID_CUSTOM_SCALAR_SERIALIZATION_NOT_SUPPORTED = "ID0020"
ERROR_ID_BAD_ANY_TYPE_USE = "ID0021"
ERROR_ID_BAD_NUMERIC_CPP_TYPE = "ID0022"


class IDLError(Exception):
    """Base class for all IDL exceptions."""

    pass


class ParserError(common.SourceLocation):
    """
    ParserError represents an error from the IDL compiler.

    A Parser error consists of
    - error_id - IDxxxx where xxxx is a 0 leading number.
    - msg - a string describing an error.
    - file_name - an IDL file which contained the EOFError.
    - line - the line number of the error or near enough.
    - column - the column number of the error or near enough.
    """

    def __init__(self, error_id, msg, file_name, line, column):
        # type: (unicode, unicode, unicode, int, int) -> None
        """"Construct a parser error with source location information."""
        # pylint: disable=too-many-arguments
        self.error_id = error_id
        self.msg = msg
        super(ParserError, self).__init__(file_name, line, column)

    def __str__(self):
        # type: () -> str
        """
        Return a formatted error.

        Example error message:
        test.idl: (17, 4): ID0008: Unknown IDL node 'cpp_namespac' for YAML entity 'global'.
        """
        msg = "%s: (%d, %d): %s: %s" % (self.file_name, self.line, self.column, self.error_id,
                                        self.msg)
        return msg  # type: ignore


class ParserErrorCollection(object):
    """A collection of parser errors with source context information."""

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

    def to_list(self):
        # type: () -> List[unicode]
        """Return a list of formatted error messages."""
        return [str(error) for error in self._errors]

    def dump_errors(self):
        # type: () -> None
        """Print the list of errors."""
        ', '.join(self.to_list())
        for error_msg in self.to_list():
            print("%s\n\n" % error_msg)

    def count(self):
        # type: () -> int
        """Return the count of errors."""
        return len(self._errors)

    def __str__(self):
        # type: () -> str
        """Return a list of errors."""
        return ', '.join(self.to_list())  # type: ignore


class ParserContext(object):
    """
    IDL parser current file and error context.

    Responsible for:
    - keeping track of current file while parsing imported documents.
    - single class responsible for producing actual error messages.
    """

    # pylint: disable=too-many-public-methods

    def __init__(self, file_name, errors):
        # type: (unicode, ParserErrorCollection) -> None
        """Construct a new ParserContext."""
        self.errors = errors
        self.file_name = file_name

    def _add_error(self, location, error_id, msg):
        # type: (common.SourceLocation, unicode, unicode) -> None
        """
        Add an error with a source location information.

        This is usually directly from an idl.syntax or idl.ast class.
        """
        self.errors.add(location, error_id, msg)

    def _add_node_error(self, node, error_id, msg):
        # type: (yaml.nodes.Node, unicode, unicode) -> None
        """Add an error with source location information based on a YAML node."""
        self.errors.add(
            common.SourceLocation(self.file_name, node.start_mark.line, node.start_mark.column),
            error_id, msg)

    def add_unknown_root_node_error(self, node):
        # type: (yaml.nodes.Node) -> None
        """Add an error about an unknown YAML root node."""
        self._add_node_error(node, ERROR_ID_UNKNOWN_ROOT, (
            "Unrecognized IDL specification root level node '%s', only " +
            " (global, import, types, commands, and structs) are accepted") % (node.value))

    def add_unknown_node_error(self, node, name):
        # type: (yaml.nodes.Node, unicode) -> None
        """Add an error about an unknown node."""
        self._add_node_error(node, ERROR_ID_UNKNOWN_NODE,
                             "Unknown IDL node '%s' for YAML entity '%s'" % (node.value, name))

    def add_duplicate_symbol_error(self, location, name, duplicate_class_name, original_class_name):
        # type: (common.SourceLocation, unicode, unicode, unicode) -> None
        """Add an error about a duplicate symbol."""
        self._add_error(location, ERROR_ID_DUPLICATE_SYMBOL,
                        "%s '%s' is a duplicate symbol of an existing %s" %
                        (duplicate_class_name, name, original_class_name))

    def add_unknown_type_error(self, location, field_name, type_name):
        # type: (common.SourceLocation, unicode, unicode) -> None
        """Add an error about an unknown type."""
        self._add_error(location, ERROR_ID_UNKNOWN_TYPE,
                        "'%s' is an unknown type for field '%s'" % (type_name, field_name))

    def _is_node_type(self, node, node_name, expected_node_type):
        # type: (Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode], unicode, unicode) -> bool
        """Return True if the yaml node type is expected, otherwise returns False and logs an error."""
        if not node.id == expected_node_type:
            self._add_node_error(
                node, ERROR_ID_IS_NODE_TYPE,
                "Illegal YAML node type '%s' for '%s', expected YAML node type '%s'" %
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
        """Return True if the YAML node is a Scalar or Sequence."""
        if not node.id == "scalar" and not node.id == "sequence":
            self._add_node_error(
                node, ERROR_ID_IS_NODE_TYPE_SCALAR_OR_SEQUENCE,
                "Illegal node type '%s' for '%s', expected either node type 'scalar' or 'sequence'"
                % (node.id, node_name))
            return False
        return True

    def is_scalar_bool_node(self, node, node_name):
        # type: (Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode], unicode) -> bool
        """Return True if this YAML node is a Scalar and a valid boolean."""
        if not self._is_node_type(node, node_name, "scalar"):
            return False

        if not (node.value == "true" or node.value == "false"):
            self._add_node_error(node, ERROR_ID_IS_NODE_VALID_BOOL,
                                 "Illegal bool value for '%s', expected either 'true' or 'false'." %
                                 node_name)
            return False

        return True

    def get_bool(self, node):
        # type: (Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode]) -> bool
        """Convert a scalar to a bool."""
        assert self.is_scalar_bool_node(node, "unknown")

        if node.value == "true":
            return True
        return False

    def get_list(self, node):
        # type: (Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode]) -> List[unicode]
        """Get a YAML scalar or sequence node as a list of strings."""
        assert self.is_sequence_or_scalar_node(node, "unknown")
        if node.id == "scalar":
            return [node.value]
        else:
            # Unzip the list of ScalarNode
            return [v.value for v in node.value]

    def add_duplicate_error(self, node, node_name):
        # type: (yaml.nodes.Node, unicode) -> None
        """Add an error about a duplicate node."""
        self._add_node_error(node, ERROR_ID_DUPLICATE_NODE,
                             "Duplicate node found for '%s'" % (node_name))

    def add_empty_struct_error(self, node, name):
        # type: (yaml.nodes.Node, unicode) -> None
        """Add an error about a struct without fields."""
        self._add_node_error(node, ERROR_ID_EMPTY_FIELDS,
                             "Struct '%s' must have fields specified but no fields were found" %
                             (name))

    def add_missing_required_field_error(self, node, node_parent, node_name):
        # type: (yaml.nodes.Node, unicode, unicode) -> None
        """Add an error about a YAML node missing a required child."""
        # pylint: disable=invalid-name
        self._add_node_error(node, ERROR_ID_MISSING_REQUIRED_FIELD,
                             "IDL node '%s' is missing required scalar '%s'" %
                             (node_parent, node_name))

    def add_missing_ast_required_field_error(self, location, ast_type, ast_parent, ast_name):
        # type: (common.SourceLocation, unicode, unicode, unicode) -> None
        """Add an error about a AST node missing a required child."""
        # pylint: disable=invalid-name
        self._add_error(location, ERROR_ID_MISSING_AST_REQUIRED_FIELD,
                        "%s '%s' is missing required scalar '%s'" %
                        (ast_type, ast_parent, ast_name))

    def add_array_not_valid_error(self, location, ast_type, name):
        # type: (common.SourceLocation, unicode, unicode) -> None
        """Add an error about a 'array' not being a valid type name."""
        self._add_error(location, ERROR_ID_ARRAY_NOT_VALID_TYPE,
                        "The %s '%s' cannot be named 'array'" % (ast_type, name))

    def add_bad_bson_type_error(self, location, ast_type, ast_parent, bson_type_name):
        # type: (common.SourceLocation, unicode, unicode, unicode) -> None
        """Add an error about a bad bson type."""
        self._add_error(location, ERROR_ID_BAD_BSON_TYPE,
                        "BSON Type '%s' is not recognized for %s '%s'." %
                        (bson_type_name, ast_type, ast_parent))

    def add_bad_bson_scalar_type_error(self, location, ast_type, ast_parent, bson_type_name):
        # type: (common.SourceLocation, unicode, unicode, unicode) -> None
        """Add an error about a bad list of bson types."""
        self._add_error(location, ERROR_ID_BAD_BSON_TYPE_LIST,
                        ("BSON Type '%s' is not a scalar bson type for %s '%s'" +
                         " and cannot be used in a list of bson serialization types.") %
                        (bson_type_name, ast_type, ast_parent))

    def add_bad_bson_bindata_subtype_error(self, location, ast_type, ast_parent, bson_type_name):
        # type: (common.SourceLocation, unicode, unicode, unicode) -> None
        """Add an error about a bindata_subtype associated with a type that is not bindata."""
        # pylint: disable=invalid-name
        self._add_error(location, ERROR_ID_BAD_BSON_BINDATA_SUBTYPE_TYPE,
                        ("The bindata_subtype field for %s '%s' is not valid for bson type '%s'") %
                        (ast_type, ast_parent, bson_type_name))

    def add_bad_bson_bindata_subtype_value_error(self, location, ast_type, ast_parent, value):
        # type: (common.SourceLocation, unicode, unicode, unicode) -> None
        """Add an error about a bad value for bindata_subtype."""
        # pylint: disable=invalid-name
        self._add_error(location, ERROR_ID_BAD_BSON_BINDATA_SUBTYPE_VALUE,
                        ("The bindata_subtype field's value '%s' for %s '%s' is not valid") %
                        (value, ast_type, ast_parent))

    def add_no_string_data_error(self, location, ast_type, ast_parent):
        # type: (common.SourceLocation, unicode, unicode) -> None
        """Add an error about using StringData for cpp_type."""
        self._add_error(location, ERROR_ID_NO_STRINGDATA,
                        ("Do not use mongo::StringData for %s '%s', use std::string instead") %
                        (ast_type, ast_parent))

    def add_ignored_field_must_be_empty_error(self, location, name, field_name):
        # type: (common.SourceLocation, unicode, unicode) -> None
        """Add an error about field must be empty for ignored fields."""
        # pylint: disable=invalid-name
        self._add_error(location, ERROR_ID_FIELD_MUST_BE_EMPTY_FOR_IGNORED, (
            "Field '%s' cannot contain a value for property '%s' when a field is marked as ignored")
                        % (name, field_name))

    def add_struct_field_must_be_empty_error(self, location, name, field_name):
        # type: (common.SourceLocation, unicode, unicode) -> None
        """Add an error about field must be empty for fields of type struct."""
        # pylint: disable=invalid-name
        self._add_error(location, ERROR_ID_FIELD_MUST_BE_EMPTY_FOR_IGNORED, (
            "Field '%s' cannot contain a value for property '%s' when a field's type is a struct") %
                        (name, field_name))

    def add_not_custom_scalar_serialization_not_supported_error(self, location, ast_type,
                                                                ast_parent, bson_type_name):
        # type: (common.SourceLocation, unicode, unicode, unicode) -> None
        # pylint: disable=invalid-name
        """Add an error about field must be empty for fields of type struct."""
        self._add_error(location, ERROR_ID_CUSTOM_SCALAR_SERIALIZATION_NOT_SUPPORTED, (
            "Custom serialization for a scalar is only supported for 'string'. The %s '%s' cannot" +
            " use bson type '%s', use a bson_serialization_type of 'any' instead.") %
                        (ast_type, ast_parent, bson_type_name))

    def add_bad_any_type_use_error(self, location, ast_type, ast_parent):
        # type: (common.SourceLocation, unicode, unicode) -> None
        # pylint: disable=invalid-name
        """Add an error about any being used in a list of bson types."""
        self._add_error(location, ERROR_ID_BAD_ANY_TYPE_USE, (
            "The BSON Type 'any' is not allowed in a list of bson serialization types for" +
            "%s '%s'. It must be only a single bson type.") % (ast_type, ast_parent))

    def add_bad_cpp_numeric_type_use_error(self, location, ast_type, ast_parent, cpp_type):
        # type: (common.SourceLocation, unicode, unicode, unicode) -> None
        # pylint: disable=invalid-name
        """Add an error about any being used in a list of bson types."""
        self._add_error(location, ERROR_ID_BAD_NUMERIC_CPP_TYPE, (
            "The C++ numeric type '%s' is not allowed for %s '%s'. Only 'std::int32_t'," +
            " 'std::uint32_t', 'std::uint64_t', and 'std::int64_t' are supported.") %
                        (cpp_type, ast_type, ast_parent))


def _assert_unique_error_messages():
    # type: () -> None
    """Assert that error codes are unique."""
    error_ids = []
    for module_member in inspect.getmembers(sys.modules[__name__]):
        if module_member[0].startswith("ERROR_ID"):
            error_ids.append(module_member[1])

    error_ids_set = set(error_ids)
    if len(error_ids) != len(error_ids_set):
        raise IDLError("IDL error codes prefixed with ERRROR_ID are not unique.")


# On file import, check the error messages are unique
_assert_unique_error_messages()
