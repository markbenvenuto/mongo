
#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kFTDC


#include "mongo/platform/basic.h"

#include "mongo/db/free_mon/free_mon_processor.h"

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
                // Shutdown was triggered
                return;
            }

            // Do work here
            switch (item.get()->getType()) {
                case FreeMonMessageType::RegisterCommand: {
                    doCommandRegister(
                        client, static_cast<FreeMonRegisterCommandMessage*>(item.get().get()));
                    break;
                }
                case FreeMonMessageType::RegisterServer: {
                    doServerRegister(
                        client,
                        static_cast<FreeMonMessageWithPayload<FreeMonMessageType::RegisterServer>*>(
                            item.get().get()));
                    break;
                }

                case FreeMonMessageType::MetricsCallTimer: {
                    doMetricsCall(client);
                    break;
                }
                case FreeMonMessageType::AsyncRegisterComplete: {
                    doAsyncRegisterComplete(
                        client,
                        static_cast<
                            FreeMonMessageWithPayload<FreeMonMessageType::AsyncRegisterComplete>*>(
                            item.get().get()));
                    break;
                }
                default:
                    MONGO_UNREACHABLE;
            }
        }
    } catch (...) {
        // Stop the queue
        _queue.stop();

        warning() << "Uncaught exception in '" << exceptionToStatus()
                  << "' in free monitoring subsystem. Shutting down the "
                     "free monitoring subsystem.";
    }
}

void FreeMonProcessor::readState(Client* client) {

    auto optCtx = client->makeOperationContext();

    auto state = FreeMonStorage::read(optCtx.get());

    _lastReadState = state;

    if (state.is_initialized() && _status != FreeMonStateState::Initialized) {
        invariant(state.get().getVersion() == kProtocolVersion);

        _state = state.get();
    } else if (!state.is_initialized()) {
        // Default the state
        _state.setVersion(kProtocolVersion);
        _state.setState(StorageStateEnum::enabled);
        _state.setRegistrationId("");
        _state.setInformationalURL("");
        _state.setMessage("");
        _state.setUserReminder("");
    }
}

void FreeMonProcessor::writeState(Client* client) {

    // Do a compare and swap
    // Verify the document is the same as the one on disk, if it is the same, then do the update
    // If the local document is different, then oh-well we do nothing, and wait until the next round
    
    // Has our in-memory state changed, if so consider writing
    if (_lastReadState != _state) {

        auto optCtx = client->makeOperationContext();

        auto state = FreeMonStorage::read(optCtx.get());

        if (state == _lastReadState) {
            FreeMonStorage::replace(optCtx.get(), _state);
        }
    }
}

Date_t fromNow(Client* client, Seconds seconds) {
    return client->getServiceContext()->getPreciseClockSource()->now() + seconds;
}

void FreeMonProcessor::doServerRegister(
    Client* client, const FreeMonMessageWithPayload<FreeMonMessageType::RegisterServer>* msg) {
    
    // If we are asked to register now, then kick off a registration request
    if (msg->getPayload().first == RegistrationType::RegisterOnStart) {
        enqueue(FreeMonRegisterCommandMessage::createNow(msg->getPayload().second));
    } else if (msg->getPayload().first == RegistrationType::RegisterAfterOnTransitionToPrimary) {
        // Check if we need to wait to become primary
        // If the 'admin.system.version' has content, do not wait and just re-register
        // If the collection is empty, wait until we become primary
        //    If we become secondary, OpObserver hooks will tell us our registration id

        auto optCtx = client->makeOperationContext();

        // Check if there is an existing document
        auto state = FreeMonStorage::read(optCtx.get());

        // If there is no document, we may be in a replica set and may need to register after becoming primary
        // since we cannot record the registration id until after becoming primary
        if (!state.is_initialized()) {
            // TODO: hook OnTransitionToPrimary instead of this hack
            enqueue(FreeMonRegisterCommandMessage::createNow(msg->getPayload().second));
        } else {
            // If we have state, then we can do the normal register on startup
            enqueue(FreeMonRegisterCommandMessage::createNow(msg->getPayload().second));

        }
    }
}

