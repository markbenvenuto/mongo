#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kFTDC


#include "mongo/platform/basic.h"


#include "mongo/db/free_mon/free_monitoring_http.h"

#include <curl/curl.h>
#include <curl/easy.h>
#include <iterator>
#include <vector>

#include "mongo/base/init.h"
#include "mongo/base/data_builder.h"
#include "mongo/base/data_range_cursor.h"
#include "mongo/platform/atomic_word.h"
#include "mongo/stdx/memory.h"
#include "mongo/util/exit.h"
#include "mongo/util/scopeguard.h"
#include "mongo/util/time_support.h"
#include "mongo/util/log.h"
#include "mongo/util/concurrency/thread_pool.h"
#include "mongo/executor/task_executor.h"

namespace mongo {

namespace {
class CurlLibraryManager {
public:
    ~CurlLibraryManager() {
        if (_initialized.load()) {
            curl_global_cleanup();
        }
    }

    // initializes curl, idempotent
    void initialize() {
        if (_initialized.compareAndSwap(false, true) == false) {
            curl_global_init(CURL_GLOBAL_ALL);
        }
    }

private:
    AtomicWord<bool> _initialized;
};


size_t WriteMemoryCallback(void* ptr, size_t size, size_t nmemb, void* data) {
    size_t realsize = size * nmemb;

    DataBuilder* mem = reinterpret_cast<DataBuilder*>(data);
    if (!mem->writeAndAdvance(ConstDataRange(reinterpret_cast<const char*>(ptr),
        reinterpret_cast<const char*>(ptr) + realsize))
        .isOK()) {
        // Cause curl to generate a CURLE_WRITE_ERROR by returning a different number than how much
        // data there was to write.
        return 0;
    }

    return realsize;
}

size_t ReadMemoryCallback(char *buffer, size_t size, size_t nitems, void *instream) {

    ConstDataRangeCursor* cdrc = reinterpret_cast<ConstDataRangeCursor*>(instream);

    size_t ret = 0;

    if (cdrc->length() > 0) {
        size_t  readSize = std::min(size * nitems, cdrc->length());
        memcpy(buffer, cdrc->data(), readSize);
        invariant(cdrc->advance(readSize).isOK());
        ret = readSize;
    }

    return ret;

}


CurlLibraryManager curlLibraryManager;

class FreeMonitoringCurlHttpClient : public FreeMonitoringHttpClientInterface {
public:
    FreeMonitoringCurlHttpClient(std::unique_ptr<executor::ThreadPoolTaskExecutor> executor) :
        _executor(std::move(executor)) {}
    virtual ~FreeMonitoringCurlHttpClient() {};

