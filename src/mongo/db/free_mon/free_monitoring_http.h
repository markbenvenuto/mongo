#pragma once

#include <memory>
#include <vector>
#include <cstdint>

#include "mongo/base/string_data.h"
#include "mongo/base/data_range.h"
#include "mongo/base/status_with.h"

namespace mongo {

class FreeMonitoringHttpClientInterface {
public:
    virtual  ~FreeMonitoringHttpClientInterface();

    virtual StatusWith<std::vector<uint8_t>> post(StringData url, ConstDataRange data) = 0;
};

std::unique_ptr<FreeMonitoringHttpClientInterface> createFreeMonHttpClient();
} // namespace mongo