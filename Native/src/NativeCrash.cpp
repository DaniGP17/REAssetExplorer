#include <windows.h>
#include <dbghelp.h>
#include <cxxabi.h>
#include <unwind.h>

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <exception>
#include <filesystem>
#include <string>
#include <typeinfo>
#include <vector>

#include "REAssetNative.h"

namespace {

constexpr int MAX_FRAMES = 48;
constexpr DWORD DUMP_TIMEOUT_MS = 120000;

std::wstring g_directory;
wchar_t g_logPath[MAX_PATH * 2] = L"";
HANDLE g_request = nullptr;
HANDLE g_done = nullptr;
DWORD g_dumpThreadId = 0;
HMODULE g_self = nullptr;
std::atomic<bool> g_claimed{ false };
LPTOP_LEVEL_EXCEPTION_FILTER g_previousFilter = nullptr;

// Filled by the crashing thread; the dump thread writes the files so a blown stack still gets them.
EXCEPTION_POINTERS* g_pointers = nullptr;
DWORD g_threadId = 0;
char g_reason[8192] = "";

std::wstring ToWide(const char* utf8) {
    int length = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    std::wstring wide(length > 0 ? length - 1 : 0, L'\0');
    if (length > 1) MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide.data(), length);
    return wide;
}

void Append(HANDLE file, const char* format, ...) {
    char line[2048];
    va_list args;
    va_start(args, format);
    int length = std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (length <= 0) return;
    DWORD written = 0;
    WriteFile(file, line, static_cast<DWORD>(std::min<int>(length, sizeof(line) - 1)), &written, nullptr);
}

// "module+0xRVA", or the bare address outside any module (JIT code).
void DescribeAddress(DWORD64 address, char* out, std::size_t size) {
    HMODULE module = nullptr;
    wchar_t path[MAX_PATH] = L"";
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(address), &module) &&
        GetModuleFileNameW(module, path, MAX_PATH)) {
        const wchar_t* name = std::wcsrchr(path, L'\\');
        std::snprintf(out, size, "%ls+0x%llx", name ? name + 1 : path,
                      static_cast<unsigned long long>(address - reinterpret_cast<DWORD64>(module)));
    } else {
        std::snprintf(out, size, "0x%016llx", static_cast<unsigned long long>(address));
    }
}

bool Readable(DWORD64 address) {
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) || info.State != MEM_COMMIT) return false;
    constexpr DWORD READABLE = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE;
    return (info.Protect & READABLE) != 0 && (info.Protect & PAGE_GUARD) == 0;
}

std::string Demangle(const char* name) {
    int status = 0;
    char* demangled = abi::__cxa_demangle(name, nullptr, nullptr, &status);
    if (demangled == nullptr) return name;
    std::string result = demangled;
    std::free(demangled);
    return result;
}

// libstdc++'s __cxa_exception (Itanium C++ ABI): the thrown object follows unwindHeader.
struct CxxExceptionHeader {
    std::type_info* exceptionType;
    void (*exceptionDestructor)(void*);
    void (*unexpectedHandler)();
    void (*terminateHandler)();
    CxxExceptionHeader* nextException;
    int handlerCount;
    int handlerSwitchValue;
    const unsigned char* actionRecord;
    const unsigned char* languageSpecificData;
    void* catchTemp;
    void* adjustedPtr;
    _Unwind_Exception unwindHeader;
};

constexpr DWORD STATUS_GCC_THROW = 0x20474343;

// Type and what() of a C++ exception that nothing caught.
std::string DescribeCxxException(const EXCEPTION_RECORD& record) {
    if (record.ExceptionCode != STATUS_GCC_THROW || record.NumberParameters < 1 || record.ExceptionInformation[0] == 0) return {};
    auto* unwind = reinterpret_cast<_Unwind_Exception*>(record.ExceptionInformation[0]);
    auto* header = reinterpret_cast<CxxExceptionHeader*>(reinterpret_cast<uint8_t*>(unwind) - offsetof(CxxExceptionHeader, unwindHeader));
    void* object = unwind + 1;
    std::string text = "Uncaught C++ exception " + Demangle(header->exceptionType->name());
    if (typeid(std::exception).__do_catch(header->exceptionType, &object, 1)) {
        text += std::string(": ") + static_cast<const std::exception*>(object)->what();
    }
    return text;
}

