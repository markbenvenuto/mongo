
#include "mongo/client/sasl_iam_protocol.h"

#include <iostream>
#include "/home/mark/src/mongo/src/third_party/kms-message/src/kms_message/kms_message.h"

#include "mongo/bson/bsonobj.h"
#include "mongo/base/init.h"
#include "/home/mark/src/mongo/build/ice_local_clang/mongo/client/sasl_iam_gen.h"
//#include "mongo/client/sasl_iam_gen.h"
#include "mongo/util/base64.h"
#include "mongo/base/data_range_cursor.h"
#include "mongo/base/data_type_validated.h"

#include "mongo/platform/random.h"

namespace mongo {

/**
 * Free kms_request_t
 */
struct kms_request_tFree {
    void operator()(kms_request_t* p) noexcept {
        if (p) {
            ::kms_request_destroy(p);
        }
    }
};

using UniqueKmsRequest = std::unique_ptr<kms_request_t, kms_request_tFree>;

/**
 * Free kms_response_parser_t
 */
struct kms_response_parser_tFree {
    void operator()(kms_response_parser_t* p) noexcept {
        if (p) {
            ::kms_response_parser_destroy(p);
        }
    }
};

using UniqueKmsResponseParser = std::unique_ptr<kms_response_parser_t, kms_response_parser_tFree>;

/**
 * Free kms_response_t
 */
struct kms_response_tFree {
    void operator()(kms_response_t* p) noexcept {
        if (p) {
            ::kms_response_destroy(p);
        }
    }
};

using UniqueKmsResponse = std::unique_ptr<kms_response_t, kms_response_tFree>;

/**
 * Free kms_char_buffer
 */
struct kms_char_free {
    void operator()(char* x) {
        kms_request_free_string(x);
    }
};

using UniqueKmsCharBuffer = std::unique_ptr<char, kms_char_free>;


std::unique_ptr<SecureRandom> SaslIAMProtocol::_secureRandom;

void SaslIAMProtocol::init() {
    _secureRandom = SecureRandom::create();
}

    std::array<char, 24> SaslIAMProtocol::generateClientNonce(){
        const int nonceLenQWords = 3;
        uint64_t binaryNonce[nonceLenQWords];

        binaryNonce[0] = _secureRandom->nextInt64();
        binaryNonce[1] = _secureRandom->nextInt64();
        binaryNonce[2] = _secureRandom->nextInt64();

        std::array<char, 24> ret;
        memcpy(ret.data(), (char*)&binaryNonce, sizeof(ret));
        return ret;
    }
    std::array<char, 32> SaslIAMProtocol::generateServerSalt() {
        const int nonceLenQWords = 4;
        uint64_t binaryNonce[nonceLenQWords];

        binaryNonce[0] = _secureRandom->nextInt64();
        binaryNonce[1] = _secureRandom->nextInt64();
        binaryNonce[2] = _secureRandom->nextInt64();
        binaryNonce[3] = _secureRandom->nextInt64();

        std::array<char, 32> ret;
        memcpy(ret.data(), (char*)&binaryNonce, sizeof(ret));
        return ret;

    }


std::string SaslIAMProtocol::generateClientFirst() {

    IamClientFirst first;

    auto nonce = generateClientNonce();
    first.setNonce(nonce);

    BSONObj obj = first.toBSON();

    return base64::encode((obj.objdata()), obj.objsize());
}

template <typename T>
T decode(StringData base64) {
    // TODO - avoid string copy
    auto clientFirstBinary = base64::decode(base64.toString());

    ConstDataRange cdr(clientFirstBinary.data(), clientFirstBinary.size());

    auto clientFirstBson = cdr.read<Validated<BSONObj>>();


    return T::parse(IDLParserErrorContext("sasl"), clientFirstBson);
}

template <typename T>
std::string encode(T object) {
    BSONObj obj = object.toBSON();

    std::cout << "ENCODE: " << obj << std::endl;

    return base64::encode((obj.objdata()), obj.objsize());
}


std::string SaslIAMProtocol::generateServerFirst(StringData clientFirstBase64) {

    auto clientFirst = decode<IamClientFirst>(clientFirstBase64);

    // TODO - assert lengths of client data

    IamServerFirst first;

    first.setNonce(clientFirst.getNonce());

    auto serverSalt = generateServerSalt();

    first.setSalt(serverSalt);

    return encode(first);
}



void uassertKmsRequestInternal(kms_request_t* request, bool ok) {
    if (!ok) {
        const char* msg = kms_request_get_error(request);
        uasserted(51250, str::stream() << "Internal AWS KMS Error: " << msg);
    }
}

#define uassertKmsRequest(X) uassertKmsRequestInternal(request.get(), (X));


std::string SaslIAMProtocol::generateClientSecond(StringData serverFirstBase64, StringData awsKey, StringData secretKey, boost::optional<std::string>& securityToken)
{
    auto serverFirst = decode<IamServerFirst>(serverFirstBase64);

    // TODO - check nonce

    auto request =
        UniqueKmsRequest(kms_caller_identity_request_new(
                                                 NULL));


    // use current time
    uassertKmsRequest(kms_request_set_date(request.get(), nullptr));

    // Region does not matter for sts requests since sts is global
    uassertKmsRequest(kms_request_set_region(request.get(), "us-east-1"));

    // kms is always the name of the service
    uassertKmsRequest(kms_request_set_service(request.get(), "sts"));
    uassertKmsRequest(kms_request_add_header_field(
             request.get(), "Host", "sts.amazonaws.com"));
    uassertKmsRequest(kms_request_add_header_field(
             request.get(), "x-mongodb-server-salt", "SALTSALT"));

    uassertKmsRequest(kms_request_set_access_key_id(request.get(), awsKey.toString().c_str()));
    uassertKmsRequest(kms_request_set_secret_key(request.get(), secretKey.toString().c_str()));



    // TODO
    if (securityToken) {
        // TODO: move this into kms-message
        uassertKmsRequest(kms_request_add_header_field(
            request.get(), "X-Amz-Security-Token", securityToken.get().c_str()));
    }

    auto buffer2 = UniqueKmsCharBuffer(kms_request_get_signature(request.get()));

    std::cout << "SIGNAT: " << buffer2.get() << std::endl;


    //auto buffer = UniqueKmsCharBuffer(kms_request_get_signed(request.get()));


    IamClientSecond second;

    // TODO - what should this be to make this safe?
    second.setNonce(serverFirst.getNonce());

    second.setRequestAuthHeader(kms_request_get_signature(request.get()));

    IamClientHeaders headers;
    // TODO - assert
    headers.setContentLength(kms_request_get_canonical_header(request.get(), "Content-Length"));

    headers.setContentType(kms_request_get_canonical_header(request.get(), "Content-Type"));

    headers.setHost(kms_request_get_canonical_header(request.get(), "Host"));

    headers.setXAmzDate(kms_request_get_canonical_header(request.get(), "X-Amz-Date"));

    // TODO - token
    const char* token = kms_request_get_canonical_header(request.get(), "X-Amz-Security-Token");
    if(token) {
        headers.setXAmzSecurityToken(boost::optional<StringData>(token));

    }

    headers.setXMongodbServerSalt(kms_request_get_canonical_header(request.get(), "X-Mongodb-Server-Salt"));

    second.setHeaders(headers);

    return encode(second);
}
IamClientSecond SaslIAMProtocol::parseClientSecond(StringData clientSecond) {
    return decode<IamClientSecond>(clientSecond);
}

MONGO_INITIALIZER(SasLIamInit)(::mongo::InitializerContext* context) {
    SaslIAMProtocol::init();
    return Status::OK();
}


}