    Future<std::vector<uint8_t>> postAsync(StringData url, const BSONObj obj) override {

        Promise<std::vector<uint8_t>> promise;
        auto future = promise.getFuture();

        auto shared_promise = promise.share();

        std::string urlString(url.toString());

        //std::unique_ptr<executor::ThreadPoolTaskExecutor> foo;

        auto status = _executor->scheduleWork([shared_promise, urlString, obj](const executor::TaskExecutor::CallbackArgs& cbArgs) mutable {

            ConstDataRange data(obj.objdata(), obj.objdata() + obj.objsize());

            LOG(0) << "Posting data to (" << data.length() << "): " << urlString;

            ConstDataRangeCursor cdrc(data);

            std::unique_ptr<CURL, void(*)(CURL*)> myHandle(curl_easy_init(), curl_easy_cleanup);

            if (!myHandle) {
                shared_promise.setError({ErrorCodes::InternalError, "Curl initialization failed"});
                return;
            }
            curl_easy_setopt(myHandle.get(), CURLOPT_URL, urlString.c_str());
            curl_easy_setopt(myHandle.get(), CURLOPT_POST, 1);

            curl_easy_setopt(myHandle.get(), CURLOPT_PROTOCOLS, CURLPROTO_HTTPS | CURLPROTO_HTTP);
            curl_easy_setopt(myHandle.get(), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);



            // TODO: just use std::vector instead
            DataBuilder dataBuilder(4096);

            curl_easy_setopt(myHandle.get(), CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
            curl_easy_setopt(myHandle.get(), CURLOPT_WRITEDATA, &dataBuilder);

            curl_easy_setopt(myHandle.get(), CURLOPT_READFUNCTION, ReadMemoryCallback);
            curl_easy_setopt(myHandle.get(), CURLOPT_READDATA, &cdrc);
            curl_easy_setopt(myHandle.get(), CURLOPT_POSTFIELDSIZE, (long)cdrc.length());

            // CURLOPT_EXPECT_100_TIMEOUT_MS??
            curl_easy_setopt(myHandle.get(), CURLOPT_CONNECTTIMEOUT, 60);
            curl_easy_setopt(myHandle.get(), CURLOPT_TIMEOUT, 120);
            // Requires >= 7.34.0
            // curl_easy_setopt(myHandle.get(), CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
            curl_easy_setopt(myHandle.get(), CURLOPT_FOLLOWLOCATION, 0);

            curl_easy_setopt(myHandle.get(), CURLOPT_NOSIGNAL, 1);
            //             If server log level > 3

            curl_easy_setopt(myHandle.get(), CURLOPT_VERBOSE, 1);
            //curl_easy_setopt(myHandle.get(), CURLOPT_DEBUGFUNCTION , ???);

            {
                struct curl_slist *chunk = NULL;

                chunk = curl_slist_append(chunk, "Content-Type: application/octet-stream");
                chunk = curl_slist_append(chunk, "Accept: application/octet-stream");
                chunk = curl_slist_append(chunk, "Expect:");
                curl_easy_setopt(myHandle.get(), CURLOPT_HTTPHEADER, chunk);
                /* TODO use curl_slist_free_all() after the *perform() call to free this
                list again */
            }


            CURLcode result = curl_easy_perform(myHandle.get());
            if (result != CURLE_OK) {
                shared_promise.setError({ErrorCodes::OperationFailed, str::stream() << "Bad HTTP response from API server: "
                    << curl_easy_strerror(result)});
                return;
            }

            auto d = dataBuilder.getCursor();
            shared_promise.emplaceValue(std::vector<uint8_t>(d.data(), d.data() + d.length()));

            return;
        });

        uassertStatusOK(status);
        //std::vector<int> kBackoffSleepDurations{1, 5, 10, 15, 20, 25, 30};

        //for (std::size_t attempt = 0; attempt < kBackoffSleepDurations.size(); ++attempt) {
        //    std::unique_ptr<CURL, void(*)(CURL*)> myHandle(curl_easy_init(), curl_easy_cleanup);
        //    if (!myHandle) {
        //        return{ErrorCodes::InternalError, "Curl initialization failed"};
        //    }

        //    curl_easy_setopt(myHandle.get(), CURLOPT_URL, url.c_str());

        //    struct curl_slist* list = nullptr;
        //    const auto guard = MakeGuard([&] {
        //        if (list)
        //            curl_slist_free_all(list);
        //    });
        //    list = curl_slist_append(list, secretHeader.c_str());

        //    DataBuilder data(count);
        //    curl_easy_setopt(myHandle.get(), CURLOPT_HTTPHEADER, list);
        //    curl_easy_setopt(myHandle.get(), CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        //    curl_easy_setopt(myHandle.get(), CURLOPT_WRITEDATA, &data);

        //    CURLcode result = curl_easy_perform(myHandle.get());
        //    if (result != CURLE_OK) {
        //        lastErr = str::stream() << "Bad HTTP response from API server: "
        //            << curl_easy_strerror(result);
        //        sleepsecs(kBackoffSleepDurations[attempt]);
        //        continue;
        //    }

        //    uassertStatusOK(buf.write(data.getCursor()));

        //    return{data.size()};
        //}

        return future;
    }
private:
    std::unique_ptr<executor::ThreadPoolTaskExecutor> _executor;
};

}  // namespace


std::unique_ptr<FreeMonitoringHttpClientInterface> createFreeMonHttpClient(std::unique_ptr<executor::ThreadPoolTaskExecutor> executor) {
    curlLibraryManager.initialize();
    return std::make_unique<FreeMonitoringCurlHttpClient>(std::move(executor));
}

} // namespace mongo