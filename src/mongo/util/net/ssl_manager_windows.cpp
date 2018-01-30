/**
 * Copyright (C) 2018 MongoDB Inc.
 *
 * This program is free software: you can redistribute it and/or  modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, the copyright holders give permission to link the
 * code of portions of this program with the OpenSSL library under certain
 * conditions as described in each individual source file and distribute
 * linked combinations including the program with the OpenSSL library. You
 * must comply with the GNU Affero General Public License in all respects
 * for all of the code used other than as permitted herein. If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so. If you do not
 * wish to do so, delete this exception statement from your version. If you
 * delete this exception statement from all source files in the program,
 * then also delete it in the license file.
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kNetwork

#include "mongo/platform/basic.h"

#include "mongo/util/net/ssl_manager.h"

#include <asio.hpp>
#include <asio/ssl.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stack>
#include <string>
#include <vector>

#include "mongo/base/init.h"
#include "mongo/base/initializer_context.h"
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
#include "mongo/util/uuid.h"

namespace mongo {

namespace {

SimpleMutex sslManagerMtx;
SSLManagerInterface* theSSLManager = NULL;

/**
* Free a Certificate Context.
*/
struct CERTFree {
    void operator()(const CERT_CONTEXT * p) noexcept {
        if (p) {
            ::CertFreeCertificateContext(p);
        }
    }
};

typedef std::unique_ptr<const CERT_CONTEXT, CERTFree> UniqueCertificate;

/**
* A simple generic class to manage Windows handle like things. Behaves similiar to std::unique_ptr/
*
* Only supports move.
*/
template < typename HandleT, class Deleter>
class AutoHandle {
public:
    AutoHandle() : _handle(0) {}
    AutoHandle(HandleT handle) : _handle(handle) {}
    AutoHandle(AutoHandle<HandleT, Deleter>&& handle) : _handle(handle._handle) { handle._handle = nullptr; }

    ~AutoHandle() {
        if (_handle != 0) {
            Deleter()(_handle);
        }
    }

    AutoHandle(const AutoHandle&) = delete;

    AutoHandle& operator= (const HandleT other) {
        _handle = other;
        return *this;
    }

    AutoHandle& operator= (const AutoHandle<HandleT, Deleter>& other) = delete;

    AutoHandle& operator= (AutoHandle<HandleT, Deleter>&& other) {
        _handle = other._handle;
        other._handle = 0;
        return *this;
    }

    operator HandleT() { return _handle; }

private:
    HandleT _handle;
};

/**
* Free a HCRYPTPROV  Handle
*/
struct CryptProviderFree {
    void operator()(HCRYPTPROV const h) noexcept {
        if (h) {
            ::CryptReleaseContext(h, 0);
        }
    }
};

typedef AutoHandle<HCRYPTPROV, CryptProviderFree> UniqueCryptProvider;

/**
* Free a HCRYPTKEY  Handle
*/
struct CryptKeyFree {
    void operator()(HCRYPTKEY const h) noexcept {
        if (h) {
            ::CryptDestroyKey(h);
        }
    }
};

typedef AutoHandle<HCRYPTKEY, CryptKeyFree> UniqueCryptKey;

} // namespace

/**
 * Manage state for a SSL Connection. Used by the Socket class.
 */
class SSLConnectionSChannel : public SSLConnectionInterface  {
public:
    SCHANNEL_CRED* _cred;
    Socket* socket;
    asio::ssl::detail::engine _engine;

    std::vector<char> _tempBuffer;

    SSLConnectionSChannel(SCHANNEL_CRED* cred, Socket* sock, const char* initialBytes, int len);

    ~SSLConnectionSChannel();

    std::string getSNIServerName() const final { 
        // TODO
        return "";
    };
};


class SSLManagerSChannel : public SSLManagerInterface {
public:
    explicit SSLManagerSChannel(const SSLParams& params, bool isServer);

