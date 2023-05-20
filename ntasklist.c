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
#include <Pdh.h>
#include "fzf\\fzf.h"

#pragma comment(lib, "pdh.lib")

#define FOREGROUND_WHITE (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)
#define FOREGROUND_CYAN (FOREGROUND_GREEN)
#define FOREGROUND_CYAN2 (FOREGROUND_CYAN | BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE)
#define BACKGROUND_WHITE (BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE)
#define BACKGROUND_CYAN (BACKGROUND_INTENSITY | BACKGROUND_GREEN | BACKGROUND_BLUE)

#define VK_J 0x4A
#define VK_K 0x4B

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

typedef struct Drive Drive;
struct Drive
{
    CHAR *longPath;
    unsigned __int64 lastSystemTime;
    __int64 lastBytesRead;
    __int64 lastBytesWritten;
    float readsPerSecond;
    float writesPerSecond;
};

typedef struct ShortRect
{
    SHORT left;
    SHORT top;
    SHORT right;
    SHORT bottom;
} ShortRect;

typedef struct Process Process;
struct Process
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
    FILETIME cpuKernelTime;
    unsigned __int64 cpuUserTime;
    unsigned __int64 ioSystemTime;
    unsigned __int64 ioReads;
    unsigned __int64 ioWrites;
    Process *next;
};

typedef struct CpuReading CpuReading;
struct CpuReading
{
    SHORT value;
    WORD second;
    WORD minute;
    WORD hour;
    int numberOfProcesses;
    int numberOfThreads;
    DWORD memoryPercent;
    DWORDLONG totalMemory;
    DWORDLONG availableMemory;
    float readsPerSecond;
    float writesPerSecond;
    ULONGLONG idle;
    ULONGLONG krnl;
    ULONGLONG usr;
    CpuReading *next;
    CpuReading *previous;
    Process **processes;
    Process *lastProcess;
    int processArraySize;
};

typedef struct IoReading IoReading;
struct IoReading
{
    float readsPerSecond;
    float writesPerSecond;
    IoReading *next;
};

typedef struct MemoryReading MemoryReading;
struct MemoryReading
{
    DWORD value;
    MemoryReading *next;
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

typedef struct ProcessSnapshot
{
    DWORD pid;
    int numberOfThreads;
} ProcessSnapshot;

enum Mode
{
    Normal,
    Search,
    Help,
    ProcessDetails,
    ReadingsList,
    ProcessHistory
};

enum TextType
{
    Header,
    Plain
};

typedef struct TextLine
{
    enum TextType lineType;
    CHAR* text;
} TextLine;

fzf_slab_t *slab;

static CRITICAL_SECTION SyncLock;
static HANDLE StatRefreshThread;
int NUMBER_OF_CPU_READINGS;
int NUMBER_OF_IO_READINGS;
int NUMBER_OF_MEMORY_READINGS;
enum Mode g_mode = Normal;
BOOL g_isFiltered;
ULONGLONG g_upTime;
CpuReading *g_cpuReadings;
CpuReading *g_lastCpuReading;
CpuReading *g_lastDisplayCpuReading;
int g_MaxCpuPercent;
SHORT g_selectedCpuReadingIndex;

int g_cpuReadingIndex = 0;
int g_numberOfProcesses = 0;
int g_processor_count_ = 0;
int g_numberOfThreads = 0;
Process *g_processes[1024];
int g_sortColumn = 5;
SHORT g_selectedIndex = 0;
DWORD g_focusedProcessPid;

IoReading *g_ioReadings;
IoReading *g_lastIoReading;
int g_ioReadingIndex;
float g_largestIoReading;
SHORT g_largestCpuReading;

MemoryReading *g_memoryReadings;
MemoryReading *g_lastMemoryReading;
int g_memoryReadingIndex;
DWORD g_largestMemoryReading;

int g_numberOfDrives;
/* Drive g_drives[MAX_PATH]; */

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

SHORT g_io_graph_border_top;
SHORT g_io_graph_border_bottom;
SHORT g_io_graph_border_left;
SHORT g_io_graph_border_right;

SHORT g_io_graph_top;
SHORT g_io_graph_bottom;
SHORT g_io_graph_left;
SHORT g_io_graph_right;
SHORT g_io_graph_axis_left;

SHORT g_memory_graph_border_top;
SHORT g_memory_graph_border_bottom;
SHORT g_memory_graph_border_left;
SHORT g_memory_graph_border_right;

SHORT g_memory_graph_top;
SHORT g_memory_graph_bottom;
SHORT g_memory_graph_left;
SHORT g_memory_graph_right;
SHORT g_memory_graph_axis_left;

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

ShortRect g_help_view_border_rect;
ShortRect g_help_view_rect;

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

unsigned __int64 ConvertFileTimeToInt64(FILETIME *fileTime)
{
    ULARGE_INTEGER result;

    result.LowPart  = fileTime->dwLowDateTime;
    result.HighPart = fileTime->dwHighDateTime;

    return result.QuadPart;
}

BOOL sm_EnableTokenPrivilege(LPCTSTR pszPrivilege)
{
    HANDLE hToken = 0;
    TOKEN_PRIVILEGES tkp = {0}; 

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
    {
        return FALSE;
    }

    if(LookupPrivilegeValue(NULL, pszPrivilege, &tkp.Privileges[0].Luid)) 
    {
        tkp.PrivilegeCount = 1;
        tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, (PTOKEN_PRIVILEGES)NULL, 0); 

        if (GetLastError() != ERROR_SUCCESS)
        {
            return FALSE;
        }

        return TRUE;
    }

