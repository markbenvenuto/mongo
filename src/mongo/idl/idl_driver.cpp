/**
 *  Copyright (C) 2016 MongoDB Inc.
 */

#include "mongo/platform/basic.h"

#include "mongo/base/init.h"
#include "mongo/base/initializer.h"
#include "mongo/db/service_context_noop.h"
#include "mongo/stdx/memory.h"
#include "mongo/util/invariant.h"
#include "mongo/util/quick_exit.h"
#include "mongo/util/signal_handlers.h"
#include "mongo/util/text.h"
#include "mongo/util/version.h"

#include "idl_options.h"

#include "yaml-cpp/yaml.h"

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
    StringData getCurrentFile() { return _file;  }
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
    void parse(std::istream& stream);
private:
    // Parse the file
    //
    void parseImport(StringData filename);
    void parseStruct(const IDLParserContext& context, const YAML::Node& node);
    void parseType(const IDLParserContext& context, const YAML::Node& node);
    void loadBuiltinTypes();

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

namespace {

MONGO_INITIALIZER(SetGlobalEnvironment)(InitializerContext* context) {
    setGlobalServiceContext(stdx::make_unique<ServiceContextNoop>());
    return Status::OK();
}

int idlToolMain(int argc, char* argv[], char** envp) {
    setupSignalHandlers();
    runGlobalInitializersOrDie(argc, argv, envp);
    startSignalProcessingThread();

    std::cout << "Welcome" << std::endl;

    // Basic Steps
    // 1. Parse Document
    // 2. Validate AST
    // 3. Generate Code


    return 0;
}

}  // namespace
}  // namespace mongo

#if defined(_WIN32)
// In Windows, wmain() is an alternate entry point for main(), and receives the same parameters
// as main() but encoded in Windows Unicode (UTF16); "wide" 16-bit wchar_t characters.  The
// WindowsCommandLine object converts these wide character strings to a UTF-8 coded equivalent
// and makes them available through the argv() and envp() members.  This enables decryptToolMain()
// to process UTF-8 encoded arguments and environment variables without regard to platform.
int wmain(int argc, wchar_t* argvW[], wchar_t* envpW[]) {
    mongo::WindowsCommandLine wcl(argc, argvW, envpW);
    int exitCode = mongo::idlToolMain(argc, wcl.argv(), wcl.envp());
    mongo::quickExit(exitCode);
}
#else
int main(int argc, char* argv[], char** envp) {
    int exitCode = mongo::ldapToolMain(argc, argv, envp);
    mongo::quickExit(exitCode);
}
#endif
