#include <windows.h>
#include <psapi.h>
#include <lmcons.h>
#include <tchar.h>
#include <tlhelp32.h>
#include <assert.h>
#include <conio.h>
#include <pdh.h>
#include <stdio.h>
#include <math.h>
#include <shlwapi.h>
#include <strsafe.h>
#include "fzf\\fzf.h"

#define FOREGROUND_WHITE (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)
#define FOREGROUND_CYAN (FOREGROUND_GREEN)
#define FOREGROUND_CYAN2 (FOREGROUND_CYAN | BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE)
#define BACKGROUND_WHITE (BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE)
#define BACKGROUND_CYAN (BACKGROUND_INTENSITY | BACKGROUND_GREEN | BACKGROUND_BLUE)

static SHORT Width;
static SHORT Height;
static SHORT OldHeight;
static SHORT OldWidth;

static WORD SavedAttributes;

int SizeX;
int SizeY;

HANDLE ConsoleHandle;
static HANDLE OldConsoleHandle;
static WORD CurrentColor;

typedef struct CpuReading CpuReading;
struct CpuReading
{
    SHORT value;
    ULONGLONG idle;
    ULONGLONG krnl;
    ULONGLONG usr;
    CpuReading *next;
};

typedef struct ProcessTime
{
    DWORD pid;
    unsigned __int64 systemTime;
    unsigned __int64 processTime;
} ProcessTime;

typedef struct ProcessIo
{
    DWORD pid;
    unsigned __int64 systemTime;
    unsigned __int64 reads;
    unsigned __int64 writes;
} ProcessIo;

typedef struct Process
{
    DWORD pid;
    TCHAR *name;
    size_t workingSet;
    size_t privateBytes;
    float cpuPercent;
    float ioWritesPerSecond;
    float ioReadsPerSecond;
    int numberOfThreads;
    unsigned __int64 upTime;
    unsigned __int64 cpuSystemTime;
    unsigned __int64 cpuProcessTime;
    unsigned __int64 ioSystemTime;
    unsigned __int64 ioReads;
    unsigned __int64 ioWrites;
} Process;

typedef struct ProcessSnapshot
{
    DWORD pid;
    int numberOfThreads;
} ProcessSnapshot;

enum Mode
{
    Normal,
    Search
};

fzf_slab_t *slab;

static CRITICAL_SECTION SyncLock;
static HANDLE StatRefreshThread;
int NUMBER_OF_CPU_READINGS;
enum Mode g_mode = Normal;
ULONGLONG g_upTime;
CpuReading *g_cpuReadings;
CpuReading *g_lastCpuReading;
int g_cpuReadingIndex = 0;
int g_numberOfProcesses = 0;
int g_processor_count_ = 0;
int g_numberOfThreads = 0;
Process g_processes[1024];
int g_sortColumn = 5;
SHORT g_selectedIndex = 0;

char g_searchString[1024];
SHORT g_searchStringIndex = 0;

ProcessTime g_lastProcessTimes[1024];
int g_numberOfLastProcessTimes;

ProcessIo g_lastProcessIoReadings[1024];
int g_numberOfLastProcessIoReadings;

NUMBERFMT g_numFmt = { 0 };

BOOL g_controlState = FALSE;

SHORT g_summary_view_border_top;
SHORT g_summary_view_border_bottom;
SHORT g_summary_view_border_left;
SHORT g_summary_view_border_right;

SHORT g_summary_view_top;
SHORT g_summary_view_bottom;
SHORT g_summary_view_left;
SHORT g_summary_view_right;

SHORT g_cpu_graph_border_top;
SHORT g_cpu_graph_border_bottom;
SHORT g_cpu_graph_border_left;
SHORT g_cpu_graph_border_right;

SHORT g_cpu_graph_top;
SHORT g_cpu_graph_bottom;
SHORT g_cpu_graph_left;
SHORT g_cpu_graph_right;
SHORT g_cpu_graph_axis_left;

SHORT g_search_view_border_top;
SHORT g_search_view_border_bottom;
SHORT g_search_view_border_left;
SHORT g_search_view_border_right;

SHORT g_search_view_top;
SHORT g_search_view_left;

SHORT g_processes_view_border_top;
SHORT g_processes_view_border_bottom;
SHORT g_processes_view_border_left;
SHORT g_processes_view_border_right;

SHORT g_processes_view_header_top;
SHORT g_processes_view_header_bottom;
SHORT g_processes_view_header_left;
SHORT g_processes_view_header_right;

SHORT g_processes_view_top;
SHORT g_processes_view_bottom;
SHORT g_processes_view_left;
SHORT g_processes_view_right;

SHORT g_processes_view_x = 4;
SHORT g_processes_view_y = 2;
SHORT g_processes_view_number_of_display_lines;

int g_nameWidth = 0;

Process *g_displayProcesses[1024];
SHORT g_numberOfDisplayItems = 0;

int g_scrollOffset = 0;

#define INPUT_LOOP_DELAY 30

#ifdef _MSC_VER
#define NORETURN __declspec(noreturn)
#elif defined(__GNUC__) && defined(__MINGW32__)
#define NORETURN __attribute__((noreturn))
#else
#define NORETURN
#endif

NORETURN void Die(TCHAR *Fmt, ...)
{
    TCHAR Buffer[1024];
    va_list VaList;

    va_start(VaList, Fmt);
    _vstprintf_s(Buffer, _countof(Buffer), Fmt, VaList);
    va_end(VaList);

    system("cls");

    WriteFile(GetStdHandle(STD_ERROR_HANDLE), Buffer, (DWORD)(sizeof(*Buffer) * (_tcslen(Buffer) + 1)), 0, 0);
    exit(EXIT_FAILURE);
}