// Function symbols of this DLL's COFF symbol table (MinGW keeps it; there is no PDB), by RVA.
class OwnSymbols {
public:
    OwnSymbols() {
        wchar_t path[MAX_PATH];
        if (!GetModuleFileNameW(g_self, path, MAX_PATH)) return;
        HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return;
        HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        CloseHandle(file);
        if (!mapping) return;
        view = static_cast<const uint8_t*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
        CloseHandle(mapping);
        if (!view) return;
        auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(view);
        auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(view + dos->e_lfanew);
        const IMAGE_FILE_HEADER& header = nt->FileHeader;
        if (header.PointerToSymbolTable == 0) return;
        const IMAGE_SECTION_HEADER* sections = IMAGE_FIRST_SECTION(nt);
        auto* symbols = reinterpret_cast<const IMAGE_SYMBOL*>(view + header.PointerToSymbolTable);
        strings = reinterpret_cast<const char*>(symbols + header.NumberOfSymbols);
        for (DWORD i = 0; i < header.NumberOfSymbols; i += 1 + symbols[i].NumberOfAuxSymbols) {
            const IMAGE_SYMBOL& symbol = symbols[i];
            if (symbol.SectionNumber <= 0 || symbol.SectionNumber > header.NumberOfSections || !ISFCN(symbol.Type)) continue;
            functions.push_back({ sections[symbol.SectionNumber - 1].VirtualAddress + symbol.Value, &symbol });
        }
        std::sort(functions.begin(), functions.end(), [](const Function& a, const Function& b) { return a.rva < b.rva; });
    }
    ~OwnSymbols() {
        if (view) UnmapViewOfFile(view);
    }

    // "Name+0xOffset", or an empty string when no symbol precedes rva.
    std::string Describe(DWORD64 rva) const {
        auto it = std::upper_bound(functions.begin(), functions.end(), rva, [](DWORD64 value, const Function& f) { return value < f.rva; });
        if (it == functions.begin()) return {};
        --it;
        const IMAGE_SYMBOL& symbol = *it->symbol;
        auto* shortName = reinterpret_cast<const char*>(symbol.N.ShortName);
        std::string name = symbol.N.Name.Short != 0 ? std::string(shortName, strnlen(shortName, 8)) : std::string(strings + symbol.N.Name.Long);
        char offset[32];
        std::snprintf(offset, sizeof(offset), "+0x%llx", static_cast<unsigned long long>(rva - it->rva));
        return Demangle(name.c_str()) + offset;
    }

private:
    struct Function {
        DWORD64 rva;
        const IMAGE_SYMBOL* symbol;
    };
    const uint8_t* view = nullptr;
    const char* strings = nullptr;
    std::vector<Function> functions;
};

// Steps context to its caller's frame.
bool Unwind(CONTEXT& context) {
    DWORD64 imageBase = 0;
    PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
    if (!Readable(context.Rsp)) return false;
    if (function == nullptr) {
        // A leaf function: the return address is on top of the stack.
        context.Rip = *reinterpret_cast<DWORD64*>(context.Rsp);
        context.Rsp += 8;
        return true;
    }
    void* handlerData = nullptr;
    DWORD64 establisher = 0;
    RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context, &handlerData, &establisher, nullptr);
    return true;
}

HMODULE ModuleOf(DWORD64 address) {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(address), &module);
    return module;
}

bool IsManagedImage(HMODULE module) {
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const uint8_t*>(module) + dos->e_lfanew);
    return nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR].VirtualAddress != 0;
}

