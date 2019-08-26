#include <array>
#include <string>
#include <boost/optional.hpp>

#include "/home/mark/src/mongo/build/ice_local_clang/mongo/client/sasl_iam_gen.h"
#include "mongo/base/string_data.h"
#include "mongo/platform/random.h"

namespace mongo {
class SaslIAMProtocol {
public:

static void init();

static std::string generateClientFirst();

static std::string generateServerFirst(StringData clientFirst);

static std::string generateClientSecond(StringData serverFirst, StringData awsKey, StringData secretKey, boost::optional<std::string>& securityToken);


static IamClientSecond parseClientSecond(StringData clientSecond);

private:
    static std::array<char, 24> generateClientNonce();
    static std::array<char, 32> generateServerSalt();
private:
    static std::unique_ptr<SecureRandom> _secureRandom;

};

}