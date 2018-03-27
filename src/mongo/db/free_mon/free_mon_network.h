#pragma once

#include <boost/optional.hpp>
#include <thread>
#include <queue>
#include <vector>

#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/db/free_mon/free_mon_protocol_gen.h"
#include "mongo/db/ftdc//controller.h"
#include "mongo/util/clock_source.h"
#include "mongo/util/duration.h"
#include "mongo/util/future.h"

namespace mongo {

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
    virtual Future<FreeMonRegistrationResponse> sendRegistrationAsync(
        const FreeMonRegistrationRequest& req) = 0;

    /**
      * POSTs FreeMonMetricsRequest to endpoint.
      *
      * Returns a FreeMonMetricsResponse or throws an error on non-HTTP 200.
      */
    virtual Future<FreeMonMetricsResponse> sendMetricsAsync(const FreeMonMetricsRequest& req) = 0;
};
}