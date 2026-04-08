/*
 * Paprika Tool — portable C port of the Poco downloader.
 * Public declarations shared between translation units.
 */
#ifndef PAPRIKA_H
#define PAPRIKA_H

#include <stdbool.h>
#include <stddef.h>

#define PAPRIKA_NAME    "Paprika Tool"
#define PAPRIKA_VERSION "1.1.2"

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

/* Per-line callback used by long-running operations. `line` is NUL-terminated
 * and trimmed of trailing CR/LF. The callback may be invoked from a worker
 * thread, so implementations must be thread-safe. */
typedef void (*paprika_line_cb)(const char *line, void *userdata);

/* ── proc.c ──────────────────────────────────────────────────────────────── */

/* Run a shell command and return its exit code. Each line of merged
 * stdout+stderr is delivered through `cb` (or discarded if cb is NULL).
 * On Windows, children spawn with CREATE_NO_WINDOW so the GUI build never
 * pops a console window. */
int  paprika_run_capture(const char *cmd, paprika_line_cb cb, void *userdata);

/* Same as above, but discards output. Use for cheap PATH probes. */
int  paprika_run_silent(const char *cmd);

/* ── util.c ──────────────────────────────────────────────────────────────── */

bool paprika_exe_dir(char *out, size_t cap);
bool paprika_config_dir(char *out, size_t cap);
bool paprika_default_download_dir(char *out, size_t cap);
void paprika_path_join(char *out, size_t cap, const char *a, const char *b);
bool paprika_file_exists(const char *path);
bool paprika_mkdir_p(const char *path);
bool paprika_make_executable(const char *path);
void paprika_shell_quote(char *out, size_t cap, const char *arg);
void paprika_chomp(char *line);

/* ── installer.c ─────────────────────────────────────────────────────────── */

/* Directory where bundled tools (yt-dlp, ffmpeg) live. On Windows this is
 * the directory containing the executable. On macOS it's the per-user
 * support dir, so the app can be installed read-only in /Applications. */
void paprika_tools_dir(char *out, size_t cap);

void paprika_ytdlp_path(char *out, size_t cap);
void paprika_ffmpeg_path(char *out, size_t cap);
void paprika_ytdlp_command(char *out, size_t cap);
void paprika_ffmpeg_command(char *out, size_t cap);
bool paprika_ytdlp_present(void);
bool paprika_ffmpeg_present(void);

/* The installer variants accept an optional progress callback. Pass NULL for
 * silent / fall-back-to-stderr behaviour. */
bool paprika_install_ytdlp(paprika_line_cb cb, void *ud);
bool paprika_install_ffmpeg(paprika_line_cb cb, void *ud);

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
    bool            tiktok;
} paprika_download_opts;

/* Run yt-dlp synchronously. Each output line is delivered via `cb` (or to
 * stderr if cb is NULL). Returns true on success. */
bool paprika_download(const char *url,
                      const char *output_dir,
                      const paprika_download_opts *opts,
                      paprika_line_cb cb,
                      void *userdata);

/* ── autocut.c ───────────────────────────────────────────────────────────── */

bool paprika_autocut(const char *input,
                     const char *output,
                     float threshold_db,
                     int   fade_ms,
                     paprika_line_cb cb,
                     void *userdata);

/* ── config.c ────────────────────────────────────────────────────────────── */

typedef struct {
    char            output_dir[PAPRIKA_PATH_MAX];
    bool            audio_only;
    paprika_quality quality;
} paprika_config;

void paprika_config_load(paprika_config *cfg);
void paprika_config_save(const paprika_config *cfg);
const char *paprika_quality_label(paprika_quality q);

/* ── thread.c ────────────────────────────────────────────────────────────── */

typedef struct paprika_mutex paprika_mutex;
typedef struct paprika_thread paprika_thread;

paprika_mutex *paprika_mutex_new(void);
void           paprika_mutex_free(paprika_mutex *m);
void           paprika_mutex_lock(paprika_mutex *m);
void           paprika_mutex_unlock(paprika_mutex *m);

/* Spawns a detached thread. Returns true if the thread was started. */
bool           paprika_thread_spawn(void (*entry)(void *), void *arg);

/* ── job.c ───────────────────────────────────────────────────────────────── */

/* Background job streaming yt-dlp / ffmpeg output into a fixed-size ring
 * buffer. The GUI polls `paprika_job_drain` each frame from the main thread.
 *
 * Lifetime: create → start_*() → poll drain + state until done → free. */

#define PAPRIKA_JOB_MAX_LINES 512
#define PAPRIKA_JOB_MAX_LINE  512

typedef enum {
    PAPRIKA_JOB_IDLE = 0,
    PAPRIKA_JOB_RUNNING,
    PAPRIKA_JOB_DONE_OK,
    PAPRIKA_JOB_DONE_FAIL
} paprika_job_state;

typedef struct paprika_job paprika_job;

paprika_job      *paprika_job_new(void);
void              paprika_job_free(paprika_job *j);

paprika_job_state paprika_job_state_get(paprika_job *j);
float             paprika_job_progress(paprika_job *j);   /* 0.0 – 1.0; -1 if unknown */

/* Drain queued lines into `out` (one line per slot, up to `max`). Returns the
 * number of lines copied. Each output buffer is at least PAPRIKA_JOB_MAX_LINE. */
size_t paprika_job_drain(paprika_job *j,
                         char (*out)[PAPRIKA_JOB_MAX_LINE],
                         size_t max);

/* Job launchers — copy their args, then return immediately. */
bool paprika_job_start_download(paprika_job *j,
                                const char *url,
                                const char *output_dir,
                                const paprika_download_opts *opts);

bool paprika_job_start_autocut(paprika_job *j,
                               const char *input,
                               const char *output,
                               float threshold_db,
                               int   fade_ms);

bool paprika_job_start_install_ytdlp(paprika_job *j);
bool paprika_job_start_install_ffmpeg(paprika_job *j);

/* ── gui.c ───────────────────────────────────────────────────────────────── */

#ifdef PAPRIKA_HAS_GUI
int paprika_gui_run(paprika_config *cfg);
#endif

#endif /* PAPRIKA_H */
