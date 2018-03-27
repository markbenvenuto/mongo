
#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kFTDC


#include "mongo/platform/basic.h"

#include "mongo/db/free_mon/free_mon_queue.h"

#include <chrono>

#include "mongo/db/operation_context.h"
#include "mongo/db/service_context.h"
#include "mongo/util/exit.h"
#include "mongo/util/log.h"

namespace mongo {


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

boost::optional<std::shared_ptr<FreeMonMessage>> FreeMonMessageQueue::dequeue(
    ClockSource* clockSource) {
    {
        std::unique_lock<std::mutex> lock(_mutex);
        if (_stop) {
            return {};
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
            return {};
        }

        auto item = _queue.top();
        _queue.pop();
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

}  // namespace mongo