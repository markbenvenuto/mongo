/**
 *    Copyright (C) 2019-present MongoDB, Inc.
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

#define MONGO_LOGV2_DEFAULT_COMPONENT ::mongo::logv2::LogComponent::kControl

#include <kms_message/kms_message.h>

#include <stdlib.h>

#include "mongo/base/init.h"
#include "mongo/base/parse_number.h"
#include "mongo/base/secure_allocator.h"
#include "mongo/base/status_with.h"
#include "mongo/bson/json.h"
#include "mongo/db/commands/test_commands_enabled.h"
#include "mongo/shell/kms.h"
#include "mongo/shell/kms_gen.h"
#include "mongo/util/base64.h"
#include "mongo/util/kms_message_support.h"
#include "mongo/util/net/hostandport.h"
#include "mongo/util/net/sock.h"
#include "mongo/util/net/ssl_manager.h"
#include "mongo/util/net/ssl_options.h"
#include "mongo/util/text.h"
#include "mongo/util/time_support.h"

namespace mongo {
namespace {

/**
 * Make a request to a KMIP HTTP endpoint.
 *
 * Does not maintain a persistent HTTP connection.
 */
class KMIPConnection {
public:
    KMIPConnection(SSLManagerInterface* ssl)
        : _sslManager(ssl), _socket(std::make_unique<Socket>(10, logv2::LogSeverity::Info())) {}

    UniqueKmsResponse makeOneRequest(const HostAndPort& host, ConstDataRange request);

private:
    UniqueKmsResponse sendRequest(ConstDataRange request);

    void connect(const HostAndPort& host);

private:
    // SSL Manager for connections
    SSLManagerInterface* _sslManager;

    // Synchronous socket
    std::unique_ptr<Socket> _socket;
};

/**
 * Manages SSL information and config for how to talk to KMIP KMS.
 */
class KMIPKMSService final : public KMSService {
public:
    KMIPKMSService() = default;

    static std::unique_ptr<KMSService> create(const KmipKMS& config);

    std::vector<uint8_t> encrypt(ConstDataRange cdr, StringData kmsKeyId) final;

    SecureVector<uint8_t> decrypt(ConstDataRange cdr, BSONObj masterKey) final;

    BSONObj encryptDataKey(ConstDataRange cdr, StringData keyId) final;

private:
    // SSL Manager
    std::shared_ptr<SSLManagerInterface> _sslManager;

    // Server to connect to
    HostAndPort _server;
};

void uassertKmsRequestInternal(kms_request_t* request, bool ok) {
    if (!ok) {
        const char* msg = kms_request_get_error(request);
        uasserted(51135, str::stream() << "Internal KMIP KMS Error: " << msg);
    }
}

#define uassertKmsRequest(X) uassertKmsRequestInternal(request, (X));


std::vector<uint8_t> toVector(const std::string& str) {
    std::vector<uint8_t> blob;

    std::transform(std::begin(str), std::end(str), std::back_inserter(blob), [](auto c) {
        return static_cast<uint8_t>(c);
    });

    return blob;
}

SecureVector<uint8_t> toSecureVector(const std::string& str) {
    SecureVector<uint8_t> blob(str.length());

    std::transform(std::begin(str), std::end(str), blob->data(), [](auto c) {
        return static_cast<uint8_t>(c);
    });

    return blob;
}

