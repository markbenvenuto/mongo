// hello

#include "mongo/client/sasl_iam_protocol.h"

#include "mongo/unittest/unittest.h"

namespace mongo {
    namespace {

TEST(SaslIamProtocol, Basic) {

    auto clientFirst = SaslIAMProtocol::generateClientFirst();


    auto serverFirst = SaslIAMProtocol::generateServerFirst(clientFirst);

    boost::optional<std::string> token;
    auto clientSecond = SaslIAMProtocol::generateClientSecond(serverFirst, "FAKEFAKEFAKEFAKEFAKE", "FAKEFAKEFAKEFAKEFAKEfakefakefakefakefake", token);
}

    }
}
