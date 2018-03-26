// Free Monitoring Controller

#include <boost/optional.hpp>
#include <thread>
//#include <priority_queue>
//#include <heap>
#include <queue>
#include <vector>

#include "mongo/db/free_mon/free_monitoring_protocol_gen.h"
#include "mongo/db/free_mon/free_monitoring_storage_gen.h"
#include "mongo/db/free_mon/free_monitoring_commands_gen.h"
#include "mongo/util/duration.h"
#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/db/ftdc//controller.h"
#include "mongo/util/clock_source.h"
#include "mongo/util/future.h"

namespace mongo {
using FreeMonCollectorInterface = FTDCCollectorInterface;
using FreeMonCollectorCollection = FTDCCollectorCollection;

/**
 * Makes HTTPS calls to cloud endpoint
 */
class FreeMonNetworkInterface {
public:
    virtual ~FreeMonNetworkInterface();

    /**
     * POSTs FreeMonRegistrationRequest to endpoint.
     * 
     * Returns a FreeMonRegistrationResponse or throws an error on non-HTTP 200.
     */
    virtual Future<FreeMonRegistrationResponse> sendRegistrationAsync(const FreeMonRegistrationRequest& req) = 0;

   /**
     * POSTs FreeMonMetricsRequest to endpoint.
     * 
     * Returns a FreeMonMetricsResponse or throws an error on non-HTTP 200.
     */
    virtual Future<FreeMonMetricsResponse> sendMetricsAsync(const FreeMonMetricsRequest& req) = 0;
};


// See src/mongo/db/repl/storage_interface.h
// Just use StorageInterface to 
/**
 * Storage tier for Free Monitoring. Provides access to storage engine.
 */
class FreeMonStorage {
public:
    /**
     * Reads document from disk if it exists.
     */
    static boost::optional<FreeMonStorageState> read(OperationContext* opCtx);

    /**
     * Replaces document on disk with contents of document. Creates document if it does not exist.
     */
    static bool replace(OperationContext* opCtx, const FreeMonStorageState& doc);

    /**
     * Deletes document on disk if it exists.
     */
    static bool deleteState(OperationContext* opCtx);

    /**
     * Reads first document from local.clustermanager.
     */
    static BSONObj readClusterManagerState(OperationContext* opCtx);
};

// class RetryCounter {
//     duration retry(kind);
//     reset();   
// };

enum class FreeMonMessageType {
    RegisterServer,
    RegisterCommand,
    //DeRegister,
    
    MetricsCallTimer,
    //MetricsCollectTimer,

//#error ToDo
    // Make HTTP and collection separate loops
    // HTTP could be made async
        // Send Request
        // RequestComplete
        // Only allow one outstanding HTTP call at a time.
        // If an upload is in-flight (i.e. slow), then the metrics is buffered
        // Each upload wether new or retried gathers ALL samples and do all uploads
    HttpRequest,
    AsyncHttpRequest,

    AsyncRegisterComplete,

    AsyncMetricsComplete,

    // TODO
    //OnPrimary,
    //OpObserver,
};


enum class RegistrationType {
    /**
    * Do not register on start because it was not configured via commandline/config file.
    */
    DoNotRegister,

    /**
    * Register immediately on start since we are a standalone.
    */
    RegisterOnStart,

    /**
    * Register after transition to becoming primary because we are in a replica set.
    */
    RegisterAfterOnTransitionToPrimary,
};




class FreeMonMessage {
public:
    static std::shared_ptr<FreeMonMessage> createNow(FreeMonMessageType type) {
        return std::make_shared<FreeMonMessage>(type, Date_t::min());
    }

    static std::shared_ptr<FreeMonMessage> createWithDeadline(FreeMonMessageType type, Date_t deadline) {
        return std::make_shared<FreeMonMessage>(type, deadline);
    }

    FreeMonMessage(FreeMonMessage&) = delete;
    FreeMonMessage(FreeMonMessage&&) = default;