static int ConPrintf(TCHAR *Fmt, ...)
{
    TCHAR Buffer[1024];
    va_list VaList;

    va_start(VaList, Fmt);
    int CharsWritten = _vstprintf_s(Buffer, _countof(Buffer), Fmt, VaList);
    va_end(VaList);

    DWORD Dummy;
    WriteConsole(ConsoleHandle, Buffer, CharsWritten, &Dummy, 0);

    return CharsWritten;
}

static void ConPutc(char c)
{
    DWORD Dummy;
    WriteConsole(ConsoleHandle, &c, 1, &Dummy, 0);
}

static void ConPutcW(wchar_t c)
{
    DWORD Dummy;
    WriteConsoleW(ConsoleHandle, &c, 1, &Dummy, 0);
}

static void SetColor(WORD Color)
{
    Color;
    CurrentColor = Color;
    SetConsoleTextAttribute(ConsoleHandle, Color);
}

static void SetConCursorPos(SHORT X, SHORT Y)
{
    COORD Coord;
    Coord.X = X;
    Coord.Y = Y;
    SetConsoleCursorPosition(ConsoleHandle, Coord);
}

static void RestoreConsole(void)
{
    SetConsoleActiveScreenBuffer(OldConsoleHandle);
}

static void DisableCursor(void)
{
    CONSOLE_CURSOR_INFO CursorInfo;
    GetConsoleCursorInfo(ConsoleHandle, &CursorInfo);
    CursorInfo.bVisible = FALSE;
    SetConsoleCursorInfo(ConsoleHandle, &CursorInfo);
}

void kill_process(Process *process)
{
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, process->pid);
    TerminateProcess(hProcess, 0);
    CloseHandle(hProcess);
}

ProcessTime *find_process_time_by_pid(DWORD pid)
{
    for(int i = 0; i < g_numberOfLastProcessTimes; i++)
    {
        if(g_lastProcessTimes[i].pid == pid)
        {
            return &g_lastProcessTimes[i];
        }
    }

    return NULL;
}

void format_time(TCHAR *toFill, ULONGLONG value)
{
    int seconds = (int)(value / 1000 % 60);
    int minutes = (int)(value/ 60000 % 60);
    int hours = (int)(value / 3600000 % 24);
    int days = (int)(value / 86400000);
    wsprintf(toFill, _T("%02d:%02d:%02d:%02d"), days, hours, minutes, seconds);
}

ProcessIo* find_process_io_by_pid(DWORD pid)
{
    for(int i = 0; i < g_numberOfLastProcessIoReadings; i++)
    {
        if(g_lastProcessIoReadings[i].pid == pid)
        {
            return &g_lastProcessIoReadings[i];
        }
    }

    return NULL;
}

void fill_size_in_units(CHAR *toFill, int maxLen, ULONGLONG inBytes)
{
    CHAR suffix[5][3] = { "B", "KB", "MB", "GB", "TB" };
    ULONGLONG bytes = inBytes;

    for(int i = 0; i < 5; i++)
    {
        ULONGLONG newBytes = bytes / 1024;
        if(newBytes > 0)
        {
            bytes = newBytes;
        }
        else
        {
            sprintf_s(
                    toFill,
                    maxLen,
                    "%I64u %s",
                    bytes,
                    suffix[i - 1]);
        }
    }
}

void fill_cpu_usage(CpuReading *toFill, CpuReading *lastReading)
{
    FILETIME ftIdle, ftKrnl, ftUsr;
    if(GetSystemTimes(&ftIdle, &ftKrnl, &ftUsr))
    {
        ULONGLONG uIdle = ((ULONGLONG)ftIdle.dwHighDateTime << 32) | ftIdle.dwLowDateTime;
        ULONGLONG uKrnl = ((ULONGLONG)ftKrnl.dwHighDateTime << 32) | ftKrnl.dwLowDateTime;
        ULONGLONG uUsr = ((ULONGLONG)ftUsr.dwHighDateTime << 32) | ftUsr.dwLowDateTime;

        toFill->idle = uIdle;
        toFill->krnl = uKrnl;
        toFill->usr = uUsr;

        if(lastReading)
        {
            ULONGLONG uOldIdle = lastReading->idle;
            ULONGLONG uOldKrnl = lastReading->krnl;
            ULONGLONG uOldUsr = lastReading->usr;

            ULONGLONG uDiffIdle = uIdle - uOldIdle;
            ULONGLONG uDiffKrnl = uKrnl - uOldKrnl;
            ULONGLONG uDiffUsr = uUsr - uOldUsr;

            SHORT nRes = 0;
            if(uDiffKrnl + uDiffUsr)
            {
                nRes = (SHORT)((uDiffKrnl + uDiffUsr - uDiffIdle) * 100 / (uDiffKrnl + uDiffUsr));
            }

            toFill->value = nRes;
        }
        else
        {
            toFill->value = 0;
        }
    }
}

static int get_processor_number()
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return (int)info.dwNumberOfProcessors;
}

void add_cpu_reading(void)
{
    if(!g_cpuReadings)
    {
        g_cpuReadings = calloc(1, sizeof(CpuReading));
        fill_cpu_usage(g_cpuReadings, NULL);
        g_lastCpuReading = g_cpuReadings;
        g_cpuReadingIndex = 1;
    }
    else
    {
        CpuReading *cpuReading = calloc(1, sizeof(CpuReading));
        fill_cpu_usage(cpuReading, g_lastCpuReading);
        g_lastCpuReading->next = cpuReading;
        g_lastCpuReading = cpuReading;

        if(g_cpuReadingIndex >= NUMBER_OF_CPU_READINGS - 1)
        {
            CpuReading *firstReading = g_cpuReadings;
            g_cpuReadings = firstReading->next;
            free(firstReading);
        }
        else
        {
            g_cpuReadingIndex++;
        }
    }
}

void point_graph_axis_markers(void)
{
    for(SHORT i = 0; i < 10; i++)
    {
        SetConCursorPos(g_cpu_graph_axis_left, g_cpu_graph_bottom - i);
        /* ConPutc(196); */
        ConPutc('_');
    }
}

