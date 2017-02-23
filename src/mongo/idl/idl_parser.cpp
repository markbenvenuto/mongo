#include <string>

#include "mongo/idl/idl_parser.h"

#include "mongo/db/namespace_string.h"
#include "mongo/stdx/memory.h"
#include "mongo/base/status_with.h"
#include "mongo/base/string_data.h"
#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/util/net/hostandport.h"

namespace mongo {

void IDLParserErrorContext::assertNotEmptyObject(StringData fieldName) {

}

}  //namespace mongo