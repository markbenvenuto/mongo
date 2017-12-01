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

#ifdef MONGO_CONFIG_SSL
#include <wincrypt.h>
#endif

#include <stdio.h>
#include "mongo/util/uuid.h"


typedef struct _CLIENT_ID {
    HANDLE UniqueProcess;
    HANDLE UniqueThread;
} CLIENT_ID;


typedef struct _MONGO_TEB {



    NT_TIB                  Tib;
    PVOID                   EnvironmentPointer;
    CLIENT_ID               Cid;
    PVOID                   ActiveRpcInfo;
    PVOID                   ThreadLocalStoragePointer;
    PPEB                    Peb;
    ULONG                   LastErrorValue;
    ULONG                   CountOfOwnedCriticalSections;
    PVOID                   CsrClientThread;
    PVOID                   Win32ThreadInfo;
    ULONG                   Win32ClientInfo[0x1F];
    PVOID                   WOW32Reserved;
    ULONG                   CurrentLocale;
    ULONG                   FpSoftwareStatusRegister;
    PVOID                   SystemReserved1[0x36];
    PVOID                   Spare1;
    ULONG                   ExceptionCode;
    ULONG                   SpareBytes1[0x28];
    PVOID                   SystemReserved2[0xA];
    ULONG                   GdiRgn;
    ULONG                   GdiPen;
    ULONG                   GdiBrush;
    CLIENT_ID               RealClientId;
    PVOID                   GdiCachedProcessHandle;
    ULONG                   GdiClientPID;
    ULONG                   GdiClientTID;
    PVOID                   GdiThreadLocaleInfo;
    PVOID                   UserReserved[5];
    PVOID                   GlDispatchTable[0x118];
    ULONG                   GlReserved1[0x1A];
    PVOID                   GlReserved2;
    PVOID                   GlSectionInfo;
    PVOID                   GlSection;
    PVOID                   GlTable;
    PVOID                   GlCurrentRC;
    PVOID                   GlContext;
    NTSTATUS                LastStatusValue;
    UNICODE_STRING          StaticUnicodeString;
    WCHAR                   StaticUnicodeBuffer[0x105];
    PVOID                   DeallocationStack;
    PVOID                   TlsSlots[0x40];
    LIST_ENTRY              TlsLinks;
    PVOID                   Vdm;
    PVOID                   ReservedForNtRpc;
    PVOID                   DbgSsReserved[0x2];
    ULONG                   HardErrorDisabled;
    PVOID                   Instrumentation[0x10];
    PVOID                   WinSockData;
    ULONG                   GdiBatchCount;
    ULONG                   Spare2;
    ULONG                   Spare3;
    ULONG                   Spare4;
    PVOID                   ReservedForOle;
    ULONG                   WaitingOnLoaderLock;
    PVOID                   StackCommit;
    PVOID                   StackCommitMax;
    PVOID                   StackReserved;

} MONGO_TEB;


static MONGO_TEB foobar123;


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
}  // namespace

SSLPeerInfo& SSLPeerInfo::forSession(const transport::SessionHandle& session) {
    return peerInfoForSession(session.get());
}

SSLParams sslGlobalParams;

const SSLParams& getSSLGlobalParams() {
    return sslGlobalParams;
}


SimpleMutex sslManagerMtx;
SSLManagerInterface* theSSLManager = NULL;

static const int BUFFER_SIZE = 8 * 1024;
static const int DATE_LEN = 128;

struct CERTFree {
    void operator()(const CERT_CONTEXT * p) noexcept {
        if (p) {
            //invariant(false);
            ::CertFreeCertificateContext(p);
        }
    }
};

typedef std::unique_ptr<const CERT_CONTEXT, CERTFree> UniqueCertificate;

template < typename HandleT, class Deleter>
class AutoHandle {
public:
    AutoHandle() : _handle(nullptr) {}
    AutoHandle(HandleT handle) : _handle(handle) {}
    AutoHandle(AutoHandle<HandleT, Deleter>&& handle) : _handle(handle._handle) { handle._handle = nullptr;  }
    ~AutoHandle() {
        if (_handle != nullptr) {
            Deleter()(_handle);
        }
    }
    AutoHandle(const AutoHandle&) = delete;

    AutoHandle& operator = (const HandleT other) {
        _handle = other;
        return *this;
    }
    
    AutoHandle& operator= (const AutoHandle<HandleT, Deleter>& other) = delete;

    AutoHandle& operator= (AutoHandle<HandleT, Deleter>&& other) {
        _handle = other._handle;
        other._handle = nullptr;
        return *this;
    }

    operator HandleT() { return _handle; }

private:
    HandleT _handle;
};

struct CertStoreFree {
    void operator()(HCERTSTORE const p) noexcept {
        if (p) {
            // For leak detection, add CERT_CLOSE_STORE_CHECK_FLAG
            ::CertCloseStore(p, 0);
        }
    }
};

typedef AutoHandle<HCERTSTORE, CertStoreFree> UniqueCertStore;

class SSLManager : public SSLManagerInterface {
public:
    explicit SSLManager(const SSLParams& params, bool isServer);

    /**
     * Initializes an OpenSSL context according to the provided settings. Only settings which are
     * acceptable on non-blocking connections are set.
     */
    Status initSSLContext(SCHANNEL_CRED* cred,
        const SSLParams& params,
        ConnectionDirection direction) final;

    virtual SSLConnection* connect(Socket* socket);

    virtual SSLConnection* accept(Socket* socket, const char* initialBytes, int len);

    virtual SSLPeerInfo parseAndValidatePeerCertificateDeprecated(const SSLConnection* conn,
                                                                  const std::string& remoteHost);

    StatusWith<boost::optional<SSLPeerInfo>> parseAndValidatePeerCertificate(
        PCtxtHandle ssl, const std::string& remoteHost) final;


    virtual const SSLConfiguration& getSSLConfiguration() const {
        return _sslConfiguration;
    }

