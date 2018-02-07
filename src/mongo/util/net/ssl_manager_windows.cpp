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
#include "mongo/db/server_options.h"
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
    void operator()(const CERT_CONTEXT* p) noexcept {
        if (p) {
            ::CertFreeCertificateContext(p);
        }
    }
};

typedef std::unique_ptr<const CERT_CONTEXT, CERTFree> UniqueCertificate;


/**
* Free a CRL Handle
*/
struct CryptCRLFree {
    void operator()(const CRL_CONTEXT* p) noexcept {
        if (p) {
            ::CertFreeCRLContext(p);
        }
    }
};

using UniqueCRL = std::unique_ptr<const CRL_CONTEXT, CryptCRLFree>;


/**
* Free a Certificate Chain Context
*/
struct CryptCertChainFree {
    void operator()(const CERT_CHAIN_CONTEXT* p) noexcept {
        if (p) {
            ::CertFreeCertificateChain(p);
        }
    }
};

using UniqueCertChain = std::unique_ptr<const CERT_CHAIN_CONTEXT, CryptCertChainFree>;


/**
* A simple generic class to manage Windows handle like things. Behaves similiar to std::unique_ptr/
*
* Only supports move.
*/
template <typename HandleT, class Deleter>
class AutoHandle {
public:
    AutoHandle() : _handle(0) {}
    AutoHandle(HandleT handle) : _handle(handle) {}
    AutoHandle(AutoHandle<HandleT, Deleter>&& handle) : _handle(handle._handle) {
        handle._handle = nullptr;
    }

    ~AutoHandle() {
        if (_handle != 0) {
            Deleter()(_handle);
        }
    }

    AutoHandle(const AutoHandle&) = delete;

    AutoHandle& operator=(const HandleT other) {
        _handle = other;
        return *this;
    }

    AutoHandle& operator=(const AutoHandle<HandleT, Deleter>& other) = delete;

    AutoHandle& operator=(AutoHandle<HandleT, Deleter>&& other) {
        _handle = other._handle;
        other._handle = 0;
        return *this;
    }

    operator HandleT() {
        return _handle;
    }

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

/**
* Free a CERTSTORE Handle
*/
struct CertStoreFree {
    void operator()(HCERTSTORE const p) noexcept {
        if (p) {
            // For leak detection, add CERT_CLOSE_STORE_CHECK_FLAG
            // Currently, we open very few cert stores and let the certs live beyond the cert store
            // so the leak detection flag is not useful.
            ::CertCloseStore(p, 0);
        }
    }
};

typedef AutoHandle<HCERTSTORE, CertStoreFree> UniqueCertStore;

std::string getCertificateSubjectName(PCCERT_CONTEXT cert) {
    DWORD needed = CertGetNameStringA(cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, NULL, NULL, 0);
    uassert(50662,
            str::stream() << "CertGetNameString size query failed with: " << needed,
            needed != 0);

    std::unique_ptr<BYTE> nameBuf(new BYTE[needed]);
    DWORD cbConverted = CertGetNameStringA(
        cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, NULL, (LPSTR)nameBuf.get(), needed);
    uassert(50663,
            str::stream() << "CertGetNameString retrieval failed with: " << cbConverted,
            needed == cbConverted);

    return std::string(reinterpret_cast<char*>(nameBuf.get()));
}

}  // namespace

/**
 * Manage state for a SSL Connection. Used by the Socket class.
 */
class SSLConnectionWindows : public SSLConnectionInterface {
public:
    SCHANNEL_CRED* _cred;
    Socket* socket;
    asio::ssl::detail::engine _engine;

    std::vector<char> _tempBuffer;

    SSLConnectionWindows(SCHANNEL_CRED* cred, Socket* sock, const char* initialBytes, int len);

    ~SSLConnectionWindows();

    std::string getSNIServerName() const final {
        // TODO
        return "";
    };
};


class SSLManagerWindows : public SSLManagerInterface {
public:
    explicit SSLManagerWindows(const SSLParams& params, bool isServer);

    /**
     * Initializes an OpenSSL context according to the provided settings. Only settings which are
     * acceptable on non-blocking connections are set.
     */
    Status initSSLContext(SCHANNEL_CRED* cred,
                          const SSLParams& params,
                          ConnectionDirection direction) final;

    SSLConnectionInterface* connect(Socket* socket) final;

    SSLConnectionInterface* accept(Socket* socket, const char* initialBytes, int len) final;

    SSLPeerInfo parseAndValidatePeerCertificateDeprecated(const SSLConnectionInterface* conn,
                                                          const std::string& remoteHost) final;

    StatusWith<boost::optional<SSLPeerInfo>> parseAndValidatePeerCertificate(
        PCtxtHandle ssl, const std::string& remoteHost) final;


