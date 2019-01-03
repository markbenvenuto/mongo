
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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kControl

#include "mongo/platform/basic.h"

#include "mongo/db/initialize_server_global_state.h"

#include <iostream>
#include <memory>
#include <signal.h>

#ifndef _WIN32
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <syslog.h>
#endif

#include "mongo/base/init.h"
#include "mongo/client/authenticate.h"
#include "mongo/config.h"
#include "mongo/db/auth/authorization_manager.h"
#include "mongo/db/auth/sasl_command_constants.h"
#include "mongo/db/auth/security_key.h"
#include "mongo/db/server_options.h"
#include "mongo/db/server_parameters.h"
#include "mongo/logger/console_appender.h"
#include "mongo/logger/logger.h"
#include "mongo/logger/message_event.h"
#include "mongo/logger/message_event_utf8_encoder.h"
#include "mongo/logger/ramlog.h"
#include "mongo/logger/rotatable_file_appender.h"
#include "mongo/logger/rotatable_file_manager.h"
#include "mongo/logger/rotatable_file_writer.h"
#include "mongo/logger/syslog_appender.h"
#include "mongo/platform/process_id.h"
#include "mongo/util/log.h"
#include "mongo/util/mongoutils/str.h"
#include "mongo/util/net/ssl_manager.h"
#include "mongo/util/processinfo.h"
#include "mongo/util/quick_exit.h"
#include "mongo/util/signal_handlers_synchronous.h"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace mongo {

bool initializeServerSecurityGlobalState(ServiceContext* service) {

    int clusterAuthMode = serverGlobalParams.clusterAuthMode.load();
    if (!serverGlobalParams.keyFile.empty() &&
        clusterAuthMode != ServerGlobalParams::ClusterAuthMode_x509) {
        if (!setUpSecurityKey(serverGlobalParams.keyFile)) {
            // error message printed in setUpPrivateKey
            return false;
        }
    }

    // Auto-enable auth unless we are in mixed auth/no-auth or clusterAuthMode was not provided.
    // clusterAuthMode defaults to "keyFile" if a --keyFile parameter is provided.
    if (clusterAuthMode != ServerGlobalParams::ClusterAuthMode_undefined &&
        !serverGlobalParams.transitionToAuth) {
        AuthorizationManager::get(service)->setAuthEnabled(true);
    }

#ifdef MONGO_CONFIG_SSL
    if (clusterAuthMode == ServerGlobalParams::ClusterAuthMode_x509 ||
        clusterAuthMode == ServerGlobalParams::ClusterAuthMode_sendX509) {
        auth::setInternalUserAuthParams(
            BSON(saslCommandMechanismFieldName
                 << "MONGODB-X509"
                 << saslCommandUserDBFieldName
                 << "$external"
                 << saslCommandUserFieldName
                 << getSSLManager()->getSSLConfiguration().clientSubjectName.toString()));
    }
#endif

    return true;
}
}  // namespace mongo