    virtual int SSL_read(SSLConnection* conn, void* buf, int num);

    virtual int SSL_write(SSLConnection* conn, const void* buf, int num);

    virtual unsigned long ERR_get_error();

    virtual char* ERR_error_string(unsigned long e, char* buf);

    virtual int SSL_get_error(const SSLConnection* conn, int ret);

    virtual int SSL_shutdown(SSLConnection* conn);

    virtual void SSL_free(SSLConnection* conn);

private:
    bool _weakValidation;
    bool _allowInvalidCertificates;
    bool _allowInvalidHostnames;
    SSLConfiguration _sslConfiguration;

    SCHANNEL_CRED _clientCred;
    SCHANNEL_CRED _serverCred;

    // Windows
    UniqueCertificate _certificate;
    PCCERT_CONTEXT _certificates[1];
    UniqueCertStore _certstore;

    /*
     * Parse and store x509 subject name from the PEM keyfile.
     * For server instances check that PEM certificate is not expired
     * and extract server certificate notAfter date.
     * @param keyFile referencing the PEM file to be read.
     * @param subjectName as a pointer to the subject name variable being set.
     * @param serverNotAfter a Date_t object pointer that is valued if the
     * date is to be checked (as for a server certificate) and null otherwise.
     * @return bool showing if the function was successful.
     */
    bool _parseAndValidateCertificate(const std::string& keyFile,
                                      const std::string& keyPassword,
                                      std::string* subjectName,
                                      Date_t* serverNotAfter);


    StatusWith<stdx::unordered_set<RoleName>> _parsePeerRoles(PCCERT_CONTEXT peerCert) const;

    /*
     * sub function for checking the result of an SSL operation
     */
    bool _doneWithSSLOp(SSLConnection* conn, int status);

    /*
     * Send and receive network data
     */
    void _flushNetworkBIO(SSLConnection* conn);

    /*
     * match a remote host name to an x.509 host name
     */
    bool _hostNameMatch(const char* nameToMatch, const char* certHostName);
};

MONGO_INITIALIZER(SSLManager)(InitializerContext*) {
    stdx::lock_guard<SimpleMutex> lck(sslManagerMtx);
    if (!isSSLServer || (sslGlobalParams.sslMode.load() != SSLParams::SSLMode_disabled)) {
        theSSLManager = new SSLManager(sslGlobalParams, isSSLServer);
    }

    return Status::OK();
}

std::unique_ptr<SSLManagerInterface> SSLManagerInterface::create(const SSLParams& params,
                                                                 bool isServer) {
    return stdx::make_unique<SSLManager>(params, isServer);
}

SSLManagerInterface* getSSLManager() {
    stdx::lock_guard<SimpleMutex> lck(sslManagerMtx);
    if (theSSLManager)
        return theSSLManager;
    return NULL;
}

std::string getCertificateSubjectName(PCCERT_CONTEXT cert) {
    DWORD cbNeeded = CertGetNameStringA(cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, NULL, NULL, 0);
    if (cbNeeded == 0) {
        invariant(false);
    }

    std::unique_ptr<BYTE> nameBuf(new BYTE[privateKeyLen]);
    DWORD cbConverted = CertGetNameStringA(cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, NULL, nameBuf.get(), cbNeeded);
    if (cbConverted != cbNeeded) {
        invariant(false);
    }

    return std::string(nameBuf);
}

SSLConnection::SSLConnection(SCHANNEL_CRED* cred, Socket* sock, const char* initialBytes, int len)
    : _cred(cred), socket(sock), _engine(_cred) {

    if (len > 0) {
        _engine.put_input();
        //int toBIO = BIO_write(networkBIO, initialBytes, len);
        //if (toBIO != len) {
        //    LOG(3) << "Failed to write initial network data to the SSL BIO layer";
        //    throw SocketException(SocketException::RECV_ERROR, socket->remoteString());
        //}
    }
}

SSLConnection::~SSLConnection() {

}

SSLManagerInterface::~SSLManagerInterface() {}

SSLManager::SSLManager(const SSLParams& params, bool isServer)
    : _weakValidation(params.sslWeakCertificateValidation),
      _allowInvalidCertificates(params.sslAllowInvalidCertificates),
      _allowInvalidHostnames(params.sslAllowInvalidHostnames) {

    uassertStatusOk(initSSLContext(&_clientContext, params, ConnectionDirection::kOutgoing));

    // pick the certificate for use in outgoing connections,
    std::string clientPEM, clientPassword;
    if (!isServer || params.sslClusterFile.empty()) {
        // We are either a client, or a server without a cluster key,
        // so use the PEM key file, if specified
        clientPEM = params.sslPEMKeyFile;
        clientPassword = params.sslPEMKeyPassword;
    } else {
        // We are a server with a cluster key, so use the cluster key file
        clientPEM = params.sslClusterFile;
        clientPassword = params.sslClusterPassword;
    }

    if (!clientPEM.empty()) {
        if (!_parseAndValidateCertificate(
                clientPEM, clientPassword, &_sslConfiguration.clientSubjectName, NULL)) {
            uasserted(16941, "ssl initialization problem");
        }
    }
    
    // SSL server specific initialization
    if (isServer) {
        uassertStatusOk(initSSLContext(&_clientContext, params, ConnectionDirection::kIngoing));

        if (!_parseAndValidateCertificate(params.sslPEMKeyFile,
                                          params.sslPEMKeyPassword,
                                          &_sslConfiguration.serverSubjectName,
                                          &_sslConfiguration.serverCertificateExpirationDate)) {
            uasserted(16942, "ssl initialization problem");
        }

        static CertificateExpirationMonitor task =
            CertificateExpirationMonitor(_sslConfiguration.serverCertificateExpirationDate);
    }
}

int SSLManager::SSL_read(SSLConnection* conn, void* buf, int num) {
    int status;
    do {
        status = ::SSL_read(conn->ssl, buf, num);
    } while (!_doneWithSSLOp(conn, status));

    if (status <= 0)
        _handleSSLError(SSL_get_error(conn, status), status);
    return status;
}