std::vector<uint8_t> KMIPKMSService::encrypt(ConstDataRange cdr, StringData kmsKeyId) {
    auto request =
        UniqueKmsRequest(kms_encrypt_request_new(reinterpret_cast<const uint8_t*>(cdr.data()),
                                                 cdr.length(),
                                                 kmsKeyId.toString().c_str(),
                                                 NULL));

    auto buffer = UniqueKmsCharBuffer(kms_request_get_signed(request.get()));
    auto buffer_len = strlen(buffer.get());

    KMIPConnection connection(_sslManager.get());
    auto response = connection.makeOneRequest(_server, ConstDataRange(buffer.get(), buffer_len));

    auto body = kms_response_get_body(response.get(), nullptr);

    BSONObj obj = fromjson(body);

    auto field = obj["__type"];

    if (!field.eoo()) {
        KmipKMSError kmipResponse;
        try {
            kmipResponse = KmipKMSError::parse(IDLParserErrorContext("kmipEncryptError"), obj);
        } catch (DBException& dbe) {
            uasserted(51274,
                      str::stream() << "KMIP KMS failed to parse error message: " << dbe.toString()
                                    << ", Response : " << obj);
        }

        uasserted(51224,
                  str::stream() << "KMIP KMS failed to encrypt: " << kmipResponse.getType() << " : "
                                << kmipResponse.getMessage());
    }

    auto kmipResponse = KmipEncryptResponse::parse(IDLParserErrorContext("kmipEncryptResponse"), obj);

    auto blobStr = base64::decode(kmipResponse.getCiphertextBlob().toString());

    return toVector(blobStr);
}

BSONObj KMIPKMSService::encryptDataKey(ConstDataRange cdr, StringData keyId) {
    auto dataKey = encrypt(cdr, keyId);

    KmipMasterKey masterKey;
    masterKey.setKey(keyId);
    masterKey.setRegion(parseCMK(keyId));
    masterKey.setEndpoint(boost::optional<StringData>(_server.toString()));

    KmipMasterKeyAndMaterial keyAndMaterial;
    keyAndMaterial.setKeyMaterial(dataKey);
    keyAndMaterial.setMasterKey(masterKey);


    return keyAndMaterial.toBSON();
}

SecureVector<uint8_t> KMIPKMSService::decrypt(ConstDataRange cdr, BSONObj masterKey) {
    auto kmipMasterKey = KmipMasterKey::parse(IDLParserErrorContext("kmipMasterKey"), masterKey);

    auto request = UniqueKmsRequest(kms_decrypt_request_new(
        reinterpret_cast<const uint8_t*>(cdr.data()), cdr.length(), nullptr));

    if (_server.empty()) {
        _server = getDefaultHost(kmipMasterKey.getRegion());
    }

    initRequest(request.get(), _server.host(), kmipMasterKey.getRegion());

    auto buffer = UniqueKmsCharBuffer(kms_request_get_signed(request.get()));
    auto buffer_len = strlen(buffer.get());
    KMIPConnection connection(_sslManager.get());
    auto response = connection.makeOneRequest(_server, ConstDataRange(buffer.get(), buffer_len));

    auto body = kms_response_get_body(response.get(), nullptr);

    BSONObj obj = fromjson(body);

    auto field = obj["__type"];

    if (!field.eoo()) {
        KmipKMSError kmipResponse;
        try {
            kmipResponse = KmipKMSError::parse(IDLParserErrorContext("kmipDecryptError"), obj);
        } catch (DBException& dbe) {
            uasserted(51275,
                      str::stream() << "KMIP KMS failed to parse error message: " << dbe.toString()
                                    << ", Response : " << obj);
        }

        uasserted(51225,
                  str::stream() << "KMIP KMS failed to decrypt: " << kmipResponse.getType() << " : "
                                << kmipResponse.getMessage());
    }

    auto kmipResponse = KmipDecryptResponse::parse(IDLParserErrorContext("kmipDecryptResponse"), obj);

    auto blobStr = base64::decode(kmipResponse.getPlaintext().toString());

    return toSecureVector(blobStr);
}

void KMIPConnection::connect(const HostAndPort& host) {
    SockAddr server(host.host().c_str(), host.port(), AF_UNSPEC);

    uassert(51136,
            str::stream() << "KMIP KMS server address " << host.host() << " is invalid.",
            server.isValid());

    int attempt = 0;
    bool connected = false;
    while ((connected == false) && (attempt < 20)) {
        connected = _socket->connect(server);
        attempt++;
    }
    uassert(51137,
            str::stream() << "Could not connect to KMIP KMS server " << server.toString(),
            connected);

    uassert(51138,
            str::stream() << "Failed to perform SSL handshake with the KMIP KMS server "
                          << host.toString(),
            _socket->secure(_sslManager, host.host()));
}