void paint_graph_column(SHORT readingNumber, SHORT value)
{
    SetColor(FOREGROUND_INTENSITY);
    WCHAR lowerEigth = { 0x2581 };
    WCHAR lowerQuarter = { 0x2582 };
    WCHAR lowerHalf = { 0x2583 };
    WCHAR fullBar = { 0x2588 };
    SHORT numberOfFullNumbers = value / 10;
    for(SHORT i = 0; i < 10; i++)
    {
        SetConCursorPos(g_cpu_graph_left + readingNumber, g_cpu_graph_bottom - i);
        if(i == numberOfFullNumbers)
        {
            int remainder = value % 10;
            if(remainder == 0)
            {
                ConPutc(' ');
            }
            else if(remainder <= 2)
            {
                /* SHORT row = g_cpu_graph_bottom - numberOfFullNumbers - 2; */
                /* SetConCursorPos(g_cpu_graph_left + readingNumber, row); */
                ConPutcW(lowerEigth);
            }
            else if(remainder < 5)
            {
                /* SHORT row = g_cpu_graph_bottom - numberOfFullNumbers - 2; */
                /* SetConCursorPos(g_cpu_graph_left + readingNumber, row); */
                ConPutcW(lowerQuarter);
            }
            else if(remainder >= 5)
            {
                /* SHORT row = g_cpu_graph_bottom - numberOfFullNumbers; */
                /* SetConCursorPos(g_cpu_graph_left + readingNumber, row); */
                ConPutcW(lowerHalf);
            }
        }
        else if(i > numberOfFullNumbers)
        {
            ConPutc(' ');
        }
        else
        {
            ConPutcW(fullBar);
        }
    }

}

void paint_graph_window()
{
    point_graph_axis_markers();
    SHORT readingIndex = 0;

    CpuReading *currentReading = g_cpuReadings;
    while(currentReading)
    {
        paint_graph_column(readingIndex, currentReading->value);
        currentReading = currentReading->next;
        readingIndex++;
    }
}

void refreshsummary()
{
    CHAR modeBuff[MAX_PATH];
    if(g_mode == Normal)
    {
        StringCchCopyA (modeBuff, MAX_PATH, "Normal");
    }
    else if(g_mode == Search)
    {
        StringCchCopyA(modeBuff, MAX_PATH, "Search");
    }

    g_upTime = GetTickCount64();

    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof (statex);
    GlobalMemoryStatusEx (&statex);

    CHAR totalMemoryInUnits[MAX_PATH];
    fill_size_in_units(totalMemoryInUnits, MAX_PATH, statex.ullTotalPhys);

    CHAR availableMemoryInUnits[MAX_PATH];
    fill_size_in_units(availableMemoryInUnits, MAX_PATH, statex.ullAvailPhys);

    int cpuPercent = 0;
    if(g_lastCpuReading)
    {
        cpuPercent = g_lastCpuReading->value;
    }

    TCHAR upTimeStr[MAX_PATH];
    format_time(upTimeStr, g_upTime);
    
    TCHAR cpuStr[MAX_PATH];
    sprintf_s(
            cpuStr,
            MAX_PATH,
            "%3d%%",
            cpuPercent);

    TCHAR memStr[MAX_PATH];
    sprintf_s(
            memStr,
            MAX_PATH,
            "%3d%%",
            statex.dwMemoryLoad);

    SetConCursorPos(g_summary_view_left, g_summary_view_top);
    ConPrintf(
            "%-12s %13s",
            "CPU:",
            cpuStr);

    SetConCursorPos(g_summary_view_left, g_summary_view_top + 1);
    ConPrintf(
            "%-12s %13d",
            "Cores:",
            g_processor_count_);

    SetConCursorPos(g_summary_view_left, g_summary_view_top + 2);
    ConPrintf(
            "%-12s %13d",
            "Processes:",
            g_numberOfProcesses);

    SetConCursorPos(g_summary_view_left, g_summary_view_top + 3);
    ConPrintf(
            "%-12s %13d",
            "Threads:",
            g_numberOfThreads);

    SetConCursorPos(g_summary_view_left, g_summary_view_top + 4);
    ConPrintf(
            "%-12s %13s",
            "Memory:",
            memStr);

    SetConCursorPos(g_summary_view_left, g_summary_view_top + 5);
    ConPrintf(
            "%-12s %13s",
            "Total:",
            totalMemoryInUnits);

    SetConCursorPos(g_summary_view_left, g_summary_view_top + 6);
    ConPrintf(
            "%-12s %13s",
            "Avail:",
            availableMemoryInUnits);

    SetConCursorPos(g_summary_view_left, g_summary_view_top + 7);
    ConPrintf(
            "%-12s %13s",
            "Up Time:",
            upTimeStr);
}

unsigned __int64 ConvertFileTimeToInt64(FILETIME *fileTime)
{
    ULARGE_INTEGER result;

    result.LowPart  = fileTime->dwLowDateTime;
    result.HighPart = fileTime->dwHighDateTime;

    return result.QuadPart;
}

BOOL populate_process_io(Process *process, ProcessIo *lastProcessIo, HANDLE processHandle)
{
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    IO_COUNTERS counters;
    if(!GetProcessIoCounters(processHandle, &counters))
    {
        return FALSE;
    }

    unsigned __int64 systemTime = ConvertFileTimeToInt64(&now);
    process->ioSystemTime = systemTime;
    process->ioReads = counters.ReadOperationCount;
    process->ioWrites = counters.WriteOperationCount;

    if(!lastProcessIo)
    {
        process->ioReadsPerSecond = 0;
        process->ioWritesPerSecond = 0;
        return FALSE;
    }

    unsigned __int64 readDiff = counters.ReadOperationCount - lastProcessIo->reads;
    unsigned __int64 writeDiff = counters.WriteOperationCount - lastProcessIo->writes;
    unsigned __int64 timeDiff = systemTime - lastProcessIo->systemTime;

    float numberOfSecondsBetweenReadings = (float)timeDiff / (float)10000000;

    float readsPerSecond = readDiff / numberOfSecondsBetweenReadings;
    float writesPerSecond = writeDiff / numberOfSecondsBetweenReadings;

    process->ioReadsPerSecond = readsPerSecond;
    process->ioWritesPerSecond = writesPerSecond;

    return TRUE;
}

