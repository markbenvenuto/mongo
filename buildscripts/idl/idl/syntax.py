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
IDL Parser Syntax classes.

These class represent the structure of the raw IDL document.
It maps 1-1 to the YAML file, and has not been checked if
it follows the rules of the IDL, etc.
"""

from __future__ import absolute_import, print_function, unicode_literals

from typing import List, Union, Any, Optional, Tuple

from . import common
from . import errors


class IDLParsedSpec(object):
    """A parsed IDL document or a set of errors if parsing failed."""

    def __init__(self, spec, error_collection):
        # type: (IDLSpec, errors.ParserErrorCollection) -> None
        """Must specify either an IDL document or errors, not both."""
        assert (spec is None and error_collection is not None) or (spec is not None and
                                                                   error_collection is None)
        self.spec = spec
        self.errors = error_collection


class IDLSpec(object):
    """
    The in-memory representation of an IDL file.

    - Includes all imported files.
    """

    def __init__(self):
        # type: () -> None
        """Construct an IDL spec."""
        self.symbols = SymbolTable()  # type: SymbolTable
        self.globals = None  # type: Optional[Global]
        self.imports = None  # type: Optional[Import]


def parse_array_type(name):
    # type: (unicode) -> unicode
    """Parse a type name of the form 'array<type>' and extract type."""
    if not name.startswith("array<") and not name.endswith(">"):
        return None

    name = name[len("array<"):]
    name = name[:-1]

    # V1 restriction, ban nested array types to reduce scope.
    if name.startswith("array<") and name.endswith(">"):
        return None

    return name


class SymbolTable(object):
    """
    IDL Symbol Table.

    - Contains all information to resolve commands, types, and structs.
    - Checks for duplicate names across the union of (commands, types, structs)
    """

    def __init__(self):
        # type: () -> None
        """Construct an empty symbol table."""
        self.types = []  # type: List[Type]
        self.structs = []  # type: List[Struct]
        self.commands = []  # type: List[Command]

    def _is_duplicate(self, ctxt, location, name, duplicate_class_name):
        # type: (errors.ParserContext, common.SourceLocation, unicode, unicode) -> bool
        """Return true if the given item already exist in the symbol table."""
        for command in self.commands:
            if command.name == name:
                ctxt.add_duplicate_symbol_error(location, name, duplicate_class_name, 'command')
                return True

        for struct in self.structs:
            if struct.name == name:
                ctxt.add_duplicate_symbol_error(location, name, duplicate_class_name, 'struct')
                return True

        for idltype in self.types:
            if idltype.name == name:
                ctxt.add_duplicate_symbol_error(location, name, duplicate_class_name, 'type')
                return True

        return False

    def add_struct(self, ctxt, struct):
        # type: (errors.ParserContext, Struct) -> None
        """Add an IDL struct to the symbol table and check for duplicates."""
        if not self._is_duplicate(ctxt, struct, struct.name, "struct"):
            self.structs.append(struct)

    def add_type(self, ctxt, idltype):
        # type: (errors.ParserContext, Type) -> None
        """Add an IDL type to the symbol table and check for duplicates."""
        if not self._is_duplicate(ctxt, idltype, idltype.name, "type"):
            self.types.append(idltype)

    def add_command(self, ctxt, command):
        # type: (errors.ParserContext, Command) -> None
        """Add an IDL command to the symbol table and check for duplicates."""
        if not self._is_duplicate(ctxt, command, command.name, "command"):
            self.commands.append(command)

    def add_imported_symbol_table(self, ctxt, imported_symbols):
        # type: (errors.ParserContext, SymbolTable) -> None
        """
        Merge all the symbols in the imported_symbols symbol table into the symbol table.

        Marks imported structs as imported, and errors on duplicate symbols.
        """
        for command in imported_symbols.commands:
            if not self._is_duplicate(ctxt, command, command.name, "command"):
                command.imported = True
                self.commands.append(command)

        for struct in imported_symbols.structs:
            if not self._is_duplicate(ctxt, struct, struct.name, "struct"):
                struct.imported = True
                self.structs.append(struct)

        for idltype in imported_symbols.types:
            self.add_type(ctxt, idltype)

    def resolve_field_type(self, ctxt, field):
        # type: (errors.ParserContext, Field) -> Optional[Union[Command, Struct, Type]]
        """Find the type or struct a field refers to or log an error."""
        return self._resolve_field_type(ctxt, field, field.type)

    def _resolve_field_type(self, ctxt, field, type_name):
        # type: (errors.ParserContext, Field, unicode) ->  Optional[Union[Command, Struct, Type]]
        """Find the type or struct a field refers to or log an error."""
        for command in self.commands:
            if command.name == type_name:
                return command

        for struct in self.structs:
            if struct.name == type_name:
                return struct

        for idltype in self.types:
            if idltype.name == type_name:
                return idltype

        if type_name.startswith('array<'):
            array_type_name = parse_array_type(type_name)
            if not array_type_name:
                ctxt.add_bad_array_type_name(field, field.name, type_name)
                return None

            return self._resolve_field_type(ctxt, field, array_type_name)

        ctxt.add_unknown_type_error(field, field.name, type_name)

        return None


class Global(common.SourceLocation):
    """
    IDL global object container.

    Not all fields may be populated. If they do not exist in the source document, they are not
    populated.
    """

    def __init__(self, file_name, line, column):
        # type: (unicode, int, int) -> None
        """Construct a Global."""
        self.cpp_namespace = None  # type: unicode
        self.cpp_includes = []  # type: List[unicode]
        super(Global, self).__init__(file_name, line, column)


class Import(common.SourceLocation):
    """IDL imports object."""

    def __init__(self, file_name, line, column):
        # type: (unicode, int, int) -> None
        """Construct an Imports section."""
        self.imports = []  # type: List[unicode]

        # These are not part of the IDL syntax but are produced by the parser.
        # List of imports with structs.
        self.resolved_imports = []  # type: List[unicode]
        # All imports directly or indirectly included
        self.dependencies = []  # type: List[unicode]

        super(Import, self).__init__(file_name, line, column)


class Type(common.SourceLocation):
    """
    Stores all type information about an IDL type.

    The fields name, description, cpp_type, and bson_serialization_type are required.
    Other fields may be populated. If they do not exist in the source document, they are not
    populated.
    """

    # pylint: disable=too-many-instance-attributes

    def __init__(self, file_name, line, column):
        # type: (unicode, int, int) -> None
        """Construct a Type."""
        self.name = None  # type: unicode
        self.description = None  # type: unicode
        self.cpp_type = None  # type: unicode
        self.bson_serialization_type = None  # type: List[unicode]
        self.bindata_subtype = None  # type: unicode
        self.serializer = None  # type: unicode
        self.deserializer = None  # type: unicode
        self.default = None  # type: unicode

        super(Type, self).__init__(file_name, line, column)


class Field(common.SourceLocation):
    """
    An instance of a field in a struct.

    The fields name, and type are required.
    Other fields may be populated. If they do not exist in the source document, they are not
    populated.
    """

    # pylint: disable=too-many-instance-attributes

    def __init__(self, file_name, line, column):
        # type: (unicode, int, int) -> None
        """Construct a Field."""
        self.name = None  # type: unicode
        self.cpp_name = None  # type: unicode
        self.description = None  # type: unicode
        self.type = None  # type: unicode
        self.ignore = False  # type: bool
        self.optional = False  # type: bool
        self.default = None  # type: unicode

        super(Field, self).__init__(file_name, line, column)


class Struct(common.SourceLocation):
    """
    IDL struct information.

    All fields are either required or have a non-None default.
    """

    def __init__(self, file_name, line, column):
        # type: (unicode, int, int) -> None
        """Construct a Struct."""
        self.name = None  # type: unicode
        self.description = None  # type: unicode
        self.strict = True  # type: bool
        self.fields = None  # type: List[Field]

        # Internal property that is not represented as syntax. An imported struct is read from an
        # imported file, and no code is generated for it.
        self.imported = False  # type: bool

        super(Struct, self).__init__(file_name, line, column)


class Command(Struct):
    """
    IDL command information, a subtype of Struct.

    Namespace is required.
    """

    def __init__(self, file_name, line, column):
        # type: (unicode, int, int) -> None
        """Construct a Struct."""
        self.namespace = None  # type: unicode

        super(Command, self).__init__(file_name, line, column)
