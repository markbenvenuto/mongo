#pragma once

#include <istream>
#include <map>
#include <unordered_map>
#include <yaml-cpp/yaml.h>

#include "mongo/base/status.h"
#include "mongo/base/string_data.h"
#include "mongo/stdx/memory.h"
#include "mongo/util/invariant.h"
#include "mongo/util/text.h"

#include "idl_options.h"

namespace mongo {

class IDLFileLineInfo {
public:
    IDLFileLineInfo(std::string file, int line, int column) : _file(file), _line(line), _column(column) { }
private:
    std::string _file;
    int _line;
    int _column;
};

class IDLParserContext {
public:
    StringData getCurrentFile() { return _file; }
private:
    std::string _file;
};

enum class IDLTypeKind {
    scalar,
    list,
};

class IDLObject {
public:
    //void dump(std::ostream& stream);
private:
    bool _imported;
    IDLFileLineInfo _location;
};

// Not used for code generation
class IDLType : public IDLObject {
public:
    static std::unique_ptr<IDLType> create(StringData name);
    void dump(std::ostream& stream);
private:
    std::string _name;
};

// Merges with IDLType during bind
class IDLFieldType {
public:
    static std::unique_ptr<IDLFieldType> create(StringData name);
    void dump(std::ostream& stream);
private:
    std::string _name;
    // default, required
    // min, max?
};

class IDLField :public IDLObject {
public:
    static std::unique_ptr<IDLField> create(StringData name);
    void dump(std::ostream& stream);
private:
    std::string _name;
    // alias, 
    std::unique_ptr<IDLFieldType> _fieldType;
};

class IDLStruct : public IDLObject {
public:
    static std::unique_ptr<IDLStruct> create(StringData name);
    void dump(std::ostream& stream);
private:
    std::string _name;
    std::map<std::string, std::unique_ptr<IDLField>> _fields;
};

class IDLSymbolTable {
public:
    Status addStruct(std::unique_ptr<IDLStruct> structure);
    Status addType(std::unique_ptr<IDLType> type);
private:
    std::map<std::string, std::unique_ptr<IDLStruct>> _structs;
    std::map<std::string, std::unique_ptr<IDLType>> _types;
};

class IDLParser {
public:
    Status parse(std::istream& stream);
private:
    // Parse the file
    //
    Status parseImport(StringData filename);
    Status parseStruct(const IDLParserContext& context, const YAML::Node& node);
    Status parseType(const IDLParserContext& context, const YAML::Node& node);
    Status loadBuiltinTypes();

    // Validate and Bind the AST
    // dup names, etc
    void bind();

    void dump(std::ostream& stream);
private:
    IDLSymbolTable _symbolTable;
};

class IDLGenerator {
public:
    void generate(IDLSymbolTable symbolTable);
};

class IntendedTextWriter {
public:

};

} // namespace mongo