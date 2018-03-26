#include "mongo/db/free_mon/free_monitoring_controller.h"

#include "mongo/db/client.h"
#include "mongo/db/concurrency/d_concurrency.h"
#include "mongo/db/concurrency/write_conflict_exception.h"
#include "mongo/db/db_raii.h"
#include "mongo/db/dbhelpers.h"
#include "mongo/db/namespace_string.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/repl/replication_coordinator.h"
#include "mongo/db/repl/replication_coordinator_mock.h"
#include "mongo/db/repl/storage_interface_impl.h"
#include "mongo/db/service_context_d_test_fixture.h"
#include "mongo/db/storage/recovery_unit_noop.h"
#include "mongo/stdx/memory.h"
#include "mongo/util/mongoutils/str.h"


namespace mongo {

static const NamespaceString adminSystemVersionNss("admin.system.version");

boost::optional<FreeMonStorageState> FreeMonStorage::read(OperationContext* opCtx) {
    Lock::DBLock dblk(opCtx, adminSystemVersionNss.db(), MODE_IS);
    Lock::CollectionLock lk(opCtx->lockState(), adminSystemVersionNss.ns(), MODE_IS);
    BSONObj mv;
    if (Helpers::getSingleton(opCtx, adminSystemVersionNss.ns().c_str(), mv)) {
        return{};
    }
    
    return{};
}

bool FreeMonStorage::replace(OperationContext* opCtx, const FreeMonStorageState& doc) { return true; }

bool FreeMonStorage::deleteState(OperationContext* opCtx) { return true; }

BSONObj FreeMonStorage::readClusterManagerState(OperationContext* opCtx) { return BSONObj(); }

}// namespace mongo