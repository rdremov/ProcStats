#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#include <windows.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <chrono>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

static std::string utf16_to_utf8(const wchar_t* utf16, int len) {
    if (!utf16)
        return "";
    if (len < 0)
        len = (int)wcslen(utf16);
    if (!len)
        return "";
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, utf16, len, NULL, 0, NULL, NULL);
    std::string utf8(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, utf16, len, &utf8[0], size_needed, NULL, NULL);
    return utf8;
}

static std::string get_process_str(HANDLE hProcess, const void* pstr, int len, bool unicode) {
    std::string str;
    if (pstr) {
        char buf[4096];
        size_t size;
        if (len < 0)
            size = sizeof(buf);
        else
            size = len;
        assert(size <= sizeof(buf));
        if (ReadProcessMemory(hProcess, pstr, buf, size, NULL))
            if (unicode)
                str = utf16_to_utf8((const wchar_t*)buf, len);
            else
                str.assign(buf, size);
    }
    return str;
}

static std::string get_image_str(void* image, bool unicode, HANDLE hProcess) {
    std::string str;
    if (image) {
        void* ptr{};
        if (ReadProcessMemory(hProcess, image, &ptr, sizeof(void*), NULL))
            str = get_process_str(hProcess, ptr, -1, unicode);
    }
    return str;
}


float time_stamp() {
    static std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    auto t = std::chrono::steady_clock::now();
    std::chrono::duration<float> elapsed_seconds = t - start;
    return elapsed_seconds.count();
}

class ProcStats {
    struct INFO {
        enum TYPE {PROCESS, THREAD, DLL, OUTPUT, RIP, EXCEPTION};

        TYPE type;
        DWORD pid;
        DWORD tid;
        std::string str;
        void* ptr;
        float start;
        float finish{};

        INFO(DWORD processId, void* base)
            : type(PROCESS), pid(processId), tid(0), ptr(base), start(time_stamp()) {
        }

        INFO(DWORD processId, DWORD threadId, void* base)
            : type(THREAD), pid(processId), tid(threadId), ptr(base), start(time_stamp()) {
        }

        INFO(DWORD processId, DWORD threadId, void* base, std::string name)
            : type(DLL), pid(processId), tid(threadId), str(name), ptr(base), start(time_stamp()) {
        }

        INFO(DWORD processId, DWORD threadId, std::string output)
            : type(OUTPUT), pid(processId), tid(threadId), str(output), ptr(nullptr), start(time_stamp()) {
        }

        INFO(DWORD processId, DWORD threadId, DWORD error, DWORD type)
            : type(RIP), pid(processId), tid(threadId), ptr(nullptr), start(time_stamp()) {
            str = "error" + std::to_string(error) + "_type" + std::to_string(type);
        }

        INFO(DWORD processId, DWORD threadId, void* addr, DWORD code)
            : type(EXCEPTION), pid(processId), tid(threadId), ptr(addr), start(time_stamp()) {
            str = "code" + std::to_string(code);
        }
    };

    std::vector<INFO> infos_;
    std::unordered_map<DWORD_PTR, size_t> id2index_;
    std::unordered_map<DWORD, HANDLE> pid2handle_;
    int unknownDllIndex{};

public:
    ProcStats() {
        time_stamp();
    }

    void print(const char* outFile) {
        auto finish = time_stamp();
        static const char* typeStrs[] = {"PROCESS", "THREAD", "DLL", "OUTPUT", "RIP", "EXCEPTION"};
        printf("Stats %g seconds %d infos:\n", finish, (int)infos_.size());
        std::string str;
        FILE* pf = fopen(outFile, "w");
        for(const auto& info : infos_) {
            str = info.str;
            std::replace(str.begin(), str.end(), '\n', ' ');
            std::replace(str.begin(), str.end(), '\r', ' ');
            fprintf(pf, "%s %d %d %g %g \"%s\"\n",
                typeStrs[info.type], info.pid, info.tid, info.start, info.finish, str.c_str());
        }
        fclose(pf);
    }

    void onCreateProcess(const CREATE_PROCESS_DEBUG_INFO& debugInfo, DWORD processId) {
        auto id = GetProcessId(debugInfo.hProcess);
        assert(id == processId);
        auto index = infos_.size();
        id2index_[processId] = index;
        infos_.emplace_back(processId, debugInfo.lpBaseOfImage);
        pid2handle_[processId] = debugInfo.hProcess;
    }

    auto onExitProcess(const EXIT_PROCESS_DEBUG_INFO& debugInfo, DWORD processId) {
        auto index = id2index_[processId];
        infos_[index].finish = time_stamp();
        pid2handle_.erase(processId);
        return pid2handle_.empty();
    }