int SSLManager::SSL_write(SSLConnection* conn, const void* buf, int num) {
    int status;
    do {
        status = ::SSL_write(conn->ssl, buf, num);
    } while (!_doneWithSSLOp(conn, status));

    if (status <= 0)
        _handleSSLError(SSL_get_error(conn, status), status);
    return status;
}

unsigned long SSLManager::ERR_get_error() {
    return ::ERR_get_error();
}

char* SSLManager::ERR_error_string(unsigned long e, char* buf) {
    return ::ERR_error_string(e, buf);
}

int SSLManager::SSL_get_error(const SSLConnection* conn, int ret) {
    return ::SSL_get_error(conn->ssl, ret);
}

int SSLManager::SSL_shutdown(SSLConnection* conn) {
    int status;
    do {
        status = ::SSL_shutdown(conn->ssl);
    } while (!_doneWithSSLOp(conn, status));

    if (status < 0)
        _handleSSLError(SSL_get_error(conn, status), status);
    return status;
}

void SSLManager::SSL_free(SSLConnection* conn) {
    return ::SSL_free(conn->ssl);
}

StatusWith<UniqueCertificate> readPEMFile(StringData fileName, StringData password) {

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

    //StringData privateKey = buf.find("-----BEGIN ENCRYPTED PRIVATE KEY-----");

    //if (pem_private) {
    //    MONGOC_ERROR("Detected unsupported encrypted private key");
    //    goto fail;
    //}

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

    BOOL ret;

     PCCERT_CONTEXT cert;
    ret = CryptQueryObject(
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
        reinterpret_cast<const void       **>(&cert)
    );

    UniqueCertificate certHolder(cert);
/*
    PCCERT_CONTEXT  cert = CertCreateCertificateContext(X509_ASN_ENCODING, certBlob.pbData, certBlob.cbData);
    if (cert) {
        DWORD gle = GetLastError();
        return Status(ErrorCodes::InvalidSSLConfiguration, str::stream() << "CryptQueryObject Failed " << errnoWithDescription(gle));
    }
*/
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
            return Status(ErrorCodes::InvalidSSLConfiguration, str::stream() << "CryptStringToBinaryA Failed to get size of key " << errnoWithDescription(gle));
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
        return Status(ErrorCodes::InvalidSSLConfiguration, str::stream() << "CryptStringToBinaryA Failed to read key " << errnoWithDescription(gle));
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
            return Status(ErrorCodes::InvalidSSLConfiguration, str::stream() << "CryptDecodeObjectEx Failed to get size of key " << errnoWithDescription(gle));
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
        return Status(ErrorCodes::InvalidSSLConfiguration, str::stream() << "CryptDecodeObjectEx Failed to get size of key " << errnoWithDescription(gle));
    }

    //const wchar_t* keySetNameW = L"FooBar1234";
    //const char* keySetNameA = "FooBar1234";
    // TODO: leak or free? CryptReleaseContext
    // Note: must use PROV_RSA_SCHANNEL 
    HCRYPTPROV hProv;
    //UUID uuid = UUID::gen();
    //std::wstring wstr = toWideString(uuid.toString().c_str());
    ret = CryptAcquireContextW(&hProv,
        NULL,
        MS_DEF_RSA_SCHANNEL_PROV_W,
        PROV_RSA_SCHANNEL,
        0
    );
    if (!ret) {
        DWORD gle = GetLastError();
        return Status(ErrorCodes::InvalidSSLConfiguration, str::stream() << "CryptAcquireContextA Failed  " << errnoWithDescription(gle));
    }

    // TODO: CryptDestroyKey
    HCRYPTKEY hkey;
    ret = CryptImportKey(
        hProv,
        privateBlobBuf.get(),
        privateBlobLen,
        0,
        CRYPT_EXPORTABLE,
        &hkey);
    if (!ret) {
        DWORD gle = GetLastError();
        return Status(ErrorCodes::InvalidSSLConfiguration, str::stream() << "CryptImportKey Failed  " << errnoWithDescription(gle));
    }

    ret = CertSetCertificateContextProperty(
        cert,
        CERT_KEY_PROV_HANDLE_PROP_ID,
        0,
        (const void *)hProv);
    if (!ret) {
        DWORD gle = GetLastError();
        return Status(ErrorCodes::InvalidSSLConfiguration, str::stream() << "CertSetCertificateContextProperty Failed  " << errnoWithDescription(gle));
    }

    {
        DWORD keyBlobLen;

        ret = CertGetCertificateContextProperty(cert,
            CERT_KEY_PROV_HANDLE_PROP_ID,
            NULL,
            &keyBlobLen);

        if (!ret) {
            DWORD gle = GetLastError();
            if (gle != ERROR_MORE_DATA) {
                return Status(ErrorCodes::InvalidSSLConfiguration, str::stream() << "CertGetCertificateContextProperty Failed to get size of key " << errnoWithDescription(gle));
            }
        }

        std::unique_ptr<BYTE> keyBlob(new BYTE[keyBlobLen]);
        ret = CertGetCertificateContextProperty(cert,
            CERT_KEY_PROV_HANDLE_PROP_ID,
            keyBlob.get(),
            &keyBlobLen);

        if (!ret) {
            DWORD gle = GetLastError();
            return Status(ErrorCodes::InvalidSSLConfiguration, str::stream() << "CertGetCertificateContextProperty Failed to get size of key " << errnoWithDescription(gle));
        }
    }


    //DWORD keyBlobLen;

    //ret = CertGetCertificateContextProperty(cert,
    //    CERT_KEY_PROV_INFO_PROP_ID,
    //    NULL,
    //    &keyBlobLen);
   
    //if (!ret) {
    //    DWORD gle = GetLastError();
    //    if (gle != ERROR_MORE_DATA) {
    //        return Status(ErrorCodes::InvalidSSLConfiguration, str::stream() << "CertGetCertificateContextProperty Failed to get size of key " << errnoWithDescription(gle));
    //    }
    //}

    //std::unique_ptr<BYTE> keyBlob(new BYTE[keyBlobLen]);
    //ret = CertGetCertificateContextProperty(cert,
    //    CERT_KEY_PROV_INFO_PROP_ID,
    //    keyBlob.get(),
    //    &keyBlobLen);

    //if (!ret) {
    //    DWORD gle = GetLastError();
    //        return Status(ErrorCodes::InvalidSSLConfiguration, str::stream() << "CertGetCertificateContextProperty Failed to get size of key " << errnoWithDescription(gle));
    //}

    //CRYPT_KEY_PROV_INFO* foo = (CRYPT_KEY_PROV_INFO*)(keyBlob.get());

    //CRYPT_KEY_PROV_INFO keyProvInfo;
    //memset(&keyProvInfo, 0, sizeof(keyProvInfo));
    //keyProvInfo.pwszContainerName = NULL;
    //keyProvInfo.pwszProvName = const_cast<wchar_t*>(MS_ENHANCED_PROV);
    //keyProvInfo.dwProvType = PROV_RSA_FULL;
    //keyProvInfo.dwKeySpec = AT_KEYEXCHANGE;
    //if (!CertSetCertificateContextProperty(cert, CERT_KEY_PROV_INFO_PROP_ID, 0, &keyProvInfo)) {
    //    DWORD gle = GetLastError();
    //    return Status(ErrorCodes::InvalidSSLConfiguration, str::stream() << "CertSetCertificateContextProperty Failed  " << errnoWithDescription(gle));
    //}

    //CERT_KEY_CONTEXT keyContext;
    //memset(&keyContext, 0, sizeof(keyContext));
    //keyContext.cbSize = sizeof(keyContext);
    //keyContext.hCryptProv = hProv;
    //keyContext.dwKeySpec = AT_KEYEXCHANGE;
    //if (!CertSetCertificateContextProperty(cert, CERT_KEY_CONTEXT_PROP_ID, 0, &keyContext)) {
    //    DWORD gle = GetLastError();
    //    return Status(ErrorCodes::InvalidSSLConfiguration, str::stream() << "CertSetCertificateContextProperty Failed  " << errnoWithDescription(gle));
    //}

    //PCCERT_CONTEXT certOut;
    //UniqueCertStore certStore = CertOpenStore(
    //    CERT_STORE_PROV_MEMORY,
    //    0, // Note needed
    //    NULL,
    //    0,
    //    NULL);
    //if (certStore == nullptr) {
    //    DWORD gle = GetLastError();
    //    return Status(ErrorCodes::InvalidSSLConfiguration, str::stream() << "CertOpenStore Failed  " << errnoWithDescription(gle));
    //}


    //ret = CertAddCertificateContextToStore(certStore, certHolder.get(),
    //    CERT_STORE_ADD_NEW, &certOut);

    //if (!ret) {
    //    DWORD gle = GetLastError();
    //    return Status(ErrorCodes::InvalidSSLConfiguration, str::stream() << "CertAddCertificateContextToStore Failed  " << errnoWithDescription(gle));
    //}

    //CRYPT_KEY_PROV_INFO keyProvInfo;
    //memset(&keyProvInfo, 0, sizeof(keyProvInfo));
    //keyProvInfo.pwszContainerName = (LPWSTR)wstr.c_str();
    //keyProvInfo.pwszProvName = const_cast<wchar_t*>(MS_ENHANCED_PROV);
    //keyProvInfo.dwProvType = PROV_RSA_FULL;
    //keyProvInfo.dwKeySpec = AT_KEYEXCHANGE;
    //if (!CertSetCertificateContextProperty(certOut, CERT_KEY_PROV_INFO_PROP_ID, 0, &keyProvInfo)) {
    //    DWORD gle = GetLastError();
    //    return Status(ErrorCodes::InvalidSSLConfiguration, str::stream() << "CertSetCertificateContextProperty Failed  " << errnoWithDescription(gle));
    //}


    ////return std::move(certHolder);

    //return UniqueCertificate(certOut);
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


    CRYPT_KEY_PROV_INFO keyProvInfo;
    memset(&keyProvInfo, 0, sizeof(keyProvInfo));
    keyProvInfo.pwszContainerName = (LPWSTR)wKeyName.c_str();
    keyProvInfo.pwszProvName = const_cast<wchar_t*>(MS_ENHANCED_PROV);
    keyProvInfo.dwProvType = PROV_RSA_FULL;
    keyProvInfo.dwKeySpec = AT_KEYEXCHANGE;
    if (!CertSetCertificateContextProperty(cert, CERT_KEY_PROV_INFO_PROP_ID, 0, &keyProvInfo)) {
        DWORD gle = GetLastError();
        return Status(ErrorCodes::InvalidSSLConfiguration, str::stream() << "CertSetCertificateContextProperty Failed  " << errnoWithDescription(gle));
    }


    return std::move(certHolder);

}