BOOL populate_process_cpu_usage(Process *process, ProcessTime *lastProcessTime, HANDLE hProcess)
{
    FILETIME now;
    FILETIME creation_time;
    FILETIME exit_time;
    FILETIME kernel_time;
    FILETIME user_time;
    ULONGLONG processTimeDifference;
    ULONGLONG systemTimeDifference;

    float cpu = -1;

    process->cpuPercent = 0;
    if(process->pid == 0)
    {
        return FALSE;
    }

    GetSystemTimeAsFileTime(&now);
    if(!GetProcessTimes(hProcess, &creation_time, &exit_time, &kernel_time, &user_time))
    {
        return FALSE;
    }

    unsigned __int64 processCreationTime = ConvertFileTimeToInt64(&creation_time);
    unsigned __int64 systemTotalTime = ConvertFileTimeToInt64(&now);
    unsigned __int64 processUserTime = ConvertFileTimeToInt64(&user_time) / g_processor_count_;
    unsigned __int64 processKernelTime = ConvertFileTimeToInt64(&kernel_time) / g_processor_count_;
    unsigned __int64 processTotalTime = processUserTime + processKernelTime;

    process->upTime = (systemTotalTime - processCreationTime) / 10000;

    if(!lastProcessTime)
    {
        return FALSE;
    }

    processTimeDifference = processTotalTime - lastProcessTime->processTime;
    systemTimeDifference = systemTotalTime - lastProcessTime->systemTime;

    process->cpuSystemTime = systemTotalTime;
    process->cpuProcessTime = processTotalTime;

    if(systemTimeDifference == 0)
    {
        return TRUE;
    }

    cpu = ((float)processTimeDifference * 100 / (float)systemTimeDifference);
    if(cpu > 100)
    {
        return TRUE;
    }

    process->cpuPercent = cpu;

    return TRUE;
}

void populate_process_from_pid(Process *process, HANDLE hProcess)
{
    PROCESS_MEMORY_COUNTERS_EX pmc;
    GetProcessMemoryInfo(hProcess, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
    process->privateBytes = pmc.PrivateUsage / 1024;
    process->workingSet = pmc.WorkingSetSize / 1024;
}

void FormatNumber(CHAR *buf, size_t toFormat, NUMBERFMT* fmt)
{
    CHAR numStr[MAX_PATH];
    _snprintf_s(numStr, MAX_PATH, 15, "%zu", toFormat);
    GetNumberFormatA(LOCALE_USER_DEFAULT,
            0,
            numStr,
            fmt,
            buf,
            MAX_PATH);
}

int CompareProcessForSort(const void *a, const void *b)
{
    Process *processA = (Process *)a;
    Process *processB = (Process *)b;

    if(g_sortColumn == 1)
    {
        if(!processB->name)
        {
            return 1;
        }
        else if(!processA->name)
        {
            return -1;
        }
        int Compare = _tcsncicmp(processB->name, processA->name, MAX_PATH) * -1;
        return Compare;
    }
    if(g_sortColumn == 2)
    {
        return (int)(processB->privateBytes - processA->privateBytes);
    }
    if(g_sortColumn == 3)
    {
        return (int)(processB->workingSet - processA->workingSet);
    }
    if(g_sortColumn == 4)
    {
        return (int)(processB->pid - processA->pid);
    }
    if(g_sortColumn == 5)
    {
        return (int)((processB->cpuPercent * 10) - (processA->cpuPercent * 10));
    }
    return 0;
}

void populate_processes(void)
{
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPALL, 0);

    PROCESSENTRY32 pEntry = {0};
    pEntry.dwSize = sizeof(PROCESSENTRY32);
    unsigned int count = 0;

    g_numberOfThreads = 0;
    do
    {
        if(pEntry.szExeFile[0] != '\0')
        {
            if(g_processes[count].name)
            {
                free(g_processes[count].name);
            }
            g_processes[count].name = StrDupA(pEntry.szExeFile);
            g_processes[count].pid = pEntry.th32ProcessID;
            int processNumberOfThreads = pEntry.cntThreads;
            g_numberOfThreads += processNumberOfThreads;
            g_processes[count].numberOfThreads = processNumberOfThreads;
            HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pEntry.th32ProcessID);
            if(hProcess)
            {
                populate_process_from_pid(&g_processes[count], hProcess);
                ProcessTime *lastProcessTime = find_process_time_by_pid(g_processes[count].pid);
                populate_process_cpu_usage(&g_processes[count], lastProcessTime, hProcess);
                ProcessIo *lastProcessIo = find_process_io_by_pid(g_processes[count].pid);
                populate_process_io(&g_processes[count], lastProcessIo, hProcess);
                CloseHandle(hProcess);
                count++;
            }
        }
    } while(Process32Next(hSnapshot, &pEntry));

    g_numberOfProcesses = count;
    for(int i = 0; i < g_numberOfProcesses; i++)
    {
        g_lastProcessTimes[i].pid = g_processes[i].pid;
        g_lastProcessTimes[i].systemTime = g_processes[i].cpuSystemTime;
        g_lastProcessTimes[i].processTime = g_processes[i].cpuProcessTime;
    }

    g_numberOfLastProcessTimes = g_numberOfProcesses;

    for(int i = 0; i < g_numberOfProcesses; i++)
    {
        g_lastProcessIoReadings[i].pid = g_processes[i].pid;
        g_lastProcessIoReadings[i].systemTime = g_processes[i].ioSystemTime;
        g_lastProcessIoReadings[i].reads = g_processes[i].ioReads;
        g_lastProcessIoReadings[i].writes = g_processes[i].ioWrites;
    }

    g_numberOfLastProcessIoReadings = g_numberOfProcesses;
}