    /**
     * Initializes an OpenSSL context according to the provided settings. Only settings which are
     * acceptable on non-blocking connections are set.
     */
    Status initSSLContext(SCHANNEL_CRED* cred,
        const SSLParams& params,
        ConnectionDirection direction) final;

    SSLConnectionInterface* connect(Socket* socket) final;

    SSLConnectionInterface* accept(Socket* socket, const char* initialBytes, int len)  final;

    SSLPeerInfo parseAndValidatePeerCertificateDeprecated(const SSLConnectionInterface* conn,
                                                                  const std::string& remoteHost)  final;

    StatusWith<boost::optional<SSLPeerInfo>> parseAndValidatePeerCertificate(
        PCtxtHandle ssl, const std::string& remoteHost) final;


    const SSLConfiguration& getSSLConfiguration() const  final {
        return _sslConfiguration;
    } 

    int SSL_read(SSLConnectionInterface* conn, void* buf, int num)  final;

    int SSL_write(SSLConnectionInterface* conn, const void* buf, int num)  final;

    int SSL_shutdown(SSLConnectionInterface* conn) final;

private:
    bool _weakValidation;
    bool _allowInvalidCertificates;
    bool _allowInvalidHostnames;
    SSLConfiguration _sslConfiguration;

    SCHANNEL_CRED _clientCred;
    SCHANNEL_CRED _serverCred;

    UniqueCertificate _pemCertificate;
    UniqueCertificate _clusterPEMCertificate;
    PCCERT_CONTEXT _clientCertificates[1];
    PCCERT_CONTEXT _serverCertificates[1];

    Status loadCertificates(const SSLParams& params);

    void handshake(SSLConnectionSChannel* conn, bool client);
};

// Global variable indicating if this is a server or a client instance
bool isSSLServer = false;

MONGO_INITIALIZER(SSLManager)(InitializerContext*) {
    stdx::lock_guard<SimpleMutex> lck(sslManagerMtx);
    if (!isSSLServer || (sslGlobalParams.sslMode.load() != SSLParams::SSLMode_disabled)) {
        theSSLManager = new SSLManagerSChannel(sslGlobalParams, isSSLServer);
    }

    return Status::OK();
}

SSLConnectionSChannel::SSLConnectionSChannel(SCHANNEL_CRED* cred, Socket* sock, const char* initialBytes, int len)
    : _cred(cred), socket(sock), _engine(_cred) {

    // TODO: hard code SSL packet size somewhere
    _tempBuffer.resize(17 * 1024);

    if (len > 0) {
        _engine.put_input(asio::const_buffer(initialBytes, len));
    }
}

SSLConnectionSChannel::~SSLConnectionSChannel() {

}

std::unique_ptr<SSLManagerInterface> SSLManagerInterface::create(const SSLParams& params,
                                                                 bool isServer) {
    return stdx::make_unique<SSLManagerSChannel>(params, isServer);
}

SSLManagerInterface* getSSLManager() {
    stdx::lock_guard<SimpleMutex> lck(sslManagerMtx);
    if (theSSLManager)
        return theSSLManager;
    return NULL;
}

SSLManagerSChannel::SSLManagerSChannel(const SSLParams& params, bool isServer)
    : _weakValidation(params.sslWeakCertificateValidation),
      _allowInvalidCertificates(params.sslAllowInvalidCertificates),
      _allowInvalidHostnames(params.sslAllowInvalidHostnames) {

    uassertStatusOK(loadCertificates(params));

    uassertStatusOK(initSSLContext(&_clientCred, params, ConnectionDirection::kOutgoing));

    // TODO: validate client certificate

    // SSL server specific initialization
    if (isServer) {
        uassertStatusOK(initSSLContext(&_serverCred, params, ConnectionDirection::kIncoming));

        // TODO: validate server certificate
    }
}