// .NET ends the process without calling the unhandled exception filter when a native fault unwinds
// into managed code, so those are caught first chance: faults whose frames pass through this DLL
// before reaching managed code (JIT code has no module). Managed null references never pass here.
bool ReachesManagedThroughThisDll(const CONTEXT& start) {
    CONTEXT context = start;
    bool throughThisDll = false;
    for (int frame = 0; frame < 256 && context.Rip != 0; frame++) {
        HMODULE module = ModuleOf(context.Rip);
        if (module == nullptr || IsManagedImage(module)) return throughThisDll;
        if (module == g_self) throughThisDll = true;
        if (!Unwind(context)) break;
    }
    return false;
}

bool IsFatal(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_PRIV_INSTRUCTION:
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_IN_PAGE_ERROR:
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        case 0xC0000374:
            return true;
        default:
            return false;
    }
}

const char* ExceptionName(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION: return "access violation";
        case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
        case EXCEPTION_IN_PAGE_ERROR: return "in-page error";
        case 0xC0000374: return "heap corruption";
        case 0x20474343: return "C++ exception";
        default: return "exception";
    }
}

void WriteReport(const std::wstring& path, const SYSTEMTIME& time, DWORD dumpError) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    Append(file, "REAssetExplorer crash %04u-%02u-%02u %02u:%02u:%02u\r\n", time.wYear, time.wMonth, time.wDay, time.wHour,
           time.wMinute, time.wSecond);
    Append(file, "Thread: %lu\r\n", g_threadId);
    if (g_logPath[0]) Append(file, "Log: %ls\r\n", g_logPath);
    if (dumpError != 0) Append(file, "Minidump: not written (error %lu)\r\n", dumpError);
    if (g_reason[0]) {
        DWORD written = 0;
        WriteFile(file, "\r\n", 2, &written, nullptr);
        WriteFile(file, g_reason, static_cast<DWORD>(std::strlen(g_reason)), &written, nullptr);
        WriteFile(file, "\r\n", 2, &written, nullptr);
    }
    if (g_pointers != nullptr) {
        const EXCEPTION_RECORD& record = *g_pointers->ExceptionRecord;
        char where[512];
        DescribeAddress(reinterpret_cast<DWORD64>(record.ExceptionAddress), where, sizeof(where));
        Append(file, "\r\nException: 0x%08lX (%s) at %s\r\n", record.ExceptionCode, ExceptionName(record.ExceptionCode), where);
        if ((record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION || record.ExceptionCode == EXCEPTION_IN_PAGE_ERROR) &&
            record.NumberParameters >= 2) {
            const char* access = record.ExceptionInformation[0] == 0 ? "reading" : record.ExceptionInformation[0] == 8 ? "executing" : "writing";
            Append(file, "Access: %s 0x%016llx\r\n", access, static_cast<unsigned long long>(record.ExceptionInformation[1]));
        }
        Append(file, "\r\nStack:\r\n");
        OwnSymbols symbols;
        CONTEXT context = *g_pointers->ContextRecord;
        for (int frame = 0; frame < MAX_FRAMES && context.Rip != 0; frame++) {
            DescribeAddress(context.Rip, where, sizeof(where));
            std::string function;
            if (ModuleOf(context.Rip) == g_self) function = symbols.Describe(context.Rip - reinterpret_cast<DWORD64>(g_self));
            Append(file, "  %-40s %s\r\n", where, function.c_str());
            if (!Unwind(context)) break;
        }
    }
    CloseHandle(file);
}

