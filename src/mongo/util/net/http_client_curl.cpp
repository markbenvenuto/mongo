/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#include "mongo/platform/basic.h"

#include "mongo/transport/transport_layer.h"
#include <cstddef>
#include <curl/curl.h>
#include <curl/easy.h>
#include <memory>
#include <string>

#include "mongo/base/data_builder.h"
#include "mongo/base/data_range.h"
#include "mongo/base/data_range_cursor.h"
#include "mongo/base/init.h"
#include "mongo/base/status.h"
#include "mongo/base/status_with.h"
#include "mongo/base/string_data.h"
#include "mongo/bson/bsonobj.h"
#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/db/commands/server_status.h"
#include "mongo/executor/connection_pool.h"
#include "mongo/executor/connection_pool_stats.h"
#include "mongo/platform/mutex.h"
#include "mongo/stdx/unordered_map.h"
#include "mongo/util/alarm.h"
#include "mongo/util/alarm_runner_background_thread.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/concurrency/thread_pool.h"
#include "mongo/util/functional.h"
#include "mongo/util/net/hostandport.h"
#include "mongo/util/net/http_client.h"
#include "mongo/util/strong_weak_finish_line.h"
#include "mongo/util/system_clock_source.h"
#include "mongo/util/timer.h"


namespace mongo {

namespace {

class CurlLibraryManager {
public:
    // No copying and no moving because we give libcurl the address of our members.
    // In practice, we'll never want to copy/move this instance anyway,
    // but if that ever changes, we can write trivial implementations to deal with it.
    CurlLibraryManager(const CurlLibraryManager&) = delete;
    CurlLibraryManager& operator=(const CurlLibraryManager&) = delete;
    CurlLibraryManager(CurlLibraryManager&&) = delete;
    CurlLibraryManager& operator=(CurlLibraryManager&&) = delete;

    CurlLibraryManager() = default;
    ~CurlLibraryManager() {
        // Ordering matters: curl_global_cleanup() must happen last.
        if (_initialized) {
            curl_global_cleanup();
        }
    }