    const SSLConfiguration& getSSLConfiguration() const final {
        return _sslConfiguration;
    }

    int SSL_read(SSLConnectionInterface* conn, void* buf, int num) final;

    int SSL_write(SSLConnectionInterface* conn, const void* buf, int num) final;

    int SSL_shutdown(SSLConnectionInterface* conn) final;

private:
    Status _validateCertificate(PCCERT_CONTEXT cert,
                                std::string* subjectName,
                                Date_t* serverCertificateExpirationDate);

private:
    bool _weakValidation;
    bool _allowInvalidCertificates;
    bool _allowInvalidHostnames;
    SSLConfiguration _sslConfiguration;

    SCHANNEL_CRED _clientCred;
    SCHANNEL_CRED _serverCred;

    UniqueCertificate _pemCertificate;
    UniqueCertificate _clusterPEMCertificate;
    PCCERT_CONTEXT _clientCertificates[1] = {0};
    PCCERT_CONTEXT _serverCertificates[1] = {0};
    UniqueCertStore _certstore;

    Status loadCertificates(const SSLParams& params);

    void handshake(SSLConnectionWindows* conn, bool client);
};

// Global variable indicating if this is a server or a client instance
bool isSSLServer = false;

MONGO_INITIALIZER(SSLManager)(InitializerContext*) {
    stdx::lock_guard<SimpleMutex> lck(sslManagerMtx);
    if (!isSSLServer || (sslGlobalParams.sslMode.load() != SSLParams::SSLMode_disabled)) {
        theSSLManager = new SSLManagerWindows(sslGlobalParams, isSSLServer);
    }

    return Status::OK();
}

SSLConnectionWindows::SSLConnectionWindows(SCHANNEL_CRED* cred,
                                           Socket* sock,
                                           const char* initialBytes,
                                           int len)
    : _cred(cred), socket(sock), _engine(_cred) {

    _tempBuffer.resize(17 * 1024);

    if (len > 0) {
        _engine.put_input(asio::const_buffer(initialBytes, len));
    }
}

SSLConnectionWindows::~SSLConnectionWindows() {}

std::unique_ptr<SSLManagerInterface> SSLManagerInterface::create(const SSLParams& params,
                                                                 bool isServer) {
    return stdx::make_unique<SSLManagerWindows>(params, isServer);
}

SSLManagerInterface* getSSLManager() {
    stdx::lock_guard<SimpleMutex> lck(sslManagerMtx);
    if (theSSLManager)
        return theSSLManager;
    return NULL;
}

SSLManagerWindows::SSLManagerWindows(const SSLParams& params, bool isServer)
    : _weakValidation(params.sslWeakCertificateValidation),
      _allowInvalidCertificates(params.sslAllowInvalidCertificates),
      _allowInvalidHostnames(params.sslAllowInvalidHostnames) {

    // Certificates may not be loaded. This typically occurs in unit tests.
    uassertStatusOK(loadCertificates(params));

    uassertStatusOK(initSSLContext(&_clientCred, params, ConnectionDirection::kOutgoing));

    if (_clientCertificates[0] != nullptr) {
        uassertStatusOK(
            _validateCertificate(_clientCertificates[0], &_sslConfiguration.clientSubjectName, NULL));
    }

    // SSL server specific initialization
    if (isServer) {
        uassertStatusOK(initSSLContext(&_serverCred, params, ConnectionDirection::kIncoming));

        if (_serverCertificates[0] != nullptr) {
            uassertStatusOK(_validateCertificate(_serverCertificates[0],
                &_sslConfiguration.serverSubjectName,
                &_sslConfiguration.serverCertificateExpirationDate));
        }

        // Monitor the server certificate's expiration
        static CertificateExpirationMonitor task =
            CertificateExpirationMonitor(_sslConfiguration.serverCertificateExpirationDate);
    }
}

int SSLManagerWindows::SSL_read(SSLConnectionInterface* connInterface, void* buf, int num) {
    SSLConnectionWindows* conn = static_cast<SSLConnectionWindows*>(connInterface);

    while (true) {
        size_t bytes_transferred;
        asio::error_code ec;
        asio::ssl::detail::engine::want want =
            conn->_engine.read(asio::mutable_buffer(buf, num), ec, bytes_transferred);
        if (ec) {
            throwSocketError(SocketErrorKind::RECV_ERROR, ec.message());
        }

        switch (want) {
            case asio::ssl::detail::engine::want_input_and_retry: {
                // ASIO wants more data before it can continue:
                // 1. fetch some from the network
                // 2. give it to ASIO
                // 3. retry
                int ret =
                    recv(conn->socket->rawFD(), reinterpret_cast<char*>(buf), num, portRecvFlags);
                if (ret == SOCKET_ERROR) {
                    conn->socket->handleRecvError(ret, num);
                }

                conn->_engine.put_input(asio::const_buffer(buf, ret));

                continue;
            }
            case asio::ssl::detail::engine::want_nothing: {
                // ASIO wants nothing, return to caller with anything transfered.
                return bytes_transferred;
            }
            default:
                MONGO_UNREACHABLE;
        }
    }
}