StatusWith<UniqueCertificate> readCAPEMFile(StringData fileName) {

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

    CERT_BLOB certBlob;
    certBlob.cbData = buf.size() - publicKey;
    certBlob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(buf.data() + publicKey));

    BOOL ret;

    PCCERT_CONTEXT cert;
    ret = CryptQueryObject(
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
        reinterpret_cast<const void       **>(&cert)
    );

    return UniqueCertificate(cert);
}


StatusWith<UniqueCertStore> readCertChains(StringData caFile, StringData crlFile) {
    UniqueCertStore certStore = CertOpenStore(
        CERT_STORE_PROV_MEMORY,
        0, // Note needed
        NULL,
        0,
        NULL);
    if (certStore == nullptr) {
        DWORD gle = GetLastError();
        return Status(ErrorCodes::InvalidSSLConfiguration, str::stream() << "CertOpenStore Failed  " << errnoWithDescription(gle));
    }

    if (!caFile.empty()) {
        auto swCertificate = readCAPEMFile(caFile);
        if (!swCertificate.isOK()) {
            return swCertificate.getStatus();
        }

        BOOL ret = CertAddCertificateContextToStore(certStore, swCertificate.getValue().get(),
            CERT_STORE_ADD_NEW, NULL);

            if (!ret) {
                DWORD gle = GetLastError();
                return Status(ErrorCodes::InvalidSSLConfiguration, str::stream() << "CertAddCertificateContextToStore Failed  " << errnoWithDescription(gle));
            }
    }

    if (!crlFile.empty()) {
        // TODO
    }


    return std::move(certStore);
    //return{std::move(certStore)};
}