void WriteCrashFiles() {
    SYSTEMTIME time;
    GetLocalTime(&time);
    wchar_t stamp[32];
    std::swprintf(stamp, 32, L"%04u-%02u-%02u_%02u-%02u-%02u", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
                  time.wSecond);
    CreateDirectoryW(g_directory.c_str(), nullptr);
    std::wstring base = g_directory + L"\\" + stamp;

    DWORD dumpError = 0;
    HANDLE dump = CreateFileW((base + L".dmp").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (dump == INVALID_HANDLE_VALUE) {
        dumpError = GetLastError();
    } else {
        MINIDUMP_EXCEPTION_INFORMATION exception{ g_threadId, g_pointers, FALSE };
        // Full memory would copy every loaded texture; stacks, globals and what they point at are enough.
        auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs | MiniDumpWithIndirectlyReferencedMemory |
                                               MiniDumpWithThreadInfo | MiniDumpWithHandleData | MiniDumpWithUnloadedModules);
        if (!MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump, type, g_pointers ? &exception : nullptr, nullptr,
                               nullptr)) {
            dumpError = GetLastError();
        }
        CloseHandle(dump);
    }
    WriteReport(base + L".txt", time, dumpError);
}

DWORD WINAPI DumpThread(void*) {
    WaitForSingleObject(g_request, INFINITE);
    WriteCrashFiles();
    SetEvent(g_done);
    return 0;
}

// Only the first crash is written; any later one waits for it.
void Capture(EXCEPTION_POINTERS* pointers, const char* reason) {
    if (g_request == nullptr || GetCurrentThreadId() == g_dumpThreadId) return;
    if (g_claimed.exchange(true)) {
        WaitForSingleObject(g_done, DUMP_TIMEOUT_MS);
        return;
    }
    g_pointers = pointers;
    g_threadId = GetCurrentThreadId();
    if (reason) std::snprintf(g_reason, sizeof(g_reason), "%s", reason);
    SetEvent(g_request);
    WaitForSingleObject(g_done, DUMP_TIMEOUT_MS);
}

LONG CALLBACK FirstChanceHandler(EXCEPTION_POINTERS* pointers) {
    DWORD code = pointers->ExceptionRecord->ExceptionCode;
    // A stack overflow always ends a .NET process, and there is too little stack left to walk it.
    if (code == EXCEPTION_STACK_OVERFLOW || (IsFatal(code) && ReachesManagedThroughThisDll(*pointers->ContextRecord))) {
        Capture(pointers, nullptr);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS* pointers) {
    std::string cxx = DescribeCxxException(*pointers->ExceptionRecord);
    Capture(pointers, cxx.empty() ? nullptr : cxx.c_str());
    return g_previousFilter ? g_previousFilter(pointers) : EXCEPTION_CONTINUE_SEARCH;
}

void OnTerminate() {
    std::string reason = "std::terminate";
    if (std::exception_ptr current = std::current_exception()) {
        try {
            std::rethrow_exception(current);
        } catch (const std::exception& e) {
            reason += std::string(": uncaught exception: ") + e.what();
        } catch (...) {
            reason += ": uncaught exception of unknown type";
        }
    }
    Capture(nullptr, reason.c_str());
    std::abort();
}

void OnAbort(int) {
    Capture(nullptr, "abort()");
}

}

RAE_API void rae_install_crash_handler(const char* crashDir, const char* logPath) {
    if (g_request != nullptr) return;
    g_directory = ToWide(crashDir);
    std::error_code ignored;
    std::filesystem::create_directories(g_directory, ignored);
    std::swprintf(g_logPath, sizeof(g_logPath) / sizeof(g_logPath[0]), L"%ls", ToWide(logPath ? logPath : "").c_str());
    g_request = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE thread = CreateThread(nullptr, 0, DumpThread, nullptr, 0, &g_dumpThreadId);
    if (thread) CloseHandle(thread);
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&rae_install_crash_handler), &g_self);
    g_previousFilter = SetUnhandledExceptionFilter(UnhandledFilter);
    AddVectoredExceptionHandler(1, FirstChanceHandler);
    std::set_terminate(OnTerminate);
    std::signal(SIGABRT, OnAbort);
}

RAE_API void rae_write_crash_report(const char* reason) {
    Capture(nullptr, reason);
}
