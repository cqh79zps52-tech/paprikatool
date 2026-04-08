/*
 * Paprika Tool — portable C port of the Poco downloader.
 * Public declarations shared between translation units.
 */
#ifndef PAPRIKA_H
#define PAPRIKA_H

#include <stdbool.h>
#include <stddef.h>

#define PAPRIKA_NAME    "Paprika Tool"
#define PAPRIKA_VERSION "1.0.0"

#if defined(_WIN32)
#  define PAPRIKA_WINDOWS 1
#  define PAPRIKA_PATH_SEP '\\'
#  define PAPRIKA_EXE_SUFFIX ".exe"
#else
#  define PAPRIKA_POSIX 1
#  define PAPRIKA_PATH_SEP '/'
#  define PAPRIKA_EXE_SUFFIX ""
#endif

#define PAPRIKA_PATH_MAX 4096

/* ── util.c ──────────────────────────────────────────────────────────────── */

/* Directory containing the running executable. Result written to `out`. */
bool paprika_exe_dir(char *out, size_t cap);

/* OS-appropriate config directory for Paprika Tool (created if missing). */
bool paprika_config_dir(char *out, size_t cap);

/* User's Downloads folder (best guess; falls back to $HOME or cwd). */
bool paprika_default_download_dir(char *out, size_t cap);

/* Join two path components into `out`. */
void paprika_path_join(char *out, size_t cap, const char *a, const char *b);

bool paprika_file_exists(const char *path);
bool paprika_mkdir_p(const char *path);
bool paprika_make_executable(const char *path); /* no-op on Windows */

/* Quote `arg` for the host shell, appending to `out`. */
void paprika_shell_quote(char *out, size_t cap, const char *arg);

/* Trim trailing CR/LF in-place. */
void paprika_chomp(char *line);

/* ── installer.c ─────────────────────────────────────────────────────────── */

/* Path to the local copy of yt-dlp / ffmpeg next to the Paprika executable. */
void paprika_ytdlp_path(char *out, size_t cap);
void paprika_ffmpeg_path(char *out, size_t cap);

/* Returns the command name to use for yt-dlp / ffmpeg (local copy if present,
 * otherwise just the bare name so it resolves through PATH). */
void paprika_ytdlp_command(char *out, size_t cap);
void paprika_ffmpeg_command(char *out, size_t cap);

bool paprika_ytdlp_present(void);
bool paprika_ffmpeg_present(void);

/* Download + place yt-dlp / ffmpeg next to the executable. */
bool paprika_install_ytdlp(void);
bool paprika_install_ffmpeg(void);

/* ── downloader.c ────────────────────────────────────────────────────────── */

typedef enum {
    PAPRIKA_QUALITY_BEST = 0,
    PAPRIKA_QUALITY_1080,
    PAPRIKA_QUALITY_720,
    PAPRIKA_QUALITY_480
} paprika_quality;

typedef struct {
    bool            audio_only;
    paprika_quality quality;
    bool            tiktok;       /* use TikTok-friendly format string */
} paprika_download_opts;

/* Run yt-dlp synchronously, streaming its output to stdout.
 * Returns true on success. */
bool paprika_download(const char *url,
                      const char *output_dir,
                      const paprika_download_opts *opts);

/* ── autocut.c ───────────────────────────────────────────────────────────── */

/* Strip silence from `input` and write to `output`, using ffmpeg's
 * silenceremove filter. `threshold_db` is negative (e.g. -40). */
bool paprika_autocut(const char *input,
                     const char *output,
                     float threshold_db,
                     int   fade_ms);

/* ── config.c ────────────────────────────────────────────────────────────── */

typedef struct {
    char            output_dir[PAPRIKA_PATH_MAX];
    bool            audio_only;
    paprika_quality quality;
} paprika_config;

void paprika_config_load(paprika_config *cfg);
void paprika_config_save(const paprika_config *cfg);
const char *paprika_quality_label(paprika_quality q);

#endif /* PAPRIKA_H */
