
#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kFTDC


#include "mongo/platform/basic.h"

#include "mongo/db/free_mon/free_mon_mongod.h"

#include "mongo/db/free_mon/free_monitoring_controller.h"
#include "mongo/db/free_mon/free_monitoring_http.h"
#include "mongo/db/ftdc/ftdc_server.h"
#include "mongo/db/ftdc/constants.h"
#include "mongo/db/server_parameters.h"
#include "mongo/db/service_context.h"
#include "mongo/util/log.h"
#include "mongo/util/options_parser/startup_option_init.h"
#include "mongo/util/options_parser/startup_options.h"
#include "mongo/base/data_type_validated.h"
#include "mongo/rpc/object_check.h"
#include "mongo/db/jsobj.h"
#include "mongo/db/repl/replication_coordinator.h"
#include "mongo/executor/thread_pool_task_executor.h"
#include "mongo/executor/network_interface_factory.h"
#include "mongo/util/concurrency/thread_pool.h"

namespace mongo {

namespace {

const auto getFreeMonController = ServiceContext::declareDecoration<std::unique_ptr<FreeMonController>>();

FreeMonController* getGlobalFreeMonController() {
    if (!hasGlobalServiceContext()) {
        return nullptr;
    }

    return getFreeMonController(getGlobalServiceContext()).get();
}


/**
* Expose cloudFreeMonitoringEndpointURL set parameter to URL for free monitoring.
*/
class ExportedFreeMonEndpointURL : public ServerParameter {
public:
    ExportedFreeMonEndpointURL()
        : ServerParameter(ServerParameterSet::getGlobal(),
            "cloudFreeMonitoringEndpointURL",
            true,
            false) {}


    void append(OperationContext* opCtx, BSONObjBuilder& b, const std::string& name) final {
        stdx::lock_guard<stdx::mutex> guard(_lock);
        b.append(name, _path);
    }

    Status set(const BSONElement& newValueElement) {
        if (newValueElement.type() != String) {
            return Status(ErrorCodes::BadValue,
                "ExportedFreeMonEndpointURL only supports type string");
        }

        std::string str = newValueElement.str();
        return setFromString(str);
    }

    Status setFromString(const std::string& str) final {
        stdx::lock_guard<stdx::mutex> guard(_lock);
        _path = str;
        return Status::OK();
    }

    std::string getURL() {
        stdx::lock_guard<stdx::mutex> guard(_lock);
        return _path;
    }


private:
    // Lock to guard _path
    stdx::mutex _lock;

    std::string _path;
} exportedExportedFreeMonEndpointURL;


//class FreeMonitoringHttpClientInterface {
//    ~FreeMonitoringHttpClientInterface();
//
//    void post(StringData url, ConstDataRange data) = 0;
//};

class FreeMonNetworkHttp : public FreeMonNetworkInterface {
public:
    FreeMonNetworkHttp(std::unique_ptr<FreeMonitoringHttpClientInterface> client) :
        _client(std::move(client)) {}
    ~FreeMonNetworkHttp() {}

    Future<FreeMonRegistrationResponse> sendRegistrationAsync(const FreeMonRegistrationRequest& req) override {
        log() << "Sending Registration ...";

        BSONObj reqObj = req.toBSON();

        log() << "Sending data: " << reqObj.toString();

        return _client->postAsync(exportedExportedFreeMonEndpointURL.getURL() + "/register",
            reqObj).then(
            [](std::vector<uint8_t> blob) {

            //uassertStatusOK(swPost.getStatus());

            //auto blob = swPost.getValue();

            if (blob.size() == 0) {
                uassertStatusOK(Status(ErrorCodes::BadPerfCounterPath, "Short runt"));
            }

            ConstDataRange cdr(reinterpret_cast<char*>(blob.data()), blob.size());

            auto swDoc = cdr.read<Validated<BSONObj>>();
            uassertStatusOK(swDoc.getStatus());

            BSONObj respObj(swDoc.getValue());

            log() << "Received data: " << respObj.toString();

            auto resp = FreeMonRegistrationResponse::parse(IDLParserErrorContext("response"), respObj);


            return resp;
        });
    }