    Status initialize() {
        auto status = _initializeGlobal();
        if (!status.isOK()) {
            return status;
        }

        return Status::OK();
    }

private:
    Status _initializeGlobal() {
        if (_initialized) {
            return Status::OK();
        }

        CURLcode ret = curl_global_init(CURL_GLOBAL_ALL);
        if (ret != CURLE_OK) {
            return {ErrorCodes::InternalError,
                    str::stream() << "Failed to initialize CURL: " << static_cast<int64_t>(ret)};
        }

        curl_version_info_data* version_data = curl_version_info(CURLVERSION_NOW);
        if (!(version_data->features & CURL_VERSION_SSL)) {
            return {ErrorCodes::InternalError, "Curl lacks SSL support, cannot continue"};
        }

        _initialized = true;
        return Status::OK();
    }

private:
    bool _initialized = false;

} curlLibraryManager;

/**
 * Receives data from the remote side.
 */
size_t WriteMemoryCallback(void* ptr, size_t size, size_t nmemb, void* data) {
    const size_t realsize = size * nmemb;

    auto* mem = reinterpret_cast<DataBuilder*>(data);
    if (!mem->writeAndAdvance(ConstDataRange(reinterpret_cast<const char*>(ptr),
                                             reinterpret_cast<const char*>(ptr) + realsize))
             .isOK()) {
        // Cause curl to generate a CURLE_WRITE_ERROR by returning a different number than how much
        // data there was to write.
        return 0;
    }

    return realsize;
}

/**
 * Sends data to the remote side
 */
size_t ReadMemoryCallback(char* buffer, size_t size, size_t nitems, void* instream) {

    auto* cdrc = reinterpret_cast<ConstDataRangeCursor*>(instream);

    size_t ret = 0;

    if (cdrc->length() > 0) {
        size_t readSize = std::min(size * nitems, cdrc->length());
        memcpy(buffer, cdrc->data(), readSize);
        invariant(cdrc->advanceNoThrow(readSize).isOK());
        ret = readSize;
    }

    return ret;
}

struct CurlEasyCleanup {
    void operator()(CURL* handle) {
        if (handle) {
            curl_easy_cleanup(handle);
        }
    }
};
using CurlHandle = std::unique_ptr<CURL, CurlEasyCleanup>;

struct CurlSlistFreeAll {
    void operator()(curl_slist* list) {
        if (list) {
            curl_slist_free_all(list);
        }
    }
};
using CurlSlist = std::unique_ptr<curl_slist, CurlSlistFreeAll>;


long longSeconds(Seconds tm) {
    return static_cast<long>(durationCount<Seconds>(tm));
}


CurlHandle createCurlHandle() {
    CurlHandle handle(curl_easy_init());
    uassert(ErrorCodes::InternalError, "Curl initialization failed", handle);

    curl_easy_setopt(handle.get(), CURLOPT_CONNECTTIMEOUT, longSeconds(kConnectionTimeout));
    curl_easy_setopt(handle.get(), CURLOPT_FOLLOWLOCATION, 0);
    curl_easy_setopt(handle.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(handle.get(), CURLOPT_NOSIGNAL, 1);
    curl_easy_setopt(handle.get(), CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
#ifdef CURLOPT_TCP_KEEPALIVE
    curl_easy_setopt(handle.get(), CURLOPT_TCP_KEEPALIVE, 1);
#endif
    curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT, longSeconds(kTotalRequestTimeout));

#if LIBCURL_VERSION_NUM > 0x072200
    // Requires >= 7.34.0
    curl_easy_setopt(handle.get(), CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
#endif
    curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(handle.get(), CURLOPT_HEADERFUNCTION, WriteMemoryCallback);

    // TODO: CURLOPT_EXPECT_100_TIMEOUT_MS?
    // TODO: consider making this configurable
    curl_easy_setopt(handle.get(), CURLOPT_VERBOSE, 1);

    return handle;
}

// struct HandleSingleton {
// public:
//     CurlHandle get() {
//         if (!_handle) {
//             _handle = createCurlHandle();
//         }
//         return std::move(_handle);
//     }

//     void returnHandle(CurlHandle h) {
//         _handle = std::move(h);
//     }

// private:
//     CurlHandle _handle;
// };

// HandleSingleton _singleton;

using namespace executor;

ConnectionPool::Options makePoolOptions(Seconds timeout) {
    ConnectionPool::Options opts;
    opts.refreshTimeout = timeout;
    opts.minConnections = 1;
    opts.maxConnections = 10;
    opts.maxConnecting = 4;
    opts.refreshRequirement = Seconds(60);
    opts.hostTimeout = Seconds(300);
    return opts;
}


// class CurlHostTimingData {
// public:
//     void markFailed() {
//         stdx::lock_guard<Latch> lk(_mutex);
//         _failed = true;
//     }

//     void updateLatency(Milliseconds millis) {
//         stdx::lock_guard<Latch> lk(_mutex);
//         if (_failed) {
//             _latency = millis;
//             _failed = false;
//         } else {
//             // This calculates a moving average of the round trip time - this formula was taken
//             from
//             //
//             https://github.com/mongodb/specifications/blob/master/source/server-selection/server-selection.rst#calculation-of-average-round-trip-times
//             constexpr double alpha = 0.2;
//             double newLatency = alpha * millis.count() + (1 - alpha) * _latency.count();
//             _latency = Milliseconds(static_cast<int64_t>(std::nearbyint(newLatency)));
//         }
//     }

//     Milliseconds getLatency() const {
//         stdx::lock_guard<Latch> lk(_mutex);
//         return _failed ? Milliseconds::max() : _latency;
//     }

//     AtomicWord<int64_t>& uses() {
//         return _uses;
//     }

// private:
//     mutable Mutex _mutex = MONGO_MAKE_LATCH("CurlHostTimingData::_mutex");
//     Milliseconds _latency{0};
//     bool _failed = true;
//     AtomicWord<int64_t> _uses{0};
// };

// struct CurlHandlePoolTimingData {
//     Mutex mutex = MONGO_MAKE_LATCH("CurlHandlePoolTimingData::mutex");
//     stdx::unordered_map<HostAndPort, std::shared_ptr<CurlHostTimingData>> timingData;
// };


/*
 * This implements the timer interface for the ConnectionPool.
 * Timers will be expired in order on a single background thread.
 */
// TODO - make this a common type since there is nothing LDAP in this
class CurlHandleTimer : public ConnectionPool::TimerInterface {
public:
    explicit CurlHandleTimer(ClockSource* clockSource, std::shared_ptr<AlarmScheduler> scheduler)
        : _clockSource(clockSource), _scheduler(std::move(scheduler)), _handle(nullptr) {}

    virtual ~CurlHandleTimer() {
        if (_handle) {
            _handle->cancel().ignore();
        }
    }

    void setTimeout(Milliseconds timeout, TimeoutCallback cb) final {
        auto res = _scheduler->alarmFromNow(timeout);
        _handle = std::move(res.handle);

        std::move(res.future).getAsync([cb](Status status) {
            if (status == ErrorCodes::CallbackCanceled) {
                return;
            }

            fassert(51052, status);
            cb();
        });
    }

    void cancelTimeout() final {
        auto handle = std::move(_handle);
        if (handle) {
            handle->cancel().ignore();
        }
    }

    Date_t now() final {
        return _clockSource->now();
    }

private:
    ClockSource* const _clockSource;
    std::shared_ptr<AlarmScheduler> _scheduler;
    AlarmScheduler::SharedHandle _handle;
};

class CurlHandleTypeFactory : public executor::ConnectionPool::DependentTypeFactoryInterface {
public:
    CurlHandleTypeFactory()
        : _clockSource(SystemClockSource::get()),
          _executor(std::make_shared<ThreadPool>(_makeThreadPoolOptions())),
          _timerScheduler(std::make_shared<AlarmSchedulerPrecise>(_clockSource)),
          _timerRunner({_timerScheduler})
    //   ,_timingData(std::make_shared<CurlHandlePoolTimingData>())
    {}

    std::shared_ptr<ConnectionPool::ConnectionInterface> makeConnection(const HostAndPort&,
                                                                        transport::ConnectSSLMode,
                                                                        size_t generation) final;

    std::shared_ptr<ConnectionPool::TimerInterface> makeTimer() final {
        _start();
        return std::make_shared<CurlHandleTimer>(_clockSource, _timerScheduler);
    }

    const std::shared_ptr<OutOfLineExecutor>& getExecutor() final {
        return _executor;
    }

    Date_t now() final {
        return _clockSource->now();
    }

    void shutdown() final {
        if (!_running) {
            return;
        }
        _timerRunner.shutdown();

        auto pool = checked_pointer_cast<ThreadPool>(_executor);
        pool->shutdown();
        pool->join();
    }

    // protected:

    //     ThreadPool::Stats getThreadPoolStats() const {
    //         auto threadPool = static_cast<ThreadPool*>(_executor.get());
    //         return threadPool->getStats();
    //     }

private:
    void _start() {
        if (_running)
            return;
        _timerRunner.start();

        auto pool = checked_pointer_cast<ThreadPool>(_executor);
        pool->startup();

        _running = true;
    }

    static inline ThreadPool::Options _makeThreadPoolOptions() {
        ThreadPool::Options opts;
        opts.poolName = "CurlConnPool";
        opts.maxThreads = ThreadPool::Options::kUnlimited;
        opts.maxIdleThreadAge = Seconds{5};

        return opts;
    }

    ClockSource* const _clockSource;
    std::shared_ptr<OutOfLineExecutor> _executor;
    std::shared_ptr<AlarmScheduler> _timerScheduler;
    bool _running = false;
    AlarmRunnerBackgroundThread _timerRunner;
    // std::shared_ptr<CurlHandlePoolTimingData> _timingData;
};


transport::ConnectSSLMode mapProtocolToSSLMode(HttpClient::Protocols protocol) {
    return (protocol == HttpClient::Protocols::kHttpsOnly) ? transport::kEnableSSL
                                                           : transport::kDisableSSL;
}

HttpClient::Protocols mapSSLModeToProtocol(transport::ConnectSSLMode sslMode) {
    return (sslMode == transport::kEnableSSL) ? HttpClient::Protocols::kHttpsOnly
                                              : HttpClient::Protocols::kHttpOrHttps;
}

class PooledCurlHandle : public ConnectionPool::ConnectionInterface,
                         public std::enable_shared_from_this<PooledCurlHandle> {
public:
    PooledCurlHandle(std::shared_ptr<OutOfLineExecutor> executor,
                     ClockSource* clockSource,
                     const std::shared_ptr<AlarmScheduler>& alarmScheduler,
                     const HostAndPort& host,
                     HttpClient::Protocols protocol,
                     size_t generation)
        : ConnectionInterface(generation),
          _executor(std::move(executor)),
          _alarmScheduler(alarmScheduler),
          _timer(clockSource, alarmScheduler),
          _target(host),
          _protocol(protocol) {}


    virtual ~PooledCurlHandle() = default;

    const HostAndPort& getHostAndPort() const final {
        return _target;
    }

    // This cannot block under any circumstances because the ConnectionPool is holding
    // a mutex while calling isHealthy(). Since we don't have a good way of knowing whether
    // the connection is healthy, just return true here.
    bool isHealthy() final {
        return true;
    }

    void setTimeout(Milliseconds timeout, TimeoutCallback cb) final {
        _timer.setTimeout(timeout, cb);
    }

    void cancelTimeout() final {
        _timer.cancelTimeout();
    }

    Date_t now() final {
        return _timer.now();
    }

    transport::ConnectSSLMode getSslMode() const final {
        return mapProtocolToSSLMode(_protocol);
    }


    CURL* get() {
        return _handle.get();
    }

private:
    void setup(Milliseconds timeout, SetupCallback cb) final {
        auto anchor = shared_from_this();
        _executor->schedule([this, anchor, cb = std::move(cb)](auto execStatus) {
            if (!execStatus.isOK()) {
                cb(this, execStatus);
                return;
            }

            _handle = createCurlHandle();

            if (_protocol == HttpClient::Protocols::kHttpOrHttps) {
                curl_easy_setopt(
                    _handle.get(), CURLOPT_PROTOCOLS, CURLPROTO_HTTPS | CURLPROTO_HTTP);
            } else {
                curl_easy_setopt(_handle.get(), CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
            }

            cb(this, Status::OK());
        });
    }

    void refresh(Milliseconds timeout, RefreshCallback cb) final {
        auto anchor = shared_from_this();
        _executor->schedule([this, anchor, cb = std::move(cb)](auto execStatus) {
            if (!execStatus.isOK()) {
                cb(this, execStatus);
                return;
            }

            // We lie because curl will automatically reconnect for us
            // We just want the connection pool to prune handles on a timer for us.
            indicateSuccess();

            cb(this, Status::OK());
        });
    }

private:
    std::shared_ptr<OutOfLineExecutor> _executor;
    std::shared_ptr<AlarmScheduler> _alarmScheduler;
    CurlHandleTimer _timer;
    HostAndPort _target;

    HttpClient::Protocols _protocol;
    CurlHandle _handle;
};


std::shared_ptr<executor::ConnectionPool::ConnectionInterface>
CurlHandleTypeFactory::makeConnection(const HostAndPort& host,
                                      transport::ConnectSSLMode sslMode,
                                      size_t generation) {
    _start();

    return std::make_shared<PooledCurlHandle>(
        _executor, _clockSource, _timerScheduler, host, mapSSLModeToProtocol(sslMode), generation);
}

class CurlFactoryHandle {
public:
    CurlFactoryHandle(executor::ConnectionPool::ConnectionHandle handle, CURL* curlHandle)
        : _poolHandle(std::move(handle)), _handle(curlHandle) {}

~CurlFactoryHandle() {
    if(!_success) {
        _poolHandle->indicateFailure(Status(ErrorCodes::HostUnreachable, "unknown curl handle failure"));
    }
}

    CURL* get() {
        return _handle;
    }

    void indicateSuccess() {
        _poolHandle->indicateSuccess();
        _success = true;
    }

private:
    executor::ConnectionPool::ConnectionHandle _poolHandle;
    bool _success = false;
    CURL* _handle;
};

class CurlHandleFactory {
public:
    CurlHandleFactory()
        : _typeFactory(std::make_shared<CurlHandleTypeFactory>()),
          _pool(std::make_shared<executor::ConnectionPool>(
              _typeFactory, "Curl", makePoolOptions(Seconds(60)))) {}

    CurlFactoryHandle get(HostAndPort server, HttpClient::Protocols protocol) {

        auto sslMode = mapProtocolToSSLMode(protocol);

        auto semi = _pool->get(server, sslMode, Seconds(60));
        // invariant(semi.isReady());

        StatusWith<executor::ConnectionPool::ConnectionHandle> swHandle =
            std::move(semi).getNoThrow();
        invariant(swHandle.isOK());
        // auto handle = std::move(swHandle.getValue());
        auto curlHandle = static_cast<PooledCurlHandle*>(swHandle.getValue().get())->get();

        // return CurlFactoryHandle{implPtr};
        return CurlFactoryHandle(std::move(swHandle.getValue()), curlHandle);
    }

private:
    // std::map<std::string, CurlHandle> _handles;
    std::shared_ptr<CurlHandleTypeFactory> _typeFactory;
    std::shared_ptr<executor::ConnectionPool> _pool;
};

CurlHandleFactory factory;

HostAndPort exactHostAndPortFromUrl(StringData url) {
    // Treat the URL as a host and port
    // URL: https://(host):(port)
    //
    constexpr StringData slashes = "//"_sd;
    auto slashesIndex = url.find(slashes);
    uassert(5413902, str::stream() << "//, URL: " << url, slashesIndex != std::string::npos);

    url = url.substr(slashesIndex + slashes.size());
    if (url.find("/") != std::string::npos) {
        url = url.substr(0, url.find("/"));
    }

    return HostAndPort(url);
}


class CurlHttpClient final : public HttpClient {
public:
    CurlHttpClient(Protocols protocol) {
        // Initialize a base handle with common settings.
        // Methods like requireHTTPS() will operate on this
        // base handle.

        // curl_easy_setopt(_handle.get(), CURLOPT_DEBUGFUNCTION , ???);
    }

    ~CurlHttpClient() {
        // _singleton.returnHandle(std::move(_handle));
    }

    // void allowInsecureHTTP(bool allow) final {
    //     if (allow) {
    //         curl_easy_setopt(_handle.get(), CURLOPT_PROTOCOLS, CURLPROTO_HTTPS | CURLPROTO_HTTP);
    //     } else {
    //         curl_easy_setopt(_handle.get(), CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
    //     }
    // }

    void setHeaders(const std::vector<std::string>& headers) final {
        // Can't set on base handle because cURL doesn't deep-dup this field
        // and we don't want it getting overwritten while another thread is using it.
        _headers = headers;
    }

    void setTimeout(Seconds timeout) final {
        _timeout = timeout;
    }

    void setConnectTimeout(Seconds timeout) final {
        _connectTimeout = timeout;
    }


    HttpReply request(HttpMethod method,
                      StringData url,
                      ConstDataRange cdr = {nullptr, 0}) const final {

        auto hp = exactHostAndPortFromUrl(url);
        CurlFactoryHandle _handle(factory.get(hp, HttpClient::Protocols::kHttpOrHttps));


        // CurlHandle _handle(curl_easy_duphandle(_handle.get()));
        uassert(ErrorCodes::InternalError, "Curl initialization failed", _handle.get());

        curl_easy_setopt(_handle.get(), CURLOPT_TIMEOUT, longSeconds(_timeout));

        curl_easy_setopt(_handle.get(), CURLOPT_CONNECTTIMEOUT, longSeconds(_connectTimeout));

        ConstDataRangeCursor cdrc(cdr);
        switch (method) {
            case HttpMethod::kGET:
                uassert(ErrorCodes::BadValue,
                        "Request body not permitted with GET requests",
                        cdr.length() == 0);
                break;
            case HttpMethod::kPOST:
                curl_easy_setopt(_handle.get(), CURLOPT_POST, 1);

                curl_easy_setopt(_handle.get(), CURLOPT_READFUNCTION, ReadMemoryCallback);
                curl_easy_setopt(_handle.get(), CURLOPT_READDATA, &cdrc);
                curl_easy_setopt(_handle.get(), CURLOPT_POSTFIELDSIZE, (long)cdrc.length());
                break;
            case HttpMethod::kPUT:
                curl_easy_setopt(_handle.get(), CURLOPT_PUT, 1);

                curl_easy_setopt(_handle.get(), CURLOPT_READFUNCTION, ReadMemoryCallback);
                curl_easy_setopt(_handle.get(), CURLOPT_READDATA, &cdrc);
                curl_easy_setopt(_handle.get(), CURLOPT_INFILESIZE_LARGE, (long)cdrc.length());
                break;
            default:
                MONGO_UNREACHABLE;
        }

        const auto urlString = url.toString();
        curl_easy_setopt(_handle.get(), CURLOPT_URL, urlString.c_str());

        DataBuilder dataBuilder(4096), headerBuilder(4096);
        curl_easy_setopt(_handle.get(), CURLOPT_WRITEDATA, &dataBuilder);
        curl_easy_setopt(_handle.get(), CURLOPT_HEADERDATA, &headerBuilder);

        curl_slist* chunk = curl_slist_append(nullptr, "Connection: keep-alive");
        for (const auto& header : _headers) {
            chunk = curl_slist_append(chunk, header.c_str());
        }
        curl_easy_setopt(_handle.get(), CURLOPT_HTTPHEADER, chunk);
        CurlSlist _headers(chunk);

        CURLcode result = curl_easy_perform(_handle.get());
        uassert(ErrorCodes::OperationFailed,
                str::stream() << "Bad HTTP response from API server: "
                              << curl_easy_strerror(result),
                result == CURLE_OK);

        long statusCode;
        result = curl_easy_getinfo(_handle.get(), CURLINFO_RESPONSE_CODE, &statusCode);
        uassert(ErrorCodes::OperationFailed,
                str::stream() << "Unexpected error retrieving response: "
                              << curl_easy_strerror(result),
                result == CURLE_OK);

        _handle.indicateSuccess();

        return HttpReply(statusCode, std::move(headerBuilder), std::move(dataBuilder));
    }

private:
    std::vector<std::string> _headers;

    Seconds _timeout;
    Seconds _connectTimeout;
};

}  // namespace

// Transitional API used by blockstore to trigger libcurl init
// until it's been migrated to use the HTTPClient API.
Status curlLibraryManager_initialize() {
    return curlLibraryManager.initialize();
}

std::unique_ptr<HttpClient> HttpClient::create(Protocols protocol) {
    uassertStatusOK(curlLibraryManager.initialize());
    return std::make_unique<CurlHttpClient>(protocol);
}

BSONObj HttpClient::getServerStatus() {

    BSONObjBuilder info;
    info.append("type", "curl");

    {
        BSONObjBuilder v(info.subobjStart("compiled"));
        v.append("version", LIBCURL_VERSION);
        v.append("version_num", LIBCURL_VERSION_NUM);
    }

    {
        auto* curl_info = curl_version_info(CURLVERSION_NOW);

        BSONObjBuilder v(info.subobjStart("running"));
        v.append("version", curl_info->version);
        v.append("version_num", static_cast<int>(curl_info->version_num));
    }

    return info.obj();
}

}  // namespace mongo
