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
#include "mongo/db/ftdc//controller.h"
#include "mongo/util/clock_source.h"
#include "mongo/util/duration.h"
#include "mongo/util/future.h"

namespace mongo {


// class RetryCounter {
//     duration retry(kind);
//     reset();
// };

enum class FreeMonMessageType {
    RegisterServer,
    RegisterCommand,
    // DeRegister,

    MetricsCallTimer,
    // MetricsCollectTimer,

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
    // OnPrimary,
    // OpObserver,
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

    static std::shared_ptr<FreeMonMessage> createWithDeadline(FreeMonMessageType type,
                                                              Date_t deadline) {
        return std::make_shared<FreeMonMessage>(type, deadline);
    }

    FreeMonMessage(FreeMonMessage&) = delete;
    FreeMonMessage(FreeMonMessage&&) = default;

    FreeMonMessageType getType() const {
        return _type;
    }
    Date_t getDeadline() const {
        return _deadline;
    }

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

template <FreeMonMessageType typeT>
struct FreeMonPayloadForMessage {
    using payload_type = void;
};

template <>
struct FreeMonPayloadForMessage<FreeMonMessageType::AsyncRegisterComplete> {
    using payload_type = FreeMonRegistrationResponse;
};

template <>
struct FreeMonPayloadForMessage<FreeMonMessageType::RegisterServer> {
    using payload_type = std::pair<RegistrationType, std::vector<std::string> >;
};


template <FreeMonMessageType typeT>
class FreeMonMessageWithPayload : public FreeMonMessage {
public:
    using payload_type = typename FreeMonPayloadForMessage<typeT>::payload_type;

    static std::shared_ptr<FreeMonMessageWithPayload> createNow(payload_type t) {
        return std::make_shared<FreeMonMessageWithPayload>(t, Date_t::min());
    }

    const payload_type& getPayload() const {
        return _t;
    }

public:
    FreeMonMessageWithPayload(payload_type t, Date_t deadline)
        : FreeMonMessage(typeT, deadline), _t(t) {}

private:
    payload_type _t;
};


// class FreeMonServerRegisterMessage : public FreeMonMessage {
// public:
//    static std::shared_ptr<FreeMonServerRegisterMessage> createNow(RegistrationType
//    registrationType) {
//        return std::make_shared<FreeMonServerRegisterMessage>(registrationType, Date_t::min());
//    }
//
//    RegistrationType getRegistrationType() const { return _registrationType; }
//
// public:
//    FreeMonServerRegisterMessage(RegistrationType registrationType, Date_t deadline) :
//        FreeMonMessage(FreeMonMessageType::RegisterServer, deadline),
//        _registrationType(registrationType) {}
// private:
//    RegistrationType _registrationType;
//};


//template <FreeMonMessageType typeT>
//class FreeMonMessageWaitablePayload : public FreeMonMessage {
//public:
//    using payload_type = typename FreeMonPayloadForMessage<typeT>::payload_type;
//
//    static std::shared_ptr<FreeMonMessageWithPayload> createNow(payload_type t) {
//        return std::make_shared<FreeMonMessageWithPayload>(t, Date_t::min());
//    }
//
//    const payload_type& getPayload() const {
//        return _t;
//    }
//
//public:
//    FreeMonMessageWithPayload(payload_type t, Date_t deadline)
//        : FreeMonMessage(typeT, deadline), _t(t) {}
//
//private:
//    payload_type _t;
//};

//class FreeMonRegisterCommandMessage : public FreeMonMessage {
//public:
//    static std::shared_ptr<FreeMonRegisterCommandMessage> createNow() {
//        return std::make_shared<FreeMonRegisterCommandMessage>(Date_t::min());
//    }
//
//
//    void setStatus(Status status) {
//        _waitable.set(status);
//    }
//
//    Status wait_for(Milliseconds duration) {
//        return _waitable.wait_for(duration);
//    }
//
//public:
//    FreeMonRegisterCommandMessage(Date_t deadline)
//        : FreeMonMessage(FreeMonMessageType::RegisterCommand, deadline)
//         {}
//
//private:
//    WaitableResult _waitable{};
//};



class FreeMonRegisterCommandMessage : public FreeMonMessage {
public:
    static std::shared_ptr<FreeMonRegisterCommandMessage> createNow(const std::vector<std::string>& tags) {
        return std::make_shared<FreeMonRegisterCommandMessage>(tags, Date_t::min());
    }

    const std::vector<std::string>& getTags() const { return _tags; }

    void setStatus(Status status) {
        _waitable.set(status);
    }

    Status wait_for(Milliseconds duration) {
        return _waitable.wait_for(duration);
    }

public:
    FreeMonRegisterCommandMessage(const std::vector<std::string>& tags, Date_t deadline)
        : FreeMonMessage(FreeMonMessageType::RegisterCommand, deadline), _tags(tags)
    {}

private:
    WaitableResult _waitable{};
    const std::vector<std::string> _tags;
};


//class FreeMonRegisterCommandMessage : public FreeMonMessage {
//public:
//    static std::shared_ptr<FreeMonRegisterCommandMessage> createNow(bool acceptedEula) {
//        return std::make_shared<FreeMonRegisterCommandMessage>(acceptedEula, Date_t::min());
//    }
//
//    bool getAcceptedEula() const {
//        return _acceptedEula;
//    }
//
//    void setStatus(Status status) {
//        _waitable.set(status);
//    }
//
//    Status wait_for(Milliseconds duration) {
//        return _waitable.wait_for(duration);
//    }
//
//public:
//    FreeMonRegisterCommandMessage(bool acceptedEula, Date_t deadline)
//        : FreeMonMessage(FreeMonMessageType::RegisterCommand, deadline),
//          _acceptedEula(acceptedEula) {}
//
//private:
//    bool _acceptedEula;
//    WaitableResult _waitable{};
//};


// class FMNotifyObserver : FMMessage {
// public:
// private:
//    FMDocument _doc;
//}


} // namespace mongo