int SSLManagerSChannel::SSL_read(SSLConnectionInterface* connInterface, void* buf, int num) {
    SSLConnectionSChannel* conn = static_cast<SSLConnectionSChannel*>(connInterface);

    while (true) {
        size_t bytes_transferred;
        asio::error_code ec;
        asio::ssl::detail::engine::want want = conn->_engine.read(asio::mutable_buffer(buf, num), ec, bytes_transferred);
        if (ec) {
           throwSocketError(SocketErrorKind::RECV_ERROR, ec.message());
        }

        switch (want) {
        case asio::ssl::detail::engine::want_input_and_retry:
        {
            // ASIO wants more data before it can continue:
            // 1. fetch some from the network
            // 2. give it to ASIO
            // 3. retry
            int ret = recv(conn->socket->rawFD(), reinterpret_cast<char*>(buf), num, portRecvFlags);
            if (ret == SOCKET_ERROR) {
                conn->socket->handleRecvError(ret, num);
            }

            conn->_engine.put_input(asio::const_buffer(buf, ret));

            continue;
        }
        case asio::ssl::detail::engine::want_nothing:
        {
            // ASIO wants nothing, return to caller with anything transfered.
            return bytes_transferred;
        }
        default:
            MONGO_UNREACHABLE;
        }
    }
}

int SSLManagerSChannel::SSL_write(SSLConnectionInterface* connInterface, const void* buf, int num) {
    SSLConnectionSChannel* conn = static_cast<SSLConnectionSChannel*>(connInterface);

    while (true) {
        size_t bytes_transferred;
        asio::error_code ec;
        asio::ssl::detail::engine::want want = conn->_engine.write(asio::const_buffer(buf, num), ec, bytes_transferred);
        if (ec) {
            throwSocketError(SocketErrorKind::SEND_ERROR, ec.message());
        }

        switch (want) {
        case asio::ssl::detail::engine::want_output:
        case asio::ssl::detail::engine::want_output_and_retry:
        {
            // ASIO wants us to send data out:
            // 1. get data from ASIO
            // 2. give it to the network
            // 3. retry if needed
            
            asio::mutable_buffer outBuf = conn->_engine.get_output(asio::mutable_buffer(conn->_tempBuffer.data(), conn->_tempBuffer.size()));

            int ret = send(conn->socket->rawFD(), reinterpret_cast<const char*>(outBuf.data()), outBuf.size(), portSendFlags);
            if (ret == SOCKET_ERROR) {
                conn->socket->handleSendError(ret, "");
            }

            if (want == asio::ssl::detail::engine::want_output_and_retry) {
                continue;
            }

            return bytes_transferred;
        }
        default:
            MONGO_UNREACHABLE;
        }
    }
}

int SSLManagerSChannel::SSL_shutdown(SSLConnectionInterface* conn) {
    invariant(false);
    return 0;
}

