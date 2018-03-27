#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "mongo/base/data_range.h"
#include "mongo/base/string_data.h"
#include "mongo/bson/bsonobj.h"
#include "mongo/executor/thread_pool_task_executor.h"
#include "mongo/util/concurrency/thread_pool.h"
#include "mongo/util/future.h"

namespace mongo {

class FreeMonHttpClientInterface {
public:
    virtual ~FreeMonHttpClientInterface();

    virtual Future<std::vector<uint8_t>> postAsync(StringData url, const BSONObj data) = 0;
};

std::unique_ptr<FreeMonHttpClientInterface> createFreeMonHttpClient(
    std::unique_ptr<executor::ThreadPoolTaskExecutor> executor);
}  // namespace mongo