    FreeMonMessageType getType() const { return _type; }
    Date_t getDeadline() const { return _deadline; }

public:
    FreeMonMessage(FreeMonMessageType type, Date_t deadline) : _type(type), _deadline(deadline) {}

private:
    FreeMonMessageType _type;
    Date_t _deadline;
};


/**
* 
*/
class WaitableResult {
public:
    WaitableResult() : _status(Status::OK()) {}

    void set(Status status) {
        stdx::lock_guard<stdx::mutex> lock(_mutex);

        _set = true;
        _status = status;
        _condvar.notify_one();
    }

    Status wait_for(Milliseconds duration) {
        stdx::unique_lock<stdx::mutex> lock(_mutex);

        if (!_condvar.wait_for(lock, duration.toSystemDuration(), [this]() { return _set; })) {
            return Status(ErrorCodes::LockTimeout, "Time...");
        }

        return _status;
    }
private:
    bool _set{false};
    Status _status;

    stdx::mutex _mutex;
    stdx::condition_variable _condvar;
};

template<FreeMonMessageType typeT>
struct FreeMonPayloadForMessage {
    using payload_type = void;
};

template<>
struct FreeMonPayloadForMessage<FreeMonMessageType::AsyncRegisterComplete>{
    using payload_type = FreeMonRegistrationResponse;
};

template<>
struct FreeMonPayloadForMessage<FreeMonMessageType::RegisterServer> {
    using payload_type = RegistrationType;
};


template<FreeMonMessageType typeT>
class FreeMonMessageWithPayload : public FreeMonMessage {
public:
    using payload_type = typename FreeMonPayloadForMessage<typeT>::payload_type;

    static std::shared_ptr<FreeMonMessageWithPayload> createNow(payload_type t) {
        return std::make_shared<FreeMonMessageWithPayload>(t, Date_t::min());
    }

    const payload_type& getPayload() const { return _t; }

public:
    FreeMonMessageWithPayload(payload_type t, Date_t deadline) :
        FreeMonMessage(typeT, deadline), _t(t) {}
private:
    payload_type _t;
};


//class FreeMonServerRegisterMessage : public FreeMonMessage {
//public:
//    static std::shared_ptr<FreeMonServerRegisterMessage> createNow(RegistrationType registrationType) {
//        return std::make_shared<FreeMonServerRegisterMessage>(registrationType, Date_t::min());
//    }
//
//    RegistrationType getRegistrationType() const { return _registrationType; }
//
//public:
//    FreeMonServerRegisterMessage(RegistrationType registrationType, Date_t deadline) :
//        FreeMonMessage(FreeMonMessageType::RegisterServer, deadline), _registrationType(registrationType) {}
//private:
//    RegistrationType _registrationType;
//};

class FreeMonRegisterCommandMessage : public FreeMonMessage {
public:
    static std::shared_ptr<FreeMonRegisterCommandMessage> createNow(bool acceptedEula) {
        return std::make_shared<FreeMonRegisterCommandMessage>(acceptedEula, Date_t::min());
    }

    bool getAcceptedEula() const { return _acceptedEula; }

    void setStatus(Status status) { _waitable.set(status); }
    Status wait_for(Milliseconds duration) { return _waitable.wait_for(duration); }

public:
    FreeMonRegisterCommandMessage(bool acceptedEula, Date_t deadline) :
        FreeMonMessage(FreeMonMessageType::RegisterCommand, deadline), _acceptedEula(acceptedEula){}
private:
    bool _acceptedEula;
    WaitableResult _waitable{};
};


//class FMNotifyObserver : FMMessage {
//public:
//private:
//    FMDocument _doc;
//}


struct FreeMonMessageGreater
{
    bool operator()(const FreeMonMessage& left, const FreeMonMessage& right) const
    {
        return (left.getDeadline() > right.getDeadline());
    }
    bool operator()(const std::shared_ptr<FreeMonMessage>& left, const std::shared_ptr<FreeMonMessage>& right) const
    {
        return (left->getDeadline() > right->getDeadline());
    }
};

//template <typename T,
//          typename CompT = std::less<T> >
//class movable_priority_queue {
//public:
//    bool empty() { return _vector.empty(); }
//    void emplace(T item) {
//        _vector.push_back(std::move(item));
//        std::push_heap(_vector.begin(), _vector.end(), _comp);
//    }
//    const T& top() const {
//        return *(_vector.begin());
//    }
//    T pop() {
//        std::pop_heap(_vector.begin(), _vector.end(), _comp);
//        T item = std::move(_vector.back());
//        _vector.pop_back();
//        return item;
//    }
//private:
//    std::vector<T> _vector;
//    CompT _comp;
//};

class FreeMonMessageQueue {
public:
    FreeMonMessageQueue();
    void enqueue(std::shared_ptr<FreeMonMessage> msg);
    boost::optional<std::shared_ptr<FreeMonMessage>> dequeue(ClockSource* clockSource);

