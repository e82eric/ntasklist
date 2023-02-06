#include <curses.h>
#include <stdlib.h>
#include <windows.h>
#include <psapi.h>
#include <shlwapi.h>
#include <tlhelp32.h>
#include "fzf\\fzf.h"

#ifndef CTRL
#define CTRL(c) ((c) & 037)
#endif

#define COLOR_GREY 67
#define NORMAL_UNSELECTED_TEXT_PAIR 1
#define NORMAL_SELECTED_TEXT_PAIR 2
#define FUZZYMATCH_UNSELECTED_TEXT 3
#define FUZZYMATCH_SELECTED_TEXT 4
#define HEADER_TEXT_PAIR 5

NUMBERFMT numFmt = { 0 };

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

WINDOW *window;
WINDOW *headerWindow;
WINDOW *summaryWindow;
WINDOW *searchWindow;

Process g_processes[1024];
Process *g_displayProcesses[1024];
int g_numberOfProcesses = 0;

ProcessTime **g_lastProcessTimes;
int g_numberOfLastProcessTimes;

ProcessIo **g_lastProcessIoReadings;
int g_numberOfLastProcessIoReadings;

ULONGLONG g_lastTimeOfProcessListRefresh;
int g_sortColumn = 5;
char g_searchStirng[1024];
int g_searchStringIndex = 0;
int g_processor_count_;
int g_numberOfThreads;

int g_numberOfDisplayItems = 0;
int g_selectedIndex = 0;

int g_nameWidth = 0;
fzf_slab_t *slab;

int g_scrollOffset = 0;

enum Mode g_mode = Normal;

int g_lastChar;
ULONGLONG g_lastCharTime;

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

int CompareProcessPrivateBytes(const void *a, const void *b)
{
    Process *processA = (Process *)a;
    Process *processB = (Process *)b;

    if(g_sortColumn == 2)
    {
        return (processB->privateBytes - processA->privateBytes);
    }
    if(g_sortColumn == 3)
    {
        return (processB->workingSet - processA->workingSet);
    }
    if(g_sortColumn == 4)
    {
        return (processB->pid - processA->pid);
    }
    if(g_sortColumn == 5)
    {
        return ((processB->cpuPercent * 10) - (processA->cpuPercent * 10));
    }
    return 0;
}

static int get_processor_number()
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return (int)info.dwNumberOfProcessors;
}

unsigned __int64 ConvertFileTimeToInt64(FILETIME *fileTime)
{
    ULARGE_INTEGER result;

    result.LowPart  = fileTime->dwLowDateTime;
    result.HighPart = fileTime->dwHighDateTime;

    return result.QuadPart;
}

BOOL populate_process_io_from_pid(Process *process, HANDLE processHandle, ProcessIo *lastProcessIoReading, ProcessIo *processIoReadingToFillout)
{
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    IO_COUNTERS counters;
    if(!GetProcessIoCounters(processHandle, &counters))
    {
        return FALSE;
    }

    unsigned __int64 systemTime = ConvertFileTimeToInt64(&now);
    processIoReadingToFillout->systemTime = systemTime;
    processIoReadingToFillout->reads = counters.ReadOperationCount;
    processIoReadingToFillout->writes = counters.WriteOperationCount;

    if(!lastProcessIoReading)
    {
        return TRUE;
    }

    unsigned __int64 timeDiff = processIoReadingToFillout->systemTime - lastProcessIoReading->systemTime;
    float numberOfSecondsBetweenReadings = (float)timeDiff / (float)10000000;
    unsigned __int64 readDiff = processIoReadingToFillout->reads - lastProcessIoReading->reads;
    unsigned __int64 writeDiff = processIoReadingToFillout->writes - lastProcessIoReading->writes;

    float readsPerSecond = readDiff / numberOfSecondsBetweenReadings;
    float writesPerSecond = writeDiff / numberOfSecondsBetweenReadings;

    process->ioReadsPerSecond = readsPerSecond;
    process->ioWritesPerSecond = writesPerSecond;

    return TRUE;
}