void FreeMonProcessor::doCommandRegister(Client* client, const FreeMonRegisterCommandMessage* msg) {
    // TODO: check if register is in-flight
    if (_futureResponse.get()) {
        //#error request pending
    }

    readState(client);
    
    FreeMonRegistrationRequest req;
    
    if (!_state.getRegistrationId().empty()) {
        req.setId(_state.getRegistrationId());
    }

    req.setVersion(kProtocolVersion);
    
    if (!msg->getTags().empty()) {
        req.setTag(transformVector(msg->getTags()));
    }

    // Collect the data
    auto collect = _registration.collect(client);

    req.setPayload(std::get<0>(collect));

    // Send the async request
    _futureResponse = std::make_unique<Future<void>>(
        _network->sendRegistrationAsync(req).then([this](const auto& resp) {

            // TODO: handle error information
            this->doRegisterCallback(resp);
        }));
}

void FreeMonProcessor::doRegisterCallback(const FreeMonRegistrationResponse& resp) {

    enqueue(FreeMonMessageWithPayload<FreeMonMessageType::AsyncRegisterComplete>::createNow(resp));
}


void FreeMonProcessor::doAsyncRegisterComplete(
    Client* client,
    const FreeMonMessageWithPayload<FreeMonMessageType::AsyncRegisterComplete>* msg) {

    auto& resp = msg->getPayload();

    // Any validation failure stops registration from proceeding to upload
    if (resp.getVersion() != kProtocolVersion) {
        warning() << "Unexpected registration response protocol version, expected '" << kProtocolVersion << "', received '" << resp.getVersion() << "'";
        return;
    }

    // Did cloud ask us to stop uploading?
    if (resp.getHaltMetricsUploading()) {
        log() << "Halting metrics upload due to response";
        return;
    }

    if (resp.getInformationalURL().size() >= kInformationalURLMaxLength) {
        warning() << "InformationURL is '"<<resp.getInformationalURL().size()<<"' bytes in length, maximum allowed length is '"<< kInformationalURLMaxLength<<"'";
        return;
    }
    // TODO: validate userReminder, Message

    if (resp.getReportingInterval() < kReportingIntervalMinutesMin || resp.getReportingInterval() > kReportingIntervalMinutesMax) {
        // TODO
        return;
    }


    // TODO: duplicate registrations?

    // Update in-memory state
    _reportingInterval = Seconds(resp.getReportingInterval());


    _state.setRegistrationId(resp.getId());

    if (resp.getUserReminder().is_initialized()) {
        _state.setUserReminder(resp.getUserReminder().get());
    } else {
        _state.setUserReminder("");
    }
    _state.setMessage(resp.getMessage());
    _state.setInformationalURL(resp.getInformationalURL());


    // Persist state
    writeState(client);
    
    // Enqueue next metrics upload
    enqueue(FreeMonMessage::createWithDeadline(FreeMonMessageType::MetricsCallTimer,
                                               fromNow(client, _reportingInterval)));


}

void FreeMonProcessor::doUnregister(Client* client) {}

void FreeMonProcessor::doMetricsCall(Client* client) {
    // Send Request
    // RequestComplete
    // Only allow one outstanding HTTP call at a time.
    // If an upload is in-flight (i.e. slow), then the metrics is buffered
    // Each upload wether new or retried gathers ALL samples and do all uploads


    readState(client);

    FreeMonMetricsRequest req;
    invariant(!_state.getRegistrationId().empty());


    req.setVersion(kProtocolVersion);
    req.setEncoding(MetricsEncodingEnum::snappy);

    req.setId(_state.getRegistrationId());

    // Collect the data
    auto collect = _metrics.collect(client);

    // TODO Compress the data
    BSONObj& obj = std::get<0>(collect);
    req.setMetrics(ConstDataRange(obj.objdata(), obj.objdata() + obj.objsize()));

    try {
        // auto resp _network->sendMetrics(req);

        //// TODO: validate response

        //// TODO: validate version - halt on bad version
        //// TODO resp.getHaltMetricsUploading();
        //// TODO: resp.haltMetricsUploading
        //// TODO:         resp.getUserReminder();

        //_state.reportingInterval = Seconds(resp.getReportingInterval());
        // if (resp.getMessage().is_initialized()) {
        //    _state.informationalMessage = resp.getMessage().get().toString();
        //}

    } catch (const DBException&) {
        // TODO: do retry stuff
        error() << "Unexpected exception" << exceptionToStatus();
    }

    // Persist state
    writeState(client);

    // Enqueue next metrics upload
    enqueue(FreeMonMessage::createWithDeadline(FreeMonMessageType::MetricsCallTimer,
                                               fromNow(client, _reportingInterval)));
}


}  // namespace mongo