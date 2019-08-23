// hello

#include "mongo/client/sasl_iam_protocol.h"

#include "mongo/unittest/unittest.h"

namespace mongo {
    namespace {

TEST(SaslIamProtocol, Basic) {

    SaslIAMProtocol::init();

    auto clientFirst = SaslIAMProtocol::generateClientFirst();


    auto serverFirst = SaslIAMProtocol::generateServerFirst(clientFirst);

    auto clientSecond = SaslIAMProtocol::generateClientSecond(serverFirst, "FAKEFAKEFAKEFAKEFAKE", "FAKEFAKEFAKEFAKEFAKEfakefakefakefakefake", boost::none);
}

    }
}
