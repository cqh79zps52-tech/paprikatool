#include "paprika.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void emit(paprika_line_cb cb, void *ud, const char *line)
{
    if (cb) cb(line, ud);
    else    fprintf(stderr, "%s\n", line);
}

bool paprika_download(const char *url,
                      const char *output_dir,
                      const paprika_download_opts *opts,
                      paprika_line_cb cb,
                      void *ud)
{
    if (!url || !*url) {
        emit(cb, ud, "[paprika] empty URL");
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

    char cmd[PAPRIKA_PATH_MAX * 4] = {0};

    paprika_shell_quote(cmd, sizeof(cmd), ytdlp);
    strcat(cmd, " --newline -o ");
    paprika_shell_quote(cmd, sizeof(cmd), output_template);

    if (have_local_ffmpeg) {
        char ffmpeg_dir[PAPRIKA_PATH_MAX];
        paprika_tools_dir(ffmpeg_dir, sizeof(ffmpeg_dir));
        strcat(cmd, " --ffmpeg-location ");
        paprika_shell_quote(cmd, sizeof(cmd), ffmpeg_dir);
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

    {
        char banner[sizeof(cmd) + 32];
        snprintf(banner, sizeof(banner), "[paprika] %s", cmd);
        emit(cb, ud, banner);
    }

    int rc = paprika_run_capture(cmd, cb, ud);
    if (rc != 0) {
        char msg[64];
        snprintf(msg, sizeof(msg), "[paprika] yt-dlp exited with status %d", rc);
        emit(cb, ud, msg);
        return false;
    }
    return true;
}
