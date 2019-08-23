#include <array>
#include <string>
#include <boost/optional.hpp>

#include "mongo/base/string_data.h"
#include "mongo/platform/random.h"

namespace mongo {
class SaslIAMProtocol {
public:

static void init();

static std::string generateClientFirst();

static std::string generateServerFirst(StringData clientFirst);

static std::string generateClientSecond(StringData serverFirst, StringData awsKey, StringData secretKey, boost::optional<StringData> securityToken);

private:
    static std::array<char, 24> generateClientNonce();
    static std::array<char, 32> generateServerSalt();
private:
    static std::unique_ptr<SecureRandom> _secureRandom;

};

}