StatusWith<UniqueCertificate> readPEMFile(StringData fileName, StringData password, bool client) {

    std::ifstream pemFile(fileName.toString(), std::ios::binary);
    if (!pemFile.is_open()) {
        return Status(ErrorCodes::InvalidSSLConfiguration, str::stream() << "Failed to open PEM file: " << fileName);
    }

    std::string buf((std::istreambuf_iterator<char>(pemFile)),
        std::istreambuf_iterator<char>());

    pemFile.close();

    // Search the buffer for the various strings that make up a PEM file
    size_t publicKey = buf.find("-----BEGIN CERTIFICATE-----");
    if (publicKey == std::string::npos) {
        return Status(ErrorCodes::InvalidSSLConfiguration, str::stream() << "Failed to find Certifiate in: " << fileName);
    }


    // TODO: decode encrypted pem
    // StringData encryptedPrivateKey = buf.find("-----BEGIN ENCRYPTED PRIVATE KEY-----");

    // TODO: check if we need both
    size_t privateKey = buf.find("-----BEGIN RSA PRIVATE KEY-----");
    if (privateKey == std::string::npos) {
        privateKey = buf.find("-----BEGIN PRIVATE KEY-----");
    }

    if (privateKey == std::string::npos) {
        return Status(ErrorCodes::InvalidSSLConfiguration, str::stream() << "Failed to find privateKey in: " << fileName);
    }

    CERT_BLOB certBlob;
    certBlob.cbData = buf.size() - publicKey;
    certBlob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(buf.data() + publicKey));

    PCCERT_CONTEXT cert;
    BOOL ret = CryptQueryObject(
        CERT_QUERY_OBJECT_BLOB,
        &certBlob,
        CERT_QUERY_CONTENT_FLAG_ALL, //CERT_QUERY_CONTENT_FLAG_CERT, // CERT_QUERY_CONTENT_FLAG_ALL??
        CERT_QUERY_FORMAT_FLAG_ALL, //CERT_QUERY_FORMAT_FLAG_BASE64_ENCODED,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        reinterpret_cast<const void**>(&cert)
    );
    if (!ret) {
        DWORD gle = GetLastError();
        return Status(ErrorCodes::InvalidSSLConfiguration, str::stream() << "CryptQueryObject failed to get cert: " << errnoWithDescription(gle));
    }

    UniqueCertificate certHolder(cert);
    DWORD privateKeyLen{0};

    ret = CryptStringToBinaryA(
        buf.c_str() + privateKey,
        0, // null terminated string
        CRYPT_STRING_BASE64HEADER,
        NULL,
        &privateKeyLen,
        NULL,
        NULL
    );
    if (!ret) {
        DWORD gle = GetLastError();
        if (gle != ERROR_MORE_DATA) {
            return Status(ErrorCodes::InvalidSSLConfiguration, str::stream() << "CryptStringToBinary failed to get size of key: " << errnoWithDescription(gle));
        }
    }

    std::unique_ptr<BYTE> privateKeyBuf(new BYTE[privateKeyLen]);
    ret = CryptStringToBinaryA(
        buf.c_str() + privateKey,
        0, // null terminated string
        CRYPT_STRING_BASE64HEADER,
        privateKeyBuf.get(),
        &privateKeyLen,
        NULL,
        NULL
    );
    if (!ret) {
        DWORD gle = GetLastError();
        return Status(ErrorCodes::InvalidSSLConfiguration, str::stream() << "CryptStringToBinary failed to read key: " << errnoWithDescription(gle));
    }


    DWORD privateBlobLen{0};

    ret = CryptDecodeObjectEx(
        X509_ASN_ENCODING,
        PKCS_RSA_PRIVATE_KEY,
        privateKeyBuf.get(),
        privateKeyLen,
        0,
        NULL,
        NULL,
        &privateBlobLen);
    if (!ret) {
        DWORD gle = GetLastError();
        if (gle != ERROR_MORE_DATA) {
            return Status(ErrorCodes::InvalidSSLConfiguration, str::stream() << "CryptDecodeObjectEx failed to get size of key: " << errnoWithDescription(gle));
        }
    }

    std::unique_ptr<BYTE> privateBlobBuf(new BYTE[privateBlobLen]);

    ret = CryptDecodeObjectEx(
        X509_ASN_ENCODING,
        PKCS_RSA_PRIVATE_KEY,
        privateKeyBuf.get(),
        privateKeyLen,
        0,
        NULL,
        privateBlobBuf.get(),
        &privateBlobLen);
    if (!ret) {
        DWORD gle = GetLastError();
        return Status(ErrorCodes::InvalidSSLConfiguration, str::stream() << "CryptDecodeObjectEx failed to read key: " << errnoWithDescription(gle));
    }

    // TODO: leak or free? CryptReleaseContext
    // TODO: fix this
    auto us = UUID::gen().toString();
    std::wstring wstr = toNativeString(us.c_str());

    HCRYPTPROV hProv;
    // See https://msdn.microsoft.com/en-us/library/windows/desktop/aa375195(v=vs.85).aspx
    // Note: Server side requires CRYPT_VERIFYCONTEXT off
    ret = CryptAcquireContextW(&hProv,
        wstr.c_str(),
        MS_ENHANCED_PROV,
        PROV_RSA_FULL,
        CRYPT_NEWKEYSET
    );
    if (!ret) {
        DWORD gle = GetLastError();
        return Status(ErrorCodes::InvalidSSLConfiguration, str::stream() << "CryptAcquireContextA failed  " << errnoWithDescription(gle));
    }

    UniqueCryptProvider cryptProvider(hProv);

    HCRYPTKEY hkey;
    ret = CryptImportKey(
        hProv,
        privateBlobBuf.get(),
        privateBlobLen,
        0,
        0,
        &hkey);
    if (!ret) {
        DWORD gle = GetLastError();
        return Status(ErrorCodes::InvalidSSLConfiguration, str::stream() << "CryptImportKey failed  " << errnoWithDescription(gle));
    }
    UniqueCryptKey(hKey);

    // Server-side SChannel requires a different way of attaching the private key to the certificate
    if (true) {
        DWORD nameBlobLen{0};

        ret = CryptGetProvParam(hProv,
            PP_CONTAINER,
            NULL,
            &nameBlobLen,
            0);

        if (!ret) {
            DWORD gle = GetLastError();
            if (gle != ERROR_MORE_DATA) {
                return Status(ErrorCodes::InvalidSSLConfiguration, str::stream() << "CryptGetProvParam Failed to get size of key " << errnoWithDescription(gle));
            }
        }

        std::unique_ptr<BYTE> nameBlob(new BYTE[nameBlobLen]);
        ret = CryptGetProvParam(hProv,
            PP_CONTAINER,
            nameBlob.get(),
            &nameBlobLen,
            0);
        if (!ret) {
            DWORD gle = GetLastError();
            return Status(ErrorCodes::InvalidSSLConfiguration, str::stream() << "CryptGetProvParam Failed to get size of key " << errnoWithDescription(gle));
        }

        std::wstring wKeyName = toWideString((char*)nameBlob.get());

        // NOTE: This is used to set the certificate for server side SCHannel
        CRYPT_KEY_PROV_INFO keyProvInfo;
        memset(&keyProvInfo, 0, sizeof(keyProvInfo));
        keyProvInfo.pwszContainerName = (LPWSTR)wKeyName.c_str();
        keyProvInfo.pwszProvName = const_cast<wchar_t*>(MS_ENHANCED_PROV);
        keyProvInfo.dwFlags = CERT_SET_KEY_PROV_HANDLE_PROP_ID | CERT_SET_KEY_CONTEXT_PROP_ID;
        keyProvInfo.dwProvType = PROV_RSA_FULL;
        keyProvInfo.dwKeySpec = AT_KEYEXCHANGE;
        if (!CertSetCertificateContextProperty(cert, CERT_KEY_PROV_INFO_PROP_ID, 0, &keyProvInfo)) {
            DWORD gle = GetLastError();
            return Status(ErrorCodes::InvalidSSLConfiguration, str::stream() << "CertSetCertificateContextProperty Failed  " << errnoWithDescription(gle));
        }
    }

    // NOTE: This is used to set the certificate for client side SChannel
    ret = CertSetCertificateContextProperty(
        cert,
        CERT_KEY_PROV_HANDLE_PROP_ID,
        0,
        (const void *)hProv);
    if (!ret) {
        DWORD gle = GetLastError();
        return Status(ErrorCodes::InvalidSSLConfiguration, str::stream() << "CertSetCertificateContextProperty failed  " << errnoWithDescription(gle));
    }


    return std::move(certHolder);
}