BOOL get_process_cpu_usage(Process *process, ProcessTime *existingProcessTime, ProcessTime *processTimeToPopulate, HANDLE hProcess)
{
    FILETIME now;
    FILETIME creation_time;
    FILETIME exit_time;
    FILETIME kernel_time;
    FILETIME user_time;
    ULONGLONG processTimeDifference;
    ULONGLONG systemTimeDifference;

    float cpu = -1;

    if(process->pid == 0)
    {
        process->cpuPercent = 0;
        return FALSE;
    }

    GetSystemTimeAsFileTime(&now);
    if(!GetProcessTimes(hProcess, &creation_time, &exit_time, &kernel_time, &user_time))
    {
        process->cpuPercent = 0;
        return FALSE;
    }

    unsigned __int64 systemTotalTime = ConvertFileTimeToInt64(&now);
    unsigned __int64 processUserTime = ConvertFileTimeToInt64(&user_time) / g_processor_count_;
    unsigned __int64 processKernelTime = ConvertFileTimeToInt64(&kernel_time) / g_processor_count_;
    unsigned __int64 processTotalTime = processUserTime + processKernelTime;

    processTimeToPopulate->systemTime = systemTotalTime;
    processTimeToPopulate->processTime = processTotalTime;

    if(!existingProcessTime)
    {
        process->cpuPercent = 0;
        return TRUE;
    }

    processTimeDifference = processTotalTime - existingProcessTime->processTime;
    systemTimeDifference = systemTotalTime - existingProcessTime->systemTime;

    if(systemTimeDifference == 0)
    {
        process->cpuPercent = 0;
        return TRUE;
    }

    cpu = ((float)processTimeDifference * 100 / (float)systemTimeDifference);

    process->cpuPercent = cpu;

    return TRUE;
}

void populate_process_from_pid(Process *process, HANDLE hProcess)
{
    PROCESS_MEMORY_COUNTERS_EX pmc;
    CHAR lpImageFileName[MAX_PATH] = TEXT("<unknown>");
    LPCSTR processShortFileName = TEXT("<unknown>");
    GetProcessMemoryInfo(hProcess, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));

    if(NULL != hProcess)
    {
        GetProcessImageFileNameA(
                hProcess,
                lpImageFileName,
                MAX_PATH);
        processShortFileName = PathFindFileNameA(lpImageFileName);
    }

    process->privateBytes = pmc.PrivateUsage / 1024;
    process->workingSet = pmc.WorkingSetSize / 1024;

    if(process->name)
    {
        free(process->name);
    }
    process->name = _strdup(processShortFileName);
}

void populate_processes(void)
{
    DWORD pids[1024], numberOfPidBytes;
    EnumProcesses(pids, sizeof(pids), &numberOfPidBytes);
    int numEnumProcesses = numberOfPidBytes / sizeof(DWORD);

    int numberOfProcesses = 0;
    for(int i = 0; i < numEnumProcesses; i++)
    {
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pids[i]);
        if(hProcess)
        {
            if(g_processes[numberOfProcesses].name)
            {
                free(g_processes[numberOfProcesses].name);
            }
            ZeroMemory(&g_processes[numberOfProcesses], sizeof(Process));
            g_processes[numberOfProcesses].pid = pids[i];
            populate_process_from_pid(&g_processes[numberOfProcesses], hProcess);
            numberOfProcesses++;
        }
        CloseHandle(hProcess);
    }

    g_numberOfProcesses = numberOfProcesses;
}

ProcessTime* find_process_time_by_pid(DWORD pid)
{
    for(int i = 0; i < g_numberOfLastProcessTimes; i++)
    {
        if(g_lastProcessTimes[i]->pid == pid)
        {
            return g_lastProcessTimes[i];
        }
    }

    return NULL;
}

