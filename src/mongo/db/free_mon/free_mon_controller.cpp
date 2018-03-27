
#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kFTDC


#include "mongo/platform/basic.h"

#include "mongo/db/free_mon/free_mon_controller.h"

#include <chrono>

#include "mongo/db/operation_context.h"
#include "mongo/db/service_context.h"
#include "mongo/util/exit.h"
#include "mongo/util/log.h"

namespace mongo {

namespace {

constexpr auto kProtocolVersion = 1;

constexpr auto kInformationalURLMaxLength = 4096;
constexpr auto kInformationalMessageMaxLength = 4096;
constexpr auto kUserReminderMaxLength = 4096;

constexpr auto kReportingIntervalMinutesMin = 1;
constexpr auto kReportingIntervalMinutesMax = 60 * 60 * 24;
}

FreeMonNetworkInterface::~FreeMonNetworkInterface() {}

void FreeMonController::addRegistrationCollector(
    std::unique_ptr<FreeMonCollectorInterface> collector) {
    {
        stdx::lock_guard<stdx::mutex> lock(_mutex);
        invariant(_state == State::kNotStarted);
        _registrationCollectors.add(std::move(collector));
    }
}

void FreeMonController::addMetricsCollector(std::unique_ptr<FreeMonCollectorInterface> collector) {
    {
        stdx::lock_guard<stdx::mutex> lock(_mutex);
        invariant(_state == State::kNotStarted);

        _metricCollectors.add(std::move(collector));
    }
}

void FreeMonController::registerServerStartup(RegistrationType registrationType, std::vector<std::string>& tags) {
    _enqueue(
        FreeMonMessageWithPayload<FreeMonMessageType::RegisterServer>::createNow(
            FreeMonMessageWithPayload<FreeMonMessageType::RegisterServer>::payload_type(registrationType, tags)));
}

Status FreeMonController::registerServerCommand(Milliseconds timeout) {
    auto msg = FreeMonRegisterCommandMessage::createNow(std::vector<std::string>());
    _enqueue(msg);

    if (timeout > Milliseconds::min()) {
        return msg->wait_for(timeout);
    }

    return Status::OK();
}

void FreeMonController::_enqueue(std::shared_ptr<FreeMonMessage> msg) {
    {
        stdx::lock_guard<stdx::mutex> lock(_mutex);
        invariant(_state == State::kStarted);
    }

    _processor->enqueue(msg);
}

void FreeMonController::start(RegistrationType registrationType) {
    {
        stdx::lock_guard<stdx::mutex> lock(_mutex);

        invariant(_state == State::kNotStarted);
    }

    // Start the agent
    _processor = std::make_unique<FreeMonProcessor>(
        _registrationCollectors, _metricCollectors, _network.get());

    _thread = stdx::thread([this] { _processor->doLoop(); });

    {
        stdx::lock_guard<stdx::mutex> lock(_mutex);

        invariant(_state == State::kNotStarted);
        _state = State::kStarted;
    }

    if (registrationType != RegistrationType::DoNotRegister) {
        std::vector<std::string> vec;
        registerServerStartup(registrationType, vec);
    }
}

void FreeMonController::stop() {
    // Stop the agent
    log() << "Shutting down free monitoring";

    {
        stdx::lock_guard<stdx::mutex> lock(_mutex);

        bool started = (_state == State::kStarted);

        invariant(_state == State::kNotStarted || _state == State::kStarted);

        if (!started) {
            _state = State::kDone;
            return;
        }

        _state = State::kStopRequested;

        // Wake up the thread if sleeping so that it will check if we are done
        _processor->stop();
    }

    _thread.join();

    _state = State::kDone;
}



}  // namespace mongo