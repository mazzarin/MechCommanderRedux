// Built without the precompiled header: MCCore's Win32 stand-ins (MCWin32Defs.h) and Windows.h cannot meet.
#include "MCCrashTrace.h"
#include "MCConsole.h"

#include <algorithm>
#include <array>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <memory>
#include <span>
#include <string_view>

#include "platform/MCNeverDestroyed.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <DbgHelp.h>
#include <crtdbg.h>

#pragma comment(lib, "dbghelp.lib")

namespace
{
    /// <summary>
    /// Closes a Win32 handle (a thread, an event, a file), unless it is one the process only borrows or a failed call's
    /// INVALID_HANDLE_VALUE.
    /// </summary>
    struct MCHandleCloser
    {
        /// <summary>Whether the handle is closed with the owner (stderr's handle is not).</summary>
        bool Owned = true;

        void operator()(HANDLE handle) const
        {
            if (Owned && handle != INVALID_HANDLE_VALUE)
            {
                CloseHandle(handle);
            }
        }
    };

    /// <summary>A Win32 handle owned alone, closed with the owner.</summary>
    using MCWin32Handle = std::unique_ptr<void, MCHandleCloser>;

    /// <summary>Whether <paramref name="handle"/> holds an open object (a failed call gives null or INVALID_HANDLE_VALUE).</summary>
    bool IsOpen(const MCWin32Handle& handle)
    {
        return handle != nullptr && handle.get() != INVALID_HANDLE_VALUE;
    }

    /// <summary>What the handler keeps from <see cref="MCCrashTrace::Install"/> to the crash.</summary>
    struct MCCrashState
    {
        /// <summary>The game-state printer set by <see cref="MCCrashTrace::SetReporter"/>, if any.</summary>
        std::function<void()> Reporter;

        /// <summary>
        /// The thread that writes the report, made at <see cref="MCCrashTrace::Install"/> so a crash needs no stack,
        /// heap or thread creation of its own: the crashed thread may have overflowed its stack or broken the heap.
        /// </summary>
        MCWin32Handle WorkerThread;

        /// <summary>Set by the crashed thread to start the worker.</summary>
        MCWin32Handle CrashEvent;

        /// <summary>Set by the worker when the report and the dump are written.</summary>
        MCWin32Handle DoneEvent;

        /// <summary>Where the report goes: stderr's handle (borrowed), or MCRedux.crash.txt; empty when nowhere.</summary>
        MCWin32Handle ReportFile;
    };

    /// <summary>
    /// The handler's state, never destroyed: the worker waits on its events while the process ends, and a crash in a
    /// static destructor still reaches it.
    /// </summary>
    MCCrashState& State()
    {
        static MCNeverDestroyed<MCCrashState> state;
        return *state;
    }

    /// <summary>The crash being reported, copied for the worker: its record and the crashed thread's registers.</summary>
    EXCEPTION_RECORD CrashRecord{};

    /// <summary>See <see cref="CrashRecord"/>.</summary>
    alignas(16) CONTEXT CrashContext{};

    /// <summary>The id of the thread that crashed.</summary>
    DWORD CrashThreadId = 0;

    /// <summary>Nonzero once a thread has entered the handler; a second one waits for the first to end the process.</summary>
    volatile LONG Handling = 0;

    /// <summary>The report's path when it goes to a file, else empty.</summary>
    wchar_t ReportPath[MAX_PATH] = {};

    /// <summary>The reports' record's path (<see cref="MCCrashTrace::SetRecordPath"/>), else empty.</summary>
    wchar_t RecordPath[MAX_PATH] = {};

    /// <summary>Frames print as the record has them: a module's name and offset, the function if known.</summary>
    bool PlainFrames = false;