// Sends a request message to the KMIP KMS server and creates a KMS Response.
UniqueKmsResponse KMIPConnection::sendRequest(ConstDataRange request) {
    std::array<char, 512> resp;

    _socket->send(
        reinterpret_cast<const char*>(request.data()), request.length(), "KMIP KMS request");

    auto parser = UniqueKmsResponseParser(kms_response_parser_new());
    int bytes_to_read = 0;

    while ((bytes_to_read = kms_response_parser_wants_bytes(parser.get(), resp.size())) > 0) {
        bytes_to_read = std::min(bytes_to_read, static_cast<int>(resp.size()));
        bytes_to_read = _socket->unsafe_recv(resp.data(), bytes_to_read);

        uassert(51139,
                "kms_response_parser_feed failed",
                kms_response_parser_feed(
                    parser.get(), reinterpret_cast<uint8_t*>(resp.data()), bytes_to_read));
    }

    auto response = UniqueKmsResponse(kms_response_parser_get_response(parser.get()));

    return response;
}

UniqueKmsResponse KMIPConnection::makeOneRequest(const HostAndPort& host, ConstDataRange request) {
    connect(host);

    auto resp = sendRequest(request);

    _socket->close();

    return resp;
}

boost::optional<std::string> toString(boost::optional<StringData> str) {
    if (str) {
        return {str.get().toString()};
    }
    return boost::none;
}

std::unique_ptr<KMSService> KMIPKMSService::create(const KmipKMS& config) {
    auto kmipKMS = std::make_unique<KMIPKMSService>();

    SSLParams params;
    params.sslPEMKeyFile = "";
    params.sslPEMKeyPassword = "";
    params.sslClusterFile = "";
    params.sslClusterPassword = "";
    params.sslCAFile = "";

    params.sslCRLFile = "";

    // Copy the rest from the global SSL manager options.
    params.sslFIPSMode = sslGlobalParams.sslFIPSMode;

    // KMS servers never should have invalid certificates
    params.sslAllowInvalidCertificates = false;
    params.sslAllowInvalidHostnames = false;

    params.sslDisabledProtocols =
        std::vector({SSLParams::Protocols::TLS1_0, SSLParams::Protocols::TLS1_1});

    // Leave the CA file empty so we default to system CA but for local testing allow it to inherit
    // the CA file.
    if (!config.getUrl().value_or("").empty()) {
        params.sslCAFile = sslGlobalParams.sslCAFile;
        kmipKMS->_server = parseUrl(config.getUrl().get());
    }

    kmipKMS->_sslManager = SSLManagerInterface::create(params, false);

    kmipKMS->_config.accessKeyId = config.getAccessKeyId().toString();

    kmipKMS->_config.secretAccessKey = config.getSecretAccessKey().toString();

    kmipKMS->_config.sessionToken = toString(config.getSessionToken());

    return kmipKMS;
}

/**
 * Factory for KMIPKMSService if user specifies kmip config to mongo() JS constructor.
 */
class KMIPKMSServiceFactory final : public KMSServiceFactory {
public:
    KMIPKMSServiceFactory() = default;
    ~KMIPKMSServiceFactory() = default;

    std::unique_ptr<KMSService> create(const BSONObj& config) final {
        auto field = config[KmsProviders::kKmipFieldName];
        if (field.eoo()) {
            return nullptr;
        }
        auto obj = field.Obj();
        return KMIPKMSService::create(KmipKMS::parse(IDLParserErrorContext("root"), obj));
    }
};

}  // namespace

MONGO_INITIALIZER(KMSRegister)(::mongo::InitializerContext* context) {
    kms_message_init();
    KMSServiceController::registerFactory(KMSProviderEnum::kmip,
                                          std::make_unique<KMIPKMSServiceFactory>());
    return Status::OK();
}

}  // namespace mongo
