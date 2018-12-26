
/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kBridge

#include "mongo/platform/basic.h"

#include <boost/optional.hpp>
#include <cstdint>

#include "mongo/base/init.h"
#include "mongo/base/initializer.h"
#include "mongo/db/dbmessage.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/service_context.h"
#include "mongo/fle/mongofle_commands.h"
#include "mongo/fle/mongofle_options.h"
#include "mongo/platform/atomic_word.h"
#include "mongo/platform/random.h"
#include "mongo/rpc/factory.h"
#include "mongo/rpc/message.h"
#include "mongo/rpc/reply_builder_interface.h"
#include "mongo/stdx/memory.h"
#include "mongo/stdx/mutex.h"
#include "mongo/stdx/thread.h"
#include "mongo/transport/message_compressor_manager.h"
#include "mongo/transport/service_entry_point_impl.h"
#include "mongo/transport/service_executor_synchronous.h"
#include "mongo/transport/transport_layer_asio.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/exit.h"
#include "mongo/util/log.h"
#include "mongo/util/mongoutils/str.h"
#include "mongo/util/quick_exit.h"
#include "mongo/util/signal_handlers.h"
#include "mongo/util/text.h"
#include "mongo/util/time_support.h"
#include "mongo/util/timer.h"

namespace mongo {
class FLEContext {
public:
    Status runFLECommand(const OpMsgRequest& request, BSONObjBuilder* builder) {
        
        auto status = FLECommand::findCommand(request.getCommandName());
        if (!status.isOK()) {
            log() << "failed to find parsing command: " << request.getCommandName();

            return status.getStatus();
        }

        log() << "Processing FLE command: " << request.getCommandName();

        FLECommand* command = status.getValue();
        return command->run(request, builder);
    }

    static FLEContext* get();

private:
    static const ServiceContext::Decoration<FLEContext> _get;
};

const ServiceContext::Decoration<FLEContext> FLEContext::_get =
    ServiceContext::declareDecoration<FLEContext>();

FLEContext* FLEContext::get() {
    return &_get(getGlobalServiceContext());
}

class ServiceEntryPointFLE;

class ServiceEntryPointFLE final : public ServiceEntryPointImpl {
public:
    explicit ServiceEntryPointFLE(ServiceContext* svcCtx) : ServiceEntryPointImpl(svcCtx) {}

