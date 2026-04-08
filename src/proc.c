/*
 * Subprocess helpers.
 *
 * On Windows the GUI build runs as a /SUBSYSTEM:WINDOWS executable, which
 * means it has no inherited console. Calling system() or _popen() in that
 * mode causes the CRT to allocate a brand-new cmd.exe console window for
 * each child — visible as a console that flashes open and slams shut every
 * time we run yt-dlp, ffmpeg, curl, where, etc. It also blocks the calling
 * thread, so any UI-thread invocation makes the window go "Not responding".
 *
 * This module routes every external-process call through CreateProcessA with
 * CREATE_NO_WINDOW (Windows) or popen (POSIX). The GUI never spawns from the
 * UI thread — workers run on background threads in job.c — but the helpers
 * are also used directly from CLI mode.
 */
#include "paprika.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(PAPRIKA_WINDOWS)
#  include <windows.h>
#else
#  include <sys/wait.h>
#endif

static void emit(paprika_line_cb cb, void *ud, const char *line)
{
    if (cb) cb(line, ud);
}

#if defined(PAPRIKA_WINDOWS)

int paprika_run_capture(const char *cmd, paprika_line_cb cb, void *ud)
{
    /* cmd.exe handles redirections (>, 2>&1, &&, etc.) inside `cmd`. */
    char cmdline[16384];
    int n = snprintf(cmdline, sizeof(cmdline), "cmd.exe /S /C \"%s\"", cmd);
    if (n < 0 || n >= (int)sizeof(cmdline)) return -1;

    SECURITY_ATTRIBUTES sa = {0};
    sa.nLength        = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE rd = NULL, wr = NULL;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;
    /* The read end must NOT be inheritable or the child holds it open
     * forever and ReadFile blocks past process exit. */
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {0};
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError  = wr;
    si.hStdInput  = NULL;

    PROCESS_INFORMATION pi = {0};
    BOOL ok = CreateProcessA(
        NULL, cmdline,
        NULL, NULL,
        TRUE,                      /* inherit handles for the pipe */
        CREATE_NO_WINDOW,          /* the whole point of this file  */
        NULL, NULL,
        &si, &pi);

    /* Drop our copy of the write end so ReadFile sees EOF when the child exits. */
    CloseHandle(wr);

    if (!ok) {
        CloseHandle(rd);
        return -1;
    }

    char  buf[4096];
    char  line[PAPRIKA_PATH_MAX];
    size_t line_len = 0;
    DWORD got;
    while (ReadFile(rd, buf, sizeof(buf), &got, NULL) && got > 0) {
        for (DWORD i = 0; i < got; ++i) {
            char c = buf[i];
            if (c == '\r') continue;
            if (c == '\n' || line_len + 1 >= sizeof(line)) {
                line[line_len] = '\0';
                emit(cb, ud, line);
                line_len = 0;
            } else {
                line[line_len++] = c;
            }
        }
    }
    if (line_len > 0) {
        line[line_len] = '\0';
        emit(cb, ud, line);
    }
    CloseHandle(rd);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)exit_code;
}

int paprika_run_silent(const char *cmd)
{
    return paprika_run_capture(cmd, NULL, NULL);
}

#else  /* POSIX */

int paprika_run_capture(const char *cmd, paprika_line_cb cb, void *ud)
{
    /* Make sure stderr flows through stdout if the caller didn't do it. */
    char wrapped[16384];
    if (!strstr(cmd, "2>&1")) {
        snprintf(wrapped, sizeof(wrapped), "%s 2>&1", cmd);
    } else {
        snprintf(wrapped, sizeof(wrapped), "%s", cmd);
    }
    FILE *p = popen(wrapped, "r");
    if (!p) return -1;
    char line[PAPRIKA_PATH_MAX];
    while (fgets(line, sizeof(line), p)) {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
        emit(cb, ud, line);
    }
    int rc = pclose(p);
    if (rc == -1) return -1;
    if (WIFEXITED(rc)) return WEXITSTATUS(rc);
    return rc;
}

int paprika_run_silent(const char *cmd)
{
    char buf[16384];
    snprintf(buf, sizeof(buf), "%s >/dev/null 2>&1", cmd);
    int rc = system(buf);
    if (rc == -1) return -1;
    if (WIFEXITED(rc)) return WEXITSTATUS(rc);
    return rc;
}

#endif