    /// <summary>Formats into a static buffer and writes it to the report, without touching the heap.</summary>
    template <typename... Args> void Print(std::format_string<Args...> format, Args&&... args)
    {
        static std::array<char, 4096> buffer;
        const auto result = std::format_to_n(buffer.begin(), buffer.size(), format, std::forward<Args>(args)...);
        const HANDLE file = State().ReportFile.get();

        if (result.size <= 0 || !IsOpen(State().ReportFile))
        {
            return;
        }

        const DWORD size = static_cast<DWORD>(std::min<ptrdiff_t>(result.size, buffer.size()));
        DWORD written = 0;
        WriteFile(file, buffer.data(), size, &written, nullptr);
    }

    /// <summary>
    /// <paramref name="text"/> in UTF-8, in a static buffer (cut at its size), for <see cref="Print"/>: no heap.
    /// </summary>
    std::string_view Narrow(std::wstring_view text)
    {
        static std::array<char, MAX_PATH * 3> buffer;
        const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), buffer.data(),
                                               static_cast<int>(buffer.size()), nullptr, nullptr);
        return std::string_view(buffer.data(), static_cast<size_t>(std::max(length, 0)));
    }

    /// <summary>
    /// Fills <paramref name="path"/> with the executable's path, its extension replaced by
    /// <paramref name="extension"/>. False when the path doesn't fit.
    /// </summary>
    bool GetPathBesideExecutable(wchar_t (&path)[MAX_PATH], std::wstring_view extension)
    {
        const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);

        if (length == 0 || length >= MAX_PATH)
        {
            return false;
        }

        const size_t dot = std::wstring_view(path, length).rfind(L'.');

        if (dot == std::wstring_view::npos || dot + extension.size() >= MAX_PATH)
        {
            return false;
        }

        const std::span<wchar_t, MAX_PATH> all(path);
        std::ranges::copy(extension, all.begin() + static_cast<ptrdiff_t>(dot));
        all[dot + extension.size()] = L'\0';
        return true;
    }

    /// <summary>
    /// Writes MCRedux.dmp beside the executable: threads, stacks and the data segments. Returns whether it was written;
    /// <paramref name="path"/> gets its path.
    /// </summary>
    bool WriteDump(wchar_t (&path)[MAX_PATH])
    {
        if (!GetPathBesideExecutable(path, L".dmp"))
        {
            return false;
        }

        const MCWin32Handle file(
            CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));

        if (!IsOpen(file))
        {
            return false;
        }

        EXCEPTION_POINTERS exception{&CrashRecord, &CrashContext};
        MINIDUMP_EXCEPTION_INFORMATION info{};
        info.ThreadId = CrashThreadId;
        info.ExceptionPointers = &exception;
        info.ClientPointers = FALSE;
        const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs | MiniDumpWithIndirectlyReferencedMemory |
                                                     MiniDumpWithThreadInfo);
        return MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file.get(), type, &info, nullptr,
                                 nullptr) != FALSE;
    }

    /// <summary>A SYMBOL_INFO with room for a name of MAX_SYM_NAME characters after it.</summary>
    struct MCSymbolBuffer
    {
        SYMBOL_INFO Info;
        std::array<char, MAX_SYM_NAME> Name;
    };

    /// <summary>Prints one stack frame: the function and source line when the PDB has them, else the module offset.</summary>
    void PrintFrame(int index, DWORD64 address)
    {
        static MCSymbolBuffer symbol;
        symbol.Info.SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol.Info.MaxNameLen = MAX_SYM_NAME;
        DWORD64 displacement = 0;
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD lineDisplacement = 0;
        const HANDLE process = GetCurrentProcess();
        const bool haveSymbol = SymFromAddr(process, address, &displacement, &symbol.Info) != FALSE;
        const bool haveLine = SymGetLineFromAddr64(process, address, &lineDisplacement, &line) != FALSE;

        if (PlainFrames)
        {
            IMAGEHLP_MODULE64 module{};
            module.SizeOfStruct = sizeof(module);
            const DWORD64 base = SymGetModuleBase64(process, address);
            const std::string_view name = SymGetModuleInfo64(process, address, &module) != FALSE
                                              ? std::string_view(module.ModuleName)
                                              : std::string_view("?");

            if (haveSymbol)
            {
                Print("{} +0x{:x} {} + 0x{:x}\n", name, address - base, std::string_view(symbol.Info.Name),
                      displacement);
            }
            else
            {
                Print("{} +0x{:x}\n", name, address - base);
            }

            return;
        }

        if (haveSymbol && haveLine)
        {
            Print("  #{:02} {}+0x{:x}  {}({})\n", index, std::string_view(symbol.Info.Name), displacement,
                  std::string_view(line.FileName), line.LineNumber);
        }
        else if (haveSymbol)
        {
            Print("  #{:02} {}+0x{:x}\n", index, std::string_view(symbol.Info.Name), displacement);
        }
        else
        {
            const DWORD64 base = SymGetModuleBase64(process, address);
            Print("  #{:02} 0x{:x} (module 0x{:x} + 0x{:x})\n", index, address, base, address - base);
        }
    }

    /// <summary>Prints the exception and the crashed thread's symbolized stack.</summary>
    void PrintStack()
    {
        const EXCEPTION_RECORD& record = CrashRecord;
        Print("\nCrash: exception 0x{:08x} at 0x{:016X} on thread {}", record.ExceptionCode,
              reinterpret_cast<uintptr_t>(record.ExceptionAddress), CrashThreadId);

        if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2)
        {
            Print(" ({} 0x{:x})", record.ExceptionInformation[0] == 1 ? "writing" : "reading",
                  record.ExceptionInformation[1]);
        }
        else if (record.ExceptionCode == EXCEPTION_STACK_OVERFLOW)
        {
            Print(" (stack overflow)");
        }

        Print("\n");

        const HANDLE process = GetCurrentProcess();
        const MCWin32Handle thread(OpenThread(THREAD_ALL_ACCESS, FALSE, CrashThreadId));
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        SymInitialize(process, nullptr, TRUE);

        static CONTEXT context;
        context = CrashContext;
        STACKFRAME64 frame{};
        frame.AddrPC.Offset = context.Rip;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = context.Rbp;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = context.Rsp;
        frame.AddrStack.Mode = AddrModeFlat;

        // A stack overflow is deep recursion: the top frames show where it loops, the bottom ones how it got there.
        const int maxFrames = record.ExceptionCode == EXCEPTION_STACK_OVERFLOW ? 4096 : 64;

        for (int index = 0; index < maxFrames; ++index)
        {
            if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread.get(), &frame, &context, nullptr,
                             SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
            {
                break;
            }

            if (frame.AddrPC.Offset == 0)
            {
                break;
            }

            PrintFrame(index, frame.AddrPC.Offset);
        }

        SymCleanup(process);
    }

    /// <summary>The stack as the record has it, guarded (its function holds no objects, as __try needs).</summary>
    void PrintPlainStack()
    {
        PlainFrames = true;

        __try
        {
            PrintStack();
        }

        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }

        PlainFrames = false;
    }

    /// <summary>Writes the reports' record, when there is a path for it, the report's file set aside meanwhile.</summary>
    void WriteRecord()
    {
        if (RecordPath[0] == L'\0')
        {
            return;
        }

        MCWin32Handle report = std::move(State().ReportFile);
        State().ReportFile = MCWin32Handle(CreateFileW(RecordPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));

        if (IsOpen(State().ReportFile))
        {
            PrintPlainStack();
        }

        State().ReportFile = std::move(report);
    }

    /// <summary>Runs the reporter, if any, its stderr going where the report goes.</summary>
    void RunReporter()
    {
        if (!State().Reporter)
        {
            return;
        }

        if (ReportPath[0] != L'\0')
        {
            // The reporter prints with stdio: point stderr at the end of the report.
            FILE* stream = nullptr;

            if (_wfreopen_s(&stream, ReportPath, L"a", stderr) != 0)
            {
                return;
            }
        }

        State().Reporter();
        std::fflush(stderr);
    }

    /// <summary>
    /// Opens the report: stderr, or, started without a terminal (stderr goes nowhere), MCRedux.crash.txt beside the
    /// executable, whose path <see cref="ReportPath"/> then holds.
    /// </summary>
    void OpenReport()
    {
        MCWin32Handle& reportFile = State().ReportFile;

        if (!MCConsole::HasOutput() && GetPathBesideExecutable(ReportPath, L".crash.txt"))
        {
            reportFile = MCWin32Handle(CreateFileW(ReportPath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                   nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));

            if (!IsOpen(reportFile))
            {
                ReportPath[0] = L'\0';
            }
        }
        else
        {
            ReportPath[0] = L'\0';
            std::fflush(stderr);
            reportFile = MCWin32Handle(GetStdHandle(STD_ERROR_HANDLE), MCHandleCloser{false});
        }
    }

    /// <summary>Opens the report file again to add to it (when the report goes to a file).</summary>
    void ReopenReport()
    {
        if (ReportPath[0] != L'\0')
        {
            State().ReportFile =
                MCWin32Handle(CreateFileW(ReportPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        }
    }

    /// <summary>Closes the report file, so the reporter's stdio can add to it (stderr stays as it is).</summary>
    void CloseReport()
    {
        if (ReportPath[0] != L'\0')
        {
            State().ReportFile.reset();
        }
    }

    /// <summary>
    /// Writes the dump, then the report, then runs the reporter, each step guarded: a fault in one (the state it reads
    /// may be what broke) costs only what comes after it in that step.
    /// </summary>
    void WriteCrashReport()
    {
        wchar_t dumpPath[MAX_PATH] = {};
        bool dumped = false;

        __try
        {
            dumped = WriteDump(dumpPath);
        }

        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }

        OpenReport();

        __try
        {
            PrintStack();
        }

        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Print("\n(the stack walk faulted)\n");
        }

        if (dumped)
        {
            Print("Minidump: {}\n", Narrow(dumpPath));
        }
        else
        {
            Print("Minidump: not written (error {})\n", GetLastError());
        }

        CloseReport();
        WriteRecord();

        __try
        {
            RunReporter();
        }

        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            ReopenReport();
            Print("\n(the reporter faulted)\n");
            CloseReport();
        }

        if (ReportPath[0] != L'\0')
        {
            wchar_t message[MAX_PATH + 64];
            swprintf_s(message, L"MCRedux crashed. The report is in\n%ls", ReportPath);
            MessageBoxW(nullptr, message, L"MCRedux", MB_OK | MB_ICONERROR | MB_TOPMOST);
        }
    }

    /// <summary>The worker: sleeps until a crash, then writes its report.</summary>
    DWORD WINAPI CrashWorker(void*)
    {
        WaitForSingleObject(State().CrashEvent.get(), INFINITE);
        WriteCrashReport();
        SetEvent(State().DoneEvent.get());
        return 0;
    }

    LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* exception)
    {
        // This runs on the crashed thread, maybe at the end of its stack: it only hands the crash to the worker.
        if (InterlockedCompareExchange(&Handling, 1, 0) != 0)
        {
            // Another thread crashed first (or this one faulted again): its report ends the process.
            Sleep(INFINITE);
        }

        CrashRecord = *exception->ExceptionRecord;
        CrashContext = *exception->ContextRecord;
        CrashThreadId = GetCurrentThreadId();
        MCCrashState& state = State();

        if (state.WorkerThread != nullptr)
        {
            SetEvent(state.CrashEvent.get());
            WaitForSingleObject(state.DoneEvent.get(), INFINITE);
        }
        else
        {
            WriteCrashReport();
        }

        return EXCEPTION_CONTINUE_SEARCH;
    }
}

