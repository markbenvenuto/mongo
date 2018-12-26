// expression_serialization_context.h
#include <vector>
#include <boost/optional.hpp>

#include "mongo/bson/bsonobj.h"
#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/db/matcher/path.h"

namespace mongo {
class ExpressionSerializationContext {
public:

virtual boost::optional<std::vector<char>> encrypt(ElementPath path, BSONElement element) = 0;

};

} // namespace mongo