Status SSLManagerSChannel::loadCertificates(const SSLParams& params) {
    _clientCertificates[0] = nullptr;
    _serverCertificates[0] = nullptr;

    // Load the normal PEM file
    if (!params.sslPEMKeyFile.empty()) {
        auto swCertificate = readPEMFile(params.sslPEMKeyFile, params.sslPEMKeyPassword, true);
        if (!swCertificate.isOK()) {
            return swCertificate.getStatus();
        }

        _pemCertificate = std::move(swCertificate.getValue());
    }
    
    // Load the cluster PEM file, only applies to server side code
    if (!params.sslClusterFile.empty()) {
        auto swCertificate = readPEMFile(params.sslClusterFile, params.sslClusterPassword, true);
        if (!swCertificate.isOK()) {
            return swCertificate.getStatus();
        }

        _clusterPEMCertificate = std::move(swCertificate.getValue());
    } 
    
    if (_pemCertificate) {
        _clientCertificates[0] = _pemCertificate.get();
        _serverCertificates[0] = _pemCertificate.get();
    }

    if (_clusterPEMCertificate) {
        _clientCertificates[0] = _clusterPEMCertificate.get();
    }

    return Status::OK();
}

Status SSLManagerSChannel::initSSLContext(SCHANNEL_CRED* cred,
    const SSLParams& params,
    ConnectionDirection direction) {

    ZeroMemory(cred, sizeof(*cred));
    cred->dwVersion = SCHANNEL_CRED_VERSION;
    cred->dwFlags = SCH_CRED_SNI_CREDENTIAL;

    uint32_t supportedProtocols = SCH_USE_STRONG_CRYPTO;

    // TODO: revisit this
    if (direction == ConnectionDirection::kIncoming) {
        cred->dwFlags |= SCH_CRED_NO_SYSTEM_MAPPER // Do not map certificate to user account
            | SCH_CRED_DISABLE_RECONNECTS;
        supportedProtocols = SP_PROT_TLS1_SERVER | SP_PROT_TLS1_0_SERVER | SP_PROT_TLS1_1_SERVER | SP_PROT_TLS1_2_SERVER;
    } else {
        supportedProtocols = SP_PROT_TLS1_CLIENT | SP_PROT_TLS1_0_CLIENT | SP_PROT_TLS1_1_CLIENT | SP_PROT_TLS1_2_CLIENT;

        cred->dwFlags |=
            SCH_CRED_AUTO_CRED_VALIDATION | SCH_CRED_REVOCATION_CHECK_CHAIN
            | SCH_CRED_NO_SERVERNAME_CHECK;
        cred->dwFlags |= SCH_CRED_NO_DEFAULT_CREDS // No Default Certificate
            | SCH_CRED_MANUAL_CRED_VALIDATION; // Validate Certificate Manually
    }

    // Set the supported TLS protocols. Allow --sslDisabledProtocols to disable selected
    // ciphers.
    for (const SSLParams::Protocols& protocol : params.sslDisabledProtocols) {
        if (protocol == SSLParams::Protocols::TLS1_0) {
            supportedProtocols &= ~(SP_PROT_TLS1_0_CLIENT | SP_PROT_TLS1_0_SERVER);
        } else if (protocol == SSLParams::Protocols::TLS1_1) {
            supportedProtocols &= ~(SP_PROT_TLS1_1_CLIENT | SP_PROT_TLS1_1_SERVER);
        } else if (protocol == SSLParams::Protocols::TLS1_2) {
            supportedProtocols &= ~(SP_PROT_TLS1_2_CLIENT | SP_PROT_TLS1_2_SERVER);
        }
    }

    cred->grbitEnabledProtocols = supportedProtocols;
    
    if (!params.sslCipherConfig.empty()) {
        warning() << "sslCipherConfig parameter is not supported with Windows SChannel and is ignored.";
    }

    cred->cCreds = 1;
    if (direction == ConnectionDirection::kOutgoing) {
        if (_clientCertificates[0]) {
            cred->paCred = _clientCertificates;
        }
    } else {
        cred->paCred = _serverCertificates;
    }

    return Status::OK();
}

