
#define WIN32_LEAN_AND_MEAN  // Exclude rarely-used stuff from Windows headers

#include <windows.h>

// Windows Header Files:
#include <msiquery.h>
#include <strsafe.h>  // TODO: we should use this

// WiX Header Files:
#include <assert.h>
#include <memory>
#include <string>
#include <vector>

using namespace std;

/*
<Directory Id = "MONGO_DATA_PATH" Name = "data" / >
<Directory Id = "MONGO_LOG_PATH" Name = "log" / >
*/

std::string toUtf8String(const std::wstring& wide) {
    if (wide.size() == 0)
        return "";

    // Calculate necessary buffer size
    int len = ::WideCharToMultiByte(
        CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), NULL, 0, NULL, NULL);

    // Perform actual conversion
    if (len > 0) {
        std::vector<char> buffer(len);
        len = ::WideCharToMultiByte(CP_UTF8,
                                    0,
                                    wide.c_str(),
                                    static_cast<int>(wide.size()),
                                    &buffer[0],
                                    static_cast<int>(buffer.size()),
                                    NULL,
                                    NULL);
        if (len > 0) {
            // verify(len == static_cast<int>(buffer.size()));
            return std::string(&buffer[0], buffer.size());
        }
    }

    assert(0);
    return "";
}


constexpr wchar_t kwBIN[] = L"BIN";

// constexpr char kMongoConfigYaml[] = "MONGO_CONFIG_YAML";
// constexpr wchar_t kwMongoConfigYaml[] = L"MONGO_CONFIG_YAML";

constexpr char kMongoDataPath[] = "%MONGO_DATA_PATH%";
constexpr wchar_t kwMongoDataPath[] = L"MONGO_DATA_PATH";

constexpr char kMongoLogPath[] = "%MONGO_LOG_PATH%";
constexpr wchar_t kwMongoLogPath[] = L"MONGO_LOG_PATH";

HRESULT LogMessage(MSIHANDLE hInstall, INSTALLMESSAGE eMessageType, char const* format, ...) {
    va_list args;
    char buf[4096];

    va_start(args, format);
    vsnprintf_s(buf, 4096, format, args);
    va_end(args);

    PMSIHANDLE hRecord = ::MsiCreateRecord(1);

    if (NULL != hRecord) {
        HRESULT hr = ::MsiRecordSetStringA(hRecord, 0, buf);
        if (SUCCEEDED(hr)) {
            return ::MsiProcessMessage(hInstall, eMessageType, hRecord);
        }

        return hr;
    }

    return E_FAIL;
}


string do_replace(MSIHANDLE hInstall, string source, string original, string replacement) {
    int pos = source.find(original, 0);

    if (pos == string::npos) {
        LogMessage(hInstall,
                   INSTALLMESSAGE_WARNING,
                   "Failed to find '%s' in '%s'",
                   original.c_str(),
                   source.c_str());
    }

    return source.replace(pos, original.length(), replacement);
}

#define CHECKHR_AND_LOG(hr, ...)                                                 \
    if (!SUCCEEDED(hr)) {                                                        \
        LogMessage(hInstall, INSTALLMESSAGE_WARNING, "Received HRESULT %x", hr); \
        LogMessage(hInstall, INSTALLMESSAGE_WARNING, __VA_ARGS__);               \
        goto Exit;                                                               \
    }

#define CHECKGLE_AND_LOG(...)                                                           \
    {                                                                                   \
        LONG _gle = GetLastError();                                                     \
        LogMessage(hInstall, INSTALLMESSAGE_WARNING, "Received GetLastError %x", _gle); \
        LogMessage(hInstall, INSTALLMESSAGE_WARNING, __VA_ARGS__);                      \
        hr = E_FAIL;                                                                    \
        goto Exit;                                                                      \
    }

#define LOG_INFO(...) \
    { LogMessage(hInstall, INSTALLMESSAGE_INFO, __VA_ARGS__); }


HRESULT GetProperty(MSIHANDLE hInstall, WCHAR* pwszName, wstring* outString) {

    DWORD size = 0;
    WCHAR emptyString[1] = L"";

    UINT ret = MsiGetPropertyW(hInstall, pwszName, emptyString, &size);

    if (ret != ERROR_MORE_DATA) {
        LogMessage(hInstall,
                   INSTALLMESSAGE_WARNING,
                   "Received UINT %x during GetProperty size check",
                   ret);
        return E_FAIL;
    }

    ++size;  // bump for null terminator

    unique_ptr<wchar_t[]> buf(new wchar_t[size]);

    ret = MsiGetPropertyW(hInstall, pwszName, buf.get(), &size);
    if (ret != ERROR_SUCCESS) {
        LogMessage(hInstall, INSTALLMESSAGE_WARNING, "Received UINT %x during GetProperty", ret);
        return E_FAIL;
    }

    *outString = wstring(buf.get());

    return S_OK;
}

