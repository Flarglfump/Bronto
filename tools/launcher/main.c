/*
 * BRONTO EDITOR - editor.com, the editor for command lines on Windows.
 *
 * On Windows the editor (editor.exe) is a windowed program: started from the
 * Start menu or Explorer, it opens no console window. But a windowed program
 * started from a command line doesn't hold up the command line, and what it
 * prints doesn't show there. editor.com is a small console program beside it
 * that runs editor.exe for command lines:
 *
 *   editor --build windows my_game      waits for the build, shows its output, returns its exit status
 *   editor --help / --headless / --write-icon ...   the same
 *   editor my_game                      opens the editor and returns at once
 *
 * Typing "editor" in a folder with both runs editor.com (Windows tries .com
 * before .exe), and BRONTO_EDITOR (make install) names it, for scripts.
 *
 * Elsewhere there is nothing to do: the editor is one program.
 */
#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <stdio.h>
#  include <stdlib.h>
#  include <string.h>
#  include <wchar.h>
#  if defined(_MSC_VER)
#    pragma comment(lib, "shell32.lib")             /* CommandLineToArgvW */
#  endif

/* Options that are a job for the command line: wait for them, with their output here. */
static int waits_for(int argc, wchar_t **argv) {
    static const wchar_t *const jobs[] = { L"--build", L"--help", L"-h", L"--headless", L"--write-icon" };
    int i;
    size_t k;
    for (i = 1; i < argc; ++i)
        for (k = 0; k < sizeof(jobs) / sizeof(jobs[0]); ++k)
            if (wcscmp(argv[i], jobs[k]) == 0) return 1;
    return 0;
}

int main(void) {
    wchar_t exe[MAX_PATH * 2], *line, *args = GetCommandLineW(), **argv;
    DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH * 2), code = 1;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    size_t len;
    int argc = 0, wait;
    if (n == 0 || n >= MAX_PATH * 2 - 4) { fprintf(stderr, "editor: can't find myself\n"); return 1; }
    while (n > 0 && exe[n - 1] != L'.' && exe[n - 1] != L'\\') --n;      /* editor.com -> editor.exe */
    if (n == 0 || exe[n - 1] != L'.') { fprintf(stderr, "editor: can't find editor.exe\n"); return 1; }
    wcscpy(exe + n, L"exe");
    argv = CommandLineToArgvW(args, &argc);
    wait = argv ? waits_for(argc, argv) : 1;
    if (argv) LocalFree(argv);
    if (*args == L'"') {                                    /* the arguments: the command line after the program's name */
        ++args;
        while (*args && *args != L'"') ++args;
        if (*args) ++args;
    } else {
        while (*args && *args != L' ' && *args != L'\t') ++args;
    }
    len = wcslen(exe) + wcslen(args) + 4;
    if (!(line = (wchar_t *)malloc(len * sizeof(wchar_t)))) return 1;
    swprintf(line, len, L"\"%ls\"%ls", exe, args);
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));
    if (wait) {                                             /* its output comes here */
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        SetConsoleCtrlHandler(NULL, TRUE);                  /* (Ctrl+C is for it, not for this) */
    }
    if (!CreateProcessW(exe, line, NULL, NULL, wait ? TRUE : FALSE, wait ? 0 : DETACHED_PROCESS, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "editor: can't start %ls (error %lu)\n", exe, (unsigned long)GetLastError());
        free(line);
        return 1;
    }
    free(line);
    if (wait) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &code);
    } else {
        code = 0;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
}

#else
#  include <stdio.h>
int main(void) {
    fprintf(stderr, "editor.com is for Windows: run the editor itself\n");
    return 1;
}
#endif