    void stop();
private:
    bool _stop;
    std::condition_variable _condvar;
    std::mutex _mutex;

    //std::priority_queue < FreeMonMessage, std::vector<FreeMonMessage>, FreeMonMessageGreater> _queue;
    //movable_priority_queue< FreeMonMessage, FreeMonMessageGreater> _queue;
    std::priority_queue < std::shared_ptr<FreeMonMessage>, std::vector<std::shared_ptr<FreeMonMessage>>, FreeMonMessageGreater> _queue;

};


enum class FreeMonStateState {
    Initialized = 0,
    Enabled = 1,
    Disabled = 2,
};

class FreeMonState {
public:
    // TODO make constant
    FreeMonState() : reportingInterval(60), state(FreeMonStateState::Initialized){}

    Seconds reportingInterval;
    FreeMonStateState state;

    std::string registrationId;


    std::string informationalURL;
    std::string informationalMessage;

    Minutes userReminder;
};


class FreeMonProcessor {
public:
    FreeMonProcessor(FreeMonCollectorCollection& registration, FreeMonCollectorCollection &metrics,
        FreeMonNetworkInterface* network) :
        _registration(registration), _metrics(metrics),
        _network(network)
        {}
    void enqueue(std::shared_ptr<FreeMonMessage> msg);
    void stop();

    void doLoop();
private:
    void readState(Client* client);
    void writeState(Client* client);

    void doCommandRegister(Client* client, const FreeMonRegisterCommandMessage* msg);
    void doServerRegister(Client* client, const  FreeMonMessageWithPayload<FreeMonMessageType::RegisterServer>* msg);
    void doUnregister(Client* client);
    void doMetricsCall(Client* client);

    void doRegisterCallback(const FreeMonRegistrationResponse& resp);
    void doMetricsCallback(const FreeMonMetricsResponse& resp);

    void doAsyncRegisterComplete(Client* client, const  FreeMonMessageWithPayload<FreeMonMessageType::AsyncRegisterComplete>* msg);
    void doAsyncMetricsComplete(Client* client, const  FreeMonMessageWithPayload<FreeMonMessageType::AsyncMetricsComplete>* msg);


    void doOpObserver(Client* client);
private:

    FreeMonCollectorCollection& _registration;
    FreeMonCollectorCollection &_metrics;
    FreeMonNetworkInterface* _network; 

    // Producer Consumer Message queue, runs on background thread
    FreeMonMessageQueue _queue;

    // TODO Add sync value
    FreeMonState _state;

    std::unique_ptr<Future<void>> _futureResponse;
};

/**
 * Manages and control Free Monitoring.
 */
class FreeMonController {
public:

    FreeMonController(

        std::unique_ptr<FreeMonNetworkInterface> network

    ):
    _network(std::move(network)) {}



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
    void registerServerStartup(RegistrationType registrationType);

    /**
    * Start registration of mongod with remote service.
    *
    * Only sends one remote registration at a time.
    * Returns after timeout if registrations is not complete. Registration continues though.
    */
    Status registerServerCommand(bool acceptedEULA, Milliseconds timeout);

    
    Status deregisterServer();


    void getStatus(BSONObjBuilder* builder);
    void getServerStatus(BSONObjBuilder* builder);
    
    //void notifyObserver(const FreeMonState& doc);
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