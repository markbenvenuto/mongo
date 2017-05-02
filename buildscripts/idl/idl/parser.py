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
IDL Parser.

Converts a YAML document to an idl.syntax tree.
Only validates the document is syntatically correct, not semantically.
"""
from __future__ import absolute_import, print_function, unicode_literals

from abc import ABCMeta, abstractmethod
import io
import yaml
from yaml import nodes
from typing import Any, Callable, Dict, List, Set, Tuple, Union

from . import common
from . import errors
from . import syntax


class _RuleDesc(object):
    """
    Describe a simple parser rule for the generic YAML node parser.

    node_type is either (scalar, scalar_bool, scalar_or_sequence, or mapping)
    - scalar_bool - means a scalar node which is a valid bool, populates a bool
    - scalar_or_sequence - means a scalar or sequence node, populates a list
    mapping_parser_func is only called when parsing a mapping yaml node
    """

    # TODO: after porting to Python 3, use an enum
    REQUIRED = 1
    OPTIONAL = 2

    def __init__(self, node_type, required=OPTIONAL, mapping_parser_func=None):
        # type: (unicode, int, Callable[[errors.ParserContext,yaml.nodes.MappingNode], Any]) -> None
        """Construct a parser rule description."""
        assert required == _RuleDesc.REQUIRED or required == _RuleDesc.OPTIONAL

        self.node_type = node_type  # type: unicode
        self.required = required  # type: int
        self.mapping_parser_func = mapping_parser_func  # type: Callable[[errors.ParserContext,yaml.nodes.MappingNode], Any]


def _generic_parser(
        ctxt,  # type: errors.ParserContext
        node,  # type: Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode]
        syntax_node_name,  # type: unicode
        syntax_node,  # type: Any
        mapping_rules  # type: Dict[unicode, _RuleDesc]
):
    # pylint: disable=too-many-branches
    field_name_set = set()  # type: Set[str]

    for node_pair in node.value:
        first_node = node_pair[0]
        second_node = node_pair[1]

        first_name = first_node.value

        if first_name in field_name_set:
            ctxt.add_duplicate_error(first_node, first_name)
            continue

        if first_name in mapping_rules:
            rule_desc = mapping_rules[first_name]

            if rule_desc.node_type == "scalar":
                if ctxt.is_scalar_node(second_node, first_name):
                    syntax_node.__dict__[first_name] = second_node.value
            elif rule_desc.node_type == "bool_scalar":
                if ctxt.is_scalar_bool_node(second_node, first_name):
                    syntax_node.__dict__[first_name] = ctxt.get_bool(second_node)
            elif rule_desc.node_type == "scalar_or_sequence":
                if ctxt.is_scalar_sequence_or_scalar_node(second_node, first_name):
                    syntax_node.__dict__[first_name] = ctxt.get_list(second_node)
            elif rule_desc.node_type == "sequence":
                if ctxt.is_scalar_sequence(second_node, first_name):
                    syntax_node.__dict__[first_name] = ctxt.get_list(second_node)
            elif rule_desc.node_type == "mapping":
                if ctxt.is_mapping_node(second_node, first_name):
                    syntax_node.__dict__[first_name] = rule_desc.mapping_parser_func(ctxt,
                                                                                     second_node)
            else:
                raise errors.IDLError("Unknown node_type '%s' for parser rule" %
                                      (rule_desc.node_type))
        else:
            ctxt.add_unknown_node_error(first_node, syntax_node_name)

        field_name_set.add(first_name)

    # Check for any missing required fields
    for name, rule_desc in mapping_rules.items():
        if not rule_desc.required == _RuleDesc.REQUIRED:
            continue

        # A bool is never "None" like other types, it simply defaults to "false".
        # It means "if bool is None" will always return false and there is no support for required
        # 'bool' at this time.
        if not rule_desc.node_type == 'bool_scalar':
            if syntax_node.__dict__[name] is None:
                ctxt.add_missing_required_field_error(node, syntax_node_name, name)
        else:
            raise errors.IDLError("Unknown node_type '%s' for parser required rule" %
                                  (rule_desc.node_type))


def _parse_global(ctxt, spec, node):
    # type: (errors.ParserContext, syntax.IDLSpec, Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode]) -> None
    """Parse a global section in the IDL file."""
    if not ctxt.is_mapping_node(node, "global"):
        return

    idlglobal = syntax.Global(ctxt.file_name, node.start_mark.line, node.start_mark.column)

    _generic_parser(ctxt, node, "global", idlglobal, {
        "cpp_namespace": _RuleDesc("scalar"),
        "cpp_includes": _RuleDesc("scalar_or_sequence"),
    })

    if spec.globals:
        ctxt.add_duplicate_error(node, "global")
        return

    spec.globals = idlglobal


def _parse_imports(ctxt, spec, node):
    # type: (errors.ParserContext, syntax.IDLSpec, Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode]) -> None
    """Parse an imports section in the IDL file."""
    if not ctxt.is_scalar_sequence(node, "imports"):
        return

    if spec.imports:
        ctxt.add_duplicate_error(node, "imports")
        return

    imports = syntax.Import(ctxt.file_name, node.start_mark.line, node.start_mark.column)
    imports.imports = ctxt.get_list(node)
    spec.imports = imports


def _parse_type(ctxt, spec, name, node):
    # type: (errors.ParserContext, syntax.IDLSpec, unicode, Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode]) -> None
    """Parse a type section in the IDL file."""
    if not ctxt.is_mapping_node(node, "type"):
        return

    idltype = syntax.Type(ctxt.file_name, node.start_mark.line, node.start_mark.column)
    idltype.name = name

    _generic_parser(ctxt, node, "type", idltype, {
        "description": _RuleDesc('scalar', _RuleDesc.REQUIRED),
        "cpp_type": _RuleDesc('scalar', _RuleDesc.REQUIRED),
        "bson_serialization_type": _RuleDesc('scalar_or_sequence', _RuleDesc.REQUIRED),
        "bindata_subtype": _RuleDesc('scalar'),
        "serializer": _RuleDesc('scalar'),
        "deserializer": _RuleDesc('scalar'),
        "default": _RuleDesc('scalar'),
    })

    spec.symbols.add_type(ctxt, idltype)


def _parse_types(ctxt, spec, node):
    # type: (errors.ParserContext, syntax.IDLSpec, Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode]) -> None
    """Parse a types section in the IDL file."""
    if not ctxt.is_mapping_node(node, "types"):
        return

    for node_pair in node.value:
        first_node = node_pair[0]
        second_node = node_pair[1]

        first_name = first_node.value

        _parse_type(ctxt, spec, first_name, second_node)


def _parse_field(ctxt, name, node):
    # type: (errors.ParserContext, str, Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode]) -> syntax.Field
    """Parse a field in a struct/command in the IDL file."""
    field = syntax.Field(ctxt.file_name, node.start_mark.line, node.start_mark.column)
    field.name = name

    _generic_parser(ctxt, node, "field", field, {
        "description": _RuleDesc('scalar'),
        "cpp_name": _RuleDesc('scalar'),
        "type": _RuleDesc('scalar', _RuleDesc.REQUIRED),
        "ignore": _RuleDesc("bool_scalar"),
        "optional": _RuleDesc("bool_scalar"),
        "default": _RuleDesc('scalar'),
    })

    return field


def _parse_fields(ctxt, node):
    # type: (errors.ParserContext, yaml.nodes.MappingNode) -> List[syntax.Field]
    """Parse a fields section in a struct in the IDL file."""

    fields = []

    field_name_set = set()  # type: Set[str]

    for node_pair in node.value:
        first_node = node_pair[0]
        second_node = node_pair[1]

        first_name = first_node.value

        if first_name in field_name_set:
            ctxt.add_duplicate_error(first_node, first_name)
            continue

        # Simple Type
        if second_node.id == "scalar":
            field = syntax.Field(ctxt.file_name, node.start_mark.line, node.start_mark.column)
            field.name = first_name
            field.type = second_node.value
            fields.append(field)
        else:
            field = _parse_field(ctxt, first_name, second_node)
            fields.append(field)

        field_name_set.add(first_name)

    return fields


def _parse_struct(ctxt, spec, name, node):
    # type: (errors.ParserContext, syntax.IDLSpec, unicode, Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode]) -> None
    """Parse a struct section in the IDL file."""
    if not ctxt.is_mapping_node(node, "struct"):
        return

    struct = syntax.Struct(ctxt.file_name, node.start_mark.line, node.start_mark.column)
    struct.name = name

    _generic_parser(ctxt, node, "struct", struct, {
        "description": _RuleDesc('scalar', _RuleDesc.REQUIRED),
        "fields": _RuleDesc('mapping', mapping_parser_func=_parse_fields),
        "chained_types": _RuleDesc('sequence'),
        "chained_structs": _RuleDesc('sequence'),
        "strict": _RuleDesc("bool_scalar"),
    })

    # TODO: SHOULD WE ALLOW STRUCTS ONLY WITH CHAINED STUFF and no fields???
    if struct.fields is None and struct.chained_types is None and struct.chained_structs is None:
        ctxt.add_empty_struct_error(node, struct.name)

    spec.symbols.add_struct(ctxt, struct)


def _parse_structs(ctxt, spec, node):
    # type: (errors.ParserContext, syntax.IDLSpec, Union[yaml.nodes.MappingNode, yaml.nodes.ScalarNode, yaml.nodes.SequenceNode]) -> None
    """Parse a structs section in the IDL file."""
    if not ctxt.is_mapping_node(node, "structs"):
        return

    for node_pair in node.value:
        first_node = node_pair[0]
        second_node = node_pair[1]

        first_name = first_node.value

        _parse_struct(ctxt, spec, first_name, second_node)


def _parse(stream, error_file_name):
    # type: (Any, unicode) -> syntax.IDLParsedSpec
    """
    Parse a YAML document into an idl.syntax tree.

    stream: is a io.Stream.
    error_file_name: just a file name for error messages to use.
    """

    # This will raise an exception if the YAML parse fails
    root_node = yaml.compose(stream)

    ctxt = errors.ParserContext(error_file_name, errors.ParserErrorCollection())

    spec = syntax.IDLSpec()

    # If the document is empty, we are done
    if not root_node:
        return syntax.IDLParsedSpec(spec, None)

    if not root_node.id == "mapping":
        raise errors.IDLError(
            "Expected a YAML mapping node as root node of IDL document, got '%s' instead" %
            root_node.id)

    field_name_set = set()  # type: Set[str]

    for node_pair in root_node.value:
        first_node = node_pair[0]
        second_node = node_pair[1]

        first_name = first_node.value

        if first_name in field_name_set:
            ctxt.add_duplicate_error(first_node, first_name)
            continue

        if first_name == "global":
            _parse_global(ctxt, spec, second_node)
        elif first_name == "imports":
            _parse_imports(ctxt, spec, second_node)
        elif first_name == "types":
            _parse_types(ctxt, spec, second_node)
        elif first_name == "structs":
            _parse_structs(ctxt, spec, second_node)
        else:
            ctxt.add_unknown_root_node_error(first_node)

        field_name_set.add(first_name)

    if ctxt.errors.has_errors():
        return syntax.IDLParsedSpec(None, ctxt.errors)
    else:
        return syntax.IDLParsedSpec(spec, None)


class ImportResolverBase(object):
    """Base class for resolving imported files."""

    __metaclass__ = ABCMeta

    def __init__(self):
        # type: () -> None
        """Construct a ImportResolver."""
        pass

    @abstractmethod
    def resolve(self, base_file, imported_file_name):
        # type: (unicode, unicode) -> unicode
        """Return the complete path to an imported file name."""
        pass

    @abstractmethod
    def open(self, resolved_file_name):
        # type: (unicode) -> Any
        """Return an io.Stream for the requested file."""
        pass


def parse(stream, input_file_name, resolver):
    # type: (Any, unicode, ImportResolverBase) -> syntax.IDLParsedSpec
    """
    Parse a YAML document into an idl.syntax tree.

    stream: is a io.Stream.
    input_file_name: a file name for error messages to use, and to help resolve imported files.
    """
    # pylint: disable=too-many-locals

    root_doc = _parse(stream, input_file_name)

    if root_doc.errors:
        return root_doc

    imports = []  # type: List[Tuple[common.SourceLocation, unicode, unicode]]
    needs_include = []  # type: List[unicode]
    if root_doc.spec.imports:
        imports = [(root_doc.spec.imports, input_file_name, import_file_name)
                   for import_file_name in root_doc.spec.imports.imports]

    resolved_file_names = []  # type: List[unicode]

    ctxt = errors.ParserContext(input_file_name, errors.ParserErrorCollection())

    # Process imports in a breadth-first search
    while imports:
        file_import_tuple = imports[0]
        imports = imports[1:]

        import_location = file_import_tuple[0]
        base_file_name = file_import_tuple[1]
        imported_file_name = file_import_tuple[2]

        # Check for already resolved file
        resolved_file_name = resolver.resolve(base_file_name, imported_file_name)
        if not resolved_file_name:
            ctxt.add_cannot_find_import(import_location, imported_file_name)
            return syntax.IDLParsedSpec(None, ctxt.errors)

        if resolved_file_name in resolved_file_names:
            continue

        resolved_file_names.append(resolved_file_name)

        # Parse imported file
        with resolver.open(resolved_file_name) as file_stream:
            parsed_doc = _parse(file_stream, resolved_file_name)

        # Check for errors
        if parsed_doc.errors:
            return parsed_doc

        # We need to generate includes for imported IDL files which have structs
        if base_file_name == input_file_name and len(parsed_doc.spec.symbols.structs):
            needs_include.append(imported_file_name)

        # Add other imported files to the list of files to parse
        if parsed_doc.spec.imports:
            imports += [(parsed_doc.spec.imports, resolved_file_name, import_file_name)
                        for import_file_name in parsed_doc.spec.imports.imports]

        # Merge symbol tables together
        root_doc.spec.symbols.add_imported_symbol_table(ctxt, parsed_doc.spec.symbols)
        if ctxt.errors.has_errors():
            return syntax.IDLParsedSpec(None, ctxt.errors)

    # Resolve the direct imports which contain structs for root document so they can be translated
    # into include file paths in generated code.
    for needs_include_name in needs_include:
        resolved_file_name = resolver.resolve(base_file_name, needs_include_name)
        root_doc.spec.imports.resolved_imports.append(resolved_file_name)

    if root_doc.spec.imports:
        root_doc.spec.imports.dependencies = resolved_file_names

    return root_doc
