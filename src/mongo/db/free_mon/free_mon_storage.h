#pragma once

#include <boost/optional.hpp>
#include <thread>
#include <queue>
#include <vector>

#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/db/free_mon/free_monitoring_commands_gen.h"
#include "mongo/db/free_mon/free_monitoring_protocol_gen.h"
#include "mongo/db/free_mon/free_monitoring_storage_gen.h"
#include "mongo/db/ftdc//controller.h"
#include "mongo/util/clock_source.h"
#include "mongo/util/duration.h"
#include "mongo/util/future.h"

namespace mongo {
using FreeMonCollectorInterface = FTDCCollectorInterface;
using FreeMonCollectorCollection = FTDCCollectorCollection;

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
    static boost::optional<BSONObj> readClusterManagerState(OperationContext* opCtx);
};

} // namespace mongo