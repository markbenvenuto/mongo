/**
 * Copyright (C) 2018 MongoDB Inc.
 *
 * This program is free software: you can redistribute it and/or  modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, the copyright holders give permission to link the
 * code of portions of this program with the OpenSSL library under certain
 * conditions as described in each individual source file and distribute
 * linked combinations including the program with the OpenSSL library. You
 * must comply with the GNU Affero General Public License in all respects
 * for all of the code used other than as permitted herein. If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so. If you do not
 * wish to do so, delete this exception statement from your version. If you
 * delete this exception statement from all source files in the program,
 * then also delete it in the license file.
 */

#pragma once

#include <boost/optional.hpp>
#include <thread>
#include <queue>
#include <vector>

#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/db/free_mon/free_mon_network.h"
#include "mongo/db/free_mon/free_mon_message.h"
#include "mongo/db/free_mon/free_mon_storage.h"
#include "mongo/db/free_mon/free_mon_queue.h"
#include "mongo/db/free_mon/free_mon_options.h"
#include "mongo/util/clock_source.h"
#include "mongo/util/duration.h"
#include "mongo/util/future.h"

namespace mongo {
using FreeMonCollectorInterface = FTDCCollectorInterface;
using FreeMonCollectorCollection = FTDCCollectorCollection;


// class RetryCounter {
//     duration retry(kind);
//     reset();
// };


enum class FreeMonStateState {
    Initialized = 0,
    Enabled = 1,
    Disabled = 2,
};

//class FreeMonState {
//public:
//    std::string registrationId;
//
//
//    std::string informationalURL;
//    std::string informationalMessage;
//
//    boost::optional<std::string> userReminder;
//};


class FreeMonProcessor {
public:
    FreeMonProcessor(FreeMonCollectorCollection& registration,
                     FreeMonCollectorCollection& metrics,
                     FreeMonNetworkInterface* network)
        : _registration(registration), _metrics(metrics), _network(network) {}
    void enqueue(std::shared_ptr<FreeMonMessage> msg);
    void stop();

    void doLoop();

private:
    void readState(Client* client);
    void writeState(Client* client);

    void doCommandRegister(Client* client, const FreeMonRegisterCommandMessage* msg);
    void doServerRegister(Client* client,
                          const FreeMonMessageWithPayload<FreeMonMessageType::RegisterServer>* msg);
    void doUnregister(Client* client);
    void doMetricsCall(Client* client);

    void doRegisterCallback(const FreeMonRegistrationResponse& resp);
    void doMetricsCallback(const FreeMonMetricsResponse& resp);

    void doAsyncRegisterComplete(
        Client* client,
        const FreeMonMessageWithPayload<FreeMonMessageType::AsyncRegisterComplete>* msg);
    void doAsyncMetricsComplete(
        Client* client,
        const FreeMonMessageWithPayload<FreeMonMessageType::AsyncMetricsComplete>* msg);


    void doOpObserver(Client* client);

private:
    FreeMonCollectorCollection& _registration;
    FreeMonCollectorCollection& _metrics;
    FreeMonNetworkInterface* _network;

    // TODO: make constant
    Seconds _reportingInterval{60};

    boost::optional<FreeMonStorageState> _lastReadState;

    FreeMonStateState  _status;
    FreeMonStorageState _state;

    // Producer Consumer Message queue, runs on background thread
    FreeMonMessageQueue _queue;

    // TODO Add sync value
    //FreeMonState _state;

    std::unique_ptr<Future<void>> _futureResponse;
};

/**
 * Manages and control Free Monitoring.
 */
class FreeMonController {
public:
    FreeMonController(std::unique_ptr<FreeMonNetworkInterface> network)
        : _network(std::move(network)) {}

    /**
     * Initializes free monitoring.
     * Start free monitoring thread in the background.
     */
    void start(RegistrationType registrationType);

    /**
     * Stops free monitoring thread.
     */
    void stop();

    /**
    * Add a metric collector to collect on registration
    */
    void addRegistrationCollector(std::unique_ptr<FreeMonCollectorInterface> collector);

    /**
    * Add a metric collector to collect periodically
    */
    void addMetricsCollector(std::unique_ptr<FreeMonCollectorInterface> collector);

    /**
    * Get the FreeMonController from ServiceContext.
    */
    static FreeMonController* get(ServiceContext* serviceContext);

    // Update is synchronous with 10sec timeout
    // kicks off register, and once register is done kicks off metrics upload

