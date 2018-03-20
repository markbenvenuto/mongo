
#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kFTDC


#include "mongo/platform/basic.h"

#include "mongo/db/free_mon/free_monitoring_controller.h"

#include <chrono>

#include "mongo/db/service_context.h"
#include "mongo/db/operation_context.h"
#include "mongo/util/exit.h"
#include "mongo/util/log.h"

namespace mongo {




FreeMonNetworkInterface::~FreeMonNetworkInterface() {}


FreeMonMessageQueue::FreeMonMessageQueue() : _stop(false) {}
void FreeMonMessageQueue::enqueue(std::shared_ptr<FreeMonMessage> msg) {

    {
        std::lock_guard<std::mutex> lock(_mutex);

        // If we were stopped, drop messages
        if (_stop) {
            return;
        }

        _queue.emplace(msg);

        // Signal the dequeue
        // TODO: avoid a spurious wakeup by check if the waiter deadline needs changing
        _condvar.notify_one();
    }
}
boost::optional<std::shared_ptr<FreeMonMessage>> FreeMonMessageQueue::dequeue(ClockSource* clockSource) {
    {
        std::unique_lock<std::mutex> lock(_mutex);
        if (_stop) {
            return{};
        }

        Date_t deadlineCV = Date_t::max();
        if (!_queue.empty()) {
            deadlineCV = _queue.top()->getDeadline();
        } else {
            deadlineCV = clockSource->now() + Hours(1);
        }

        _condvar.wait_until(lock, deadlineCV.toSystemTimePoint(), [this, clockSource]() {
            if (_stop) {
                return true;
            }

            if (this->_queue.empty()) {
                return false;
            }

            auto deadlineMessage = this->_queue.top()->getDeadline();
            if (deadlineMessage == Date_t::min()) {
                return true;
            }

            auto now = clockSource->now();

            bool check = deadlineMessage < now;
            return check;
        });

        if (_stop || _queue.empty()) {
            return{};
        }

        auto item = _queue.top();
        _queue.pop();
//        auto item = _queue.pop();
        return item;
    }
}

void FreeMonMessageQueue::stop() {
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _stop = true;
        _condvar.notify_one();
    }
}


