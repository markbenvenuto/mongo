// Free Monitoring Controller

#include <boost/optional.hpp>
#include <thread>
#include <queue>
#include <vector>

#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/db/free_mon/free_mon_network.h"
#include "mongo/db/free_mon/free_mon_message.h"

#include "mongo/util/clock_source.h"
#include "mongo/util/duration.h"
#include "mongo/util/future.h"

namespace mongo {


struct FreeMonMessageGreater {
    bool operator()(const FreeMonMessage& left, const FreeMonMessage& right) const {
        return (left.getDeadline() > right.getDeadline());
    }
    bool operator()(const std::shared_ptr<FreeMonMessage>& left,
                    const std::shared_ptr<FreeMonMessage>& right) const {
        return (left->getDeadline() > right->getDeadline());
    }
};

// Multi-Producer
// Single Consumer
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

    std::priority_queue<std::shared_ptr<FreeMonMessage>,
                        std::vector<std::shared_ptr<FreeMonMessage>>,
                        FreeMonMessageGreater>
        _queue;
};


} // namespace mongo