int SSLManagerWindows::SSL_write(SSLConnectionInterface* connInterface, const void* buf, int num) {
    SSLConnectionWindows* conn = static_cast<SSLConnectionWindows*>(connInterface);

    while (true) {
        size_t bytes_transferred;
        asio::error_code ec;
        asio::ssl::detail::engine::want want =
            conn->_engine.write(asio::const_buffer(buf, num), ec, bytes_transferred);
        if (ec) {
            throwSocketError(SocketErrorKind::SEND_ERROR, ec.message());
        }

        switch (want) {
            case asio::ssl::detail::engine::want_output:
            case asio::ssl::detail::engine::want_output_and_retry: {
                // ASIO wants us to send data out:
                // 1. get data from ASIO
                // 2. give it to the network
                // 3. retry if needed

                asio::mutable_buffer outBuf = conn->_engine.get_output(
                    asio::mutable_buffer(conn->_tempBuffer.data(), conn->_tempBuffer.size()));

                int ret = send(conn->socket->rawFD(),
                               reinterpret_cast<const char*>(outBuf.data()),
                               outBuf.size(),
                               portSendFlags);
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

int SSLManagerWindows::SSL_shutdown(SSLConnectionInterface* conn) {
    invariant(false);
    return 0;
}

StatusWith<std::string> readFile(StringData fileName) {
    std::ifstream pemFile(fileName.toString(), std::ios::binary);
    if (!pemFile.is_open()) {
        return Status(ErrorCodes::InvalidSSLConfiguration,
                      str::stream() << "Failed to open PEM file: " << fileName);
    }

    std::string buf((std::istreambuf_iterator<char>(pemFile)), std::istreambuf_iterator<char>());

    pemFile.close();

    return buf;
}

// Find a specific kind of PEM blob marked by BEGIN and END in a string
StatusWith<StringData> findPEMBlob(StringData blob, StringData type, size_t position) {
    std::string header = str::stream() << "-----BEGIN " << type << "-----";
    std::string trailer = str::stream() << "-----END " << type << "-----";

    size_t headerPosition = blob.find(header, position);
    if (headerPosition == std::string::npos) {
        return Status(ErrorCodes::InvalidSSLConfiguration,
                      str::stream() << "Failed to find PEM blobl header: " << header);
    }

    size_t trailerPosition = blob.find(trailer, headerPosition);
    if (trailerPosition == std::string::npos) {
        return Status(ErrorCodes::InvalidSSLConfiguration,
                      str::stream() << "Failed to find PEM blob trailer: " << trailer);
    }

    trailerPosition += trailer.size();

    return StringData(blob.rawData() + headerPosition, trailerPosition - headerPosition);
}

// Decode a base-64 PEM blob with headers in a binary blob
StatusWith<std::vector<BYTE>> decodePEMBlob(StringData blob) {
    DWORD decodeLen{0};

    BOOL ret = CryptStringToBinaryA(
        blob.rawData(), blob.size(), CRYPT_STRING_BASE64HEADER, NULL, &decodeLen, NULL, NULL);
    if (!ret) {
        DWORD gle = GetLastError();
        if (gle != ERROR_MORE_DATA) {
            return Status(ErrorCodes::InvalidSSLConfiguration,
                          str::stream() << "CryptStringToBinary failed to get size of key: "
                                        << errnoWithDescription(gle));
        }
    }

    std::vector<BYTE> privateKeyBuf;
    privateKeyBuf.resize(decodeLen);

    ret = CryptStringToBinaryA(blob.rawData(),
                               blob.size(),
                               CRYPT_STRING_BASE64HEADER,
                               privateKeyBuf.data(),
                               &decodeLen,
                               NULL,
                               NULL);
    if (!ret) {
        DWORD gle = GetLastError();
        return Status(ErrorCodes::InvalidSSLConfiguration,
                      str::stream() << "CryptStringToBinary failed to read key: "
                                    << errnoWithDescription(gle));
    }
    return privateKeyBuf;
}

// Read a Certificate PEM file with a private key from disk
StatusWith<UniqueCertificate> readPEMFile(StringData fileName, StringData password) {
    auto swBuf = readFile(fileName);
    if (!swBuf.isOK()) {
        return swBuf.getStatus();
    }

    std::string buf = std::move(swBuf.getValue());

    size_t encryptedPrivateKey = buf.find("-----BEGIN ENCRYPTED PRIVATE KEY-----");
    if (encryptedPrivateKey != std::string::npos) {
        return Status(ErrorCodes::InvalidSSLConfiguration,
                      str::stream() << "Encrypted private keys are not supported, use the Windows "
                                       "certificate store instead: "
                                    << fileName);
    }

    // Search the buffer for the various strings that make up a PEM file
    auto swPublicKeyBlob = findPEMBlob(buf, "CERTIFICATE"_sd, 0);
    if (!swPublicKeyBlob.isOK()) {
        return swPublicKeyBlob.getStatus();
    }

    auto publicKeyBlob = swPublicKeyBlob.getValue();

    // Multiple certificates in a PEM file are not supported since these certs need to be in the ca
    // file.
    auto secondPublicKeyBlobPosition =
        buf.find("CERTIFICATE", (publicKeyBlob.rawData() + publicKeyBlob.size()) - buf.data());
    if (secondPublicKeyBlobPosition != std::string::npos) {
        return Status(ErrorCodes::InvalidSSLConfiguration,
                      str::stream() << "Certificate PEM files should only have one certificate, "
                                       "intermediate CA certificates belong in the CA file.");
    }

    // PEM files can have either private key format
    // Also the private key can either come before or after the certificate
    auto swPrivateKeyBlob = findPEMBlob(buf, "RSA PRIVATE KEY"_sd, 0);
    // We expect to find at least one certificate
    if (!swPublicKeyBlob.isOK()) {
        auto swPrivateKeyBlob = findPEMBlob(buf, "PRIVATE KEY"_sd, 0);
        if (!swPublicKeyBlob.isOK()) {
            return swPublicKeyBlob.getStatus();
        }
    }

    auto privateKeyBlob = swPrivateKeyBlob.getValue();

    auto swCert = decodePEMBlob(publicKeyBlob);
    if (!swCert.isOK()) {
        return swCert.getStatus();
    }

    auto certBuf = swCert.getValue();

    PCCERT_CONTEXT cert =
        CertCreateCertificateContext(X509_ASN_ENCODING, certBuf.data(), certBuf.size());

    if (cert == NULL) {
        DWORD gle = GetLastError();
        return Status(ErrorCodes::InvalidSSLConfiguration,
                      str::stream() << "CertCreateCertificateContext failed to decode cert: "
                                    << errnoWithDescription(gle));
    }

    UniqueCertificate certHolder(cert);

    auto swPrivateKeyBuf = decodePEMBlob(privateKeyBlob);
    if (!swPrivateKeyBuf.isOK()) {
        return swPrivateKeyBuf.getStatus();
    }

    auto privateKeyBuf = swPrivateKeyBuf.getValue();

    DWORD privateBlobLen{0};

    BOOL ret = CryptDecodeObjectEx(X509_ASN_ENCODING,
                                   PKCS_RSA_PRIVATE_KEY,
                                   privateKeyBuf.data(),
                                   privateKeyBuf.size(),
                                   0,
                                   NULL,
                                   NULL,
                                   &privateBlobLen);
    if (!ret) {
        DWORD gle = GetLastError();
        if (gle != ERROR_MORE_DATA) {
            return Status(ErrorCodes::InvalidSSLConfiguration,
                          str::stream() << "CryptDecodeObjectEx failed to get size of key: "
                                        << errnoWithDescription(gle));
        }
    }

    std::unique_ptr<BYTE> privateBlobBuf(new BYTE[privateBlobLen]);

    ret = CryptDecodeObjectEx(X509_ASN_ENCODING,
                              PKCS_RSA_PRIVATE_KEY,
                              privateKeyBuf.data(),
                              privateKeyBuf.size(),
                              0,
                              NULL,
                              privateBlobBuf.get(),
                              &privateBlobLen);
    if (!ret) {
        DWORD gle = GetLastError();
        return Status(ErrorCodes::InvalidSSLConfiguration,
                      str::stream() << "CryptDecodeObjectEx failed to read key: "
                                    << errnoWithDescription(gle));
    }

    HCRYPTPROV hProv;
    std::wstring wstr;

    // Create the right Crypto context depending on whether we running in a server or outside.
    // See https://msdn.microsoft.com/en-us/library/windows/desktop/aa375195(v=vs.85).aspx
    if (isSSLServer) {
        // Generate a unique name for our key container
        // Use the the log file if possible
        if (!serverGlobalParams.logpath.empty()) {
            wstr = toNativeString(serverGlobalParams.logpath.c_str());
        } else {
            auto us = UUID::gen().toString();
            wstr = toNativeString(us.c_str());
        }

        // Use a new key container for the key. We cannot use the default container since the
        // default
        // container is shared across processes owned by the same user.
        // Note: Server side Schannel requires CRYPT_VERIFYCONTEXT off
        ret = CryptAcquireContextW(
            &hProv, wstr.c_str(), MS_ENHANCED_PROV, PROV_RSA_FULL, CRYPT_NEWKEYSET | CRYPT_SILENT);
        if (!ret) {
            DWORD gle = GetLastError();

            if (gle == NTE_EXISTS) {

                ret = CryptAcquireContextW(
                    &hProv, wstr.c_str(), MS_ENHANCED_PROV, PROV_RSA_FULL, CRYPT_SILENT);
                if (!ret) {
                    DWORD gle = GetLastError();
                    return Status(ErrorCodes::InvalidSSLConfiguration,
                                  str::stream() << "CryptAcquireContextW failed "
                                                << errnoWithDescription(gle));
                }

            } else {
                return Status(ErrorCodes::InvalidSSLConfiguration,
                              str::stream() << "CryptAcquireContextW failed "
                                            << errnoWithDescription(gle));
            }
        }
    } else {
        // Use a transient key container for the key
        ret = CryptAcquireContextW(
            &hProv, NULL, MS_ENHANCED_PROV, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_SILENT);
        if (!ret) {
            DWORD gle = GetLastError();
            return Status(ErrorCodes::InvalidSSLConfiguration,
                          str::stream() << "CryptAcquireContextW failed  "
                                        << errnoWithDescription(gle));
        }
    }
    // UniqueCryptProvider cryptProvider(hProv);

    HCRYPTKEY hkey;
    ret = CryptImportKey(hProv, privateBlobBuf.get(), privateBlobLen, 0, 0, &hkey);
    if (!ret) {
        DWORD gle = GetLastError();
        return Status(ErrorCodes::InvalidSSLConfiguration,
                      str::stream() << "CryptImportKey failed  " << errnoWithDescription(gle));
    }
    UniqueCryptKey keyHolder(hkey);

    if (isSSLServer) {
        // Server-side SChannel requires a different way of attaching the private key to the
        // certificate
        CRYPT_KEY_PROV_INFO keyProvInfo;
        memset(&keyProvInfo, 0, sizeof(keyProvInfo));
        keyProvInfo.pwszContainerName = const_cast<wchar_t*>(wstr.c_str());
        keyProvInfo.pwszProvName = const_cast<wchar_t*>(MS_ENHANCED_PROV);
        keyProvInfo.dwFlags = CERT_SET_KEY_PROV_HANDLE_PROP_ID | CERT_SET_KEY_CONTEXT_PROP_ID;
        keyProvInfo.dwProvType = PROV_RSA_FULL;
        keyProvInfo.dwKeySpec = AT_KEYEXCHANGE;

        if (!CertSetCertificateContextProperty(
                certHolder.get(), CERT_KEY_PROV_INFO_PROP_ID, 0, &keyProvInfo)) {
            DWORD gle = GetLastError();
            return Status(ErrorCodes::InvalidSSLConfiguration,
                          str::stream() << "CertSetCertificateContextProperty Failed  "
                                        << errnoWithDescription(gle));
        }
    }

    // NOTE: This is used to set the certificate for client side SChannel
    ret = CertSetCertificateContextProperty(
        cert, CERT_KEY_PROV_HANDLE_PROP_ID, 0, (const void*)hProv);
    if (!ret) {
        DWORD gle = GetLastError();
        return Status(ErrorCodes::InvalidSSLConfiguration,
                      str::stream() << "CertSetCertificateContextProperty failed  "
                                    << errnoWithDescription(gle));
    }

    return std::move(certHolder);
}

Status readCAPEMFile(HCERTSTORE certStore, StringData fileName) {

    auto swBuf = readFile(fileName);
    if (!swBuf.isOK()) {
        return swBuf.getStatus();
    }

    std::string buf = std::move(swBuf.getValue());

    // Search the buffer for the various strings that make up a PEM file
    size_t pos = 0;

    while (pos < buf.size()) {
        auto swBlob = findPEMBlob(buf, "CERTIFICATE"_sd, pos);

        // We expect to find at least one certificate
        if (!swBlob.isOK()) {
            if (pos == 0) {
                return swBlob.getStatus();
            } else {
                return Status::OK();
            }
        }

        auto blobBuf = swBlob.getValue();

        pos = (blobBuf.rawData() + blobBuf.size()) - buf.data();

        auto swCert = decodePEMBlob(blobBuf);
        if (!swCert.isOK()) {
            return swCert.getStatus();
        }

        auto certBuf = swCert.getValue();

        PCCERT_CONTEXT cert =
            CertCreateCertificateContext(X509_ASN_ENCODING, certBuf.data(), certBuf.size()

                                             );
        if (cert == NULL) {
            DWORD gle = GetLastError();
            return Status(ErrorCodes::InvalidSSLConfiguration,
                          str::stream() << "CertCreateCertificateContext failed to decode cert: "
                                        << errnoWithDescription(gle));
        }
        UniqueCertificate certHolder(cert);

        BOOL ret = CertAddCertificateContextToStore(certStore, cert, CERT_STORE_ADD_NEW, NULL);

        if (!ret) {
            DWORD gle = GetLastError();
            return Status(ErrorCodes::InvalidSSLConfiguration,
                          str::stream() << "CertAddCertificateContextToStore Failed  "
                                        << errnoWithDescription(gle));
        }
    }

    return Status::OK();
}


Status readCRLPEMFile(HCERTSTORE certStore, StringData fileName) {

    auto swBuf = readFile(fileName);
    if (!swBuf.isOK()) {
        return swBuf.getStatus();
    }

    std::string buf = std::move(swBuf.getValue());

    auto swCert = decodePEMBlob(buf);
    if (!swCert.isOK()) {
        return swCert.getStatus();
    }

    auto certBuf = swCert.getValue();

    PCCRL_CONTEXT crl = CertCreateCRLContext(X509_ASN_ENCODING, certBuf.data(), certBuf.size()

                                                 );
    if (crl == NULL) {
        DWORD gle = GetLastError();
        return Status(ErrorCodes::InvalidSSLConfiguration,
                      str::stream() << "CertCreateCRLContext failed to decode crl: "
                                    << errnoWithDescription(gle));
    }

    UniqueCRL crlHolder(crl);

    BOOL ret = CertAddCRLContextToStore(certStore, crl, CERT_STORE_ADD_NEW, NULL);

    if (!ret) {
        DWORD gle = GetLastError();
        return Status(ErrorCodes::InvalidSSLConfiguration,
                      str::stream() << "CertAddCRLContextToStore Failed  "
                                    << errnoWithDescription(gle));
    }


    return Status::OK();
}


StatusWith<UniqueCertStore> readCertChains(StringData caFile, StringData crlFile) {
    UniqueCertStore certStore = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, NULL, 0, NULL);
    if (certStore == nullptr) {
        DWORD gle = GetLastError();
        return Status(ErrorCodes::InvalidSSLConfiguration,
                      str::stream() << "CertOpenStore Failed  " << errnoWithDescription(gle));
    }

    if (!caFile.empty()) {
        auto status = readCAPEMFile(certStore, caFile);
        if (!status.isOK()) {
            return status;
        }
    }

    if (!crlFile.empty()) {
        auto status = readCRLPEMFile(certStore, crlFile);
        if (!status.isOK()) {
            return status;
        }
    }


    return std::move(certStore);
}

Status SSLManagerWindows::loadCertificates(const SSLParams& params) {
    _clientCertificates[0] = nullptr;
    _serverCertificates[0] = nullptr;

    // Load the normal PEM file
    if (!params.sslPEMKeyFile.empty()) {
        auto swCertificate = readPEMFile(params.sslPEMKeyFile, params.sslPEMKeyPassword);
        if (!swCertificate.isOK()) {
            return swCertificate.getStatus();
        }

        _pemCertificate = std::move(swCertificate.getValue());
    }

    // Load the cluster PEM file, only applies to server side code
    if (!params.sslClusterFile.empty()) {
        auto swCertificate = readPEMFile(params.sslClusterFile, params.sslClusterPassword);
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

    auto swChain = readCertChains(params.sslCAFile, params.sslCRLFile);
    if (!swChain.isOK()) {
        return swChain.getStatus();
    }

    _certstore = std::move(swChain.getValue());

    return Status::OK();
}

Status SSLManagerWindows::initSSLContext(SCHANNEL_CRED* cred,
                                         const SSLParams& params,
                                         ConnectionDirection direction) {
    ZeroMemory(cred, sizeof(*cred));
    cred->dwVersion = SCHANNEL_CRED_VERSION;
    cred->dwFlags = SCH_USE_STRONG_CRYPTO;  // Use strong crypto;

    cred->hRootStore = _certstore;

    uint32_t supportedProtocols = 0;

    if (direction == ConnectionDirection::kIncoming) {
        supportedProtocols = SP_PROT_TLS1_SERVER | SP_PROT_TLS1_0_SERVER | SP_PROT_TLS1_1_SERVER |
            SP_PROT_TLS1_2_SERVER;

        cred->dwFlags = cred->dwFlags       // flags
            | SCH_CRED_SNI_CREDENTIAL       // Pass along SNI creds
            | SCH_CRED_SNI_ENABLE_OCSP      // Enable OCSP
            | SCH_CRED_NO_SYSTEM_MAPPER     // Do not map certificate to user account
            | SCH_CRED_DISABLE_RECONNECTS;  // Do not support reconnects
    } else {
        supportedProtocols = SP_PROT_TLS1_CLIENT | SP_PROT_TLS1_0_CLIENT | SP_PROT_TLS1_1_CLIENT |
            SP_PROT_TLS1_2_CLIENT;

        cred->dwFlags = cred->dwFlags           // flags
            | SCH_CRED_REVOCATION_CHECK_CHAIN   // Check certificate revocation
            | SCH_CRED_NO_SERVERNAME_CHECK      // Do not validate server name against cert
            | SCH_CRED_NO_DEFAULT_CREDS         // No Default Certificate
            | SCH_CRED_MANUAL_CRED_VALIDATION;  // Validate Certificate Manually
    }

    // Set the supported TLS protocols. Allow --sslDisabledProtocols to disable selected ciphers.
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
        warning()
            << "sslCipherConfig parameter is not supported with Windows SChannel and is ignored.";
    }

    if (direction == ConnectionDirection::kOutgoing) {
        if (_clientCertificates[0]) {
            cred->cCreds = 1;
            cred->paCred = _clientCertificates;
        }
    } else {
        cred->cCreds = 1;
        cred->paCred = _serverCertificates;
    }

    return Status::OK();
}

SSLConnectionInterface* SSLManagerWindows::connect(Socket* socket) {
    std::unique_ptr<SSLConnectionWindows> sslConn =
        stdx::make_unique<SSLConnectionWindows>(&_clientCred, socket, (const char*)NULL, 0);

    handshake(sslConn.get(), true);
    return sslConn.release();
}

SSLConnectionInterface* SSLManagerWindows::accept(Socket* socket,
                                                  const char* initialBytes,
                                                  int len) {
    std::unique_ptr<SSLConnectionWindows> sslConn =
        stdx::make_unique<SSLConnectionWindows>(&_serverCred, socket, initialBytes, len);

    handshake(sslConn.get(), false);

    return sslConn.release();
}

void SSLManagerWindows::handshake(SSLConnectionWindows* connInterface, bool client) {
    SSLConnectionWindows* conn = static_cast<SSLConnectionWindows*>(connInterface);

    initSSLContext(conn->_cred,
                   getSSLGlobalParams(),
                   client ? SSLManagerInterface::ConnectionDirection::kOutgoing
                          : SSLManagerInterface::ConnectionDirection::kIncoming);

    while (true) {
        asio::error_code ec;
        asio::ssl::detail::engine::want want =
            conn->_engine.handshake(client ? asio::ssl::stream_base::handshake_type::client
                                           : asio::ssl::stream_base::handshake_type::server,
                                    ec);
        if (ec) {
            throwSocketError(SocketErrorKind::RECV_ERROR, ec.message());
        }

        switch (want) {
            case asio::ssl::detail::engine::want_input_and_retry: {
                // ASIO wants more data before it can continue,
                // 1. fetch some from the network
                // 2. give it to ASIO
                // 3. retry
                int ret = recv(conn->socket->rawFD(),
                               conn->_tempBuffer.data(),
                               conn->_tempBuffer.size(),
                               portRecvFlags);
                if (ret == SOCKET_ERROR) {
                    conn->socket->handleRecvError(ret, conn->_tempBuffer.size());
                }

                conn->_engine.put_input(asio::const_buffer(conn->_tempBuffer.data(), ret));

                continue;
            }
            case asio::ssl::detail::engine::want_output:
            case asio::ssl::detail::engine::want_output_and_retry: {
                // ASIO wants us to send data out
                // 1. get data from ASIO
                // 2. give it to the network
                // 3. retry if needed
                asio::mutable_buffer outBuf = conn->_engine.get_output(
                    asio::mutable_buffer(conn->_tempBuffer.data(), conn->_tempBuffer.size()));

                int ret = send(conn->socket->rawFD(),
                               reinterpret_cast<const char*>(outBuf.data()),
                               outBuf.size(),
                               portSendFlags);
                if (ret == SOCKET_ERROR) {
                    conn->socket->handleSendError(ret, "");
                }

                if (want == asio::ssl::detail::engine::want_output_and_retry) {
                    continue;
                }

                // ASIO wants nothing, return to caller since we are done with handshake
                return;
            }
            case asio::ssl::detail::engine::want_nothing: {
                // ASIO wants nothing, return to caller since we are done with handshake
                return;
            }
            default:
                MONGO_UNREACHABLE;
        }
    }
}

unsigned long long FiletimeToULL(FILETIME ft) {
    return *reinterpret_cast<unsigned long long*>(&ft);
}

unsigned long long FiletimeToEpocMillis(FILETIME ft) {
    uint64_t ns100 = (((int64_t)ft.dwHighDateTime << 32) + ft.dwLowDateTime) - 116444736000000000LL;
    return ns100 / 1000;
}

Status SSLManagerWindows::_validateCertificate(PCCERT_CONTEXT cert,
                                               std::string* subjectName,
                                               Date_t* serverCertificateExpirationDate) {

    *subjectName = getCertificateSubjectName(cert);

    if (serverCertificateExpirationDate != nullptr) {
        FILETIME currentTime;
        GetSystemTimeAsFileTime(&currentTime);
        unsigned long long currentTimeLong = FiletimeToULL(currentTime);

        if ((FiletimeToULL(cert->pCertInfo->NotBefore) > currentTimeLong) ||
            (currentTimeLong > FiletimeToULL(cert->pCertInfo->NotAfter))) {
            severe() << "The provided SSL certificate is expired or not yet valid.";
            fassertFailedNoTrace(50666);
        }

        *serverCertificateExpirationDate =
            Date_t::fromMillisSinceEpoch(FiletimeToEpocMillis(cert->pCertInfo->NotAfter));
    }

    return Status::OK();
}

SSLPeerInfo SSLManagerWindows::parseAndValidatePeerCertificateDeprecated(
    const SSLConnectionInterface* conn, const std::string& remoteHost) {
    auto swPeerSubjectName = parseAndValidatePeerCertificate(
        const_cast<SSLConnectionWindows*>(static_cast<const SSLConnectionWindows*>(conn))
            ->_engine.native_handle(),
        remoteHost);
    // We can't use uassertStatusOK here because we need to throw a SocketException.
    if (!swPeerSubjectName.isOK()) {
        throwSocketError(SocketErrorKind::CONNECT_ERROR, swPeerSubjectName.getStatus().reason());
    }

    return swPeerSubjectName.getValue().get_value_or(SSLPeerInfo());
}

StatusWith<boost::optional<SSLPeerInfo>> SSLManagerWindows::parseAndValidatePeerCertificate(
    PCtxtHandle ssl, const std::string& remoteHost) {

    if (!isSSLServer)
        return {boost::none};

    PCCERT_CONTEXT cert;

    // returns SEC_E_NO_CREDENTIALS if no peer certificate
    SECURITY_STATUS ss = QueryContextAttributes(ssl, SECPKG_ATTR_REMOTE_CERT_CONTEXT, &cert);

    if (ss == SEC_E_NO_CREDENTIALS) {  // no certificate presented by peer
        if (_weakValidation) {
            warning() << "no SSL certificate provided by peer";
        } else {
            auto msg = "no SSL certificate provided by peer; connection rejected";
            error() << msg;
            return Status(ErrorCodes::SSLHandshakeFailed, msg);
        }

        return {boost::none};
    }

    // Check for unexpected errors
    if (ss != SEC_E_OK) {
        return Status(ErrorCodes::SSLHandshakeFailed,
                      str::stream() << "QueryContextAttributes failed with" << ss);
    }

    // UniqueCertificate certHolder(cert);
    //
    // CERT_CHAIN_PARA chain_para = {0};

    // chain_para.cbSize = sizeof(CERT_CHAIN_PARA);

    ////chain_para.RequestedUsage.Usage.

    // PCCERT_CHAIN_CONTEXT chainContext;
    // BOOL ret = CertGetCertificateChain(
    //    NULL,
    //    certHolder.get(),
    //    NULL,
    //    _certstore,
    //    &chain_para,
    //    CERT_CHAIN_REVOCATION_CHECK_CHAIN,
    //    NULL,
    //    &chainContext
    //);
    // if (!ret) {
    //    DWORD gle = GetLastError();
    //    return Status(ErrorCodes::InvalidSSLConfiguration,
    //        str::stream() << "CertGetCertificateChain failed: "
    //        << errnoWithDescription(gle));
    //}


    //    UniqueCertChain certChainHolder(chainContext);
    //// Missing Cert???

    // CERT_CHAIN_POLICY_PARA chain_policy_para;
    // chain_policy_para.cbSize = sizeof(chain_policy_para);

    // CERT_CHAIN_POLICY_STATUS chain_policy_status;
    // chain_policy_status.cbSize = sizeof(chain_policy_status);

    // ret = CertVerifyCertificateChainPolicy(

    //    CERT_CHAIN_POLICY_SSL,
    //    certChainHolder.get(),
    //    &chain_policy_para,
    //    &chain_policy_status
    //);

    //// This means some really went wrong.
    // if (!ret) {
    //    DWORD gle = GetLastError();
    //    return Status(ErrorCodes::InvalidSSLConfiguration,
    //        str::stream() << "CertVerifyCertificateChainPolicy failed: "
    //        << errnoWithDescription(gle));
    //}

    //// This means the chain is wrong.
    // if(chain_policy_status.dwError != S_OK) {
    //    return Status(ErrorCodes::InvalidSSLConfiguration,
    //        str::stream() << "Certificate chain validation failed: "
    //        << errnoWithDescription(chain_policy_status.dwError));
    //}
    //

    return {boost::none};
}

}  // namespace mongo
