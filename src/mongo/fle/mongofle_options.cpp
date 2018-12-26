
/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kBridge

#include "mongo/fle/mongofle_options.h"

#include <algorithm>
#include <iostream>

#include "mongo/base/status.h"
#include "mongo/platform/random.h"
#include "mongo/util/log.h"
#include "mongo/util/options_parser/startup_options.h"

namespace mongo {

MongoFLEGlobalParams mongoFLEGlobalParams;

Status addMongoFLEOptions(moe::OptionSection* options) {
    options->addOptionChaining("help", "help", moe::Switch, "show this usage information");

    options->addOptionChaining("port", "port", moe::Int, "port to listen on for MongoDB messages");

    options->addOptionChaining("verbose", "verbose", moe::String, "log more verbose output")
        .setImplicit(moe::Value(std::string("v")));

    return Status::OK();
}

void printMongoFLEHelp(std::ostream* out) {
    *out << "Usage: mongobridge --port <port> --dest <dest> [ --seed <seed> ] [ --verbose <vvv> ]"
            " [ --help ]"
         << std::endl;
    *out << moe::startupOptions.helpString();
    *out << std::flush;
}

bool handlePreValidationMongoFLEOptions(const moe::Environment& params) {
    if (params.count("help")) {
        printMongoFLEHelp(&std::cout);
        return false;
    }
    return true;
}

Status storeMongoFLEOptions(const moe::Environment& params,
                               const std::vector<std::string>& args) {
    if (!params.count("port")) {
        return {ErrorCodes::BadValue, "Missing required option: --port"};
    }


    mongoFLEGlobalParams.port = params["port"].as<int>();

    if (params.count("verbose")) {
        std::string verbosity = params["verbose"].as<std::string>();
        if (std::any_of(verbosity.cbegin(), verbosity.cend(), [](char ch) { return ch != 'v'; })) {
            return {ErrorCodes::BadValue,
                    "The string for the --verbose option cannot contain characters other than 'v'"};
        }
        logger::globalLogDomain()->setMinimumLoggedSeverity(
            logger::LogSeverity::Debug(verbosity.length()));
    }

    return Status::OK();
}

}  // namespace mongo
