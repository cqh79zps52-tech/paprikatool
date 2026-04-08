#include "paprika.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(PAPRIKA_WINDOWS)
#  define POPEN  _popen
#  define PCLOSE _pclose
#else
#  define POPEN  popen
#  define PCLOSE pclose
#endif

static void emit(paprika_line_cb cb, void *ud, const char *line)
{
    if (cb) cb(line, ud);
    else    fprintf(stderr, "%s\n", line);
}

bool paprika_autocut(const char *input,
                     const char *output,
                     float threshold_db,
                     int   fade_ms,
                     paprika_line_cb cb,
                     void *ud)
{
    if (!paprika_ffmpeg_present()) {
        emit(cb, ud, "[paprika] ffmpeg not found — install it from the menu.");
        return false;
    }

    char ffmpeg[PAPRIKA_PATH_MAX];
    paprika_ffmpeg_command(ffmpeg, sizeof(ffmpeg));

    char filter[512];
    float fade_s = (fade_ms > 0 ? fade_ms : 0) / 1000.0f;
    snprintf(filter, sizeof(filter),
        "silenceremove=start_periods=1:start_silence=0.05:start_threshold=%.1fdB"
        ":stop_periods=-1:stop_silence=0.20:stop_threshold=%.1fdB"
        ",afade=in:st=0:d=%.3f",
        threshold_db, threshold_db, fade_s);

    char cmd[PAPRIKA_PATH_MAX * 3] = {0};
    paprika_shell_quote(cmd, sizeof(cmd), ffmpeg);
    strcat(cmd, " -y -i ");
    paprika_shell_quote(cmd, sizeof(cmd), input);
    strcat(cmd, " -af ");
    paprika_shell_quote(cmd, sizeof(cmd), filter);
    strcat(cmd, " ");
    paprika_shell_quote(cmd, sizeof(cmd), output);
    strcat(cmd, " 2>&1");

    {
        char banner[sizeof(cmd) + 32];
        snprintf(banner, sizeof(banner), "[paprika] %s", cmd);
        emit(cb, ud, banner);
    }

    FILE *p = POPEN(cmd, "r");
    if (!p) {
        emit(cb, ud, "[paprika] Failed to launch ffmpeg.");
        return false;
    }
    char line[2048];
    while (fgets(line, sizeof(line), p)) {
        paprika_chomp(line);
        emit(cb, ud, line);
    }
    int rc = PCLOSE(p);
    if (rc != 0) {
        char msg[64];
        snprintf(msg, sizeof(msg), "[paprika] ffmpeg exited with status %d", rc);
        emit(cb, ud, msg);
        return false;
    }
    {
        char done[PAPRIKA_PATH_MAX];
        snprintf(done, sizeof(done), "[paprika] Autocut complete -> %s", output);
        emit(cb, ud, done);
    }
    return true;
}
