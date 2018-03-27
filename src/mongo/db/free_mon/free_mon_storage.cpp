#include "mongo/db/free_mon/free_mon_controller.h"

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
#include "mongo/db/repl/storage_interface.h"

namespace mongo {

namespace {
static const NamespaceString adminSystemVersionNss("admin.system.version");

} // namespace

boost::optional<FreeMonStorageState> FreeMonStorage::read(OperationContext* opCtx) {
    auto storageInterface = repl::StorageInterface::get(opCtx);

        Lock::DBLock dblk(opCtx, adminSystemVersionNss.db(), MODE_IS);
        Lock::CollectionLock lk(opCtx->lockState(), adminSystemVersionNss.ns(), MODE_IS);

        auto swObj = storageInterface->findSingleton(opCtx, adminSystemVersionNss);
        if (!swObj.isOK()) {
            if (swObj.getStatus() == ErrorCodes::CollectionIsEmpty) {
                return{};
            }
            uassertStatusOK(swObj.getStatus());

        }

    return FreeMonStorageState::parse(IDLParserErrorContext("Foo"), swObj.getValue());
#if 0
    Lock::DBLock dblk(opCtx, adminSystemVersionNss.db(), MODE_IS);
    Lock::CollectionLock lk(opCtx->lockState(), adminSystemVersionNss.ns(), MODE_IS);
    BSONObj mv;
    if (Helpers::getSingleton(opCtx, adminSystemVersionNss.ns().c_str(), mv)) {
        
        // TODO: 
        return{};
    }

    // TODO: 

    return{};
#endif
}

bool FreeMonStorage::replace(OperationContext* opCtx, const FreeMonStorageState& doc) {
    BSONObj obj = doc.toBSON();
    
    repl::TimestampedBSONObj update;
    update.obj = obj;

    auto storageInterface = repl::StorageInterface::get(opCtx);
    {
        Lock::DBLock dblk(opCtx, adminSystemVersionNss.db(), MODE_IS);
        Lock::CollectionLock lk(opCtx->lockState(), adminSystemVersionNss.ns(), MODE_IS);

        auto swObj = storageInterface->putSingleton(opCtx, adminSystemVersionNss, update);
        if (!swObj.isOK()) {
            uassertStatusOK(swObj);
        }
    }

    return true;
}

bool FreeMonStorage::deleteState(OperationContext* opCtx) {
    // TODO
    return true; }

boost::optional<BSONObj> FreeMonStorage::readClusterManagerState(OperationContext* opCtx) {
    // TODO
    return BSONObj(); }

}// namespace mongo