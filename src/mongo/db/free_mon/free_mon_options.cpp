
#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kFTDC


#include "mongo/platform/basic.h"

#include "mongo/db/free_mon/free_mon_mongod.h"

#include "mongo/base/data_type_validated.h"
#include "mongo/db/free_mon/free_monitoring_controller.h"
#include "mongo/db/free_mon/free_monitoring_http.h"
#include "mongo/db/ftdc/constants.h"
#include "mongo/db/ftdc/ftdc_server.h"
#include "mongo/db/jsobj.h"
#include "mongo/db/repl/replication_coordinator.h"
#include "mongo/db/server_parameters.h"
#include "mongo/db/service_context.h"
#include "mongo/executor/network_interface_factory.h"
#include "mongo/executor/thread_pool_task_executor.h"
#include "mongo/rpc/object_check.h"
#include "mongo/util/concurrency/thread_pool.h"
#include "mongo/util/log.h"
#include "mongo/util/options_parser/startup_option_init.h"
#include "mongo/util/options_parser/startup_options.h"

namespace mongo {

namespace optionenvironment {
class OptionSection;
class Environment;
}  // namespace optionenvironment

namespace moe = mongo::optionenvironment;

namespace {

constexpr StringData kEnableCloudState_on = "on"_sd;
constexpr StringData kEnableCloudState_off = "off"_sd;
constexpr StringData kEnableCloudState_runtime = "runtime"_sd;

StatusWith<EnableCloudStateEnum> EnableCloudState_parse(StringData value) {
    if (value == kEnableCloudState_on) {
        return EnableCloudStateEnum::on;
    }
    if (value == kEnableCloudState_off) {
        return EnableCloudStateEnum::off;
    }
    if (value == kEnableCloudState_runtime) {
        return EnableCloudStateEnum::runtime;
    }

    // TODO
    return Status(ErrorCodes::InvalidOptions, "Unrecognized state");
}

Status addFreeMonitoringOptions(moe::OptionSection* options) {
    moe::OptionSection freeMonitoringOptions("Free Monitoring options");

    // Command Line: --enableFreeMonitoring=<on|runtime|off>
    // YAML Name: cloud.monitoring.free=<on|runtime|off>
    freeMonitoringOptions.addOptionChaining("cloud.monitoring.free.state",
                                            "enableFreeMonitoring",
                                            moe::String,
                                            "Enable Cloud Free Monitoring (on|runtime|off)");

    // Command Line: --enableFreeMonitoringTag=string
    // YAML Name: cloud.monitoring.free.tag=string
    freeMonitoringOptions.addOptionChaining(
        "cloud.monitoring.free.tag", "freeMonitoringTag", moe::String, "Cloud Free Monitoring Tag");

    Status ret = options->addSection(freeMonitoringOptions);
    if (!ret.isOK()) {
        error() << "Failed to add free monitoring option section: " << ret.toString();
        return ret;
    }

    return Status::OK();
}

Status storeFreeMonitoringOptions(const moe::Environment& params) {

    if (params.count("cloud.monitoring.free.state")) {
        auto swState =
            EnableCloudState_parse(params["cloud.monitoring.free.state"].as<std::string>());
        if (!swState.isOK()) {
            return swState.getStatus();
        }
        globalFreeMonParams.freeMonitoringState = swState.getValue();
    }

    if (params.count("cloud.monitoring.free.tag")) {
        globalFreeMonParams.freeMonitoringTag = params["cloud.monitoring.free.tag"].as<std::string>();
    }

    return Status::OK();
}


MONGO_MODULE_STARTUP_OPTIONS_REGISTER(FreeMonitoringOptions)(InitializerContext* context) {
    return addFreeMonitoringOptions(&moe::startupOptions);
}

MONGO_STARTUP_OPTIONS_STORE(FreeMonitoringOptions)(InitializerContext* context) {
    return storeFreeMonitoringOptions(moe::startupOptionsParsed);
}

}  // namespace

}  // namespace mongo
