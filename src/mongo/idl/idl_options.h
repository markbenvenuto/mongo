/*
 *    Copyright (C) 2016 MongoDB Inc.
 */

#pragma once

#include "mongo/base/status.h"

#include <iostream>
#include <string>
#include <vector>

namespace mongo {

namespace optionenvironment {
class OptionSection;
class Environment;
}  // namespace optionenvironment

namespace moe = mongo::optionenvironment;

struct IDLToolOptions {
    bool color;
    std::string inputFile;
    std::string outputDirectory;
};

extern IDLToolOptions* globalIDLToolOptions;

Status addIDLToolOptions(moe::OptionSection* options);

Status storeIDLToolOptions(const moe::Environment& params, const std::vector<std::string>& args);

}  // mongo