SSLConnectionInterface* SSLManagerSChannel::connect(Socket* socket) {
    std::unique_ptr<SSLConnectionSChannel> sslConn =
        stdx::make_unique<SSLConnectionSChannel>(&_clientCred, socket, (const char*)NULL, 0);

    handshake(sslConn.get(), true);
    return sslConn.release();
}

SSLConnectionInterface* SSLManagerSChannel::accept(Socket* socket, const char* initialBytes, int len) {
    std::unique_ptr<SSLConnectionSChannel> sslConn =
        stdx::make_unique<SSLConnectionSChannel>(&_serverCred, socket, initialBytes, len);

    handshake(sslConn.get(), false);

    return sslConn.release();
}

void SSLManagerSChannel::handshake(SSLConnectionSChannel* connInterface, bool client) {
    SSLConnectionSChannel* conn = static_cast<SSLConnectionSChannel*>(connInterface);

    initSSLContext(conn->_cred, getSSLGlobalParams(), client ?
        SSLManagerInterface::ConnectionDirection::kOutgoing : SSLManagerInterface::ConnectionDirection::kIncoming);
   
    while (true) {
        asio::error_code ec;
        asio::ssl::detail::engine::want want = conn->_engine.handshake(client ? asio::ssl::stream_base::handshake_type::client : asio::ssl::stream_base::handshake_type::server, ec);
        if (ec) {
            throwSocketError(SocketErrorKind::RECV_ERROR, ec.message());
        }

        switch (want) {
        case asio::ssl::detail::engine::want_input_and_retry:
        {
            // ASIO wants more data before it can continue,
            // 1. fetch some from the network
            // 2. give it to ASIO
            // 3. retry
            int ret = recv(conn->socket->rawFD(), conn->_tempBuffer.data(), conn->_tempBuffer.size(), portRecvFlags);
            if (ret == SOCKET_ERROR) {
                conn->socket->handleRecvError(ret, conn->_tempBuffer.size());
            }

            conn->_engine.put_input(asio::const_buffer(conn->_tempBuffer.data(), ret));

            continue;
        }
        case asio::ssl::detail::engine::want_output:
        case asio::ssl::detail::engine::want_output_and_retry:
        {
            // ASIO wants us to send data out
            // 1. get data from ASIO
            // 2. give it to the network
            // 3. retry if needed
            asio::mutable_buffer outBuf = conn->_engine.get_output(asio::mutable_buffer(conn->_tempBuffer.data(), conn->_tempBuffer.size()));

            int ret = send(conn->socket->rawFD(), reinterpret_cast<const char*>(outBuf.data()), outBuf.size(), portSendFlags);
            if (ret == SOCKET_ERROR) {
                conn->socket->handleSendError(ret, "");
            }

            if (want == asio::ssl::detail::engine::want_output_and_retry) {
                continue;
            }

            // ASIO wants nothing, return to caller since we are done with handshake
            return;
        }
        case asio::ssl::detail::engine::want_nothing:
        {
            // ASIO wants nothing, return to caller since we are done with handshake
            return;
        }
        default:
            MONGO_UNREACHABLE;
        }
    }
}

SSLPeerInfo SSLManagerSChannel::parseAndValidatePeerCertificateDeprecated(const SSLConnectionInterface* conn,
                                                                  const std::string& remoteHost) {
    auto swPeerSubjectName = parseAndValidatePeerCertificate(const_cast<SSLConnectionSChannel*>(static_cast<const SSLConnectionSChannel*>(conn))->_engine.native_handle(), remoteHost);
    // We can't use uassertStatusOK here because we need to throw a SocketException.
    if (!swPeerSubjectName.isOK()) {
        throwSocketError(SocketErrorKind::CONNECT_ERROR,
            swPeerSubjectName.getStatus().reason());
    }

    return swPeerSubjectName.getValue().get_value_or(SSLPeerInfo());
}

StatusWith<boost::optional<SSLPeerInfo>> SSLManagerSChannel::parseAndValidatePeerCertificate(
    PCtxtHandle ssl, const std::string& remoteHost) {
    if (!_sslConfiguration.hasCA && isSSLServer)
        return{boost::none};

    return{boost::none};
}


}  // namespace mongo