void print_process_at_index(SHORT index)
{
    Process *process = g_displayProcesses[index + g_scrollOffset];

    CHAR privateBytesFormatedStr[MAX_PATH];
    FormatNumber(privateBytesFormatedStr, process->privateBytes, &g_numFmt);

    CHAR workingSetFormatedStr[MAX_PATH];
    FormatNumber(workingSetFormatedStr, process->workingSet, &g_numFmt);

    SetConCursorPos(g_processes_view_left, g_processes_view_top + index);
    if(index == g_selectedIndex)
    {
        SetColor(BACKGROUND_WHITE);
    }
    else
    {
        SetColor(FOREGROUND_INTENSITY);
    }

    fzf_pattern_t *pattern = fzf_parse_pattern(CaseSmart, false, g_searchString, true);
    fzf_position_t *pos = fzf_get_positions(process->name, pattern, slab);

    TCHAR upTimeStr[MAX_PATH];
    format_time(upTimeStr, process->upTime);

    int chars = ConPrintf(
            "%-*s %08d %13s %20s %20s %8.1f %8d %8.1f %8.1f",
            g_nameWidth,
            process->name,
            process->pid,
            upTimeStr,
            workingSetFormatedStr,
            privateBytesFormatedStr,
            process->cpuPercent,
            process->numberOfThreads,
            process->ioReadsPerSecond,
            process->ioWritesPerSecond);

    assert(chars <= g_processes_view_right - g_processes_view_left + 1);

    if(pos)
    {
        if(index == g_selectedIndex)
        {
            SetColor(FOREGROUND_CYAN2);
        }
        else
        {
            SetColor(FOREGROUND_CYAN);
        }
        if(pos && pos->size > 0)
        {
            for(int i = 0; i < pos->size; i++)
            {

                SetConCursorPos(g_processes_view_left + (SHORT)pos->data[i], g_processes_view_top + index);
                ConPutc(process->name[pos->data[i]]);
            }
        }
    }
    SetColor(FOREGROUND_INTENSITY);

    fzf_free_positions(pos);
    fzf_free_pattern(pattern);
}

void clear_process_list_at_index(SHORT index)
{
    DWORD cCharsWritten;
    for(SHORT i = index; i < g_processes_view_number_of_display_lines; i++)
    {
        COORD coordScreen = { g_processes_view_left, g_processes_view_top + i };
        FillConsoleOutputAttribute(ConsoleHandle,
                FOREGROUND_INTENSITY,
                g_processes_view_right - g_processes_view_left + 1,
                coordScreen,
                &cCharsWritten);
        FillConsoleOutputCharacter(ConsoleHandle,
                (TCHAR)' ',
                g_processes_view_right - g_processes_view_left + 1,
                coordScreen,
                &cCharsWritten);
    }
}

void print_processes(void)
{
    SHORT numberOfDisplayItems = 0;

    fzf_pattern_t *pattern = fzf_parse_pattern(CaseSmart, false, g_searchString, true);
    if(g_searchStringIndex > 0)
    {
        for(int i = 0; i < g_numberOfProcesses; i++)
        {
            int score = 0;
            if(g_processes[i].name)
            {
                score = fzf_get_score(g_processes[i].name, pattern, slab);
            }
            if(score > 0)
            {
                g_displayProcesses[numberOfDisplayItems] = &g_processes[i];
                numberOfDisplayItems++;
            }
        }
    }
    else
    {
        for(int i = 0; i < g_numberOfProcesses; i++)
        {
            g_displayProcesses[numberOfDisplayItems] = &g_processes[i];
            numberOfDisplayItems++;
        }
    }

    g_numberOfDisplayItems = numberOfDisplayItems;
    SHORT numberOfItemsToPrint = g_numberOfDisplayItems;
    if(g_numberOfDisplayItems > g_processes_view_number_of_display_lines)
    {
        numberOfItemsToPrint = g_processes_view_number_of_display_lines;
    }

    if(g_selectedIndex > numberOfItemsToPrint)
    {
        g_selectedIndex = 0;
    }

    for(SHORT i = 0; i < numberOfItemsToPrint; i++)
    {
        print_process_at_index(i);
    }

    clear_process_list_at_index(numberOfItemsToPrint);
}

DWORD WINAPI StatRefreshThreadProc(LPVOID lpParam)
{
    UNREFERENCED_PARAMETER(lpParam);

    while(1)
    {
        add_cpu_reading();
        EnterCriticalSection(&SyncLock);
        refreshsummary();
        paint_graph_window();
        populate_processes();
        qsort(&g_processes[0], g_numberOfProcesses + 1, sizeof(Process), CompareProcessForSort);
        print_processes();
        LeaveCriticalSection(&SyncLock);

        Sleep(1000);
    }

    return 0;
}

void draw_box(SHORT top, SHORT bottom, SHORT left, SHORT width)
{
    //218 ┌
    //191 ┐
    //179 │
    //192 └
    //217 ┘

    SetConCursorPos(left, top);
    ConPutc(218);
    for(int i = 1; i < width - 1; i++)
    {
        ConPutc(196);
    }

    ConPutc(191);
    for(SHORT i = 1; i < bottom - top + 1; i++)
    {
        SetConCursorPos(left, i + top);
        ConPutc(179);
        SetConCursorPos(left + width - 1, i + top);
        ConPutc(179);
    }
    SetConCursorPos(left, bottom);
    ConPutc(192);

    for(int i = 1; i < width - 1; i++)
    {
        ConPutc(196);
    }
    ConPutc(217);
}

