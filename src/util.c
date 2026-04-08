#include "paprika.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#if defined(PAPRIKA_WINDOWS)
#  include <windows.h>
#  include <direct.h>
#  include <io.h>
#  define stat _stat
#else
#  include <unistd.h>
#  include <pwd.h>
#  include <errno.h>
#  if defined(__APPLE__)
#    include <mach-o/dyld.h>
#  endif
#endif

bool paprika_exe_dir(char *out, size_t cap)
{
    if (cap == 0) return false;
    out[0] = '\0';

#if defined(PAPRIKA_WINDOWS)
    char buf[PAPRIKA_PATH_MAX];
    DWORD n = GetModuleFileNameA(NULL, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) return false;
    char *slash = strrchr(buf, '\\');
    if (slash) *slash = '\0';
    if (strlen(buf) + 1 > cap) return false;
    strcpy(out, buf);
    return true;
#elif defined(__APPLE__)
    char buf[PAPRIKA_PATH_MAX];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0) return false;
    char real[PAPRIKA_PATH_MAX];
    if (!realpath(buf, real)) {
        if (strlen(buf) + 1 > cap) return false;
        strcpy(out, buf);
    } else {
        if (strlen(real) + 1 > cap) return false;
        strcpy(out, real);
    }
    char *slash = strrchr(out, '/');
    if (slash) *slash = '\0';
    return true;
#else
    char buf[PAPRIKA_PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return false;
    buf[n] = '\0';
    char *slash = strrchr(buf, '/');
    if (slash) *slash = '\0';
    if (strlen(buf) + 1 > cap) return false;
    strcpy(out, buf);
    return true;
#endif
}

static const char *home_dir(void)
{
#if defined(PAPRIKA_WINDOWS)
    const char *h = getenv("USERPROFILE");
    if (h && *h) return h;
    return getenv("HOMEPATH");
#else
    const char *h = getenv("HOME");
    if (h && *h) return h;
    struct passwd *pw = getpwuid(getuid());
    return pw ? pw->pw_dir : NULL;
#endif
}

bool paprika_config_dir(char *out, size_t cap)
{
    if (cap == 0) return false;

#if defined(PAPRIKA_WINDOWS)
    const char *base = getenv("APPDATA");
    if (!base || !*base) base = home_dir();
    if (!base) return false;
    snprintf(out, cap, "%s\\PaprikaTool", base);
#elif defined(__APPLE__)
    const char *home = home_dir();
    if (!home) return false;
    snprintf(out, cap, "%s/Library/Application Support/PaprikaTool", home);
#else
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) {
        snprintf(out, cap, "%s/PaprikaTool", xdg);
    } else {
        const char *home = home_dir();
        if (!home) return false;
        snprintf(out, cap, "%s/.config/PaprikaTool", home);
    }
#endif

    paprika_mkdir_p(out);
    return true;
}

bool paprika_default_download_dir(char *out, size_t cap)
{
    const char *home = home_dir();
    if (!home) {
        snprintf(out, cap, ".");
        return true;
    }
#if defined(PAPRIKA_WINDOWS)
    snprintf(out, cap, "%s\\Downloads", home);
#else
    snprintf(out, cap, "%s/Downloads", home);
#endif
    return true;
}

void paprika_path_join(char *out, size_t cap, const char *a, const char *b)
{
    if (cap == 0) return;
    size_t la = strlen(a);
    if (la == 0) {
        snprintf(out, cap, "%s", b);
        return;
    }
    char last = a[la - 1];
    if (last == '/' || last == '\\') {
        snprintf(out, cap, "%s%s", a, b);
    } else {
        snprintf(out, cap, "%s%c%s", a, PAPRIKA_PATH_SEP, b);
    }
}

bool paprika_file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

bool paprika_mkdir_p(const char *path)
{
    char buf[PAPRIKA_PATH_MAX];
    snprintf(buf, sizeof(buf), "%s", path);
    size_t len = strlen(buf);
    if (len == 0) return false;

    for (size_t i = 1; i < len; ++i) {
        if (buf[i] == '/' || buf[i] == '\\') {
            char saved = buf[i];
            buf[i] = '\0';
#if defined(PAPRIKA_WINDOWS)
            _mkdir(buf);
#else
            mkdir(buf, 0755);
#endif
            buf[i] = saved;
        }
    }
#if defined(PAPRIKA_WINDOWS)
    _mkdir(buf);
#else
    mkdir(buf, 0755);
#endif
    return paprika_file_exists(path);
}

bool paprika_make_executable(const char *path)
{
#if defined(PAPRIKA_WINDOWS)
    (void)path;
    return true;
#else
    return chmod(path, 0755) == 0;
#endif
}

void paprika_shell_quote(char *out, size_t cap, const char *arg)
{
    /* Append a quoted form of `arg` to `out`. */
    size_t len = strlen(out);
    if (len + 1 >= cap) return;

#if defined(PAPRIKA_WINDOWS)
    /* Windows: wrap in double quotes, escape internal double quotes with \" */
    if (len + 1 < cap) out[len++] = '"';
    for (const char *p = arg; *p && len + 2 < cap; ++p) {
        if (*p == '"') {
            if (len + 2 < cap) { out[len++] = '\\'; out[len++] = '"'; }
        } else {
            out[len++] = *p;
        }
    }
    if (len + 1 < cap) out[len++] = '"';
    out[len] = '\0';
#else
    /* POSIX: wrap in single quotes, replace ' with '\'' */
    if (len + 1 < cap) out[len++] = '\'';
    for (const char *p = arg; *p && len + 5 < cap; ++p) {
        if (*p == '\'') {
            const char *esc = "'\\''";
            for (int i = 0; esc[i] && len + 1 < cap; ++i) out[len++] = esc[i];
        } else {
            out[len++] = *p;
        }
    }
    if (len + 1 < cap) out[len++] = '\'';
    out[len] = '\0';
#endif
}

void paprika_chomp(char *line)
{
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
        line[--n] = '\0';
    }
}
