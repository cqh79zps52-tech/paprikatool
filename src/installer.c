#include "paprika.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(PAPRIKA_WINDOWS)
#  define POPEN  _popen
#  define PCLOSE _pclose
#  define YTDLP_URL "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe"
#  define FFMPEG_URL "https://github.com/BtbN/ffmpeg-builds/releases/latest/download/ffmpeg-master-latest-win64-gpl.zip"
#  define FFMPEG_EXE_NAME "ffmpeg.exe"
#elif defined(__APPLE__)
#  define POPEN  popen
#  define PCLOSE pclose
#  define YTDLP_URL "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_macos"
#  define FFMPEG_URL "https://evermeet.cx/ffmpeg/getrelease/ffmpeg/zip"
#  define FFMPEG_EXE_NAME "ffmpeg"
#else
#  define POPEN  popen
#  define PCLOSE pclose
#  define YTDLP_URL "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp"
#  define FFMPEG_URL ""
#  define FFMPEG_EXE_NAME "ffmpeg"
#endif

static void emit(paprika_line_cb cb, void *ud, const char *line)
{
    if (cb) cb(line, ud);
    else    fprintf(stderr, "%s\n", line);
}

static void emitf(paprika_line_cb cb, void *ud, const char *fmt, ...)
{
    char buf[PAPRIKA_PATH_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    emit(cb, ud, buf);
}

void paprika_ytdlp_path(char *out, size_t cap)
{
    char dir[PAPRIKA_PATH_MAX];
    if (!paprika_exe_dir(dir, sizeof(dir))) {
        snprintf(out, cap, "yt-dlp%s", PAPRIKA_EXE_SUFFIX);
        return;
    }
    char name[64];
    snprintf(name, sizeof(name), "yt-dlp%s", PAPRIKA_EXE_SUFFIX);
    paprika_path_join(out, cap, dir, name);
}

void paprika_ffmpeg_path(char *out, size_t cap)
{
    char dir[PAPRIKA_PATH_MAX];
    if (!paprika_exe_dir(dir, sizeof(dir))) {
        snprintf(out, cap, "ffmpeg%s", PAPRIKA_EXE_SUFFIX);
        return;
    }
    char name[64];
    snprintf(name, sizeof(name), "ffmpeg%s", PAPRIKA_EXE_SUFFIX);
    paprika_path_join(out, cap, dir, name);
}

void paprika_ytdlp_command(char *out, size_t cap)
{
    char p[PAPRIKA_PATH_MAX];
    paprika_ytdlp_path(p, sizeof(p));
    if (paprika_file_exists(p)) snprintf(out, cap, "%s", p);
    else                        snprintf(out, cap, "yt-dlp");
}

void paprika_ffmpeg_command(char *out, size_t cap)
{
    char p[PAPRIKA_PATH_MAX];
    paprika_ffmpeg_path(p, sizeof(p));
    if (paprika_file_exists(p)) snprintf(out, cap, "%s", p);
    else                        snprintf(out, cap, "ffmpeg");
}

bool paprika_ytdlp_present(void)
{
    char p[PAPRIKA_PATH_MAX];
    paprika_ytdlp_path(p, sizeof(p));
    if (paprika_file_exists(p)) return true;
#if defined(PAPRIKA_WINDOWS)
    return system("where yt-dlp >NUL 2>&1") == 0;
#else
    return system("command -v yt-dlp >/dev/null 2>&1") == 0;
#endif
}

bool paprika_ffmpeg_present(void)
{
    char p[PAPRIKA_PATH_MAX];
    paprika_ffmpeg_path(p, sizeof(p));
    if (paprika_file_exists(p)) return true;
#if defined(PAPRIKA_WINDOWS)
    return system("where ffmpeg >NUL 2>&1") == 0;
#else
    return system("command -v ffmpeg >/dev/null 2>&1") == 0;
#endif
}

/* Run a shell command, streaming each output line through cb. */
static int run_cmd(const char *cmd, paprika_line_cb cb, void *ud)
{
    emitf(cb, ud, "[paprika] %s", cmd);
    char wrapped[PAPRIKA_PATH_MAX * 3];
    if (!strstr(cmd, "2>&1") && !strstr(cmd, ">NUL") && !strstr(cmd, ">/dev/null")) {
        snprintf(wrapped, sizeof(wrapped), "%s 2>&1", cmd);
    } else {
        snprintf(wrapped, sizeof(wrapped), "%s", cmd);
    }
    FILE *p = POPEN(wrapped, "r");
    if (!p) return -1;
    char line[1024];
    while (fgets(line, sizeof(line), p)) {
        paprika_chomp(line);
        emit(cb, ud, line);
    }
    return PCLOSE(p);
}

static bool curl_download(const char *url, const char *dest,
                          paprika_line_cb cb, void *ud)
{
    char cmd[PAPRIKA_PATH_MAX * 2 + 64] = {0};
    strcat(cmd, "curl -L --fail --silent --show-error -o ");
    paprika_shell_quote(cmd, sizeof(cmd), dest);
    strcat(cmd, " ");
    paprika_shell_quote(cmd, sizeof(cmd), url);
    return run_cmd(cmd, cb, ud) == 0;
}

bool paprika_install_ytdlp(paprika_line_cb cb, void *ud)
{
    char dest[PAPRIKA_PATH_MAX];
    paprika_ytdlp_path(dest, sizeof(dest));

    emitf(cb, ud, "[paprika] Downloading yt-dlp -> %s", dest);
    if (!curl_download(YTDLP_URL, dest, cb, ud)) {
        emit(cb, ud, "[paprika] yt-dlp download failed.");
        return false;
    }
    paprika_make_executable(dest);
    emit(cb, ud, "[paprika] yt-dlp installed.");
    return true;
}

bool paprika_install_ffmpeg(paprika_line_cb cb, void *ud)
{
#if !defined(PAPRIKA_WINDOWS) && !defined(__APPLE__)
    emit(cb, ud, "[paprika] Auto-install of ffmpeg only supported on Windows and macOS.");
    return false;
#else
    char exe_dir[PAPRIKA_PATH_MAX];
    if (!paprika_exe_dir(exe_dir, sizeof(exe_dir))) return false;

    char zip_path[PAPRIKA_PATH_MAX];
    paprika_path_join(zip_path, sizeof(zip_path), exe_dir, "_ffmpeg_tmp.zip");

    char dest[PAPRIKA_PATH_MAX];
    paprika_ffmpeg_path(dest, sizeof(dest));

    emit(cb, ud, "[paprika] Downloading ffmpeg archive...");
    if (!curl_download(FFMPEG_URL, zip_path, cb, ud)) {
        emit(cb, ud, "[paprika] ffmpeg download failed.");
        remove(zip_path);
        return false;
    }

    char extract_dir[PAPRIKA_PATH_MAX];
    paprika_path_join(extract_dir, sizeof(extract_dir), exe_dir, "_ffmpeg_extract");
    paprika_mkdir_p(extract_dir);

    char cmd[PAPRIKA_PATH_MAX * 3] = {0};
    strcat(cmd, "tar -xf ");
    paprika_shell_quote(cmd, sizeof(cmd), zip_path);
    strcat(cmd, " -C ");
    paprika_shell_quote(cmd, sizeof(cmd), extract_dir);

    emit(cb, ud, "[paprika] Extracting...");
    if (run_cmd(cmd, cb, ud) != 0) {
        emit(cb, ud, "[paprika] Extraction failed.");
        remove(zip_path);
        return false;
    }

    char find_cmd[PAPRIKA_PATH_MAX * 3];
#if defined(PAPRIKA_WINDOWS)
    snprintf(find_cmd, sizeof(find_cmd),
             "where /R \"%s\" %s > \"%s\\_found.txt\" 2>NUL",
             extract_dir, FFMPEG_EXE_NAME, exe_dir);
#else
    snprintf(find_cmd, sizeof(find_cmd),
             "find \"%s\" -type f -name \"%s\" > \"%s/_found.txt\" 2>/dev/null",
             extract_dir, FFMPEG_EXE_NAME, exe_dir);
#endif
    system(find_cmd);

    char list_path[PAPRIKA_PATH_MAX];
    paprika_path_join(list_path, sizeof(list_path), exe_dir, "_found.txt");

    FILE *f = fopen(list_path, "r");
    if (!f) {
        emitf(cb, ud, "[paprika] Could not locate %s in archive.", FFMPEG_EXE_NAME);
        remove(zip_path);
        return false;
    }

    char found[PAPRIKA_PATH_MAX] = {0};
    if (!fgets(found, sizeof(found), f)) {
        fclose(f);
        emitf(cb, ud, "[paprika] %s not found in archive.", FFMPEG_EXE_NAME);
        remove(zip_path);
        remove(list_path);
        return false;
    }
    fclose(f);
    paprika_chomp(found);

    if (rename(found, dest) != 0) {
        FILE *src = fopen(found, "rb");
        FILE *dst = fopen(dest, "wb");
        if (!src || !dst) {
            if (src) fclose(src);
            if (dst) fclose(dst);
            emit(cb, ud, "[paprika] Failed to install ffmpeg binary.");
            return false;
        }
        char buf[65536];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), src)) > 0) fwrite(buf, 1, n, dst);
        fclose(src);
        fclose(dst);
    }

    paprika_make_executable(dest);
    remove(zip_path);
    remove(list_path);

    char rm_cmd[PAPRIKA_PATH_MAX + 32];
#if defined(PAPRIKA_WINDOWS)
    snprintf(rm_cmd, sizeof(rm_cmd), "rmdir /S /Q \"%s\"", extract_dir);
#else
    snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf \"%s\"", extract_dir);
#endif
    system(rm_cmd);

    emit(cb, ud, "[paprika] ffmpeg installed.");
    return true;
#endif
}