void FreeMonController::addRegistrationCollector(std::unique_ptr<FreeMonCollectorInterface> collector) {
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

void FreeMonController::registerServerStartup(RegistrationType registrationType) {
    _enqueue(FreeMonServerRegisterMessage::createNow(registrationType));
}

Status FreeMonController::registerServerCommand(bool acceptedEULA, Milliseconds timeout) {
    auto msg = FreeMonRegisterCommandMessage::createNow(acceptedEULA);
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
    _processor = std::make_unique<FreeMonProcessor>(_registrationCollectors, _metricCollectors, _network.get());

    _thread = stdx::thread([this] { _processor->doLoop(); });

    {
        stdx::lock_guard<stdx::mutex> lock(_mutex);

        invariant(_state == State::kNotStarted);
        _state = State::kStarted;
    }

    if (registrationType != RegistrationType::DoNotRegister) {
        registerServerStartup(registrationType);
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


void FreeMonProcessor::enqueue(std::shared_ptr<FreeMonMessage> msg) {
    _queue.enqueue(msg);
}


void FreeMonProcessor::stop() {
    _queue.stop();
}


void FreeMonProcessor::doLoop() {
    try {

        Client::initThread("free_mon");
        Client* client = &cc();

        while (true) {
            auto item = _queue.dequeue(client->getServiceContext()->getPreciseClockSource());
            if (!item.is_initialized()) {
                // Shutdown
                return;
            }

            // Do work here
            switch (item.get()->getType()) {
            case FreeMonMessageType::RegisterCommand:
            {
                doCommandRegister(client, static_cast<FreeMonRegisterCommandMessage*>(item.get().get()));
                break;
            }
            case FreeMonMessageType::RegisterServer:
            {
                doServerRegister(client, static_cast<FreeMonServerRegisterMessage*>(item.get().get()));
                break;
            }

            case FreeMonMessageType::MetricsCallTimer: {
                doMetricsCall(client);
                break;
            }
            }

        }
    }
    catch (...) {
        warning() << "Uncaught exception in '" << exceptionToStatus()
            << "' in free monitoring subsystem. Shutting down the "
            "free monitoring subsystem.";
    }

}

void FreeMonProcessor::readState(Client* client) {

    auto optCtx = client->makeOperationContext();

    auto state = _storage.read(optCtx.get());

    if (state.is_initialized() && _state.state != FreeMonStateState::Initialized) {
        invariant(state.get().getVersion() == 1);
        _state.registrationId = state.get().getRegistrationId().toString();
        _state.informationalURL = state.get().getInformationalURL().toString();
        
        _state.userReminder = Minutes(state.get().getUserReminder());
    }
}

void FreeMonProcessor::writeState(Client* client) {

    // TODO:
}

Date_t fromNow(Client* client, Seconds seconds) {
    return client->getServiceContext()->getPreciseClockSource()->now() + seconds;
}

void FreeMonProcessor::doServerRegister(Client* client, const FreeMonServerRegisterMessage* msg) {
    // If we are asked to register now, then kick off a registration request
    if (msg->getRegistrationType() == RegistrationType::RegisterOnStart) {
        enqueue(FreeMonRegisterCommandMessage::createNow(false));
    } else if (msg->getRegistrationType() == RegistrationType::RegisterAfterOnTransitionToPrimary ){
        // Check if we need to wait to become primary
        // If the 'admin.system.version' has content, do not wait and just re-register
        // If the collection is empty, wait until we become primary
        //    If we become secondary, OpObserver hooks will tell us our registration id
        
        // TODO: hook OnTransitionToPrimary instead of this hack
        enqueue(FreeMonRegisterCommandMessage::createNow(false));
    }
}

void FreeMonProcessor::doCommandRegister(Client* client, const FreeMonRegisterCommandMessage* msg) {
    readState(client);

    FreeMonRegistrationRequest req;
    if (!_state.registrationId.empty()) {
        req.setId(StringData(_state.registrationId));
    }

    req.setAcceptedEULA(msg->getAcceptedEula());

    // TODO make constant
    req.setVersion(1);
    // TODO: req.setTag();

    // Collect the data
    auto collect = _registration.collect(client);

    req.setPayload(std::get<0>(collect));

    try {
        auto resp = _network->sendRegistration(req);

        // TODO: validate response

        // TODO: validate version - halt on bad version
        // TODO resp.getHaltMetricsUploading();
        // TODO:         resp.getUserReminder();
        // TODO: duplicate registrations?

        _state.reportingInterval = Seconds(resp.getReportingInterval());
        _state.informationalMessage = resp.getMessage().toString();
        _state.informationalURL = resp.getInformationalURL().toString();
        _state.registrationId = resp.getId().toString();

    }
    catch (const DBException&) {
        // TODO: do retry stuff
        error() << "Unexpected exception" << exceptionToStatus();
    }

    // Persist state
    writeState(client);

    // Enqueue next metrics upload
    enqueue(FreeMonMessage::createWithDeadline(FreeMonMessageType::MetricsCallTimer, fromNow(client, _state.reportingInterval)));
}

void FreeMonProcessor::doUnregister(Client* client) {

}

void FreeMonProcessor::doMetricsCall(Client* client) {
    readState(client);

    FreeMonMetricsRequest req;
    invariant(!_state.registrationId.empty());


    // TODO make constant
    req.setVersion(1);
    req.setEncoding(MetricsEncodingEnum::snappy);

    req.setId(StringData(_state.registrationId));

    // Collect the data
    auto collect = _metrics.collect(client);

    // TODO Compress the data
    BSONObj& obj = std::get<0>(collect);
    req.setMetrics( ConstDataRange(obj.objdata(), obj.objdata() + obj.objsize()));

    try {
        auto resp = _network->sendMetrics(req);

        // TODO: validate response

        // TODO: validate version - halt on bad version
        // TODO resp.getHaltMetricsUploading();
        // TODO: resp.haltMetricsUploading
        // TODO:         resp.getUserReminder();

        _state.reportingInterval = Seconds(resp.getReportingInterval());
        if (resp.getMessage().is_initialized()) {
            _state.informationalMessage = resp.getMessage().get().toString();
        }

    }
    catch (const DBException&) {
        // TODO: do retry stuff
        error() << "Unexpected exception" << exceptionToStatus();
    }

    // Persist state
    writeState(client);

    // Enqueue next metrics upload
    enqueue(FreeMonMessage::createWithDeadline(FreeMonMessageType::MetricsCallTimer, fromNow(client, _state.reportingInterval)));

}


} // namespace mongo