/**
 *  Copyright (C) 2016 MongoDB Inc.
 */
#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kFTDC

#include "mongo/platform/basic.h"

#include <fstream>

#include "mongo/base/init.h"
#include "mongo/base/initializer.h"
#include "mongo/db/service_context_noop.h"
#include "mongo/stdx/memory.h"
#include "mongo/util/invariant.h"
#include "mongo/util/quick_exit.h"
#include "mongo/util/signal_handlers.h"
#include "mongo/util/text.h"
#include "mongo/util/log.h"
#include "mongo/util/version.h"

#include "idl_options.h"
#include "idl_types.h"

#include "yaml-cpp/yaml.h"

namespace mongo {



StringData nodeTypeToString(YAML::NodeType::value nodeType) {
    switch (nodeType) {
    case YAML::NodeType::Undefined:
        return "Undefined"_sd;
    case YAML::NodeType::Null:
        return "Null"_sd;
    case YAML::NodeType::Scalar:
        return "Scalar"_sd;
    case YAML::NodeType::Sequence:
        return "Sequence"_sd;
    case YAML::NodeType::Map:
        return "Map"_sd;
    default:
        return "<unknown>"_sd;
    }
}

void IDLParser::parse(IDLParserContext& context, std::istream& stream) {
    try {
        YAML::Node root = YAML::Load(stream);

        if (!root.IsMap()) {
            context.addError(str::stream() << "Invalid root YAML node, expected a Map, got '" << nodeTypeToString(root.Type()) << "' instead.", root);
                return;
        }


        for (const auto& node : root) {

                const auto& first = node.first;

                if (!first.IsScalar()) {
                    context.addError(str::stream() << "Invalid YAML node, expected a Scalar, got '" << nodeTypeToString(node.Type()) << "' instead.", node);
                    // Skip this entry
                    continue;
                }

                const auto& str = first.Scalar();

                if (str == "type") {
                    parseType(context, node.second);
                }
            
        }

        std::cout << "hell";
    }
    catch (const YAML::Exception& e) {
        context.addError(str::stream() << "Error parsing YAML idl file: " << e.what());
    }
    catch (const std::runtime_error& e) {
        context.addError(str::stream() << "Unexpected exception parsing YAML idl file: " << e.what());
    }
}

// Parse the file
//
void IDLParser::parseImport(IDLParserContext& context, StringData filename) {
}

void IDLParser::parseStruct(IDLParserContext& context, const YAML::Node& node) {
}

void IDLParser::parseType(IDLParserContext& context, const YAML::Node& node) {
}

void IDLParser::loadBuiltinTypes() {
}


// Validate and Bind the AST
// dup names, etc
void IDLParser::bind() {}

void IDLParser::dump(std::ostream& stream) {}


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

    IDLParserContext context(globalIDLToolOptions->inputFile);
    IDLParser parser;
    std::fstream  fis;
    fis.open(globalIDLToolOptions->inputFile);
    parser.parse(context, fis);
    if (context.getErrors().hasErrors()) {
        
    }

    //log() << "Parser Status " << s;


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
