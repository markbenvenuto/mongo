// api_trace.h

#define TRACE_API_CALL(_x) \
    [&]() { \
        ::mongo::ApiTracer t1 (#_x); \
        return _x; \
    }(); 

//a = TRACE_API_CALL(foo("bar"));

#include "mongo/logv2/log_detail.h"

namespace mongo {
class ApiTracer
{
public:
    ApiTracer(const char* str) : _str(str) {
        LOGV2(2, "Start trace", "api"_attr = _str);
    }

    ~ApiTracer() {
        LOGV2(3, "End trace", "api"_attr = _str);
    }

private:
    const char* _str;
};

} 