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

bool paprika_autocut(const char *input,
                     const char *output,
                     float threshold_db,
                     int   fade_ms)
{
    if (!paprika_ffmpeg_present()) {
        fprintf(stderr, "[paprika] ffmpeg not found — install it from the menu.\n");
        return false;
    }

    char ffmpeg[PAPRIKA_PATH_MAX];
    paprika_ffmpeg_command(ffmpeg, sizeof(ffmpeg));

    /* Build the silenceremove + afade filter chain. */
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

    fprintf(stderr, "[paprika] %s\n", cmd);

    FILE *p = POPEN(cmd, "r");
    if (!p) {
        fprintf(stderr, "[paprika] Failed to launch ffmpeg.\n");
        return false;
    }
    char line[2048];
    while (fgets(line, sizeof(line), p)) fputs(line, stdout);
    int rc = PCLOSE(p);
    if (rc != 0) {
        fprintf(stderr, "[paprika] ffmpeg exited with status %d\n", rc);
        return false;
    }
    fprintf(stderr, "[paprika] Autocut complete -> %s\n", output);
    return true;
}