void calcuate_layout(void)
{
    CONSOLE_SCREEN_BUFFER_INFO Csbi;
    GetConsoleScreenBufferInfo(ConsoleHandle, &Csbi);

    Width = Csbi.srWindow.Right - Csbi.srWindow.Left + 1;
    Height = Csbi.srWindow.Bottom - Csbi.srWindow.Top + 1;
    Width = Csbi.srWindow.Right - Csbi.srWindow.Left + 1;
    Height = Csbi.srWindow.Bottom - Csbi.srWindow.Top + 1;

    g_summary_view_border_top = 0;
    g_summary_view_border_bottom = 11;
    g_summary_view_border_left = 0;
    g_summary_view_border_right = Width;

    g_cpu_graph_border_top = g_summary_view_border_top;
    g_cpu_graph_border_bottom = g_summary_view_border_bottom;
    g_cpu_graph_border_left = g_summary_view_border_left + 30;
    g_cpu_graph_border_right = g_summary_view_border_right;

    g_cpu_graph_axis_left = g_cpu_graph_border_left + 1;

    g_cpu_graph_top = g_summary_view_top;
    g_cpu_graph_bottom = g_cpu_graph_border_bottom - 1;
    g_cpu_graph_left = g_cpu_graph_axis_left + 1;
    g_cpu_graph_right = g_cpu_graph_border_right - 1;

    NUMBER_OF_CPU_READINGS = g_cpu_graph_right - g_cpu_graph_axis_left;

    if(g_mode == Search)
    {
        g_search_view_border_top = g_summary_view_border_bottom + 1;
        g_search_view_border_bottom = g_search_view_border_top + 2;
        g_search_view_border_left = 0;
        g_search_view_border_right = Width;

        g_search_view_top = g_search_view_border_top + 1;
        g_search_view_left = g_search_view_border_left + 2;

        g_processes_view_border_top = g_search_view_border_bottom + 1;
    }
    else
    {
        g_search_view_border_top = 0;
        g_search_view_border_bottom = 0;
        g_search_view_border_left = 0;
        g_search_view_border_right = 0;

        g_processes_view_border_top = g_summary_view_border_bottom + 1;
    }

    g_processes_view_border_right = Width;
    g_processes_view_border_left = 0;
    g_processes_view_border_bottom = Height - 2;

    g_summary_view_top = g_summary_view_border_top + 1;
    g_summary_view_bottom = g_summary_view_border_bottom - 1;
    g_summary_view_left = g_summary_view_border_left + 2;
    g_summary_view_right = Width;

    g_processes_view_header_top = g_processes_view_border_top + 1;
    g_processes_view_header_bottom = g_processes_view_border_top + 2;
    g_processes_view_header_left = g_processes_view_border_left + 2;
    g_processes_view_header_right = g_processes_view_border_right + 2;

    g_processes_view_top = g_processes_view_header_top + 1;
    g_processes_view_bottom = g_processes_view_border_bottom - 1;
    g_processes_view_left = g_processes_view_border_left + 2;
    g_processes_view_right = g_processes_view_border_right - 2;

    g_processes_view_number_of_display_lines = g_processes_view_bottom - g_processes_view_top + 1;

    int nonNameWidth = 1 + 8 + 1 + 20 + 1 + 8 + 1 + 20 + 1 + 8 + 1 + 8 + 1 + 8 + 13;
    g_nameWidth = g_processes_view_right - g_processes_view_left - nonNameWidth;
}

void draw_processes_window(void)
{
    draw_box(g_processes_view_border_top, g_processes_view_border_bottom, g_processes_view_border_left, g_processes_view_border_right);
    SetColor(FOREGROUND_CYAN);
    SetConCursorPos(g_processes_view_header_left, g_processes_view_header_top);
    ConPrintf(
            "%-*s %8s %13s %20s %20s %8s %8s %8s %8s",
            g_nameWidth,
            "Name",
            "PID",
            "UpTime",
            "WorkingSet(kb)",
            "PrivateBytes(kb)",
            "CPU(%)",
            "Threads",
            "Reads",
            "Writes");
    SetColor(FOREGROUND_INTENSITY);
    print_processes();
}

void draw_summary_window(void)
{
    draw_box(g_summary_view_border_top, g_summary_view_border_bottom, g_summary_view_border_left, g_summary_view_border_right);
    draw_box(g_cpu_graph_border_top, g_cpu_graph_border_bottom, g_cpu_graph_border_left, g_cpu_graph_border_right - g_cpu_graph_border_left);

    refreshsummary();
}

void draw_search_view(void)
{
    if(g_mode == Search)
    {
        draw_box(g_search_view_border_top, g_search_view_border_bottom, g_search_view_border_left, g_search_view_border_right);
        for(SHORT i = g_searchStringIndex; i < g_searchStringIndex + 5; i++)
        {
            SetConCursorPos(g_search_view_left + i, g_search_view_top); 
            ConPutc(' ');
        }
        SetConCursorPos(g_search_view_left, g_search_view_top); 
        ConPrintf(
                "> %s",
                g_searchString);
    }
}

void clear_screen()
{
    COORD coordScreen = { 0, 0 };
    DWORD cCharsWritten;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    DWORD dwConSize;

    // Get the number of character cells in the current buffer.
    if (!GetConsoleScreenBufferInfo(ConsoleHandle, &csbi))
    {
        return;
    }

    dwConSize = csbi.dwSize.X * csbi.dwSize.Y;

    // Fill the entire screen with blanks.
    if (!FillConsoleOutputCharacter(ConsoleHandle,        // Handle to console screen buffer
                (TCHAR)' ',      // Character to write to the buffer
                dwConSize,       // Number of cells to write
                coordScreen,     // Coordinates of first cell
                &cCharsWritten)) // Receive number of characters written
    {
        return;
    }

    // Get the current text attribute.
    if (!GetConsoleScreenBufferInfo(ConsoleHandle, &csbi))
    {
        return;
    }

    // Set the buffer's attributes accordingly.
    if (!FillConsoleOutputAttribute(ConsoleHandle,         // Handle to console screen buffer
                csbi.wAttributes, // Character attributes to use
                dwConSize,        // Number of cells to set attribute
                coordScreen,      // Coordinates of first cell
                &cCharsWritten))  // Receive number of characters written
    {
        return;
    }

    // Put the cursor at its home coordinates.
    SetConsoleCursorPosition(ConsoleHandle, coordScreen);
}

