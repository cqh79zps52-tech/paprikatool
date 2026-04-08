/*
 * Background job runner.
 *
 * A job spawns a worker thread that runs one of the long-lived operations
 * (download / autocut / install) and pushes its output through a mutex-
 * protected ring buffer. The GUI's main thread polls `paprika_job_drain`
 * each frame and renders the lines as a scrolling log.
 */
#include "paprika.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── job state ───────────────────────────────────────────────────────────── */

typedef enum {
    KIND_DOWNLOAD = 0,
    KIND_AUTOCUT,
    KIND_INSTALL_YTDLP,
    KIND_INSTALL_FFMPEG
} job_kind;

struct paprika_job {
    paprika_mutex   *lock;
    paprika_job_state state;
    float            progress;        /* 0..1, -1 if unknown */

    /* Ring buffer of pending output lines (drained by the GUI thread). */
    char             ring[PAPRIKA_JOB_MAX_LINES][PAPRIKA_JOB_MAX_LINE];
    size_t           head;
    size_t           tail;
    size_t           count;

    /* Worker arguments — owned by the job once start_*() is called. */
    job_kind         kind;
    char             a[PAPRIKA_PATH_MAX]; /* url / input  / dest */
    char             b[PAPRIKA_PATH_MAX]; /* dir / output */
    paprika_download_opts opts;
    float            threshold_db;
    int              fade_ms;
};

/* ── ring buffer ─────────────────────────────────────────────────────────── */

static void ring_push_locked(paprika_job *j, const char *line)
{
    if (j->count == PAPRIKA_JOB_MAX_LINES) {
        /* Drop the oldest line. */
        j->head = (j->head + 1) % PAPRIKA_JOB_MAX_LINES;
        j->count--;
    }
    snprintf(j->ring[j->tail], PAPRIKA_JOB_MAX_LINE, "%s", line);
    j->tail = (j->tail + 1) % PAPRIKA_JOB_MAX_LINES;
    j->count++;
}

/* Parse "[download]   42.3% of ..." progress lines from yt-dlp into 0..1. */
static bool parse_progress(const char *line, float *out)
{
    const char *p = strstr(line, "[download]");
    if (!p) return false;
    p += 10;
    while (*p == ' ' || *p == '\t') ++p;
    char *end = NULL;
    double v = strtod(p, &end);
    if (end == p) return false;
    while (*end == ' ' || *end == '\t') ++end;
    if (*end != '%') return false;
    if (v < 0)   v = 0;
    if (v > 100) v = 100;
    *out = (float)(v / 100.0);
    return true;
}

/* Worker → ring callback. Userdata is the job pointer. */
static void worker_cb(const char *line, void *ud)
{
    paprika_job *j = (paprika_job *)ud;
    float pct;
    bool has_pct = parse_progress(line, &pct);

    paprika_mutex_lock(j->lock);
    ring_push_locked(j, line);
    if (has_pct) j->progress = pct;
    paprika_mutex_unlock(j->lock);
}

/* ── lifecycle ───────────────────────────────────────────────────────────── */

paprika_job *paprika_job_new(void)
{
    paprika_job *j = (paprika_job *)calloc(1, sizeof(*j));
    if (!j) return NULL;
    j->lock     = paprika_mutex_new();
    j->state    = PAPRIKA_JOB_IDLE;
    j->progress = -1.0f;
    return j;
}

void paprika_job_free(paprika_job *j)
{
    if (!j) return;
    paprika_mutex_free(j->lock);
    free(j);
}

paprika_job_state paprika_job_state_get(paprika_job *j)
{
    paprika_mutex_lock(j->lock);
    paprika_job_state s = j->state;
    paprika_mutex_unlock(j->lock);
    return s;
}

float paprika_job_progress(paprika_job *j)
{
    paprika_mutex_lock(j->lock);
    float p = j->progress;
    paprika_mutex_unlock(j->lock);
    return p;
}

size_t paprika_job_drain(paprika_job *j,
                         char (*out)[PAPRIKA_JOB_MAX_LINE],
                         size_t max)
{
    paprika_mutex_lock(j->lock);
    size_t n = j->count < max ? j->count : max;
    for (size_t i = 0; i < n; ++i) {
        snprintf(out[i], PAPRIKA_JOB_MAX_LINE, "%s", j->ring[j->head]);
        j->head = (j->head + 1) % PAPRIKA_JOB_MAX_LINES;
    }
    j->count -= n;
    paprika_mutex_unlock(j->lock);
    return n;
}

/* ── worker entry points ─────────────────────────────────────────────────── */

static void set_state(paprika_job *j, paprika_job_state s)
{
    paprika_mutex_lock(j->lock);
    j->state = s;
    paprika_mutex_unlock(j->lock);
}

static void worker_main(void *ud)
{
    paprika_job *j = (paprika_job *)ud;
    bool ok = false;

    switch (j->kind) {
    case KIND_DOWNLOAD:
        ok = paprika_download(j->a, j->b, &j->opts, worker_cb, j);
        break;
    case KIND_AUTOCUT:
        ok = paprika_autocut(j->a, j->b, j->threshold_db, j->fade_ms,
                             worker_cb, j);
        break;
    case KIND_INSTALL_YTDLP:
        ok = paprika_install_ytdlp(worker_cb, j);
        break;
    case KIND_INSTALL_FFMPEG:
        ok = paprika_install_ffmpeg(worker_cb, j);
        break;
    }

    paprika_mutex_lock(j->lock);
    if (ok) j->progress = 1.0f;
    j->state = ok ? PAPRIKA_JOB_DONE_OK : PAPRIKA_JOB_DONE_FAIL;
    paprika_mutex_unlock(j->lock);
}

static bool start(paprika_job *j)
{
    paprika_mutex_lock(j->lock);
    if (j->state == PAPRIKA_JOB_RUNNING) {
        paprika_mutex_unlock(j->lock);
        return false;
    }
    j->state    = PAPRIKA_JOB_RUNNING;
    j->progress = -1.0f;
    j->head = j->tail = j->count = 0;
    paprika_mutex_unlock(j->lock);

    if (!paprika_thread_spawn(worker_main, j)) {
        set_state(j, PAPRIKA_JOB_DONE_FAIL);
        return false;
    }
    return true;
}

bool paprika_job_start_download(paprika_job *j,
                                const char *url,
                                const char *output_dir,
                                const paprika_download_opts *opts)
{
    j->kind = KIND_DOWNLOAD;
    snprintf(j->a, sizeof(j->a), "%s", url);
    snprintf(j->b, sizeof(j->b), "%s", output_dir);
    j->opts = *opts;
    return start(j);
}

bool paprika_job_start_autocut(paprika_job *j,
                               const char *input,
                               const char *output,
                               float threshold_db,
                               int   fade_ms)
{
    j->kind = KIND_AUTOCUT;
    snprintf(j->a, sizeof(j->a), "%s", input);
    snprintf(j->b, sizeof(j->b), "%s", output);
    j->threshold_db = threshold_db;
    j->fade_ms      = fade_ms;
    return start(j);
}

bool paprika_job_start_install_ytdlp(paprika_job *j)
{
    j->kind = KIND_INSTALL_YTDLP;
    return start(j);
}

bool paprika_job_start_install_ffmpeg(paprika_job *j)
{
    j->kind = KIND_INSTALL_FFMPEG;
    return start(j);
}