/*
TODO
----
Validate user name and password
Validate service name does not exist - it replaces, not errors
ACL directories???
*/

extern "C" UINT __stdcall UpdateMongoYAML(MSIHANDLE hInstall) {
    HRESULT hr = S_OK;

    try {
        wstring customData;
        hr = GetProperty(hInstall,
                         const_cast<wchar_t*>(static_cast<const wchar_t*>(L"CustomActionData")),
                         &customData);
        CHECKHR_AND_LOG(hr, "Failed to get CustomActionData property");

        LOG_INFO("CA - Custom Data = %ls", customData.c_str());

        wstring binPath;
        wstring dataDir;
        wstring logDir;

        int start = 0;
        while (true) {
            int pos = customData.find(';', start);
            if (pos == std::wstring::npos) {
                pos = customData.size();
            }

            std::wstring term = customData.substr(start, pos - start - 1);
            int equals = term.find('=');
            if (equals == std::wstring::npos) {
                LOG_INFO("CA - Error searching = %ls", term.c_str());
            }

            std::wstring keyword = term.substr(0, equals);
            std::wstring value = term.substr(equals + 1);

            if (keyword == kwBIN) {
                binPath = value;
            } else if (keyword == kwMongoDataPath) {
                dataDir = value;
            } else if (keyword == kwMongoLogPath) {
                logDir = value;
            }

            if (pos == customData.size()) {
                break;
            }

            start = pos + 1;
        }

        wstring YamlFile(binPath);
        YamlFile += L"\\mongod.cfg";


        LOG_INFO("CA - BIN = %ls", binPath.c_str());

        LOG_INFO("CA - MONGO_DATA_PATH = %ls", dataDir.c_str());

        LOG_INFO("CA - MONGO_LOG_PATH = %ls", logDir.c_str());


        LOG_INFO("CA - YAML_FILE = %ls", YamlFile.c_str());


        long gle = GetFileAttributesW(YamlFile.c_str());
        if (gle == INVALID_FILE_ATTRIBUTES) {
            CHECKGLE_AND_LOG("Failed to find yaml file");
        }

        // TODO: Auto Close handle
        HANDLE hFile = CreateFileW(YamlFile.c_str(),
                                   (GENERIC_READ | GENERIC_WRITE),
                                   0,
                                   NULL,
                                   OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL,
                                   NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            CHECKGLE_AND_LOG("Failed to open yaml file");
        }

        LARGE_INTEGER fileSize;
        if (GetFileSizeEx(hFile, &fileSize) == 0) {
            CHECKGLE_AND_LOG("Failed to get size of yaml file");
        }

        LOG_INFO("CA - Allocating - %lld bytes", fileSize.QuadPart);

        size_t bufSize = static_cast<size_t>(fileSize.QuadPart + 1);

        LOG_INFO("CA - Allocating - %d bytes", bufSize);


        std::unique_ptr<char> buf(new char[bufSize]);

        LOG_INFO("CA - Reading file - %d bytes", bufSize);

        DWORD read;
        if (!ReadFile(hFile, (void*)buf.get(), bufSize, &read, NULL)) {
            CHECKGLE_AND_LOG("Failed to read yaml file");
        }

        buf.get()[read] = '\0';

        LOG_INFO("CA - Reading file - '%s'", buf.get());

        LOG_INFO("CA - Doing string subsitutions");

        string str(buf.get());

        // Do the string subsitutions
        str = do_replace(hInstall, str, kMongoDataPath, toUtf8String(dataDir));
        str = do_replace(hInstall, str, kMongoLogPath, toUtf8String(logDir));

        LOG_INFO("CA - Writing file - '%s'", buf.get());

        SetFilePointer(hFile, 0, 0, SEEK_SET);

        DWORD written;
        if (!WriteFile(hFile, str.c_str(), str.length(), &written, NULL)) {
            CHECKGLE_AND_LOG("Failed to write yaml file");
        }

        CloseHandle(hFile);
    } catch (exception& e) {
        CHECKHR_AND_LOG(E_FAIL, "Caught C++ exception %s", e.what());
    } catch (...) {
        CHECKHR_AND_LOG(E_FAIL, "Caught C++ exception");
    }

Exit:
    return SUCCEEDED(hr) ? ERROR_SUCCESS : ERROR_INSTALL_FAILURE;
}


// DllMain - Initialize and cleanup WiX custom action utils.
extern "C" BOOL WINAPI DllMain(__in HINSTANCE hInst, __in ULONG ulReason, __in LPVOID) {
    switch (ulReason) {
        case DLL_PROCESS_ATTACH:
            break;

        case DLL_PROCESS_DETACH:
            break;
    }

    return TRUE;
}
