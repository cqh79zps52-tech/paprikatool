#include "paprika.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(PAPRIKA_WINDOWS)
#  define YTDLP_URL "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe"
#  define FFMPEG_URL "https://github.com/BtbN/ffmpeg-builds/releases/latest/download/ffmpeg-master-latest-win64-gpl.zip"
#  define FFMPEG_EXE_NAME "ffmpeg.exe"
#elif defined(__APPLE__)
#  define YTDLP_URL "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_macos"
#  define FFMPEG_URL "https://evermeet.cx/ffmpeg/getrelease/ffmpeg/zip"
#  define FFMPEG_EXE_NAME "ffmpeg"
#else
#  define YTDLP_URL "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp"
#  define FFMPEG_URL ""
#  define FFMPEG_EXE_NAME "ffmpeg"
#endif

void paprika_ytdlp_path(char *out, size_t cap)
{
    char dir[PAPRIKA_PATH_MAX];
    if (!paprika_exe_dir(dir, sizeof(dir))) { snprintf(out, cap, "yt-dlp%s", PAPRIKA_EXE_SUFFIX); return; }
    char name[64];
    snprintf(name, sizeof(name), "yt-dlp%s", PAPRIKA_EXE_SUFFIX);
    paprika_path_join(out, cap, dir, name);
}

void paprika_ffmpeg_path(char *out, size_t cap)
{
    char dir[PAPRIKA_PATH_MAX];
    if (!paprika_exe_dir(dir, sizeof(dir))) { snprintf(out, cap, "ffmpeg%s", PAPRIKA_EXE_SUFFIX); return; }
    char name[64];
    snprintf(name, sizeof(name), "ffmpeg%s", PAPRIKA_EXE_SUFFIX);
    paprika_path_join(out, cap, dir, name);
}

void paprika_ytdlp_command(char *out, size_t cap)
{
    char p[PAPRIKA_PATH_MAX];
    paprika_ytdlp_path(p, sizeof(p));
    if (paprika_file_exists(p)) {
        snprintf(out, cap, "%s", p);
    } else {
        snprintf(out, cap, "yt-dlp");
    }
}

void paprika_ffmpeg_command(char *out, size_t cap)
{
    char p[PAPRIKA_PATH_MAX];
    paprika_ffmpeg_path(p, sizeof(p));
    if (paprika_file_exists(p)) {
        snprintf(out, cap, "%s", p);
    } else {
        snprintf(out, cap, "ffmpeg");
    }
}

