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

/* yt-dlp format strings, mirrored from the Rust version. */
static const char *format_for(paprika_quality q)
{
    switch (q) {
    case PAPRIKA_QUALITY_BEST:
        return "bestvideo[vcodec^=avc1][ext=mp4]+bestaudio[ext=m4a]"
               "/bestvideo[ext=mp4]+bestaudio[ext=m4a]"
               "/bestvideo+bestaudio/best[ext=mp4]/best";
    case PAPRIKA_QUALITY_1080:
        return "bestvideo[height<=1080][vcodec^=avc1][ext=mp4]+bestaudio[ext=m4a]"
               "/bestvideo[height<=1080][ext=mp4]+bestaudio[ext=m4a]"
               "/bestvideo[height<=1080]+bestaudio"
               "/best[height<=1080][ext=mp4]/best[height<=1080]/best";
    case PAPRIKA_QUALITY_720:
        return "bestvideo[height<=720][vcodec^=avc1][ext=mp4]+bestaudio[ext=m4a]"
               "/bestvideo[height<=720][ext=mp4]+bestaudio[ext=m4a]"
               "/bestvideo[height<=720]+bestaudio"
               "/best[height<=720][ext=mp4]/best[height<=720]/best";
    case PAPRIKA_QUALITY_480:
        return "bestvideo[height<=480][vcodec^=avc1][ext=mp4]+bestaudio[ext=m4a]"
               "/bestvideo[height<=480][ext=mp4]+bestaudio[ext=m4a]"
               "/bestvideo[height<=480]+bestaudio"
               "/best[height<=480][ext=mp4]/best[height<=480]/best";
    }
    return "best";
}

bool paprika_download(const char *url,
                      const char *output_dir,
                      const paprika_download_opts *opts)
{
    if (!url || !*url) {
        fprintf(stderr, "[paprika] empty URL\n");
        return false;
    }

    char ytdlp[PAPRIKA_PATH_MAX];
    paprika_ytdlp_command(ytdlp, sizeof(ytdlp));

    char ffmpeg[PAPRIKA_PATH_MAX];
    paprika_ffmpeg_path(ffmpeg, sizeof(ffmpeg));
    bool have_local_ffmpeg = paprika_file_exists(ffmpeg);

    char output_template[PAPRIKA_PATH_MAX];
    paprika_path_join(output_template, sizeof(output_template),
                      output_dir, "%(title)s.%(ext)s");

    /* Build the command. We use the shell to keep this portable across
     * Windows (cmd.exe) and macOS (sh) — both ship `popen`. */
    char cmd[PAPRIKA_PATH_MAX * 4] = {0};

    paprika_shell_quote(cmd, sizeof(cmd), ytdlp);
    strcat(cmd, " --newline -o ");
    paprika_shell_quote(cmd, sizeof(cmd), output_template);

    if (have_local_ffmpeg) {
        char ffmpeg_dir[PAPRIKA_PATH_MAX];
        if (paprika_exe_dir(ffmpeg_dir, sizeof(ffmpeg_dir))) {
            strcat(cmd, " --ffmpeg-location ");
            paprika_shell_quote(cmd, sizeof(cmd), ffmpeg_dir);
        }
    }

    if (opts->audio_only) {
        strcat(cmd, " -x --audio-format mp3");
    } else {
        strcat(cmd, " -f ");
        paprika_shell_quote(cmd, sizeof(cmd),
            opts->tiktok
            ? "bestvideo[ext=mp4]+bestaudio[ext=m4a]/bestvideo+bestaudio/best"
            : format_for(opts->quality));
        strcat(cmd, " --merge-output-format mp4");
    }

    strcat(cmd, " ");
    paprika_shell_quote(cmd, sizeof(cmd), url);

    /* Merge stderr into stdout so the user sees errors live. */
    strcat(cmd, " 2>&1");

    fprintf(stderr, "[paprika] %s\n", cmd);

    FILE *p = POPEN(cmd, "r");
    if (!p) {
        fprintf(stderr, "[paprika] Failed to launch yt-dlp.\n");
        return false;
    }

    char line[2048];
    while (fgets(line, sizeof(line), p)) {
        fputs(line, stdout);
        fflush(stdout);
    }

    int rc = PCLOSE(p);
    if (rc != 0) {
        fprintf(stderr, "[paprika] yt-dlp exited with status %d\n", rc);
        return false;
    }
    return true;
}