static BOOL PollConsoleInfo(void)
{
    CONSOLE_SCREEN_BUFFER_INFO Csbi;
    GetConsoleScreenBufferInfo(ConsoleHandle, &Csbi);
    Width = Csbi.srWindow.Right - Csbi.srWindow.Left + 1;
    Height = Csbi.srWindow.Bottom - Csbi.srWindow.Top + 1;

    if(Width != OldWidth || Height != OldHeight) {
        DisableCursor();
        COORD Size;
        Size.X = (USHORT)Width;
        Size.Y = (USHORT)Height;
        SetConsoleScreenBufferSize(ConsoleHandle, Size);

        OldWidth = Width;
        OldHeight = Height;

        return TRUE;
    }

    SizeX = (int)Csbi.dwSize.X;
    SizeY = (int)Csbi.dwSize.Y;

    if(SavedAttributes == 0)
        SavedAttributes = Csbi.wAttributes;

    return FALSE;
}

int _tmain(int argc, TCHAR *argv[])
{
    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    InitializeCriticalSection(&SyncLock);
    slab = fzf_make_default_slab();

    g_numFmt.NumDigits = 0;
    g_numFmt.LeadingZero = 0;
    g_numFmt.NumDigits = 0;
    g_numFmt.LeadingZero = 0;
    g_numFmt.Grouping = 3;
    g_numFmt.lpDecimalSep = ".";
    g_numFmt.lpThousandSep = ",";
    g_numFmt.NegativeOrder = 1;
    g_processor_count_ = get_processor_number();
    ConsoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    OldConsoleHandle = ConsoleHandle;

    ConsoleHandle = CreateConsoleScreenBuffer(GENERIC_READ|GENERIC_WRITE,
            FILE_SHARE_READ|FILE_SHARE_WRITE,
            0,
            CONSOLE_TEXTMODE_BUFFER,
            0);

    ConsoleHandle = CreateConsoleScreenBuffer(GENERIC_READ|GENERIC_WRITE,
            FILE_SHARE_READ|FILE_SHARE_WRITE,
            0,
            CONSOLE_TEXTMODE_BUFFER,
            0);

    if(ConsoleHandle == INVALID_HANDLE_VALUE) {
        Die(_T("Could not create console screen buffer: %ld\n"), GetLastError());
    }

    if(!SetConsoleActiveScreenBuffer(ConsoleHandle)) {
        Die(_T("Could not set active console screen buffer: %ld\n"), GetLastError());
    }

    SetConsoleMode(ConsoleHandle, ENABLE_PROCESSED_INPUT | ENABLE_WRAP_AT_EOL_OUTPUT);

    atexit(RestoreConsole);

    CONSOLE_CURSOR_INFO curInfo;
    GetConsoleCursorInfo(ConsoleHandle, &curInfo);
    curInfo.bVisible = FALSE;
    SetConsoleCursorInfo(ConsoleHandle, &curInfo);

    calcuate_layout();
    CONSOLE_SCREEN_BUFFER_INFO Csbi;
    GetConsoleScreenBufferInfo(ConsoleHandle, &Csbi);
    Width = Csbi.srWindow.Right - Csbi.srWindow.Left + 1;
    Height = Csbi.srWindow.Bottom - Csbi.srWindow.Top + 1;

    SetConCursorPos(0, 0);
    SetColor(FOREGROUND_INTENSITY);

    StatRefreshThread = CreateThread(0, 0, StatRefreshThreadProc, 0, 0, 0);

    while(1)
    {
        DWORD NumEvents, Num;

        GetNumberOfConsoleInputEvents(GetStdHandle(STD_INPUT_HANDLE), &NumEvents);

        if(NumEvents == 0)
        {
        }
        else
        {
            INPUT_RECORD *Records = calloc(NumEvents, sizeof(*Records));
            ReadConsoleInput(GetStdHandle(STD_INPUT_HANDLE), Records, NumEvents, &Num);

            for(DWORD i = 0; i < Num; i++)
            {
                INPUT_RECORD InputRecord = Records[i];
                if (InputRecord.EventType == KEY_EVENT)
                {
                    if (InputRecord.Event.KeyEvent.bKeyDown)
                    {
                        if(InputRecord.Event.KeyEvent.wVirtualKeyCode == VK_CONTROL)
                        {
                            g_controlState = TRUE;
                        }
                        else if(InputRecord.Event.KeyEvent.wVirtualKeyCode == VK_F1)
                        {
                            g_sortColumn = 1;
                        }
                        else if(InputRecord.Event.KeyEvent.wVirtualKeyCode == VK_F2)
                        {
                            g_sortColumn = 2;
                        }
                        else if(InputRecord.Event.KeyEvent.wVirtualKeyCode == VK_F3)
                        {
                            g_sortColumn = 3;
                        }
                        else if(InputRecord.Event.KeyEvent.wVirtualKeyCode == VK_F4)
                        {
                            g_sortColumn = 4;
                        }
                        else if(InputRecord.Event.KeyEvent.wVirtualKeyCode == VK_F5)
                        {
                            g_sortColumn = 5;
                        }
                        else if(g_controlState)
                        {
                            if(InputRecord.Event.KeyEvent.wVirtualKeyCode == 0x50) //N
                            {
                                EnterCriticalSection(&SyncLock);
                                if(g_selectedIndex > 0)
                                {
                                    g_selectedIndex--;
                                    print_process_at_index(g_selectedIndex + 1);
                                    print_process_at_index(g_selectedIndex);
                                }
                                LeaveCriticalSection(&SyncLock);
                            }
                            else if(InputRecord.Event.KeyEvent.wVirtualKeyCode == 0x4E) //P
                            {
                                g_selectedIndex++;
                                EnterCriticalSection(&SyncLock);
                                if(g_selectedIndex > 0)
                                {
                                    print_process_at_index(g_selectedIndex - 1);
                                }
                                print_process_at_index(g_selectedIndex);
                                LeaveCriticalSection(&SyncLock);
                                break;
                            }
                            else if(InputRecord.Event.KeyEvent.wVirtualKeyCode == 	0x45) //E
                            {
                                if(g_scrollOffset < g_numberOfDisplayItems - g_processes_view_number_of_display_lines)
                                {
                                    EnterCriticalSection(&SyncLock);
                                    g_scrollOffset++;
                                    print_processes();
                                    LeaveCriticalSection(&SyncLock);
                                }
                            }
                            else if(InputRecord.Event.KeyEvent.wVirtualKeyCode == 	0x59) //Y
                            {
                                if(g_scrollOffset > 0)
                                {
                                    EnterCriticalSection(&SyncLock);
                                    g_scrollOffset--;
                                    print_processes();
                                    LeaveCriticalSection(&SyncLock);
                                }
                            }
                            else if(InputRecord.Event.KeyEvent.wVirtualKeyCode == 0x44) //D
                            {
                                EnterCriticalSection(&SyncLock);
                                if(g_scrollOffset + (g_processes_view_number_of_display_lines * 2) < g_numberOfDisplayItems)
                                {
                                    g_scrollOffset = g_scrollOffset + g_processes_view_number_of_display_lines;
                                }
                                else
                                {
                                    g_scrollOffset = g_numberOfDisplayItems - g_processes_view_number_of_display_lines;
                                }
                                print_processes();
                                LeaveCriticalSection(&SyncLock);
                            }
                            else if(InputRecord.Event.KeyEvent.wVirtualKeyCode == 0x55) //U
                            {
                                EnterCriticalSection(&SyncLock);
                                if(g_scrollOffset - g_processes_view_number_of_display_lines > 0)
                                {
                                    g_scrollOffset = g_scrollOffset - g_processes_view_number_of_display_lines;
                                }
                                else
                                {
                                    g_scrollOffset = 0;
                                }
                                print_processes();
                                LeaveCriticalSection(&SyncLock);
                            }
                            else if(InputRecord.Event.KeyEvent.wVirtualKeyCode == 0x4B)
                            {
                                kill_process(g_displayProcesses[g_selectedIndex]);
                            }
                        }
                        else if(g_mode == Normal)
                        {
                            switch(InputRecord.Event.KeyEvent.uChar.AsciiChar)
                            {
                                case 'q':
                                    return EXIT_SUCCESS;
                                    break;
                                case 'j':
                                    g_selectedIndex++; EnterCriticalSection(&SyncLock);
                                    if(g_selectedIndex > 0)
                                    {
                                        print_process_at_index(g_selectedIndex - 1);
                                    }
                                    print_process_at_index(g_selectedIndex);
                                    LeaveCriticalSection(&SyncLock);
                                    break;
                                case 'k':
                                    EnterCriticalSection(&SyncLock);
                                    if(g_selectedIndex > 0)
                                    {
                                        g_selectedIndex--;
                                        print_process_at_index(g_selectedIndex + 1);
                                        print_process_at_index(g_selectedIndex);
                                    }
                                    LeaveCriticalSection(&SyncLock);
                                    break;
                                case '/':
                                    g_mode = Search;
                                    clear_screen();
                                    calcuate_layout();
                                    draw_summary_window();
                                    draw_search_view();
                                    draw_processes_window();
                                    LeaveCriticalSection(&SyncLock);
                                    break;
                                default:
                                    break;
                            }
                        }
                        else if(g_mode == Search)
                        {
                            switch(InputRecord.Event.KeyEvent.uChar.AsciiChar)
                            {
                                case VK_ESCAPE:
                                    EnterCriticalSection(&SyncLock);
                                    g_mode = Normal;
                                    g_searchString[0] = '\0';
                                    g_searchStringIndex = 0;
                                    clear_screen();
                                    calcuate_layout();
                                    paint_graph_window();
                                    draw_summary_window();
                                    draw_search_view();
                                    draw_processes_window();
                                    LeaveCriticalSection(&SyncLock);
                                    g_mode = Normal;
                                    break;
                                case VK_BACK:
                                    if(g_searchStringIndex > 0)
                                    {
                                        EnterCriticalSection(&SyncLock);
                                        g_searchStringIndex--;
                                        g_searchString[g_searchStringIndex] = '\0';
                                        draw_search_view();
                                        LeaveCriticalSection(&SyncLock);
                                    }
                                    break;
                                default:
                                    if(InputRecord.Event.KeyEvent.uChar.AsciiChar)
                                    {
                                        EnterCriticalSection(&SyncLock);
                                        g_searchString[g_searchStringIndex] = InputRecord.Event.KeyEvent.uChar.AsciiChar;
                                        g_searchStringIndex++;
                                        draw_search_view();
                                        LeaveCriticalSection(&SyncLock);
                                    }
                                    break;
                            }
                        }
                    }
                    else
                    {
                        if(InputRecord.Event.KeyEvent.wVirtualKeyCode == VK_CONTROL)
                        {
                            g_controlState = FALSE;
                        }
                    }
                }
            }
        }

        if(PollConsoleInfo())
        {
            EnterCriticalSection(&SyncLock);
            clear_screen();
            calcuate_layout();
            draw_summary_window();
            draw_search_view();
            draw_processes_window();
            LeaveCriticalSection(&SyncLock);
        }

        Sleep(INPUT_LOOP_DELAY);
    }

    return EXIT_SUCCESS;
}