namespace
{
    /// <summary>The exception code a CRT failure is raised as (an application error code: 0xE0000000 | 'CRT').</summary>
    constexpr DWORD CrtFailureException = 0xE0435254;

    /// <summary>Ends a CRT failure as a crash the handler reports: the stack it shows is the failing code's.</summary>
    [[noreturn]] void RaiseCrtFailure()
    {
        RaiseException(CrtFailureException, EXCEPTION_NONCONTINUABLE, 0, nullptr);
        std::_Exit(3);
    }

#ifdef _DEBUG
    /// <summary>
    /// A Debug CRT assertion or error (a checked iterator, "vector subscript out of range"): prints the CRT's message
    /// and crashes, instead of the CRT's Abort/Retry/Ignore box.
    /// </summary>
    int __cdecl OnCrtReport(int reportType, char* message, int* returnValue)
    {
        if (reportType == _CRT_WARN)
        {
            return FALSE;
        }

        std::fprintf(stderr, "CRT %s: %s\n", reportType == _CRT_ASSERT ? "assertion" : "error",
                     message != nullptr ? message : "");
        std::fflush(stderr);

        if (returnValue != nullptr)
        {
            *returnValue = 0;
        }

        RaiseCrtFailure();
    }
#endif

    /// <summary>abort() (std::terminate, a failed assert()): a crash, not the CRT's "abort() has been called" box.</summary>
    void __cdecl OnAbort(int)
    {
        std::fputs("abort() called\n", stderr);
        std::fflush(stderr);
        RaiseCrtFailure();
    }

