#pragma once

#include <memory>
#include <vector>
#include <cstdint>

#include "mongo/base/string_data.h"
#include "mongo/base/data_range.h"
#include "mongo/util/future.h"
#include "mongo/bson/bsonobj.h"
#include "mongo/util/concurrency/thread_pool.h"
#include "mongo/executor/thread_pool_task_executor.h"

namespace mongo {

class FreeMonitoringHttpClientInterface {
public:
    virtual  ~FreeMonitoringHttpClientInterface();

    virtual Future<std::vector<uint8_t>> postAsync(StringData url, const BSONObj data) = 0;
};

std::unique_ptr<FreeMonitoringHttpClientInterface> createFreeMonHttpClient(std::unique_ptr<executor::ThreadPoolTaskExecutor> executor);
} // namespace mongo