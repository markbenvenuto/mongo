#include <vector>
#include <numeric>
#include <tuple>
#include <string>

#include "mongo/base/string_data.h"

namespace mongo {
class MatchParserEncryptionContext {
public:
using OrdinalPath = std::vector<std::uint32_t>;
    void record(StringData name, OrdinalPath path) {
        _paths.push_back({name.toString(), path});
    }

private:
    std::vector<std::tuple<std::string, OrdinalPath>> _paths;
};

}