ProcessIo* find_process_io_by_pid(DWORD pid)
{
    for(int i = 0; i < g_numberOfLastProcessIoReadings; i++)
    {
        if(g_lastProcessIoReadings[i]->pid == pid)
        {
            return g_lastProcessIoReadings[i];
        }
    }

    return NULL;
}

void populate_cpu_stats()
{
    ProcessSnapshot snapshotEntries[4096];

    HANDLE const snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPALL, 0);
    PROCESSENTRY32 entry = { 0 };
    entry.dwSize = sizeof(entry);
    Process32First(snapshot, &entry);

    BOOL ret = TRUE;
    int numberOfSnapshots = 0;
    g_numberOfThreads = 0;
    while(ret)
    {
        numberOfSnapshots++;
        ret = Process32Next(snapshot, &entry);
        snapshotEntries[numberOfSnapshots].pid = entry.th32ProcessID;
        snapshotEntries[numberOfSnapshots].numberOfThreads = entry.cntThreads;
        g_numberOfThreads += entry.cntThreads;
    }

    CloseHandle(snapshot);

    int numberOfProcessTimes = 0;
    int numberOfIoReadings = 0;
    ProcessTime **newProcessTimes = calloc(g_numberOfProcesses, sizeof(ProcessTime*));
    ProcessIo **newIoReadings = calloc(g_numberOfProcesses, sizeof(ProcessIo*));
    for(int i = 0; i < g_numberOfProcesses; i++)
    {
        for(int j = 0; j < numberOfSnapshots; j++)
        {
            if(g_processes[i].pid == snapshotEntries[j].pid)
            {
                g_processes[i].numberOfThreads = snapshotEntries[j].numberOfThreads;
                break;
            }
        }

        DWORD pid = g_processes[i].pid;

        HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
        if(hProcess)
        {
            ProcessTime *processTimeToPopulate = calloc(1, sizeof(ProcessTime));
            processTimeToPopulate->pid = pid;
            ProcessIo *processIoToPopulate = calloc(1, sizeof(ProcessIo));
            processIoToPopulate->pid = pid;
            ProcessTime *existingProcessTime = find_process_time_by_pid(pid);
            ProcessIo *existingProcessIo = find_process_io_by_pid(pid);

            if(get_process_cpu_usage(&g_processes[i], existingProcessTime, processTimeToPopulate, hProcess))
            {
                newProcessTimes[numberOfProcessTimes] = processTimeToPopulate;
                numberOfProcessTimes++;
            }
            else
            {
                free(processTimeToPopulate);
            }

            if(populate_process_io_from_pid(&g_processes[i], hProcess, existingProcessIo, processIoToPopulate))
            {
                newIoReadings[numberOfIoReadings] = processIoToPopulate;
                numberOfIoReadings++;
            }
            else
            {
                free(processIoToPopulate);
            }

            CloseHandle(hProcess);
        }
    }

    for(int i = 0; i < g_numberOfLastProcessTimes; i++)
    {
        free(g_lastProcessTimes[i]);
    }
    if(g_lastProcessTimes)
    {
        free(g_lastProcessTimes);
    }

    for(int i = 0; i < g_numberOfLastProcessIoReadings; i++)
    {
        free(g_lastProcessIoReadings[i]);
    }
    if(g_lastProcessIoReadings)
    {
        free(g_lastProcessIoReadings);
    }

    g_numberOfLastProcessTimes = numberOfProcessTimes;
    g_lastProcessTimes = newProcessTimes;

    g_numberOfLastProcessIoReadings = numberOfIoReadings;
    g_lastProcessIoReadings = newIoReadings;
}

