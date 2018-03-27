#pragma once

#include <string>

namespace mongo {

/**
* Free Moniting Command line choices
*/
enum class EnableCloudStateEnum : std::int32_t {
    on,
    off,
    runtime,
};

/**
 * Free Monitoring configuration options
 */
struct FreeMonParams {
std::string freeMonitoringTag;
EnableCloudStateEnum freeMonitoringState = EnableCloudStateEnum::runtime;
};

FreeMonParams globalFreeMonParams;

}  // namespace mongo