    DbResponse handleRequest(OperationContext* opCtx, const Message& request) final;
};

void generateErrorResponse(OperationContext* opCtx,
                           rpc::ReplyBuilderInterface* replyBuilder,
                           const DBException& exception,
                           const BSONObj& replyMetadata,
                           BSONObj extraFields = {}) {

    // We could have thrown an exception after setting fields in the builder,
    // so we need to reset it to a clean state just to be sure.
    replyBuilder->reset();
    replyBuilder->setCommandReply(exception.toStatus(), extraFields);
    replyBuilder->getBodyBuilder().appendElements(replyMetadata);
}


DbResponse ServiceEntryPointFLE::handleRequest(OperationContext* opCtx, const Message& message) {
    auto brCtx = FLEContext::get();

    log() << "Handling Request";

    auto replyBuilder = rpc::makeReplyBuilder(rpc::protocolForMessage(message));

    OpMsgRequest request;
    try {  // Parse.
        request = rpc::opMsgRequestFromAnyProtocol(message);
    } catch (const DBException& ex) {
        // If this error needs to fail the connection, propagate it out.
        if (ErrorCodes::isConnectionFatalMessageParseError(ex.code()))
            throw;

        // Otherwise, reply with the parse error. This is useful for cases where parsing fails
        // due to user-supplied input, such as the document too deep error. Since we failed
        // during parsing, we can't log anything about the command.
        log() << "assertion while parsing command: " << ex.toString();

        BSONObjBuilder metadataBob;

        generateErrorResponse(opCtx, replyBuilder.get(), ex, metadataBob.obj(), BSONObj());

        auto response = replyBuilder->done();
        return DbResponse{std::move(response)};
    }


    try {
        BSONObjBuilder builder;
        (void)brCtx->runFLECommand(request, &builder);

        // TODO - improve
        builder.append("ok", 1);

        replyBuilder->getBodyBuilder().appendElements(builder.obj());
    } catch (const DBException& ex) {
                BSONObjBuilder metadataBob;

        generateErrorResponse(
            opCtx, replyBuilder.get(), ex, metadataBob.obj(), BSONObj());

        auto response = replyBuilder->done();
        return DbResponse{std::move(response)};

    }
    auto response = replyBuilder->done();
    return DbResponse{std::move(response)};
}

int FLEMain(int argc, char** argv, char** envp) {

    registerShutdownTask([&] {
        // NOTE: This function may be called at any time. It must not
        // depend on the prior execution of mongo initializers or the
        // existence of threads.
        if (hasGlobalServiceContext()) {
            auto sc = getGlobalServiceContext();
            if (sc->getTransportLayer())
                sc->getTransportLayer()->shutdown();

            if (sc->getServiceEntryPoint()) {
                sc->getServiceEntryPoint()->endAllSessions(transport::Session::kEmptyTagMask);
                sc->getServiceEntryPoint()->shutdown(Seconds{10});
            }
        }
    });

    setupSignalHandlers();
    runGlobalInitializersOrDie(argc, argv, envp);
    startSignalProcessingThread(LogFileStatus::kNoLogFileToRotate);

    WireSpec& spec = WireSpec::instance();

    // Since the upgrade order calls for upgrading mongos last, it only needs to talk the latest
    // wire version. This ensures that users will get errors if they upgrade in the wrong order.
    spec.outgoing.minWireVersion = LATEST_WIRE_VERSION;
    spec.outgoing.maxWireVersion = LATEST_WIRE_VERSION;

    setGlobalServiceContext(ServiceContext::make());
    auto serviceContext = getGlobalServiceContext();
    serviceContext->setServiceEntryPoint(std::make_unique<ServiceEntryPointFLE>(serviceContext));
    serviceContext->setServiceExecutor(
        std::make_unique<transport::ServiceExecutorSynchronous>(serviceContext));

    fassert(51999, serviceContext->getServiceExecutor()->start());

    transport::TransportLayerASIO::Options opts;
    opts.ipList.emplace_back("127.0.0.1");
    opts.port = mongoFLEGlobalParams.port;

    serviceContext->setTransportLayer(std::make_unique<mongo::transport::TransportLayerASIO>(
        opts, serviceContext->getServiceEntryPoint()));
    auto tl = serviceContext->getTransportLayer();
    if (!tl->setup().isOK()) {
        log() << "Error setting up transport layer";
        return EXIT_NET_ERROR;
    }

    if (!tl->start().isOK()) {
        log() << "Error starting transport layer";
        return EXIT_NET_ERROR;
    }

    serviceContext->notifyStartupComplete();
    return waitForShutdown();
}

}  // namespace mongo

#if defined(_WIN32)
// In Windows, wmain() is an alternate entry point for main(), and receives the same parameters
// as main() but encoded in Windows Unicode (UTF-16); "wide" 16-bit wchar_t characters.  The
// WindowsCommandLine object converts these wide character strings to a UTF-8 coded equivalent
// and makes them available through the argv() and envp() members.  This enables FLEMain()
// to process UTF-8 encoded arguments and environment variables without regard to platform.
int wmain(int argc, wchar_t* argvW[], wchar_t* envpW[]) {
    mongo::WindowsCommandLine wcl(argc, argvW, envpW);
    int exitCode = mongo::FLEMain(argc, wcl.argv(), wcl.envp());
    mongo::quickExit(exitCode);
}
#else
int main(int argc, char* argv[], char** envp) {
    int exitCode = mongo::FLEMain(argc, argv, envp);
    mongo::quickExit(exitCode);
}
#endif