void print_process(Process *process, int index, fzf_position_t *fzfPosition)
{
    if(g_selectedIndex > g_numberOfDisplayItems)
    {
        g_selectedIndex = g_numberOfDisplayItems;
    }

    CHAR privateBytesFormatedStr[MAX_PATH];
    FormatNumber(privateBytesFormatedStr, process->privateBytes, &numFmt);

    CHAR workingSetFormatedStr[MAX_PATH];
    FormatNumber(workingSetFormatedStr, process->workingSet, &numFmt);

    if(index == g_selectedIndex)
    {
        wattron(window, COLOR_PAIR(NORMAL_SELECTED_TEXT_PAIR));
    }
    else
    {
        wattron(window, COLOR_PAIR(NORMAL_UNSELECTED_TEXT_PAIR));
    }

    mvwprintw(
            window,
            index + 2,
            1,
            "%-*s %08d %20s %20s %8.1f %8d %8.1f %8.1f",
            g_nameWidth,
            process->name,
            process->pid,
            workingSetFormatedStr,
            privateBytesFormatedStr,
            process->cpuPercent,
            process->numberOfThreads,
            process->ioReadsPerSecond,
            process->ioWritesPerSecond);

    if(fzfPosition)
    {
        SIZE sz;
        if(fzfPosition && fzfPosition->size > 0)
        {
            for(int i = 0; i < fzfPosition->size; i++)
            {
                wmove(window, index + 2, fzfPosition->data[i] + 1);
                if(index == g_selectedIndex)
                {
                    wattron(window, COLOR_PAIR(FUZZYMATCH_SELECTED_TEXT));
                }
                else
                {
                    wattron(window, COLOR_PAIR(FUZZYMATCH_UNSELECTED_TEXT));
                }
                waddch(window, process->name[fzfPosition->data[i]]);
                if(index == g_selectedIndex)
                {
                    wattroff(window, COLOR_PAIR(FUZZYMATCH_SELECTED_TEXT));
                }
                else
                {
                    wattroff(window, COLOR_PAIR(FUZZYMATCH_UNSELECTED_TEXT));
                }
            }
        }
    }

    if(index == g_selectedIndex)
    {
        wattroff(window, COLOR_PAIR(NORMAL_SELECTED_TEXT_PAIR));
    }
    else
    {
        wattroff(window, COLOR_PAIR(NORMAL_UNSELECTED_TEXT_PAIR));
    }
}

void kill_process(Process *process)
{
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, process->pid);
    TerminateProcess(hProcess, 0);
    CloseHandle(hProcess);
}

void print_process_by_index(int index)
{
    fzf_pattern_t *pattern = fzf_parse_pattern(CaseSmart, false, g_searchStirng, true);
    fzf_position_t *pos = fzf_get_positions(g_displayProcesses[index]->name, pattern, slab);
    print_process(g_displayProcesses[index], index, pos);
    fzf_free_positions(pos);
    fzf_free_pattern(pattern);
    /* print_process(g_displayProcesses[index], index, NULL); */
}