    /**
     * Start registration of mongod with remote service.
     *
     * Only sends one remote registration at a time.
     * Returns after timeout if registrations is not complete. Registration continues though.
     */
    void registerServerStartup(RegistrationType registrationType, std::vector<std::string>& tags);

    /**
    * Start registration of mongod with remote service.
    *
    * Only sends one remote registration at a time.
    * Returns after timeout if registrations is not complete. Registration continues though.
    */
    Status registerServerCommand(Milliseconds timeout);


    Status deregisterServer();


    void getStatus(BSONObjBuilder* builder);
    void getServerStatus(BSONObjBuilder* builder);

    // void notifyObserver(const FreeMonState& doc);
private:
    void _enqueue(std::shared_ptr<FreeMonMessage> msg);

private:
    // collectRegistrationData();
    // collectMetricsData();

    // Thread state
    // NotStarted -> IsRunning -> IsStopping -> NotStarted

    // Metrics State
    // ??

    // Controller state
    // Not Registered -> Registering -> Uploading -> Not Registered
    /**
    * Private enum to track state.
    *
    *   +-----------------------------------------------------------+
    *   |                                                           v
    * +-------------+     +----------+     +----------------+     +-------+
    * | kNotStarted | --> | kStarted | --> | kStopRequested | --> | kDone |
    * +-------------+     +----------+     +----------------+     +-------+
    */
    enum class State {
        /**
        * Initial state. Either start() or stop() can be called next.
        */
        kNotStarted,

        /**
        * start() has been called. stop() should be called next.
        */
        kStarted,

        /**
        * stop() has been called, and the background thread is in progress of shutting down
        */
        kStopRequested,

        /**
        * Controller has been stopped.
        */
        kDone,
    };

    // state
    State _state{State::kNotStarted};

    // Mutext to protect internal state
    stdx::mutex _mutex;

    // Set of registration collectors
    FreeMonCollectorCollection _registrationCollectors;

    // Set of metric collectors
    FreeMonCollectorCollection _metricCollectors;

    std::unique_ptr<FreeMonNetworkInterface> _network;

    // Background thead for agent
    stdx::thread _thread;

    // Background agent
    std::unique_ptr<FreeMonProcessor> _processor;
};

#if 0
start() {
        _storage.read(data);

}

run() {
    // cond_var - sleep or wait_for config update

    switch (_state)
        case Registering:
            sleep = tryRegister();
            if (success) tryMetrics
        case Metrics:
            sleep = tryMetrics();
}

duration tryRegister() {
    data =     collectRegistrationData();

    _network.sendRegistration(data);
    if( really_bad ) {
        abort();
    } else if (transient) {
        // retry later
        return retryCounter(); 37s;
    }

    _storage.update(data);
}

duration tryMetrics() {
    current =     collectMetricData();

    payload = makeMetricsPayload(original, current);

    _network.sendMetrics(payload);
    if( really_bad ) {
        abort();
    } else if (transient) {
        // retry later
        return retryCounter(); 37s;
    } else {
        original = current;
    }

    if(update)
      _storage.update(data);
}

// Secondary noticed a change in persisted document, update state
notifyObserver(data) {
    update()
    signal();
}


notifyObserver(new_data) {
    push(NotifyObserver);
}

enum MessageType {
    Register
    DeRegister
    ...
}

class MessageType : std::tuple<MessageType, payload>

class FMMessage {
    public:
    private:
    MessageTypeEnum _enum;
}

class FMNotifyObserver : FMMessage {
    public:
    private:
    FMDocument _doc;
}


void MessageLoop() {

    while(queue.wait()) {
        // Each message has a deadline of either 0 or a time in the future
        // We wait on a cond var until it is signaled or timeout
        switch(queue.pop()) {
            case Register:
                if error/timeout:
                    push(Register);
                else:
                    push(RegisterComplete)
            case DeRegister:
                // Restart CollectMetrics
            case NotifyObserver:
                // Update stae
                // Reg change
                if(orig_data.id != new_data.id) {
                    push(RegisterComplete);
                }
            case CollectMetrics:
                // Collect up to 10 metrics until register
                push(CollectMetrics)
            case CollectAndSendMetrics:
                // Collect most recent metric after register
                push(CollectAndSendMetrics)
            case RegisterComplete:
                // Reset metrics counter
                // Send 10 metrics
                push(CollectMetrics)
        }
    }
}
// TODO: make options conditional with requires

#endif
}