    return FALSE;
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

static void SetConCursorPos(SHORT X, SHORT Y)
{
    COORD Coord;
    Coord.X = X;
    Coord.Y = Y;
    SetConsoleCursorPosition(ConsoleHandle, Coord);
}

static void ConPutc(char c)
{
    DWORD Dummy;
    WriteConsole(ConsoleHandle, &c, 1, &Dummy, 0);
}

BOOL is_inside_vertical_of_rect(COORD *coord, ShortRect *rect)
{
    if(coord->Y >= rect->top)
    {
        if(coord->Y <= rect->bottom)
        {
            return TRUE;
        }
    }

    return FALSE;
}

BOOL is_inside_rect(COORD *coord, ShortRect *rect)
{
    if(is_inside_vertical_of_rect(coord, rect))
    {
        if(coord->X >= rect->left)
        {
            if(coord->X <= rect->right)
            {
                return TRUE;
            }
        }
    }

    return FALSE;
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

void draw_rect(ShortRect *rect)
{
    draw_box(rect->top, rect->bottom, rect->left, rect->right - rect->left);
}

void clear_rect(ShortRect *rect)
{
    DWORD cCharsWritten;
    SHORT numberOfLines = rect->bottom - rect->top + 1;
    SHORT width = rect->right - rect->left;
    for(SHORT i = 0; i < numberOfLines; i++)
    {
        COORD coordScreen = { rect->left, rect->top + i };
        FillConsoleOutputAttribute(ConsoleHandle,
                FOREGROUND_INTENSITY,
                width,
                coordScreen,
                &cCharsWritten);
        FillConsoleOutputCharacter(ConsoleHandle,
                (TCHAR)' ',
                width,
                coordScreen,
                &cCharsWritten);
    }
}

static int DialogConPrintf(TCHAR *Fmt, ...)
{
    CONSOLE_SCREEN_BUFFER_INFO cbsi;
    GetConsoleScreenBufferInfo(ConsoleHandle, &cbsi);

    TCHAR Buffer[1024];
    va_list VaList;

    va_start(VaList, Fmt);
    int CharsWritten = _vstprintf_s(Buffer, _countof(Buffer), Fmt, VaList);
    va_end(VaList);

    DWORD Dummy;
    WriteConsole(ConsoleHandle, Buffer, CharsWritten, &Dummy, 0);
    return CharsWritten;
}

static int ConPrintf(TCHAR *Fmt, ...)
{
    CONSOLE_SCREEN_BUFFER_INFO cbsi;
    GetConsoleScreenBufferInfo(ConsoleHandle, &cbsi);

    TCHAR Buffer[1024];
    va_list VaList;

    va_start(VaList, Fmt);
    int CharsWritten = _vstprintf_s(Buffer, _countof(Buffer), Fmt, VaList);
    va_end(VaList);

    if((g_mode != Help && g_mode != ProcessDetails) && g_mode != ReadingsList || !is_inside_vertical_of_rect(&cbsi.dwCursorPosition, &g_help_view_border_rect))
    {
        DWORD Dummy;
        WriteConsole(ConsoleHandle, Buffer, CharsWritten, &Dummy, 0);
        return CharsWritten;
    }
    else
    {
        int charsWrittenRight = cbsi.dwCursorPosition.X + CharsWritten;
        int maxX = g_help_view_border_rect.right;
        if(charsWrittenRight > maxX)
        {
            maxX = charsWrittenRight;
        }

        int maxLeft = g_help_view_border_rect.left - cbsi.dwCursorPosition.X;
        int beforeLeft = CharsWritten;
        if(CharsWritten > maxLeft)
        {
            beforeLeft = maxLeft;
        }
        DWORD Dummy;
        WriteConsole(ConsoleHandle, Buffer, beforeLeft, &Dummy, 0);

        if(charsWrittenRight > g_help_view_border_rect.right)
        {
            int charsToWrite = maxX - g_help_view_border_rect.right;
            int startChar = CharsWritten - charsToWrite;
            SetConCursorPos(g_help_view_border_rect.right, cbsi.dwCursorPosition.Y);
            WriteConsole(ConsoleHandle, Buffer + startChar, charsToWrite, &Dummy, 0);
        }

        return 0;
    }
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

BOOL FileTimeToString(const FILETIME* pFileTime, LPSTR lpString, int cchString)
{
    ULARGE_INTEGER largeInteger;
    largeInteger.LowPart = pFileTime->dwLowDateTime;
    largeInteger.HighPart = pFileTime->dwHighDateTime;

    ULONGLONG ticks = largeInteger.QuadPart; // Convert to 100-nanosecond intervals
    ULONGLONG seconds = ticks / 10000000; // Convert to seconds
    ULONGLONG minutes = seconds / 60;
    ULONGLONG hours = minutes / 60;
    ULONGLONG days = hours / 24;

    ULONGLONG remainingHours = hours % 24;
    ULONGLONG remainingMinutes = minutes % 60;
    ULONGLONG remainingSeconds = seconds % 60;

    wnsprintf(lpString, cchString, _T("%02d:%02d:%02d:%02d"), days, remainingHours, remainingMinutes, remainingSeconds);

    return 0;
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

        SYSTEMTIME lt;
        GetLocalTime(&lt);
        toFill->second = lt.wSecond;
        toFill->minute = lt.wMinute;
        toFill->hour = lt.wHour;
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

    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof (statex);

    GlobalMemoryStatusEx (&statex);
    toFill->availableMemory = statex.ullTotalPhys;
    toFill->totalMemory = statex.ullAvailPhys;
    toFill->memoryPercent = statex.dwMemoryLoad;
    toFill->numberOfProcesses = g_numberOfProcesses;
    toFill->numberOfThreads = g_numberOfThreads;
}

static int get_processor_number()
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return (int)info.dwNumberOfProcessors;
}

void add_io_reading(float reads, float writes)
{
    if(!g_ioReadings)
    {
        g_ioReadings = calloc(1, sizeof(IoReading));
        g_ioReadings->readsPerSecond = reads;
        g_ioReadings->writesPerSecond = writes;
        g_lastIoReading = g_ioReadings;
        g_cpuReadingIndex = 1;
    }
    else
    {
        IoReading *ioReading = calloc(1, sizeof(IoReading));
        ioReading->readsPerSecond = reads;
        ioReading->writesPerSecond = writes;
        g_lastIoReading->next = ioReading;
        g_lastIoReading = ioReading;

        if(g_ioReadingIndex >= NUMBER_OF_IO_READINGS - 1)
        {
            IoReading *firstReading = g_ioReadings;
            g_ioReadings = firstReading->next;
            free(firstReading);
        }
        else
        {
            g_ioReadingIndex++;
        }
    }

    if(reads > g_largestIoReading)
    {
        g_largestIoReading = reads;
    }
}

void add_memory_reading()
{
    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof (statex);
    GlobalMemoryStatusEx (&statex);

    DWORD value = statex.dwMemoryLoad;

    if(!g_memoryReadings)
    {
        g_memoryReadings = calloc(1, sizeof(MemoryReading));
        g_memoryReadings->value = value;
        g_lastMemoryReading = g_memoryReadings;
        g_memoryReadingIndex = 1;
    }
    else
    {
        MemoryReading *memoryReading = calloc(1, sizeof(MemoryReading));
        memoryReading->value = value;
        g_lastMemoryReading->next = memoryReading;
        g_lastMemoryReading = memoryReading;

        if(g_memoryReadingIndex >= NUMBER_OF_MEMORY_READINGS - 1)
        {
            MemoryReading *firstReading = g_memoryReadings;
            g_memoryReadings = firstReading->next;
            free(firstReading);
        }
        else
        {
            g_memoryReadingIndex++;
        }
    }

    if(value > g_largestMemoryReading)
    {
        g_largestMemoryReading = value;
    }
}

CpuReading *add_cpu_reading(void)
{
    CpuReading *result;
    if(!g_cpuReadings)
    {
        g_cpuReadings = calloc(1, sizeof(CpuReading));
        g_cpuReadings->processes = calloc(g_numberOfProcesses, sizeof(Process));
        fill_cpu_usage(g_cpuReadings, NULL);
        g_lastCpuReading = g_cpuReadings;
        g_cpuReadingIndex = 1;
        result = g_cpuReadings;
    }
    else
    {
        CpuReading *cpuReading = calloc(1, sizeof(CpuReading));
        cpuReading->processes = calloc(g_numberOfProcesses, sizeof(Process*));
        fill_cpu_usage(cpuReading, g_lastCpuReading);
        cpuReading->previous = g_lastCpuReading;
        g_lastCpuReading->next = cpuReading;
        g_lastCpuReading = cpuReading;

        if(g_cpuReadingIndex >= NUMBER_OF_CPU_READINGS - 1)
        {
            CpuReading *firstReading = g_cpuReadings;
            g_cpuReadings = firstReading->next;

            for(int i = 0; i < firstReading->numberOfProcesses; i++)
            {
                free(firstReading->processes[i]->name);
                free(firstReading->processes[i]);
            }

            free(firstReading->processes);
            free(firstReading);

            g_cpuReadings->previous = NULL;
        }
        else
        {
            g_cpuReadingIndex++;
        }

        result = cpuReading;
    }

    for(int i = 0; i < g_numberOfProcesses; i++)
    {
        result->processes[i] = g_processes[i];
    }

    if(result->value > g_largestCpuReading)
    {
        g_largestCpuReading = result->value;
    }

    return result;
}

void paint_graph_axis_markers(SHORT areaLeft, SHORT areaBottom, SHORT axisLeft, SHORT axisTop, float maxValue, float axisMax)
{
    CHAR ioAxisStr[MAX_PATH];
    sprintf_s(
            ioAxisStr,
            25,
            "%-8.1f",
            axisMax);
    SetConCursorPos(axisLeft, axisTop);

    CHAR maxStr[MAX_PATH];
    sprintf_s(
            maxStr,
            25,
            "%-8.1f",
            maxValue);
    ConPrintf("(y) %s  (max) %s", ioAxisStr, maxStr);

    for(SHORT i = 0; i < 10; i++)
    {
        SetConCursorPos(areaLeft, areaBottom - i);
        ConPutc('_');
    }
}

void paint_graph_column(SHORT areaLeft, SHORT areaBottom, SHORT readingNumber, SHORT value)
{
    SetColor(FOREGROUND_INTENSITY);
    WCHAR lowerEigth = { 0x2581 };
    WCHAR lowerQuarter = { 0x2582 };
    WCHAR lowerHalf = { 0x2583 };
    WCHAR fullBar = { 0x2588 };
    SHORT numberOfFullNumbers = value / 10;
    for(SHORT i = 0; i < 10; i++)
    {
        SetConCursorPos(areaLeft + readingNumber, areaBottom - i);
        if(i == numberOfFullNumbers)
        {
            int remainder = value % 10;
            if(remainder == 0)
            {
                ConPutc(' ');
            }
            else if(remainder <= 2)
            {
                ConPutcW(lowerEigth);
            }
            else if(remainder < 5)
            {
                ConPutcW(lowerQuarter);
            }
            else if(remainder >= 5)
            {
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

void paint_cpu_graph_window()
{
    SHORT width = g_cpu_graph_border_right - g_cpu_graph_border_left;
    SHORT middle = width / 2;
    SHORT middleOfHeader = (SHORT)strlen(" CPU ") / 2;
    SHORT headerLeft = middle - middleOfHeader;

    SetConCursorPos(g_cpu_graph_border_left + headerLeft, g_cpu_graph_border_top);
    ConPrintf(" %s ", "CPU (%)");
    paint_graph_axis_markers(g_cpu_graph_border_left + 1, g_cpu_graph_bottom, g_cpu_graph_axis_left, g_cpu_graph_top, g_largestCpuReading, 100);
    SHORT readingIndex = 0;

    CpuReading *currentReading = g_cpuReadings;
    while(currentReading)
    {
        paint_graph_column(g_cpu_graph_left, g_cpu_graph_bottom, readingIndex, currentReading->value);
        currentReading = currentReading->next;
        readingIndex++;
    }
}

void paint_io_graph_window()
{
    SHORT width = g_io_graph_border_right - g_io_graph_border_left;
    SHORT middle = width / 2;
    SHORT middleOfHeader = (SHORT)strlen(" IO ") / 2;
    SHORT headerLeft = middle - middleOfHeader;

    SetConCursorPos(g_io_graph_border_left + headerLeft, g_io_graph_border_top);
    ConPrintf(" %s ", "IO");

    paint_graph_axis_markers(g_io_graph_left, g_io_graph_bottom, g_io_graph_axis_left, g_io_graph_top, g_largestIoReading, g_largestIoReading + 1000);
    SHORT readingIndex = 0;

    IoReading *currentReading = g_ioReadings;
    while(currentReading)
    {
        SHORT value = (SHORT)((currentReading->readsPerSecond / (g_largestIoReading + 1000)) * 100);
        paint_graph_column(g_io_graph_left + (SHORT)1, g_io_graph_bottom, readingIndex, value);
        currentReading = currentReading->next;
        readingIndex++;
    }
}

void paint_memory_graph_window()
{
    SHORT width = g_memory_graph_border_right - g_memory_graph_border_left;
    SHORT middle = width / 2;
    SHORT middleOfHeader = (SHORT)strlen(" memory ") / 2;
    SHORT headerLeft = middle - middleOfHeader;

    SetConCursorPos(g_memory_graph_border_left + headerLeft, g_memory_graph_border_top);
    ConPrintf(" %s ", "Mem (%)");

    paint_graph_axis_markers(g_memory_graph_left, g_memory_graph_bottom, g_memory_graph_axis_left, g_memory_graph_top, g_largestMemoryReading, 100);
    SHORT readingIndex = 0;

    MemoryReading *currentReading = g_memoryReadings;
    while(currentReading)
    {
        paint_graph_column(g_memory_graph_left + (SHORT)1, g_memory_graph_bottom, readingIndex, currentReading->value);
        currentReading = currentReading->next;
        readingIndex++;
    }
}

void refreshsummary()
{
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

    if(cpuPercent > g_MaxCpuPercent)
    {
        g_MaxCpuPercent = cpuPercent;
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

    if(g_lastIoReading)
    {
        SetConCursorPos(g_summary_view_left, g_summary_view_top + 8);
        ConPrintf(
                "%-12s %13.1f",
                "IO Reads:",
                g_lastIoReading->readsPerSecond);

        SetConCursorPos(g_summary_view_left, g_summary_view_top + 9);
        ConPrintf(
                "%-12s %13.1f",
                "IO Writes:",
                g_lastIoReading->writesPerSecond);
    }
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
    process->cpuUserTime = processUserTime;
    process->cpuKernelTime = kernel_time;

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

int CompareProcessForSort(const void *a, const void *b)
{
    Process *processA = *(Process **)a;
    Process *processB = *(Process **)b;

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

void populate_focused_process(Process *process)
{
    process->pid = g_focusedProcessPid;
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, process->pid);
    if(hProcess)
    {
        populate_process_from_pid(process, hProcess);
        ProcessTime *lastProcessTime = find_process_time_by_pid(process->pid);
        populate_process_cpu_usage(process, lastProcessTime, hProcess);
        ProcessIo *lastProcessIo = find_process_io_by_pid(process->pid);
        populate_process_io(process, lastProcessIo, hProcess);
        CloseHandle(hProcess);
    }
}

void populate_process(Process *process, PROCESSENTRY32 *pEntry)
{
    process->name = StrDupA(pEntry->szExeFile);
    process->pid = pEntry->th32ProcessID;
    process->numberOfThreads = pEntry->cntThreads;
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pEntry->th32ProcessID);
    if(hProcess)
    {
        populate_process_from_pid(process, hProcess);
        ProcessTime *lastProcessTime = find_process_time_by_pid(process->pid);
        populate_process_cpu_usage(process, lastProcessTime, hProcess);
        ProcessIo *lastProcessIo = find_process_io_by_pid(process->pid);
        populate_process_io(process, lastProcessIo, hProcess);
        CloseHandle(hProcess);
    }
}

void populate_processes(CpuReading *reading)
{
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPALL, 0);

    PROCESSENTRY32 pEntry = {0};
    pEntry.dwSize = sizeof(PROCESSENTRY32);
    unsigned int count = 0;

    float reads = 0;
    float writes = 0;
    g_numberOfThreads = 0;
    do
    {
        if(pEntry.szExeFile[0] != '\0')
        {
            g_processes[count] = calloc(1, sizeof(Process));
            populate_process(g_processes[count], &pEntry);
            g_numberOfThreads += g_processes[count]->numberOfThreads;
            reads = reads + g_processes[count]->ioReadsPerSecond;
            writes = writes + g_processes[count]->ioWritesPerSecond;
            count++;
        }
    } while(Process32Next(hSnapshot, &pEntry));

    reading->readsPerSecond = reads;
    reading->writesPerSecond = writes;
    add_io_reading(reads, writes);

    g_numberOfProcesses = count;
    for(int i = 0; i < g_numberOfProcesses; i++)
    {
        g_lastProcessTimes[i].pid = g_processes[i]->pid;
        g_lastProcessTimes[i].systemTime = g_processes[i]->cpuSystemTime;
        g_lastProcessTimes[i].processTime = g_processes[i]->cpuProcessTime;
    }

    g_numberOfLastProcessTimes = g_numberOfProcesses;

    for(int i = 0; i < g_numberOfProcesses; i++)
    {
        g_lastProcessIoReadings[i].pid = g_processes[i]->pid;
        g_lastProcessIoReadings[i].systemTime = g_processes[i]->ioSystemTime;
        g_lastProcessIoReadings[i].reads = g_processes[i]->ioReads;
        g_lastProcessIoReadings[i].writes = g_processes[i]->ioWrites;
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
            if(g_processes[i]->name)
            {
                score = fzf_get_score(g_processes[i]->name, pattern, slab);
            }
            if(score > 0)
            {
                g_displayProcesses[numberOfDisplayItems] = g_processes[i];
                numberOfDisplayItems++;
            }
        }
    }
    else
    {
        for(int i = 0; i < g_numberOfProcesses; i++)
        {
            g_displayProcesses[numberOfDisplayItems] = g_processes[i];
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

void print_focused_process(Process *process)
{
    if(!process->pid)
    {
        return;
    }

    FILETIME creationTime, exitTime, kernelTime, userTime;
    CHAR lpImageFileName[MAX_PATH];
    PROCESS_MEMORY_COUNTERS_EX pmc = {0};
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, process->pid);
    PSAPI_WORKING_SET_EX_INFORMATION wsInfo = { 0 };
    if(hProcess)
    {
        GetProcessTimes(hProcess, &creationTime, &exitTime, &kernelTime, &userTime);
        GetProcessImageFileNameA(
                hProcess,
                lpImageFileName,
                MAX_PATH);
        GetProcessMemoryInfo(hProcess, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
        QueryWorkingSetEx(hProcess, &wsInfo, sizeof(wsInfo));
    }

    SetConCursorPos(g_help_view_rect.left, g_help_view_rect.top + 0);
    DialogConPrintf("Process");

    SetConCursorPos(g_help_view_rect.left, g_help_view_rect.top + 1);
    DialogConPrintf(
            "%-25s %d",
            "Pid:",
            g_focusedProcessPid);

    SetConCursorPos(g_help_view_rect.left, g_help_view_rect.top + 2);
    DialogConPrintf(
            "%-25s %s",
            "Path:",
            lpImageFileName);

    SetConCursorPos(g_help_view_rect.left, g_help_view_rect.top + 3);
    DialogConPrintf("");

    SetConCursorPos(g_help_view_rect.left, g_help_view_rect.top + 4);
    DialogConPrintf("Memory");

    SetConCursorPos(g_help_view_rect.left, g_help_view_rect.top + 5);
    DialogConPrintf(
            "%-35s %20d",
            "Working Set:",
            process->workingSet);

    SetConCursorPos(g_help_view_rect.left, g_help_view_rect.top + 6);
    DialogConPrintf(
            "%-35s %20d",
            "Private Bytes:",
            process->privateBytes);

    TCHAR pageFaultCountStr[MAX_PATH];
    FormatNumber(pageFaultCountStr, pmc.PageFaultCount / 1024, &g_numFmt);
    SetConCursorPos(g_help_view_rect.left, g_help_view_rect.top + 7);
    DialogConPrintf(
            "%-35s %20s",
            "Page Fault Count",
            pageFaultCountStr);

    TCHAR peakWorkingSetStr[MAX_PATH];
    FormatNumber(peakWorkingSetStr, pmc.PeakWorkingSetSize / 1024, &g_numFmt);
    SetConCursorPos(g_help_view_rect.left, g_help_view_rect.top + 8);
    DialogConPrintf(
            "%-35s %20s",
            "Peak Working Set Size",
            peakWorkingSetStr);

    TCHAR workingSetStr[MAX_PATH];
    FormatNumber(workingSetStr, pmc.WorkingSetSize / 1024, &g_numFmt);
    SetConCursorPos(g_help_view_rect.left, g_help_view_rect.top + 9);
    DialogConPrintf(
            "%-35s %20s",
            "Working Set Size",
            workingSetStr);

    TCHAR quotaPeakPagedPoolUsageStr[MAX_PATH];
    FormatNumber(quotaPeakPagedPoolUsageStr, pmc.QuotaPeakPagedPoolUsage / 1024, &g_numFmt);
    SetConCursorPos(g_help_view_rect.left, g_help_view_rect.top + 10);
    DialogConPrintf(
            "%-35s %20s",
            "Quota Peak Paged Pool Usage:",
            quotaPeakPagedPoolUsageStr);

    TCHAR quotaPagedPoolUsageStr[MAX_PATH];
    FormatNumber(quotaPagedPoolUsageStr, pmc.QuotaPagedPoolUsage / 1024, &g_numFmt);
    SetConCursorPos(g_help_view_rect.left, g_help_view_rect.top + 11);
    DialogConPrintf(
            "%-35s %20s",
            "Quota Paged Pool Usage:",
            quotaPagedPoolUsageStr);

    TCHAR quotaPeakNonPagedPoolUsageStr[MAX_PATH];
    FormatNumber(quotaPeakNonPagedPoolUsageStr, pmc.QuotaPeakNonPagedPoolUsage / 1024, &g_numFmt);
    SetConCursorPos(g_help_view_rect.left, g_help_view_rect.top + 12);
    DialogConPrintf(
            "%-35s %20s",
            "Quota Peak Non Paged Pool Usage:",
            quotaPeakNonPagedPoolUsageStr);

    TCHAR quotaNonPagedPoolUsageStr[MAX_PATH];
    FormatNumber(quotaNonPagedPoolUsageStr, pmc.QuotaNonPagedPoolUsage / 1024, &g_numFmt);
    SetConCursorPos(g_help_view_rect.left, g_help_view_rect.top + 13);
    DialogConPrintf(
            "%-35s %20s",
            "Quota Non Paged Pool Usage:",
            quotaNonPagedPoolUsageStr);

    TCHAR pageFileUsageStr[MAX_PATH];
    FormatNumber(pageFileUsageStr, pmc.PagefileUsage / 1024, &g_numFmt);
    SetConCursorPos(g_help_view_rect.left, g_help_view_rect.top + 13);
    DialogConPrintf(
            "%-35s %20s",
            "Page File Usage:",
            pageFileUsageStr);

    TCHAR peakPageFileUsageStr[MAX_PATH];
    FormatNumber(peakPageFileUsageStr, pmc.PeakPagefileUsage / 1024, &g_numFmt);
    SetConCursorPos(g_help_view_rect.left, g_help_view_rect.top + 14);
    DialogConPrintf(
            "%-35s %20s",
            "Peak Page File Usage:",
            peakPageFileUsageStr);

    TCHAR privateUsageStr[MAX_PATH];
    FormatNumber(privateUsageStr, pmc.PrivateUsage / 1024, &g_numFmt);
    SetConCursorPos(g_help_view_rect.left, g_help_view_rect.top + 15);
    DialogConPrintf(
            "%-35s %20s",
            "Private Usage:",
            privateUsageStr);

    TCHAR systemTimeStr[MAX_PATH];
    format_time(systemTimeStr, process->cpuSystemTime);
    SetConCursorPos(g_help_view_rect.left, g_help_view_rect.top + 16);
    DialogConPrintf("");

    TCHAR processTimeStr[MAX_PATH];
    format_time(processTimeStr, process->cpuProcessTime);
    SetConCursorPos(g_help_view_rect.left, g_help_view_rect.top + 17);
    DialogConPrintf("CPU");

    SetConCursorPos(g_help_view_rect.left, g_help_view_rect.top + 18);
    DialogConPrintf(
            "%-20s %13.1f %%",
            "Cpu Percent:",
            process->cpuPercent);

    TCHAR processUserTimeStr[MAX_PATH];
    FileTimeToString(&userTime, processUserTimeStr, MAX_PATH);
    SetConCursorPos(g_help_view_rect.left, g_help_view_rect.top + 19);
    DialogConPrintf(
            "%-20s %15s",
            "Cpu User Time:",
            processUserTimeStr);

    TCHAR processKernelTimeStr[MAX_PATH];
    FileTimeToString(&kernelTime, processKernelTimeStr, MAX_PATH);
    SetConCursorPos(g_help_view_rect.left, g_help_view_rect.top + 20);
    DialogConPrintf(
            "%-20s %15s",
            "Cpu Kernel Time:",
            processKernelTimeStr);
}

void draw_focused_process_window(void)
{
    draw_rect(&g_help_view_border_rect);
    clear_rect(&g_help_view_rect);

    Process process;
    populate_focused_process(&process);
    print_focused_process(&process);
}

void focus_current_process(void)
{
    draw_focused_process_window();
    g_focusedProcessPid = g_processes[g_selectedIndex]->pid;
    g_mode = ProcessDetails;
}

DWORD WINAPI StatRefreshThreadProc(LPVOID lpParam)
{
    UNREFERENCED_PARAMETER(lpParam);

    while(1)
    {
        EnterCriticalSection(&SyncLock);
        add_memory_reading();
        CpuReading *reading = add_cpu_reading();
        populate_processes(reading);
        if(g_mode == ProcessDetails)
        {
            Process process;
            populate_focused_process(&process);
            print_focused_process(&process);
        }
        else
        {
            qsort(g_processes, g_numberOfProcesses, sizeof(Process*), CompareProcessForSort);
            print_processes();
        }

        refreshsummary();
        paint_cpu_graph_window();
        paint_io_graph_window();
        paint_memory_graph_window();
        LeaveCriticalSection(&SyncLock);

        Sleep(1000);
    }

    return 0;
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
    g_summary_view_border_bottom = 12;
    g_summary_view_border_left = 0;
    g_summary_view_border_right = Width;

    g_cpu_graph_border_left = g_summary_view_border_left + 30;

    SHORT graphsWidth = g_summary_view_right - g_cpu_graph_border_left;

    g_cpu_graph_border_top = g_summary_view_border_top;
    g_cpu_graph_border_bottom = g_summary_view_border_bottom;
    g_cpu_graph_border_right = (graphsWidth / 3) + 30;

    g_cpu_graph_axis_left = g_cpu_graph_border_left + 1;

    g_cpu_graph_top = g_summary_view_top;
    g_cpu_graph_bottom = g_cpu_graph_border_bottom - 1;
    g_cpu_graph_left = g_cpu_graph_axis_left + 1;
    g_cpu_graph_right = g_cpu_graph_border_right - 1;

    NUMBER_OF_CPU_READINGS = g_cpu_graph_right - g_cpu_graph_axis_left;

    g_io_graph_border_top = g_summary_view_border_top;
    g_io_graph_border_bottom = g_summary_view_border_bottom;
    g_io_graph_border_left = g_cpu_graph_border_right;
    g_io_graph_border_right = g_io_graph_border_left + (graphsWidth / 3);

    g_io_graph_axis_left = g_io_graph_border_left + 1;

    g_io_graph_top = g_summary_view_top;
    g_io_graph_bottom = g_io_graph_border_bottom - 1;
    g_io_graph_left = g_io_graph_axis_left + 1;
    g_io_graph_right = g_io_graph_border_right - 1;

    NUMBER_OF_IO_READINGS = g_io_graph_right - g_io_graph_axis_left - 2;

    g_memory_graph_border_top = g_summary_view_border_top;
    g_memory_graph_border_bottom = g_summary_view_border_bottom;
    g_memory_graph_border_left = g_io_graph_border_right;
    g_memory_graph_border_right = g_summary_view_border_right;

    g_memory_graph_axis_left = g_memory_graph_border_left + 1;

    g_memory_graph_top = g_summary_view_top;
    g_memory_graph_bottom = g_memory_graph_border_bottom - 1;
    g_memory_graph_left = g_memory_graph_axis_left + 1;
    g_memory_graph_right = g_memory_graph_border_right - 1;

    NUMBER_OF_MEMORY_READINGS = g_memory_graph_right - g_memory_graph_axis_left - 2;

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

    g_help_view_border_rect.top = 15;
    g_help_view_border_rect.left = 15;
    g_help_view_border_rect.bottom = Height - 15;
    g_help_view_border_rect.right = Width - 15;

    g_help_view_rect.top = g_help_view_border_rect.top + 1;
    g_help_view_rect.left = g_help_view_border_rect.left + 1;
    g_help_view_rect.bottom =g_help_view_border_rect.bottom - 1;
    g_help_view_rect.right = g_help_view_border_rect.right - 1;

    g_processes_view_number_of_display_lines = g_processes_view_bottom - g_processes_view_top + 1;

    int nonNameWidth = 1 + 8 + 1 + 20 + 1 + 8 + 1 + 20 + 1 + 8 + 1 + 8 + 1 + 8 + 13;
    g_nameWidth = g_processes_view_right - g_processes_view_left - nonNameWidth;
}

void print_cpu_reading_at_index(CpuReading *reading, SHORT index)
{
    int columnWidth = 12;
    int windowWidth = g_help_view_rect.right - g_help_view_rect.left - 1;
    int numberOfColumns = 8;
    int timeWidth = windowWidth - (columnWidth * numberOfColumns);

    CHAR totalMemoryInUnits[MAX_PATH];
    fill_size_in_units(totalMemoryInUnits, MAX_PATH, reading->totalMemory);

    CHAR availableMemoryInUnits[MAX_PATH];
    fill_size_in_units(availableMemoryInUnits, MAX_PATH, reading->availableMemory);

    TCHAR memStr[MAX_PATH];
    sprintf_s(
            memStr,
            MAX_PATH,
            "%3d%%",
            reading->memoryPercent);

    if(index == g_selectedCpuReadingIndex)
    {
        SetColor(BACKGROUND_WHITE);
    }
    else
    {
        SetColor(FOREGROUND_INTENSITY);
    }

    CHAR timeStr[25];
    sprintf_s(
            timeStr,
            25,
            "%02d:%02d:%02d",
            reading->hour,
            reading->minute,
            reading->second);

    char percentStr[25];
    sprintf_s(
            percentStr,
            25,
            "%-d",
            reading->value);

    char numProcessStr[25];
    sprintf_s(
            numProcessStr,
            25,
            "%-8d",
            reading->numberOfProcesses);

    char numberOfThreadsStr[25];
    sprintf_s(
            numberOfThreadsStr,
            25,
            "%-8d",
            reading->numberOfThreads);

    char readsPerSecondStr[25];
    sprintf_s(
            readsPerSecondStr,
            25,
            "%-8.1f",
            reading->readsPerSecond);

    char writesPerSecondStr[25];
    sprintf_s(
            writesPerSecondStr,
            25,
            "%-8.1f",
            reading->writesPerSecond);

    SetConCursorPos(g_help_view_rect.left, g_help_view_rect.top + index + (SHORT)1);
    DialogConPrintf(
            "%-*s%-*s%-*s%-*s%-*s%-*s%-*s%-*s%-*s",
            timeWidth,
            timeStr,
            columnWidth,
            percentStr,
            columnWidth,
            totalMemoryInUnits,
            columnWidth,
            availableMemoryInUnits,
            columnWidth,
            memStr,
            columnWidth,
            numProcessStr,
            columnWidth,
            numberOfThreadsStr,
            columnWidth,
            readsPerSecondStr,
            columnWidth,
            writesPerSecondStr);
    reading = reading->next;
}

void draw_cpu_readings(void)
{
    SHORT maxLines = g_help_view_rect.bottom - g_help_view_rect.top - 1;

    CpuReading *current = g_lastDisplayCpuReading;
    SHORT i = 0;
    while(current && i <= maxLines)
    {
        print_cpu_reading_at_index(current, i);
        i++;
        current = current->previous;
    }
    SetColor(FOREGROUND_INTENSITY);
}

void update_and_draw_cpu_readings(void)
{
    int columnWidth = 12;
    int windowWidth = g_help_view_rect.right - g_help_view_rect.left - 1;
    int numberOfColumns = 8;
    int timeWidth = windowWidth - (columnWidth * numberOfColumns);

    g_lastDisplayCpuReading = g_lastCpuReading;
    g_selectedCpuReadingIndex = 0;
    draw_rect(&g_help_view_border_rect);
    clear_rect(&g_help_view_rect);

    SetColor(FOREGROUND_CYAN);
    SetConCursorPos(g_help_view_rect.left, g_help_view_rect.top);
    DialogConPrintf(
            "%-*s%-*s%-*s%-*s%-*s%-*s%-*s%-*s%-*s",
            timeWidth,
            "Time",
            columnWidth,
            "Cpu (%)",
            columnWidth,
            "Mem (T)",
            columnWidth,
            "Mem (A)",
            columnWidth,
            "Mem (%)",
            columnWidth,
            "Processes",
            columnWidth,
            "Threads",
            columnWidth,
            "Reads",
            columnWidth,
            "Writes");
    SetColor(FOREGROUND_INTENSITY);
    draw_cpu_readings();
}

void draw_process_history(void)
{
    int columnWidth = 12;
    int windowWidth = g_help_view_rect.right - g_help_view_rect.left - 1;
    int numberOfColumns = 8;
    int timeWidth = windowWidth - (columnWidth * numberOfColumns);
    SHORT maxItems = g_help_view_rect.bottom - g_help_view_rect.top;

    g_lastDisplayCpuReading = g_lastCpuReading;
    draw_rect(&g_help_view_border_rect);
    clear_rect(&g_help_view_rect);

    SetColor(FOREGROUND_CYAN);
    SetConCursorPos(g_help_view_rect.left, g_help_view_rect.top);

    DialogConPrintf(
            "%-*s%-*s%-*s%-*s%-*s%-*s%-*s%-*s%-*s",
            timeWidth,
            "Name",
            columnWidth,
            "PID",
            columnWidth,
            "UpTime",
            columnWidth,
            "Wk",
            columnWidth,
            "Pvt",
            columnWidth,
            "CPU(%)",
            columnWidth,
            "Threads",
            columnWidth,
            "Reads",
            columnWidth,
            "Writes");

    SetColor(FOREGROUND_INTENSITY);

    int j = 0;
    CpuReading *current = g_cpuReadings;
    CpuReading *readingToShow = NULL;
    while(current)
    {
        if(j == g_selectedCpuReadingIndex)
        {
            readingToShow = current;
            break;
        }
        current = current->next;
        j++;
    }

    if(readingToShow)
    {
        qsort(readingToShow->processes, readingToShow->numberOfProcesses - 1, sizeof(Process*), CompareProcessForSort);
        for(SHORT i = 0; i < readingToShow->numberOfProcesses && i < maxItems; i++)
        {
            SetConCursorPos(g_help_view_rect.left, g_help_view_rect.top + (SHORT)1 + i);
            char pidStr[25];
            sprintf_s(
                    pidStr,
                    25,
                    "%08d",
                    readingToShow->processes[i]->pid);

            char percentStr[25];
            sprintf_s(
                    percentStr,
                    25,
                    "%.1f",
                    readingToShow->processes[i]->cpuPercent);

            char numberOfThreadsStr[25];
            sprintf_s(
                    numberOfThreadsStr,
                    25,
                    "%.1d",
                    readingToShow->processes[i]->numberOfThreads);

            char readsPerSecondStr[25];
            sprintf_s(
                    readsPerSecondStr,
                    25,
                    "%.1f",
                    readingToShow->processes[i]->ioReadsPerSecond);

            char writesPerSecondStr[25];
            sprintf_s(
                    writesPerSecondStr,
                    25,
                    "%.1f",
                    readingToShow->processes[i]->ioWritesPerSecond);

            TCHAR upTimeStr[MAX_PATH];
            format_time(upTimeStr, readingToShow->processes[i]->upTime);

            CHAR privateBytesFormatedStr[MAX_PATH];
            FormatNumber(privateBytesFormatedStr, readingToShow->processes[i]->privateBytes, &g_numFmt);

            CHAR workingSetFormatedStr[MAX_PATH];
            FormatNumber(workingSetFormatedStr, readingToShow->processes[i]->workingSet, &g_numFmt);

            DialogConPrintf(
                    "%-*s%-*s%-*s%-*s%-*s%-*s%-*s%-*s%-*s",
                    timeWidth,
                    readingToShow->processes[i]->name,
                    columnWidth,
                    pidStr,
                    columnWidth,
                    upTimeStr,
                    columnWidth,
                    privateBytesFormatedStr,
                    columnWidth,
                    workingSetFormatedStr,
                    columnWidth,
                    percentStr,
                    columnWidth,
                    numberOfThreadsStr,
                    columnWidth,
                    readsPerSecondStr,
                    columnWidth,
                    writesPerSecondStr);
        }
    }
}

void draw_help_window(void)
{
    int numberOfLines = 17;
    TextLine textLines[MAX_PATH] = {
        { Header, "Navigation:" },
        { Plain, "j/down arrow: down" },
        { Plain, "k/up arrow: up" },
        { Plain, "esc: normal mode" },
        { Plain, "q: quit" },
        { Plain, "" },
        { Header, "Normal Mode:" },
        { Plain, "/: search mode" },
        { Plain, ".: reading history" },
        { Plain, "F1: Sort process list by Name" },
        { Plain, "F2: Sort process list by Private Bytes" },
        { Plain, "F3: Sort process list by Working Set" },
        { Plain, "F4: Sort process list by PID" },
        { Plain, "F5: Sort process list by CPU" },
        { Plain, "" },
        { Header, "Reading History" },
        { Plain, "esc: normal mode" },
        { Plain, "l: show prcess readings" },
        { Plain, "" },
        { Header, "Search Mode" },
        { Plain, "esc/enter: normal mode" },
    };
    draw_rect(&g_help_view_border_rect);
    clear_rect(&g_help_view_rect);

    for(SHORT i = 0; i < numberOfLines; i ++)
    {
        if(textLines[i].lineType == Header)
        {
            SetColor(FOREGROUND_CYAN);
        }
        SetConCursorPos(g_help_view_rect.left, g_help_view_rect.top + i);
        DialogConPrintf(
                "%s",
                textLines[i].text);
        if(textLines[i].lineType == Header)
        {
            SetColor(FOREGROUND_INTENSITY);
        }
    }
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
    draw_box(g_io_graph_border_top, g_io_graph_border_bottom, g_io_graph_border_left, g_io_graph_border_right - g_io_graph_border_left);
    draw_box(g_memory_graph_border_top, g_memory_graph_border_bottom, g_memory_graph_border_left, g_memory_graph_border_right - g_memory_graph_border_left);

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

    sm_EnableTokenPrivilege(SE_DEBUG_NAME);
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
                        if(g_mode != Search)
                        {
                            switch(InputRecord.Event.KeyEvent.uChar.AsciiChar)
                            {
                                case 'q':
                                    return EXIT_SUCCESS;
                                    break;
                            }
                        }
                        
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
                                case '/':
                                    EnterCriticalSection(&SyncLock);
                                    g_mode = Search;
                                    clear_screen();
                                    calcuate_layout();
                                    draw_summary_window();
                                    draw_search_view();
                                    draw_processes_window();
                                    LeaveCriticalSection(&SyncLock);
                                    break;
                                case 'l':
                                    focus_current_process();
                                    break;
                                case '?':
                                    draw_help_window();
                                    g_mode = Help;
                                    break;
                                case '.':
                                    EnterCriticalSection(&SyncLock);
                                    update_and_draw_cpu_readings();
                                    g_mode = ReadingsList;
                                    LeaveCriticalSection(&SyncLock);
                                    break;
                                case ',':
                                    EnterCriticalSection(&SyncLock);
                                    g_searchString[0] = '\0';
                                    g_searchStringIndex = 0;
                                    draw_processes_window();
                                    LeaveCriticalSection(&SyncLock);
                                    break;
                                default:
                                    break;
                            }
                            switch(InputRecord.Event.KeyEvent.wVirtualKeyCode)
                            {
                                case VK_DOWN:
                                case VK_J:
                                    g_selectedIndex++; EnterCriticalSection(&SyncLock);
                                    if(g_selectedIndex > 0)
                                    {
                                        print_process_at_index(g_selectedIndex - 1);
                                    }
                                    print_process_at_index(g_selectedIndex);
                                    LeaveCriticalSection(&SyncLock);
                                    break;
                                case VK_K:
                                case VK_UP:
                                    EnterCriticalSection(&SyncLock);
                                    if(g_selectedIndex > 0)
                                    {
                                        g_selectedIndex--;
                                        print_process_at_index(g_selectedIndex + 1);
                                        print_process_at_index(g_selectedIndex);
                                    }
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
                                case VK_RETURN:
                                case VK_ESCAPE:
                                    EnterCriticalSection(&SyncLock);
                                    g_mode = Normal;
                                    clear_screen();
                                    calcuate_layout();
                                    paint_cpu_graph_window();
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
                        else if(g_mode == ReadingsList)
                        {
                            switch(InputRecord.Event.KeyEvent.wVirtualKeyCode)
                            {
                                case VK_J:
                                case VK_DOWN:
                                    EnterCriticalSection(&SyncLock);
                                    g_selectedCpuReadingIndex++;
                                    draw_cpu_readings();
                                    LeaveCriticalSection(&SyncLock);
                                    break;
                                case VK_K:
                                case VK_UP:
                                    EnterCriticalSection(&SyncLock);
                                    if(g_selectedCpuReadingIndex > 0)
                                    {
                                        g_selectedCpuReadingIndex--;
                                        draw_cpu_readings();
                                    }
                                    LeaveCriticalSection(&SyncLock);
                                    break;
                                case 0x4C:
                                    EnterCriticalSection(&SyncLock);
                                    draw_process_history();
                                    LeaveCriticalSection(&SyncLock);
                                    break;
                                case VK_ESCAPE:
                                    g_mode = Normal;
                                    EnterCriticalSection(&SyncLock);
                                    draw_summary_window();
                                    draw_search_view();
                                    draw_processes_window();
                                    LeaveCriticalSection(&SyncLock);
                                    break;
                            }
                        }
                        else if(g_mode == Help || g_mode == ProcessDetails || g_mode == ReadingsList)
                        {
                            switch(InputRecord.Event.KeyEvent.uChar.AsciiChar)
                            {
                                case VK_ESCAPE:
                                    g_mode = Normal;
                                    EnterCriticalSection(&SyncLock);
                                    draw_summary_window();
                                    draw_search_view();
                                    draw_processes_window();
                                    LeaveCriticalSection(&SyncLock);
                                    break;
                                case 'q':
                                    return EXIT_SUCCESS;
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
