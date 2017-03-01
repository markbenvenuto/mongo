/*
 *    Copyright (C) 2016 MongoDB Inc.
 */

#include "idl_options.h"

#include "mongo/util/exit_code.h"
#include "mongo/util/options_parser/options_parser.h"
#include "mongo/util/options_parser/startup_option_init.h"
#include "mongo/util/options_parser/startup_options.h"
#include "mongo/util/quick_exit.h"
#include "mongo/util/version.h"

namespace mongo {

namespace {

void printIDLToolHelp(std::ostream& out) {
    out << "Usage: mongoIDL [options] " << std::endl
        << "Version " << mongo::VersionInfoInterface::instance().version() << std::endl
        << std::endl
        << moe::startupOptions.helpString() << std::flush;
}

bool handlePreValidationIDLToolOptions(const moe::Environment& params) {
    if (params.count("help")) {
        printIDLToolHelp(std::cout);
        return false;
    }
    return true;
}
}  // namespace

Status addIDLToolOptions(moe::OptionSection* options) {
    options->addOptionChaining("help", "help", moe::Switch, "produce help message");
    options
        ->addOptionChaining(
            "input", "input,i", moe::String, "idl file to generate code for.").setSources(moe::SourceAllLegacy);

    options
        ->addOptionChaining(
            "output", "output", moe::String, "output directory").setSources(moe::SourceAllLegacy);

    options->addOptionChaining("color", "color", moe::Bool, "Enable colored output")
        .setSources(moe::SourceAllLegacy);

    return Status::OK();
}

Status storeIDLToolOptions(const moe::Environment& params, const std::vector<std::string>& args) {

    if (!params.count("input")) {
        return Status(ErrorCodes::BadValue, "Missing required option: \"--input\"");
    }
    globalIDLToolOptions->inputFile = params["input"].as<std::string>();

    if (!params.count("output")) {
        return Status(ErrorCodes::BadValue, "Missing required option: \"--output\"");
    }
    globalIDLToolOptions->outputDirectory = params["output"].as<std::string>();

    if (params.count("color")) {
        globalIDLToolOptions->color = params["color"].as<bool>();
    } else {
#ifdef _WIN32
        globalIDLToolOptions->color = false;
#else
        globalIDLToolOptions->color = true;
#endif
    }
    return Status::OK();
}

MONGO_GENERAL_STARTUP_OPTIONS_REGISTER(MongoIDLToolOptions)(InitializerContext* context) {
    return addIDLToolOptions(&moe::startupOptions);
}

MONGO_STARTUP_OPTIONS_VALIDATE(MongoIDLToolOptions)(InitializerContext* context) {
    if (!handlePreValidationIDLToolOptions(moe::startupOptionsParsed)) {
        quickExit(EXIT_SUCCESS);
    }
    return moe::startupOptionsParsed.validate();
}

MONGO_STARTUP_OPTIONS_STORE(MongoIDLToolOptions)(InitializerContext* context) {
    globalIDLToolOptions = new IDLToolOptions();

    Status ret = storeIDLToolOptions(moe::startupOptionsParsed, context->args());
    if (!ret.isOK()) {
        std::cerr << ret.toString() << std::endl;
        std::cerr << "try '" << context->args()[0] << " --help' for more information" << std::endl;
        quickExit(EXIT_BADOPTIONS);
    }
    return Status::OK();
}

IDLToolOptions* globalIDLToolOptions;

}  // namespace mongo