    /// <summary>A CRT function given a bad argument.</summary>
    void __cdecl OnInvalidParameter(const wchar_t* expression, const wchar_t* function, const wchar_t* file,
                                    unsigned int line, uintptr_t)
    {
        std::fprintf(stderr, "invalid parameter: %ls in %ls (%ls:%u)\n", expression != nullptr ? expression : L"?",
                     function != nullptr ? function : L"?", file != nullptr ? file : L"?", line);
        std::fflush(stderr);
        RaiseCrtFailure();
    }

    /// <summary>A pure virtual function called (an object used during its construction or destruction).</summary>
    void __cdecl OnPureCall()
    {
        std::fputs("pure virtual function call\n", stderr);
        std::fflush(stderr);
        RaiseCrtFailure();
    }
}

void MCCrashTrace::Install()
{
    // The CRT's own failures become crashes the handler below reports and dumps, never a dialog (which an unattended
    // run would wait on forever); nor does Windows show its "stopped working" box after the dump is written.
#ifdef _DEBUG
    _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, OnCrtReport);
#endif
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    std::signal(SIGABRT, OnAbort);
    _set_invalid_parameter_handler(OnInvalidParameter);
    _set_purecall_handler(OnPureCall);
    SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);

    MCCrashState& state = State();
    state.CrashEvent.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    state.DoneEvent.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));

    if (state.CrashEvent != nullptr && state.DoneEvent != nullptr)
    {
        state.WorkerThread.reset(
            CreateThread(nullptr, 1024 * 1024, CrashWorker, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr));
    }

    SetUnhandledExceptionFilter(OnUnhandledException);
}

void MCCrashTrace::SetReporter(std::function<void()> reporter)
{
    State().Reporter = std::move(reporter);
}

void MCCrashTrace::SetRecordPath(const std::filesystem::path& path)
{
    const std::wstring& text = path.native();

    // A path too long for the buffer goes unwritten rather than cut.
    if (text.size() >= MAX_PATH)
    {
        RecordPath[0] = L'\0';
        return;
    }

    std::ranges::copy(text, std::begin(RecordPath));
    RecordPath[text.size()] = L'\0';
}

#else

void MCCrashTrace::Install()
{
}

void MCCrashTrace::SetReporter(std::function<void()>)
{
}

void MCCrashTrace::SetRecordPath(const std::filesystem::path&)
{
}

#endif