bool paprika_ytdlp_present(void)
{
    char p[PAPRIKA_PATH_MAX];
    paprika_ytdlp_path(p, sizeof(p));
    if (paprika_file_exists(p)) return true;

    /* Probe PATH. */
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

/* Use the system `curl` (ships with Windows 10+ and macOS) to fetch a URL. */
static bool curl_download(const char *url, const char *dest)
{
    char cmd[PAPRIKA_PATH_MAX * 2 + 64] = {0};
    strcat(cmd, "curl -L --fail --progress-bar -o ");
    paprika_shell_quote(cmd, sizeof(cmd), dest);
    strcat(cmd, " ");
    paprika_shell_quote(cmd, sizeof(cmd), url);
    fprintf(stderr, "[paprika] %s\n", cmd);
    return system(cmd) == 0;
}

bool paprika_install_ytdlp(void)
{
    char dest[PAPRIKA_PATH_MAX];
    paprika_ytdlp_path(dest, sizeof(dest));

    fprintf(stderr, "[paprika] Downloading yt-dlp -> %s\n", dest);
    if (!curl_download(YTDLP_URL, dest)) {
        fprintf(stderr, "[paprika] yt-dlp download failed.\n");
        return false;
    }
    paprika_make_executable(dest);
    fprintf(stderr, "[paprika] yt-dlp installed.\n");
    return true;
}

bool paprika_install_ffmpeg(void)
{
#if !defined(PAPRIKA_WINDOWS) && !defined(__APPLE__)
    fprintf(stderr, "[paprika] Auto-install of ffmpeg only supported on Windows and macOS.\n");
    return false;
#else
    char exe_dir[PAPRIKA_PATH_MAX];
    if (!paprika_exe_dir(exe_dir, sizeof(exe_dir))) return false;

    char zip_path[PAPRIKA_PATH_MAX];
    paprika_path_join(zip_path, sizeof(zip_path), exe_dir, "_ffmpeg_tmp.zip");

    char dest[PAPRIKA_PATH_MAX];
    paprika_ffmpeg_path(dest, sizeof(dest));

    fprintf(stderr, "[paprika] Downloading ffmpeg archive...\n");
    if (!curl_download(FFMPEG_URL, zip_path)) {
        fprintf(stderr, "[paprika] ffmpeg download failed.\n");
        remove(zip_path);
        return false;
    }

    /* Extract using bsdtar (built into Windows 10+) / tar (macOS).
     * Both can read .zip archives transparently. We extract into a
     * temporary directory and then move the matched binary into place. */
    char extract_dir[PAPRIKA_PATH_MAX];
    paprika_path_join(extract_dir, sizeof(extract_dir), exe_dir, "_ffmpeg_extract");
    paprika_mkdir_p(extract_dir);

    char cmd[PAPRIKA_PATH_MAX * 3] = {0};
    strcat(cmd, "tar -xf ");
    paprika_shell_quote(cmd, sizeof(cmd), zip_path);
    strcat(cmd, " -C ");
    paprika_shell_quote(cmd, sizeof(cmd), extract_dir);

    fprintf(stderr, "[paprika] Extracting...\n");
    if (system(cmd) != 0) {
        fprintf(stderr, "[paprika] Extraction failed.\n");
        remove(zip_path);
        return false;
    }

    /* Find FFMPEG_EXE_NAME inside extract_dir using a portable shell command. */
    char find_cmd[PAPRIKA_PATH_MAX * 3];
#if defined(PAPRIKA_WINDOWS)
    snprintf(find_cmd, sizeof(find_cmd),
             "where /R \"%s\" %s > \"%s\\_found.txt\" 2>NUL",
             extract_dir, FFMPEG_EXE_NAME, exe_dir);
#else
    snprintf(find_cmd, sizeof(find_cmd),
             "find '%s' -type f -name '%s' > '%s/_found.txt' 2>/dev/null",
             extract_dir, FFMPEG_EXE_NAME, exe_dir);
#endif
    system(find_cmd);

    char list_path[PAPRIKA_PATH_MAX];
    paprika_path_join(list_path, sizeof(list_path), exe_dir, "_found.txt");

    FILE *f = fopen(list_path, "r");
    if (!f) {
        fprintf(stderr, "[paprika] Could not locate %s in archive.\n", FFMPEG_EXE_NAME);
        remove(zip_path);
        return false;
    }

    char found[PAPRIKA_PATH_MAX] = {0};
    if (!fgets(found, sizeof(found), f)) {
        fclose(f);
        fprintf(stderr, "[paprika] %s not found in archive.\n", FFMPEG_EXE_NAME);
        remove(zip_path);
        remove(list_path);
        return false;
    }
    fclose(f);
    paprika_chomp(found);

    /* Move (rename) the binary into place. Cross-device safe via fallback copy. */
    if (rename(found, dest) != 0) {
        FILE *src = fopen(found, "rb");
        FILE *dst = fopen(dest, "wb");
        if (!src || !dst) {
            if (src) fclose(src);
            if (dst) fclose(dst);
            fprintf(stderr, "[paprika] Failed to install ffmpeg binary.\n");
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

    /* Best-effort cleanup of extract_dir. */
#if defined(PAPRIKA_WINDOWS)
    char rm_cmd[PAPRIKA_PATH_MAX + 32];
    snprintf(rm_cmd, sizeof(rm_cmd), "rmdir /S /Q \"%s\"", extract_dir);
#else
    char rm_cmd[PAPRIKA_PATH_MAX + 32];
    snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf '%s'", extract_dir);
#endif
    system(rm_cmd);

    fprintf(stderr, "[paprika] ffmpeg installed.\n");
    return true;
#endif
}