    Future<FreeMonMetricsResponse> sendMetricsAsync(const FreeMonMetricsRequest& req) override {
        log() << "Sending Metrics ...";

        BSONObj reqObj = req.toBSON();

        log() << "Sending data: " << reqObj.toString();

        return _client->postAsync(exportedExportedFreeMonEndpointURL.getURL() + "/metrics",
            reqObj).then(
                [](std::vector<uint8_t> blob) {

            //uassertStatusOK(swPost.getStatus());

            //auto blob = swPost.getValue();

            if (blob.size() == 0) {
                uassertStatusOK(Status(ErrorCodes::BadPerfCounterPath, "Short runt"));
            }

            ConstDataRange cdr(reinterpret_cast<char*>(blob.data()), blob.size());

            auto swDoc = cdr.read<Validated<BSONObj>>();
            uassertStatusOK(swDoc.getStatus());

            BSONObj respObj(swDoc.getValue());

            log() << "Received data: " << respObj.toString();

            auto resp = FreeMonMetricsResponse::parse(IDLParserErrorContext("response"), respObj);


            return resp;
        });
    }
private:
    std::unique_ptr<FreeMonitoringHttpClientInterface> _client;
};

} // namespace

namespace optionenvironment {
class OptionSection;
class Environment;
}  // namespace optionenvironment

namespace moe = mongo::optionenvironment;


namespace {


/**
* Command line states
*/
enum class EnableCloudStateEnum : std::int32_t {
    on,
    off,
    runtime,
};

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

std::string freeMonitoringTag; 
EnableCloudStateEnum freeMonitoringState = EnableCloudStateEnum::runtime;

Status addFreeMonitoringOptions(moe::OptionSection* options) {
    moe::OptionSection freeMonitoringOptions("Free Monitoring options");

//Command Line: --enableFreeMonitoring=<on|runtime|off>
//YAML Name: cloud.monitoring.free=<on|runtime|off>
    freeMonitoringOptions.addOptionChaining(
        "cloud.monitoring.free.state", 
        "enableFreeMonitoring", moe::String, 
        "Enable Cloud Free Monitoring (on|runtime|off)");

// Command Line: --enableFreeMonitoringTag=string
// YAML Name: cloud.monitoring.free.tag=string
    freeMonitoringOptions.addOptionChaining(
        "cloud.monitoring.free.tag",
        "freeMonitoringTag",
        moe::String,
        "Cloud Free Monitoring Tag");

    Status ret = options->addSection(freeMonitoringOptions);
    if (!ret.isOK()) {
        log() << "Failed to add free monitoring option section: " << ret.toString();
        return ret;
    }

    return Status::OK();
}

Status storeFreeMonitoringOptions(const moe::Environment& params) {

    if (params.count("cloud.monitoring.free.state")) {
        auto swState = EnableCloudState_parse(params["cloud.monitoring.free.state"].as<std::string>());
        if (!swState.isOK()) {
            return swState.getStatus();
        }
        freeMonitoringState = swState.getValue();
    }

    if (params.count("cloud.monitoring.free.tag")) {
        freeMonitoringTag = params["cloud.monitoring.free.tag"].as<std::string>();
    }

    return Status::OK();
}


MONGO_MODULE_STARTUP_OPTIONS_REGISTER(FreeMonitoringOptions)(InitializerContext* context) {
    return addFreeMonitoringOptions(&moe::startupOptions);
}


//MONGO_STARTUP_OPTIONS_VALIDATE(FreeMonitoringOptions)(InitializerContext* context) {
//    return addFreeMonitoringOptions(&moe::startupOptions);
//}

MONGO_STARTUP_OPTIONS_STORE(FreeMonitoringOptions)(InitializerContext* context) {
    return storeFreeMonitoringOptions(moe::startupOptionsParsed);
}

} // namespace


auto makeTaskExecutor(ServiceContext* serviceContext) {
    ThreadPool::Options tpOptions;
    tpOptions.poolName = "freemon";
    tpOptions.maxThreads = 50;
    tpOptions.onCreateThread = [](const std::string& threadName) {
        Client::initThread(threadName.c_str());
    };
    return stdx::make_unique<executor::ThreadPoolTaskExecutor>(
        std::make_unique<ThreadPool>(tpOptions),
        executor::makeNetworkInterface(
            "NetworkInterfaceASIO-FreeMon"));
}

void startFreeMonitoring(ServiceContext* serviceContext) {
    if (freeMonitoringState == EnableCloudStateEnum::off) {
        return;
    }

    auto executor = makeTaskExecutor(serviceContext);

    executor->startup();

    std::unique_ptr<FreeMonitoringHttpClientInterface> http = createFreeMonHttpClient(std::move(executor));
        
    std::unique_ptr<FreeMonNetworkInterface> network = std::unique_ptr<FreeMonNetworkInterface>(new FreeMonNetworkHttp(std::move(http)));

    auto controller = stdx::make_unique<FreeMonController>(std::move(network));

    // These are collected only at registration
    //
    // CmdBuildInfo
    controller->addRegistrationCollector(stdx::make_unique<FTDCSimpleInternalCommandCollector>(
        "buildInfo", "buildInfo", "", BSON("buildInfo" << 1)));

    // HostInfoCmd
    controller->addRegistrationCollector(stdx::make_unique<FTDCSimpleInternalCommandCollector>(
        "hostInfo", "hostInfo", "", BSON("hostInfo" << 1)));

    // TODO: Gather one document from local.clustermanager

    // These are periodically for metrics upload
    //
    controller->addMetricsCollector(stdx::make_unique<FTDCSimpleInternalCommandCollector>(
        "getDiagnosticData",
        "diagnosticData",
        "",
        BSON("getDiagnosticData" << 1)));
    
    // These are collected at registration and as metrics periodically
    //
    if (repl::ReplicationCoordinator::get(getGlobalServiceContext())->getReplicationMode() !=
        repl::ReplicationCoordinator::modeNone) {
        // CmdReplSetGetConfig
        controller->addRegistrationCollector(stdx::make_unique<FTDCSimpleInternalCommandCollector>(
            "replSetGetConfig", "replSetGetConfig", "", BSON("replSetGetConfig" << 1)));

        controller->addMetricsCollector(stdx::make_unique<FTDCSimpleInternalCommandCollector>(
            "replSetGetConfig", "replSetGetConfig", "", BSON("replSetGetConfig" << 1)));
    }
    
    controller->addRegistrationCollector(stdx::make_unique<FTDCSimpleInternalCommandCollector>(
        "isMaster",
        "isMaster",
        "",
        BSON("isMaster" << 1)));

    controller->addMetricsCollector(stdx::make_unique<FTDCSimpleInternalCommandCollector>(
        "isMaster",
        "isMaster",
        "",
        BSON("isMaster" << 1)));


    // Install the new controller
    auto& staticFreeMon = getFreeMonController(serviceContext);

    staticFreeMon = std::move(controller);

    RegistrationType registrationType = RegistrationType::DoNotRegister;
    if (freeMonitoringState == EnableCloudStateEnum::on) {
        if (repl::ReplicationCoordinator::get(getGlobalServiceContext())->getReplicationMode() !=
            repl::ReplicationCoordinator::modeNone) {
            registrationType = RegistrationType::RegisterAfterOnTransitionToPrimary;
        } else {
            registrationType = RegistrationType::RegisterOnStart;
        }
    }


    staticFreeMon->start(registrationType);
}

void stopFreeMonitoring() {
    if (freeMonitoringState == EnableCloudStateEnum::off) {
        return;
    }

    auto controller = getGlobalFreeMonController();

    if (controller) {
        controller->stop();
    }
}


FreeMonController* FreeMonController::get(ServiceContext* serviceContext) {
    return getFreeMonController(serviceContext).get();
}

FreeMonitoringHttpClientInterface::~FreeMonitoringHttpClientInterface() {
}


} // namespace mongo