void print_list(void)
{
    int x,y;
    getmaxyx(window, x, y);
    int numberOfLines = x;
    g_numberOfDisplayItems = g_numberOfProcesses;

    int numberOfDisplayItems = 0;

    fzf_pattern_t *pattern = fzf_parse_pattern(CaseSmart, false, g_searchStirng, true);
    if(strlen(g_searchStirng) > 0)
    {
        for(int i = 0; i < g_numberOfProcesses; i++)
        {
            int score = fzf_get_score(g_processes[i].name, pattern, slab);
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

    int offset = 0;
    g_numberOfDisplayItems = numberOfDisplayItems;
    if(g_numberOfDisplayItems > numberOfLines)
    {
        offset = g_scrollOffset;
        g_numberOfDisplayItems = numberOfLines - 3;
    }

    wclear(window);
    box(window, 0, 0);

    if(g_searchStringIndex > 0)
    {
        mvwprintw(
                window,
                0,
                (y / 2) - (g_searchStringIndex / 2),
                " </%s> ",
                g_searchStirng);
    }

    g_nameWidth = y - (2) - (8 + 20 + 20 + 8 + 8 + 8 + 8) -(7);
    wattron(window, A_BOLD | COLOR_PAIR(HEADER_TEXT_PAIR));
    mvwprintw(
            window,
            1,
            1,
            "%-*s %8s %20s %20s %8s %8s %8s %8s",
            g_nameWidth,
            "Name",
            "PID",
            "WorkingSet(kb)",
            "PrivateBytes(kb)",
            "CPU(%)",
            "Threads",
            "Reads",
            "Writes");
    wattroff(window, A_BOLD | COLOR_PAIR(HEADER_TEXT_PAIR));

    for(int i = 0; i < g_numberOfDisplayItems; i++)
    {
        if(i + offset < numberOfDisplayItems)
        {
            fzf_position_t *pos = fzf_get_positions(g_displayProcesses[i]->name, pattern, slab);
            print_process(g_displayProcesses[i + offset], i, pos);
            fzf_free_positions(pos);
        }
    }

    fzf_free_pattern(pattern);

    wrefresh(window);
}

void refresh_processes(void)
{
    populate_processes();
    populate_cpu_stats();
    qsort(&g_processes[0], g_numberOfProcesses + 1, sizeof(Process), CompareProcessPrivateBytes);

    print_list();

    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    g_lastTimeOfProcessListRefresh = ConvertFileTimeToInt64(&now);
}

int get_cpu_usage(void)
{
    static int previousResult;
    int nRes = -1;

    FILETIME ftIdle, ftKrnl, ftUsr;
    if(GetSystemTimes(&ftIdle, &ftKrnl, &ftUsr))
    {
        static BOOL bUsedOnce = FALSE;
        static ULONGLONG uOldIdle = 0;
        static ULONGLONG uOldKrnl = 0;
        static ULONGLONG uOldUsr = 0;

        ULONGLONG uIdle = ((ULONGLONG)ftIdle.dwHighDateTime << 32) | ftIdle.dwLowDateTime;
        ULONGLONG uKrnl = ((ULONGLONG)ftKrnl.dwHighDateTime << 32) | ftKrnl.dwLowDateTime;
        ULONGLONG uUsr = ((ULONGLONG)ftUsr.dwHighDateTime << 32) | ftUsr.dwLowDateTime;

        if(bUsedOnce)
        {
            ULONGLONG uDiffIdle = uIdle - uOldIdle;
            ULONGLONG uDiffKrnl = uKrnl - uOldKrnl;
            ULONGLONG uDiffUsr = uUsr - uOldUsr;

            if(uDiffKrnl + uDiffUsr)
            {
                nRes = (int)((uDiffKrnl + uDiffUsr - uDiffIdle) * 100 / (uDiffKrnl + uDiffUsr));
            }
        }

        bUsedOnce = TRUE;
        uOldIdle = uIdle;
        uOldKrnl = uKrnl;
        uOldUsr = uUsr;
    }

    if(nRes < 0 || nRes > 100)
    {
        return previousResult;
    }

    return nRes;
}

void refreshsummary()
{
    CHAR modeBuff[256];
    if(g_mode == Normal)
    {
        strcpy(modeBuff, "Normal");
    }
    else if(g_mode == Search)
    {
        strcpy(modeBuff, "Search");
    }

    int x1,y1;
    getmaxyx(stdscr, y1, x1);

    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof (statex);
    GlobalMemoryStatusEx (&statex);

    wclear(summaryWindow);
    int cpuPercent = get_cpu_usage();
    mvwprintw(
            summaryWindow,
            1,
            2,
            "CPU: %d %%  Memory: %d %%  Processes: %d  Cores: %d  Threads: %d mode: %s",
            cpuPercent,
            statex.dwMemoryLoad,
            g_numberOfProcesses,
            g_processor_count_,
            g_numberOfThreads,
            modeBuff);
    box(summaryWindow, 0, 0);
    wrefresh(summaryWindow);
}

void create_search_window()
{
    if(g_mode == Search)
    {
        searchWindow = newwin(3, COLS - 2, LINES - 3, 1);
        box(searchWindow, 0, 0);
        wrefresh(searchWindow);
    }
}

void paint_search_window()
{
    wclear(searchWindow);
    mvwprintw(
            searchWindow,
            1,
            2,
            ">> %*s",
            g_selectedIndex,
            g_searchStirng);

    box(searchWindow, 0, 0);
    wrefresh(searchWindow);
}

void paint_summary_window()
{
    summaryWindow = newwin(3, COLS - 2, 0, 1);
    box(summaryWindow, 0, 0);
    refreshsummary();
}

void create_process_list_window()
{
    int numberOfLines = LINES - 6;
    if(g_mode == Normal)
    {
        numberOfLines = LINES - 3;
    }
    window = newwin(numberOfLines, COLS - 2, 3, 1);
    box(window, 0, 0);
}

void reload()
{
    resize_term(0, 0);
    wclear(stdscr);
    wclear(searchWindow);
    wclear(summaryWindow);
    wclear(window);
    clear();

    refresh();

    delwin(summaryWindow);
    paint_summary_window();
    refreshsummary();

    delwin(window);
    create_process_list_window();
    print_list();

    if(searchWindow)
    {
        delwin(searchWindow);
        searchWindow = NULL;
    }
    create_search_window();
    paint_search_window();
}

int main()
{
    slab = fzf_make_default_slab();

    sm_EnableTokenPrivilege(SE_DEBUG_NAME);
    g_processor_count_ = get_processor_number();

    numFmt.NumDigits = 0;
    numFmt.LeadingZero = 0;
    numFmt.NumDigits = 0;
    numFmt.LeadingZero = 0;
    numFmt.Grouping = 3;
    numFmt.lpDecimalSep = L".";
    numFmt.lpThousandSep = L",";
    numFmt.NegativeOrder = 1;

    initscr();
    noecho();
    curs_set(0);
    timeout(1000);
    nonl();

    if (has_colors() == FALSE) {
        endwin();
        printf("Your terminal does not support color\n");
        exit(1);
    }

    use_default_colors();
    start_color();
    init_color(255, 300, 280, 270);
    init_color(244, 60, 56, 54);
    init_color(233, 880, 880, 880);
    init_color(222, 276, 532, 544);
    init_color(211, 1000, 752, 188);

    init_pair(NORMAL_UNSELECTED_TEXT_PAIR, -1, 0);
    init_pair(NORMAL_SELECTED_TEXT_PAIR, -1, 255);
    init_pair(FUZZYMATCH_UNSELECTED_TEXT, 222, 0);
    init_pair(FUZZYMATCH_SELECTED_TEXT, 222, 255);
    init_pair(HEADER_TEXT_PAIR, 211, 0);

    wtimeout(stdscr, 1000);
    keypad(stdscr, TRUE);

    refresh();

    paint_summary_window();
    create_search_window();
    create_process_list_window();

    paint_search_window();
    refresh_processes();
    refreshsummary();

    BOOL stop = FALSE;

    int c;
    curs_set(0);

    while(!stop)
    {
        c = wgetch(stdscr);

        if(c != ERR)
        {
            switch(c)
            {
                int maxScroll = g_numberOfProcesses - g_numberOfDisplayItems;
                case KEY_F(1):
                    g_sortColumn = 1;
                    print_list();
                    break;
                case KEY_F(2):
                    g_sortColumn = 2;
                    print_list();
                    break;
                case KEY_F(3):
                    g_sortColumn = 3;
                    print_list();
                    break;
                case KEY_F(4):
                    g_sortColumn = 4;
                    print_list();
                    break;
                case KEY_F(5):
                    g_sortColumn = 5;
                    print_list();
                    break;
                case CTRL('k'):
                    if(g_selectedIndex > -1)
                    {
                        kill_process(g_displayProcesses[g_selectedIndex]);
                    }
                    break;
                case CTRL('e'):
                    if(g_scrollOffset < maxScroll)
                    {
                        g_scrollOffset++;
                    }
                    print_list();
                    break;
                case 27:
                    g_mode = Normal;
                    g_searchStirng[0] = '\0';
                    g_searchStringIndex = 0;
                    reload();
                    break;
                case CTRL('y'):
                    if(g_scrollOffset > 0)
                    {
                        g_scrollOffset--;
                    }
                    print_list();
                    break;
                case CTRL('d'):
                    if((g_scrollOffset + g_numberOfDisplayItems) > g_numberOfProcesses)
                    {
                        /* g_scrollOffset = g_numberOfProcesses - g_numberOfProcesses; */
                    }
                    else
                    {
                        g_scrollOffset = g_scrollOffset + g_numberOfDisplayItems;
                    }
                    print_list();
                    break;
                case CTRL('u'):
                    if(g_scrollOffset - g_numberOfDisplayItems < 0)
                    {
                        g_scrollOffset = 0;
                    }
                    else
                    {
                        g_scrollOffset = g_scrollOffset - g_numberOfDisplayItems;
                    }
                    print_list();
                    break;
                case CTRL('p'):
                    if(g_selectedIndex > 0)
                    {
                        g_selectedIndex--;
                        if(g_selectedIndex > -1)
                        {
                            print_process_by_index(g_selectedIndex + 1);
                        }
                        print_process_by_index(g_selectedIndex);
                    }
                    break;
                case CTRL('n'):
                    if(g_selectedIndex + 1 < g_numberOfDisplayItems)
                    {
                        g_selectedIndex++;
                        if(g_selectedIndex > 0)
                        {
                            print_process_by_index(g_selectedIndex - 1);
                        }
                        print_process_by_index(g_selectedIndex);
                    }
                    break;
                case '\b':
                    int y1, x1;
                    getyx(searchWindow, y1, x1);
                    if(x1 > 3 && g_searchStringIndex > 0)
                    {
                        wmove(searchWindow, y1, x1 - 1);
                        waddch(searchWindow, ' ');
                        wmove(searchWindow, y1, x1 - 1);
                        g_searchStirng[g_searchStringIndex - 1] = '\0';
                        g_searchStringIndex--;
                        paint_search_window();
                    }
                    break;
                case ERR:
                    break;
                case KEY_RESIZE:
                    reload();
                    break;
                default:
                    if(g_mode == Normal)
                    {
                        FILETIME nowFileTime;
                        GetSystemTimeAsFileTime(&nowFileTime);
                        ULONGLONG now = ConvertFileTimeToInt64(&nowFileTime);
                        ULONGLONG nanoSecondsSinceLastChar = now - g_lastCharTime;

                        if(c == '/')
                        {
                            g_mode = Search;
                            reload();
                        }
                        else if(c == 'g')
                        {
                            if(g_lastChar == 'g' && nanoSecondsSinceLastChar < 10000000)
                            {
                                g_scrollOffset = 0;
                                print_list();
                            }
                        }
                        else if(c == 'G')
                        {
                            g_scrollOffset = g_numberOfProcesses - g_numberOfDisplayItems;
                            print_list();
                        }
                        else if(c == 'q')
                        {
                            stop = TRUE;
                        }
                        g_lastChar = c;
                        g_lastCharTime = now;
                    }
                    else
                    {
                        g_searchStirng[g_searchStringIndex] = c;
                        g_searchStringIndex++;
                        g_scrollOffset = 0;
                        paint_search_window();
                        print_list();
                    }

                    break;
            }
        }

        FILETIME nowFileTime;
        GetSystemTimeAsFileTime(&nowFileTime);
        ULONGLONG now = ConvertFileTimeToInt64(&nowFileTime);
        ULONGLONG nanoSecondsSinceLastRefresh = now - g_lastTimeOfProcessListRefresh;

        if(nanoSecondsSinceLastRefresh > 10000000)
        {
            refreshsummary();
            refresh_processes();
        }

        wrefresh(window);
    }
    endwin();

    return 0;
}
