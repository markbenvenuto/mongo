/*    Copyright 2009 10gen Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects
 *    for all of the code used other than as permitted herein. If you modify
 *    file(s) with this exception, you may extend this exception to your
 *    version of the file(s), but you are not obligated to do so. If you do not
 *    wish to do so, delete this exception statement from your version. If you
 *    delete this exception statement from all source files in the program,
 *    then also delete it in the license file.
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kNetwork

#include "mongo/platform/basic.h"

#include "mongo/util/net/ssl_manager.h"

#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stack>
#include <string>
#include <vector>

#include "mongo/base/init.h"
#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/config.h"
#include "mongo/db/server_parameters.h"
#include "mongo/platform/atomic_word.h"
#include "mongo/stdx/memory.h"
#include "mongo/transport/session.h"
#include "mongo/util/concurrency/mutex.h"
#include "mongo/util/debug_util.h"
#include "mongo/util/exit.h"
#include "mongo/util/log.h"
#include "mongo/util/mongoutils/str.h"
#include "mongo/util/net/private/ssl_expiration.h"
#include "mongo/util/net/sock.h"
#include "mongo/util/net/socket_exception.h"
#include "mongo/util/net/ssl_options.h"
#include "mongo/util/net/ssl_types.h"
#include "mongo/util/scopeguard.h"
#include "mongo/util/text.h"

namespace mongo {

namespace {

// Because the hostname having a slash is used by `mongo::SockAddr` to determine if a hostname is a
// Unix Domain Socket endpoint, this function uses the same logic.  (See
// `mongo::SockAddr::Sockaddr(StringData, int, sa_family_t)`).  A user explicitly specifying a Unix
// Domain Socket in the present working directory, through a code path which supplies `sa_family_t`
// as `AF_UNIX` will cause this code to lie.  This will, in turn, cause the
// `SSLManager::parseAndValidatePeerCertificate` code to believe a socket is a host, which will then
// cause a connection failure if and only if that domain socket also has a certificate for SSL and
// the connection is an SSL connection.
bool isUnixDomainSocket(const std::string& hostname) {
    return end(hostname) != std::find(begin(hostname), end(hostname), '/');
}

const transport::Session::Decoration<SSLPeerInfo> peerInfoForSession =
    transport::Session::declareDecoration<SSLPeerInfo>();

/**
 * Configurable via --setParameter disableNonSSLConnectionLogging=true. If false (default)
 * if the sslMode is set to preferSSL, we will log connections that are not using SSL.
 * If true, such log messages will be suppressed.
 */
ExportedServerParameter<bool, ServerParameterType::kStartupOnly>
    disableNonSSLConnectionLoggingParameter(ServerParameterSet::getGlobal(),
                                            "disableNonSSLConnectionLogging",
                                            &sslGlobalParams.disableNonSSLConnectionLogging);

ExportedServerParameter<std::string, ServerParameterType::kStartupOnly>
    setDiffieHellmanParameterPEMFile(ServerParameterSet::getGlobal(),
                                     "opensslDiffieHellmanParameters",
                                     &sslGlobalParams.sslPEMTempDHParam);
}  // namespace

SSLPeerInfo& SSLPeerInfo::forSession(const transport::SessionHandle& session) {
    return peerInfoForSession(session.get());
}

SSLParams sslGlobalParams;

const SSLParams& getSSLGlobalParams() {
    return sslGlobalParams;
}


namespace {
void canonicalizeClusterDN(std::vector<std::string>* dn) {
    // remove all RDNs we don't care about
    for (size_t i = 0; i < dn->size(); i++) {
        std::string& comp = dn->at(i);
        boost::algorithm::trim(comp);
        if (!mongoutils::str::startsWith(comp.c_str(), "DC=") &&
            !mongoutils::str::startsWith(comp.c_str(), "O=") &&
            !mongoutils::str::startsWith(comp.c_str(), "OU=")) {
            dn->erase(dn->begin() + i);
            i--;
        }
    }
    std::stable_sort(dn->begin(), dn->end());
}
}  // namespace

bool SSLConfiguration::isClusterMember(StringData subjectName) const {
    std::vector<std::string> clientRDN = StringSplitter::split(subjectName.toString(), ",");
    std::vector<std::string> serverRDN = StringSplitter::split(serverSubjectName, ",");

    canonicalizeClusterDN(&clientRDN);
    canonicalizeClusterDN(&serverRDN);

    if (clientRDN.size() == 0 || clientRDN.size() != serverRDN.size()) {
        return false;
    }

    for (size_t i = 0; i < serverRDN.size(); i++) {
        if (clientRDN[i] != serverRDN[i]) {
            return false;
        }
    }
    return true;
}

BSONObj SSLConfiguration::getServerStatusBSON() const {
    BSONObjBuilder security;
    security.append("SSLServerSubjectName", serverSubjectName);
    security.appendBool("SSLServerHasCertificateAuthority", hasCA);
    security.appendDate("SSLServerCertificateExpirationDate", serverCertificateExpirationDate);
    return security.obj();
}

}  // namespace mongo
