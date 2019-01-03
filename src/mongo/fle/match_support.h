#include <numeric>
#include <string>
#include <tuple>
#include <vector>

#include "mongo/base/string_data.h"

namespace mongo {
class MatchParserEncryptionContext {
public:
    using OrdinalPath = std::vector<std::uint32_t>;
    void record(StringData name, OrdinalPath path) {
        _paths.push_back(std::tuple<std::string, OrdinalPath>(name.toString(), path));
    }

    const std::vector<std::tuple<std::string, OrdinalPath>>& paths() const { return _paths; }
private:
    std::vector<std::tuple<std::string, OrdinalPath>> _paths;
};
}