Status SSLManager::initSSLContext(SCHANNEL_CRED* cred,
    const SSLParams& params,
    ConnectionDirection direction) {

    ZeroMemory(cred, sizeof(&cred));
    cred->dwVersion = SCHANNEL_CRED_VERSION;
    cred->dwFlags = SCH_CRED_NO_SYSTEM_MAPPER;
    // TODO: SNI supprt - SCH_CRED_SNI_CREDENTIAL

    uint32_t supportedProtocols = 0;
    
    if (direction == ConnectionDirection::kIncoming) {
        supportedProtocols = SP_PROT_TLS1_CLIENT | SP_PROT_TLS1_0_CLIENT | SP_PROT_TLS1_1_CLIENT | SP_PROT_TLS1_2_CLIENT;
    } else {
        supportedProtocols = SP_PROT_TLS1_SERVER | SP_PROT_TLS1_0_SERVER | SP_PROT_TLS1_1_SERVER | SP_PROT_TLS1_2_SERVER;
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

    // TODO - support somehow
    // HIGH - Enable strong ciphers
    // !EXPORT - Disable export ciphers (40/56 bit)
    // !aNULL - Disable anonymous auth ciphers
    // @STRENGTH - Sort ciphers based on strength
    std::string cipherConfig = "HIGH:!EXPORT:!aNULL@STRENGTH";

    // Allow the cipher configuration string to be overriden by --sslCipherConfig
    if (!params.sslCipherConfig.empty()) {
        // TODO: error - we do not have the same syntax
        cipherConfig = params.sslCipherConfig;
    }

    if (direction == ConnectionDirection::kOutgoing && !params.sslClusterFile.empty()) {
        //::EVP_set_pw_prompt("Enter cluster certificate passphrase");
        auto swCertificate = readPEMFile(params.sslClusterFile, params.sslClusterPassword);
        if (!swCertificate.isOK()) {
            return swCertificate.getStatus();
        }
        
        _certificate = std::move(swCertificate.getValue());
        cred->cCreds = 1;
        _certificates[0] = _certificate.get();
        cred->paCred = _certificates;
    } else if (!params.sslPEMKeyFile.empty()) {
        auto swCertificate = readPEMFile(params.sslPEMKeyFile, params.sslPEMKeyPassword);
        if (!swCertificate.isOK()) {
            return swCertificate.getStatus();
        }

        _certificate = std::move(swCertificate.getValue());
        cred->cCreds = 1;
        _certificates[0] = _certificate.get();
        cred->paCred = _certificates;

        //PCCERT_CONTEXT certOut;
        //UniqueCertStore certStore = CertOpenStore(
        //    CERT_STORE_PROV_MEMORY,
        //    0, // Note needed
        //    NULL,
        //    0,
        //    NULL);
        //if (certStore == nullptr) {
        //    DWORD gle = GetLastError();
        //    return Status(ErrorCodes::InvalidSSLConfiguration, str::stream() << "CertOpenStore Failed  " << errnoWithDescription(gle));
        //}


        //BOOL ret = CertAddCertificateContextToStore(certStore, swCertificate.getValue().get(),
        //    CERT_STORE_ADD_NEW, &certOut);

        //if (!ret) {
        //    DWORD gle = GetLastError();
        //    return Status(ErrorCodes::InvalidSSLConfiguration, str::stream() << "CertAddCertificateContextToStore Failed  " << errnoWithDescription(gle));
        //}


        //_certificate = UniqueCertificate(certOut);
        //cred->cCreds = 1;
        //_certificates[0] = _certificate.get();
        //cred->paCred = _certificates;
    }

    if (!params.sslCAFile.empty() || !params.sslCAFile.empty()) {
        auto swCertStore = readCertChains(params.sslCAFile, params.sslCAFile);
        if (!swCertStore.isOK()) {
            return swCertStore.getStatus();
        }

        _certstore = std::move(swCertStore.getValue());

        cred->hRootStore = _certstore;
    }

    printf("%llux", (uint64_t)&foobar123);
    //const auto status =
    //    params.sslCAFile.empty() ? _setupSystemCA(context) : _setupCA(context, params.sslCAFile);
    //if (!status.isOK())
    //    return status;

    //if (!params.sslCRLFile.empty()) {
    //    if (!_setupCRL(context, params.sslCRLFile)) {
    //        return Status(ErrorCodes::InvalidSSLConfiguration, "Can not set up CRL file.");
    //    }
    //}


    return Status::OK();
}




bool SSLManager::_initSynchronousSSLContext(UniqueSSLContext* contextPtr,
                                            const SSLParams& params,
                                            ConnectionDirection direction) {
    *contextPtr = UniqueSSLContext(SSL_CTX_new(SSLv23_method()), free_ssl_context);

    uassertStatusOK(initSSLContext(contextPtr->get(), params, direction));

    // If renegotiation is needed, don't return from recv() or send() until it's successful.
    // Note: this is for blocking sockets only.
    SSL_CTX_set_mode(contextPtr->get(), SSL_MODE_AUTO_RETRY);

    return true;
}

Status SSLManager::_parseAndValidateCertificate(const std::string& keyFile,
                                              const std::string& keyPassword,
                                              std::string* subjectName,
                                              Date_t* serverCertificateExpirationDate) {
    auto swCertificate = readPEMFile(keyFile, keyPassword);
    if (!swCertificate.isOK()) {
        return swCertificate.getStatus();
    }

    *subjectName = getCertificateSubjectName(x509);
    if (serverCertificateExpirationDate != nullptr) {
        unsigned long long notBeforeMillis = _convertASN1ToMillis(X509_get_notBefore(x509));
        if (notBeforeMillis == 0) {
            error() << "date conversion failed";
            return false;
        }

        unsigned long long notAfterMillis = _convertASN1ToMillis(X509_get_notAfter(x509));
        if (notAfterMillis == 0) {
            error() << "date conversion failed";
            return false;
        }

        if ((notBeforeMillis > curTimeMillis64()) || (curTimeMillis64() > notAfterMillis)) {
            severe() << "The provided SSL certificate is expired or not yet valid.";
            fassertFailedNoTrace(28652);
        }

        *serverCertificateExpirationDate = Date_t::fromMillisSinceEpoch(notAfterMillis);
    }

    return true;
}

/*
* The interface layer between network and BIO-pair. The BIO-pair buffers
* the data to/from the TLS layer.
*/
void SSLManager::_flushNetworkBIO(SSLConnection* conn) {
    char buffer[BUFFER_SIZE];
    int wantWrite;

    /*
    * Write the complete contents of the buffer. Leaving the buffer
    * unflushed could cause a deadlock.
    */
    while ((wantWrite = BIO_ctrl_pending(conn->networkBIO)) > 0) {
        if (wantWrite > BUFFER_SIZE) {
            wantWrite = BUFFER_SIZE;
        }
        int fromBIO = BIO_read(conn->networkBIO, buffer, wantWrite);

        int writePos = 0;
        do {
            int numWrite = fromBIO - writePos;
            numWrite = send(conn->socket->rawFD(), buffer + writePos, numWrite, portSendFlags);
            if (numWrite < 0) {
                conn->socket->handleSendError(numWrite, "");
            }
            writePos += numWrite;
        } while (writePos < fromBIO);
    }

    int wantRead;
    while ((wantRead = BIO_ctrl_get_read_request(conn->networkBIO)) > 0) {
        if (wantRead > BUFFER_SIZE) {
            wantRead = BUFFER_SIZE;
        }

        int numRead = recv(conn->socket->rawFD(), buffer, wantRead, portRecvFlags);
        if (numRead <= 0) {
            conn->socket->handleRecvError(numRead, wantRead);
            continue;
        }

        int toBIO = BIO_write(conn->networkBIO, buffer, numRead);
        if (toBIO != numRead) {
            LOG(3) << "Failed to write network data to the SSL BIO layer";
            throw SocketException(SocketException::RECV_ERROR, conn->socket->remoteString());
        }
    }
}

bool SSLManager::_doneWithSSLOp(SSLConnection* conn, int status) {
    int sslErr = SSL_get_error(conn, status);
    switch (sslErr) {
        case SSL_ERROR_NONE:
            _flushNetworkBIO(conn);  // success, flush network BIO before leaving
            return true;
        case SSL_ERROR_WANT_WRITE:
        case SSL_ERROR_WANT_READ:
            _flushNetworkBIO(conn);  // not ready, flush network BIO and try again
            return false;
        default:
            return true;
    }
}

SSLConnection* SSLManager::connect(Socket* socket) {
    std::unique_ptr<SSLConnection> sslConn =
        stdx::make_unique<SSLConnection>(_clientContext.get(), socket, (const char*)NULL, 0);

    int ret = ::SSL_set_tlsext_host_name(sslConn->ssl, socket->remoteAddr().hostOrIp().c_str());
    if (ret != 1)
        _handleSSLError(SSL_get_error(sslConn.get(), ret), ret);

    do {
        ret = ::SSL_connect(sslConn->ssl);
    } while (!_doneWithSSLOp(sslConn.get(), ret));

    if (ret != 1)
        _handleSSLError(SSL_get_error(sslConn.get(), ret), ret);

    return sslConn.release();
}

SSLConnection* SSLManager::accept(Socket* socket, const char* initialBytes, int len) {
    std::unique_ptr<SSLConnection> sslConn =
        stdx::make_unique<SSLConnection>(_serverContext.get(), socket, initialBytes, len);

    int ret;
    do {
        ret = ::SSL_accept(sslConn->ssl);
    } while (!_doneWithSSLOp(sslConn.get(), ret));

    if (ret != 1)
        _handleSSLError(SSL_get_error(sslConn.get(), ret), ret);

    return sslConn.release();
}

// TODO SERVER-11601 Use NFC Unicode canonicalization
bool SSLManager::_hostNameMatch(const char* nameToMatch, const char* certHostName) {
    if (strlen(certHostName) < 2) {
        return false;
    }

    // match wildcard DNS names
    if (certHostName[0] == '*' && certHostName[1] == '.') {
        // allow name.example.com if the cert is *.example.com, '*' does not match '.'
        const char* subName = strchr(nameToMatch, '.');
        return subName && !strcasecmp(certHostName + 1, subName);
    } else {
        return !strcasecmp(nameToMatch, certHostName);
    }
}

StatusWith<boost::optional<SSLPeerInfo>> SSLManager::parseAndValidatePeerCertificate(
    SSL* conn, const std::string& remoteHost) {
    if (!_sslConfiguration.hasCA && isSSLServer)
        return {boost::none};

    X509* peerCert = SSL_get_peer_certificate(conn);

    if (NULL == peerCert) {  // no certificate presented by peer
        if (_weakValidation) {
            warning() << "no SSL certificate provided by peer";
        } else {
            auto msg = "no SSL certificate provided by peer; connection rejected";
            error() << msg;
            return Status(ErrorCodes::SSLHandshakeFailed, msg);
        }
        return {boost::none};
    }
    ON_BLOCK_EXIT(X509_free, peerCert);

    long result = SSL_get_verify_result(conn);

    if (result != X509_V_OK) {
        if (_allowInvalidCertificates) {
            warning() << "SSL peer certificate validation failed: "
                      << X509_verify_cert_error_string(result);
        } else {
            str::stream msg;
            msg << "SSL peer certificate validation failed: "
                << X509_verify_cert_error_string(result);
            error() << msg.ss.str();
            return Status(ErrorCodes::SSLHandshakeFailed, msg);
        }
    }

    // TODO: check optional cipher restriction, using cert.
    std::string peerSubjectName = getCertificateSubjectName(peerCert);
    LOG(2) << "Accepted TLS connection from peer: " << peerSubjectName;

    StatusWith<stdx::unordered_set<RoleName>> swPeerCertificateRoles = _parsePeerRoles(peerCert);
    if (!swPeerCertificateRoles.isOK()) {
        return swPeerCertificateRoles.getStatus();
    }

    // If this is an SSL client context (on a MongoDB server or client)
    // perform hostname validation of the remote server
    if (remoteHost.empty()) {
        return boost::make_optional(
            SSLPeerInfo(peerSubjectName, std::move(swPeerCertificateRoles.getValue())));
    }

    // Try to match using the Subject Alternate Name, if it exists.
    // RFC-2818 requires the Subject Alternate Name to be used if present.
    // Otherwise, the most specific Common Name field in the subject field
    // must be used.

    bool sanMatch = false;
    bool cnMatch = false;
    StringBuilder certificateNames;

    STACK_OF(GENERAL_NAME)* sanNames = static_cast<STACK_OF(GENERAL_NAME)*>(
        X509_get_ext_d2i(peerCert, NID_subject_alt_name, NULL, NULL));

    if (sanNames != NULL) {
        int sanNamesList = sk_GENERAL_NAME_num(sanNames);
        certificateNames << "SAN(s): ";
        for (int i = 0; i < sanNamesList; i++) {
            const GENERAL_NAME* currentName = sk_GENERAL_NAME_value(sanNames, i);
            if (currentName && currentName->type == GEN_DNS) {
                char* dnsName = reinterpret_cast<char*>(ASN1_STRING_data(currentName->d.dNSName));
                if (_hostNameMatch(remoteHost.c_str(), dnsName)) {
                    sanMatch = true;
                    break;
                }
                certificateNames << std::string(dnsName) << " ";
            }
        }
        sk_GENERAL_NAME_pop_free(sanNames, GENERAL_NAME_free);
    } else if (peerSubjectName.find("CN=") != std::string::npos) {
        // If Subject Alternate Name (SAN) doesn't exist and Common Name (CN) does,
        // check Common Name.
        int cnBegin = peerSubjectName.find("CN=") + 3;
        int cnEnd = peerSubjectName.find(",", cnBegin);
        std::string commonName = peerSubjectName.substr(cnBegin, cnEnd - cnBegin);

        if (_hostNameMatch(remoteHost.c_str(), commonName.c_str())) {
            cnMatch = true;
        }
        certificateNames << "CN: " << commonName;
    } else {
        certificateNames << "No Common Name (CN) or Subject Alternate Names (SAN) found";
    }

    if (!sanMatch && !cnMatch) {
        StringBuilder msgBuilder;
        msgBuilder << "The server certificate does not match the host name. Hostname: "
                   << remoteHost << " does not match " << certificateNames.str();
        std::string msg = msgBuilder.str();
        if (_allowInvalidCertificates || _allowInvalidHostnames || isUnixDomainSocket(remoteHost)) {
            warning() << msg;
        } else {
            error() << msg;
            return Status(ErrorCodes::SSLHandshakeFailed, msg);
        }
    }

    return boost::make_optional(SSLPeerInfo(peerSubjectName, stdx::unordered_set<RoleName>()));
}


StatusWith<boost::optional<SSLPeerInfo>> SSLManager::parseAndValidatePeerCertificate(
    PCtxtHandle conn, const std::string& remoteHost) {
    // TODO:
    return{boost::none};
}


SSLPeerInfo SSLManager::parseAndValidatePeerCertificateDeprecated(const SSLConnection* conn,
                                                                  const std::string& remoteHost) {
    auto swPeerSubjectName = parseAndValidatePeerCertificate(conn->ssl, remoteHost);
    // We can't use uassertStatusOK here because we need to throw a SocketException.
    if (!swPeerSubjectName.isOK()) {
        throw SocketException(SocketException::CONNECT_ERROR,
                              swPeerSubjectName.getStatus().reason());
    }
    return swPeerSubjectName.getValue().get_value_or(SSLPeerInfo());
}

StatusWith<stdx::unordered_set<RoleName>> SSLManager::_parsePeerRoles(X509* peerCert) const {
    // exts is owned by the peerCert
    const STACK_OF(X509_EXTENSION)* exts = X509_get0_extensions(peerCert);

    int extCount = 0;
    if (exts) {
        extCount = sk_X509_EXTENSION_num(exts);
    }

    ASN1_OBJECT* rolesObj = OBJ_nid2obj(_rolesNid);

    // Search all certificate extensions for our own
    stdx::unordered_set<RoleName> roles;
    for (int i = 0; i < extCount; i++) {
        X509_EXTENSION* ex = sk_X509_EXTENSION_value(exts, i);
        ASN1_OBJECT* obj = X509_EXTENSION_get_object(ex);

        if (!OBJ_cmp(obj, rolesObj)) {
            // We've found an extension which has our roles OID
            ASN1_OCTET_STRING* data = X509_EXTENSION_get_data(ex);

            /*
             * MongoDBAuthorizationGrant ::= CHOICE {
             *  MongoDBRole,
             *  ...!UTF8String:"Unrecognized entity in MongoDBAuthorizationGrant"
             * }
             * MongoDBAuthorizationGrants ::= SET OF MongoDBAuthorizationGrant
             */
            // Extract the set of roles from our extension, and load them into an OpenSSL stack.
            STACK_OF(ASN1_TYPE)* mongoDBAuthorizationGrants = nullptr;

            // OpenSSL's parsing function will try and manipulate the pointer it's passed. If we
            // passed it 'data->data' directly, it would modify structures owned by peerCert.
            const unsigned char* dataBytes = data->data;
            mongoDBAuthorizationGrants =
                d2i_ASN1_SET_ANY(&mongoDBAuthorizationGrants, &dataBytes, data->length);
            if (!mongoDBAuthorizationGrants) {
                return Status(ErrorCodes::FailedToParse,
                              "Failed to parse x509 authorization grants");
            }
            const auto grantGuard = MakeGuard([&mongoDBAuthorizationGrants]() {
                sk_ASN1_TYPE_pop_free(mongoDBAuthorizationGrants, ASN1_TYPE_free);
            });

            /*
             * MongoDBRole ::= SEQUENCE {
             *  role     UTF8String,
             *  database UTF8String
             * }
             */
            // Loop through every role in the stack.
            ASN1_TYPE* MongoDBRoleWrapped = nullptr;
            while ((MongoDBRoleWrapped = sk_ASN1_TYPE_pop(mongoDBAuthorizationGrants))) {
                const auto roleWrappedGuard =
                    MakeGuard([MongoDBRoleWrapped]() { ASN1_TYPE_free(MongoDBRoleWrapped); });

                if (MongoDBRoleWrapped->type == V_ASN1_SEQUENCE) {
                    // Unwrap the ASN1Type into a STACK_OF(ASN1_TYPE)
                    unsigned char* roleBytes = ASN1_STRING_data(MongoDBRoleWrapped->value.sequence);
                    int roleBytesLength = ASN1_STRING_length(MongoDBRoleWrapped->value.sequence);
                    ASN1_SEQUENCE_ANY* MongoDBRole = nullptr;
                    MongoDBRole = d2i_ASN1_SEQUENCE_ANY(
                        &MongoDBRole, (const unsigned char**)&roleBytes, roleBytesLength);
                    if (!MongoDBRole) {
                        return Status(ErrorCodes::FailedToParse,
                                      "Failed to parse role in x509 authorization grant");
                    }
                    const auto roleGuard = MakeGuard(
                        [&MongoDBRole]() { sk_ASN1_TYPE_pop_free(MongoDBRole, ASN1_TYPE_free); });

                    if (sk_ASN1_TYPE_num(MongoDBRole) != 2) {
                        return Status(ErrorCodes::FailedToParse,
                                      "Role entity in MongoDBAuthorizationGrant must have exactly "
                                      "2 sequence elements");
                    }
                    // Extract the subcomponents of the sequence, which are popped off the stack in
                    // reverse order. Here, parse the role's database.
                    ASN1_TYPE* roleComponent = sk_ASN1_TYPE_pop(MongoDBRole);
                    const auto roleDBGuard =
                        MakeGuard([roleComponent]() { ASN1_TYPE_free(roleComponent); });
                    if (roleComponent->type != V_ASN1_UTF8STRING) {
                        return Status(ErrorCodes::FailedToParse,
                                      "database in MongoDBRole must be a UTF8 string");
                    }
                    std::string roleDB(
                        reinterpret_cast<char*>(ASN1_STRING_data(roleComponent->value.utf8string)));

                    // Parse the role's name.
                    roleComponent = sk_ASN1_TYPE_pop(MongoDBRole);
                    const auto roleNameGuard =
                        MakeGuard([roleComponent]() { ASN1_TYPE_free(roleComponent); });
                    if (roleComponent->type != V_ASN1_UTF8STRING) {
                        return Status(ErrorCodes::FailedToParse,
                                      "role in MongoDBRole must be a UTF8 string");
                    }
                    std::string roleName(
                        reinterpret_cast<char*>(ASN1_STRING_data(roleComponent->value.utf8string)));

                    // Construct a RoleName from the subcomponents
                    roles.emplace(RoleName(roleName, roleDB));

                } else {
                    return Status(ErrorCodes::FailedToParse,
                                  "Unrecognized entity in MongoDBAuthorizationGrant");
                }
            }
            LOG(1) << "MONGODB-X509 authorization parsed the following roles from peer "
                      "certificate: "
                   << [&roles]() {
                          StringBuilder sb;
                          std::for_each(roles.begin(), roles.end(), [&sb](const RoleName& role) {
                              sb << role.toString();
                          });
                          return sb.str();
                      }();
        }
    }

    return roles;
}


void SSLManager::_handleSSLError(int code, int ret) {
    int err = ERR_get_error();

    switch (code) {
        case SSL_ERROR_WANT_READ:
        case SSL_ERROR_WANT_WRITE:
            // should not happen because we turned on AUTO_RETRY
            // However, it turns out this CAN happen during a connect, if the other side
            // accepts the socket connection but fails to do the SSL handshake in a timely
            // manner.
            error() << "SSL: " << code << ", possibly timed out during connect";
            break;

        case SSL_ERROR_ZERO_RETURN:
            // TODO: Check if we can avoid throwing an exception for this condition
            LOG(3) << "SSL network connection closed";
            break;
        case SSL_ERROR_SYSCALL:
            // If ERR_get_error returned 0, the error queue is empty
            // check the return value of the actual SSL operation
            if (err != 0) {
                error() << "SSL: " << getSSLErrorMessage(err);
            } else if (ret == 0) {
                error() << "Unexpected EOF encountered during SSL communication";
            } else {
                error() << "The SSL BIO reported an I/O error " << errnoWithDescription();
            }
            break;
        case SSL_ERROR_SSL: {
            error() << "SSL: " << getSSLErrorMessage(err);
            break;
        }

        default:
            error() << "unrecognized SSL error";
            break;
    }
    throw SocketException(SocketException::CONNECT_ERROR, "");
}

}  // namespace mongo