    void onCreateThread(const CREATE_THREAD_DEBUG_INFO& debugInfo, DWORD processId, DWORD threadId) {
        auto id = GetThreadId(debugInfo.hThread);
        assert(id == threadId);
        auto index = infos_.size();
        id2index_[threadId] = index;
        infos_.emplace_back(processId, threadId, debugInfo.lpThreadLocalBase);
    }

    void onExitThread(const EXIT_THREAD_DEBUG_INFO& debugInfo, DWORD processId, DWORD threadId) {
        auto index = id2index_[threadId];
        infos_[index].finish = time_stamp();
    }

    void onLoadDll(const LOAD_DLL_DEBUG_INFO& debugInfo, DWORD processId, DWORD threadId) {
        auto index = infos_.size();
        id2index_[(DWORD_PTR)debugInfo.lpBaseOfDll] = index;
        auto name = get_image_str(debugInfo.lpImageName, debugInfo.fUnicode, pid2handle_[processId]);
        if (name.empty())
            name = "?" + std::to_string(++unknownDllIndex);
        infos_.emplace_back(processId, threadId, debugInfo.lpBaseOfDll, name);
    }
    
    void onUnloadDll(const UNLOAD_DLL_DEBUG_INFO& debugInfo, DWORD processId, DWORD threadId) {
        auto index = id2index_[(DWORD_PTR)debugInfo.lpBaseOfDll];
        infos_[index].finish = time_stamp();
    }

    void onOutputString(const OUTPUT_DEBUG_STRING_INFO& debugInfo, DWORD processId, DWORD threadId) {
        auto output = get_process_str(pid2handle_[processId], debugInfo.lpDebugStringData,
            debugInfo.nDebugStringLength, debugInfo.fUnicode);
        infos_.emplace_back(processId, threadId, output);
    }

    void onRIP(const RIP_INFO& debugInfo, DWORD processId, DWORD threadId) {
        infos_.emplace_back(processId, threadId, debugInfo.dwError, debugInfo.dwType);
    }

    auto onException(const EXCEPTION_DEBUG_INFO& debugInfo, DWORD processId, DWORD threadId) {
        auto code = debugInfo.ExceptionRecord.ExceptionCode;
        infos_.emplace_back(processId, threadId, debugInfo.ExceptionRecord.ExceptionAddress, code);
        auto status = DBG_EXCEPTION_NOT_HANDLED;
        if (code == EXCEPTION_BREAKPOINT)
            status = DBG_CONTINUE;
        return status;
    }
};

int main(int argc, char *argv[]) {

    if (argc != 3) {
        printf("USE: ProcStats.exe log.txt \"c:\\full\\path\\myProcess.exe\"\n");
        return 1;
    }

    const char* outFile = argv[1];
    auto cmd = argv[2];

    STARTUPINFOA si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(nullptr, cmd, nullptr, nullptr, false,
        DEBUG_PROCESS, nullptr, nullptr, &si, &pi)) {
        printf("CreateProcess failed error=%d cmd=%s\n", GetLastError(), cmd);
        return 1;
    }

    ProcStats stats;
    bool stop{};

    for (int n=1; !stop; ++n) {
        DWORD status = DBG_CONTINUE;
        DEBUG_EVENT e{};
        if (!WaitForDebugEvent(&e, INFINITE))
            break;

        printf("event %d\r", n);

        switch (e.dwDebugEventCode) {
        case EXCEPTION_DEBUG_EVENT:
            status = stats.onException(e.u.Exception, e.dwProcessId, e.dwThreadId);
            break;
        case CREATE_THREAD_DEBUG_EVENT:
            stats.onCreateThread(e.u.CreateThread, e.dwProcessId, e.dwThreadId);
            break;
        case CREATE_PROCESS_DEBUG_EVENT:
            stats.onCreateProcess(e.u.CreateProcessInfo, e.dwProcessId);
            break;
        case EXIT_THREAD_DEBUG_EVENT:
            stats.onExitThread(e.u.ExitThread, e.dwProcessId, e.dwThreadId);
            break;
        case EXIT_PROCESS_DEBUG_EVENT:
            stop = stats.onExitProcess(e.u.ExitProcess, e.dwProcessId);
            break;
        case LOAD_DLL_DEBUG_EVENT:
            stats.onLoadDll(e.u.LoadDll, e.dwProcessId, e.dwThreadId);
            break;
        case UNLOAD_DLL_DEBUG_EVENT:
            stats.onUnloadDll(e.u.UnloadDll, e.dwProcessId, e.dwThreadId);
            break;
        case OUTPUT_DEBUG_STRING_EVENT:
            stats.onOutputString(e.u.DebugString, e.dwProcessId, e.dwThreadId);
            break;
        case RIP_EVENT:
            stats.onRIP(e.u.RipInfo, e.dwProcessId, e.dwThreadId);
            break;
        }

        ContinueDebugEvent(e.dwProcessId, e.dwThreadId, status);
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    printf("\n");
    stats.print(outFile);
    return 0;
}
