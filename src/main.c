/*
 * Paprika Tool — entry point.
 *
 * Default behaviour: launch the raylib GUI (when built with PAPRIKA_HAS_GUI).
 * `--cli`         opens the interactive text menu.
 * Any positional URL puts the tool into one-shot scripting mode.
 */
#include "paprika.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── stdout line callback used by all CLI paths ──────────────────────────── */

static void stdout_cb(const char *line, void *ud)
{
    (void)ud;
    fputs(line, stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

/* ── interactive menu (legacy CLI mode) ──────────────────────────────────── */

static void read_line(char *buf, size_t cap)
{
    if (!fgets(buf, (int)cap, stdin)) { buf[0] = '\0'; return; }
    paprika_chomp(buf);
}

static void prompt(const char *label, char *buf, size_t cap)
{
    fputs(label, stdout); fflush(stdout);
    read_line(buf, cap);
}

static int read_choice(int lo, int hi, int def)
{
    char buf[32];
    read_line(buf, sizeof(buf));
    if (!buf[0]) return def;
    int v = atoi(buf);
    if (v < lo || v > hi) return def;
    return v;
}

static void show_main_menu(const paprika_config *cfg)
{
    puts("");
    puts("─────────────────────────────────────────────");
    printf("  Output dir : %s\n", cfg->output_dir);
    printf("  Quality    : %s%s\n",
           paprika_quality_label(cfg->quality),
           cfg->audio_only ? "  (audio-only · MP3)" : "");
    puts("─────────────────────────────────────────────");
    puts("  1)  Download YouTube video");
    puts("  2)  Download TikTok video");
    puts("  3)  Autocut audio (strip silence)");
    puts("  4)  Settings");
    puts("  5)  Install / update yt-dlp");
    puts("  6)  Install / update ffmpeg");
    puts("  0)  Quit");
    puts("");
    fputs("  > ", stdout); fflush(stdout);
}

static void do_cli_download(paprika_config *cfg, bool tiktok)
{
    if (!paprika_ytdlp_present()) {
        puts("\n  yt-dlp is not installed. Install it from the main menu first.\n");
        return;
    }
    char url[2048];
    prompt("\n  URL: ", url, sizeof(url));
    if (!url[0]) return;

    paprika_download_opts opts = { cfg->audio_only, cfg->quality, tiktok };
    paprika_mkdir_p(cfg->output_dir);
    puts("");
    if (paprika_download(url, cfg->output_dir, &opts, stdout_cb, NULL))
        puts("\n  ✓ Download finished.\n");
    else
        puts("\n  ✗ Download failed.\n");
}

static void do_cli_autocut(void)
{
    char input[PAPRIKA_PATH_MAX], output[PAPRIKA_PATH_MAX];
    char db_buf[32], fade_buf[32];
    prompt("\n  Input file  : ", input, sizeof(input));
    if (!input[0]) return;
    prompt("  Output file : ", output, sizeof(output));
    if (!output[0]) return;
    prompt("  Threshold dB [-40]: ", db_buf, sizeof(db_buf));
    prompt("  Fade-in ms   [50]: ", fade_buf, sizeof(fade_buf));

    float db   = db_buf[0]   ? (float)atof(db_buf) : -40.0f;
    int   fade = fade_buf[0] ?       atoi(fade_buf) : 50;

    if (paprika_autocut(input, output, db, fade, stdout_cb, NULL))
        puts("\n  ✓ Autocut complete.\n");
    else
        puts("\n  ✗ Autocut failed.\n");
}

static void do_cli_settings(paprika_config *cfg)
{
    puts("");
    puts("  ── Settings ──");
    printf("  1) Output directory  [%s]\n", cfg->output_dir);
    printf("  2) Quality           [%s]\n", paprika_quality_label(cfg->quality));
    printf("  3) Audio-only mode   [%s]\n", cfg->audio_only ? "on" : "off");
    puts("  0) Back");
    fputs("  > ", stdout); fflush(stdout);

    int c = read_choice(0, 3, 0);
    if (c == 1) {
        char buf[PAPRIKA_PATH_MAX];
        prompt("  New output dir: ", buf, sizeof(buf));
        if (buf[0]) snprintf(cfg->output_dir, sizeof(cfg->output_dir), "%s", buf);
    } else if (c == 2) {
        puts("    1) Best   2) 1080p   3) 720p   4) 480p");
        fputs("    > ", stdout); fflush(stdout);
        int q = read_choice(1, 4, 1);
        cfg->quality = (paprika_quality)(q - 1);
    } else if (c == 3) {
        cfg->audio_only = !cfg->audio_only;
    }
    paprika_config_save(cfg);
}

static int run_interactive_cli(paprika_config *cfg)
{
    puts("");
    puts("  ╔══════════════════════════════════════════╗");
    puts("  ║              Paprika Tool                ║");
    puts("  ║         media downloader · v" PAPRIKA_VERSION "         ║");
    puts("  ╚══════════════════════════════════════════╝");

    for (;;) {
        show_main_menu(cfg);
        int c = read_choice(0, 6, 0);
        switch (c) {
        case 1: do_cli_download(cfg, false); break;
        case 2: do_cli_download(cfg, true);  break;
        case 3: do_cli_autocut(); break;
        case 4: do_cli_settings(cfg); break;
        case 5: paprika_install_ytdlp(stdout_cb, NULL); break;
        case 6: paprika_install_ffmpeg(stdout_cb, NULL); break;
        case 0:
        default:
            paprika_config_save(cfg);
            puts("\n  Goodbye.\n");
            return 0;
        }
    }
}

/* ── flag dispatcher ─────────────────────────────────────────────────────── */

static int run_command_line(int argc, char **argv,
                            paprika_config *cfg, bool *force_cli)
{
    const char *url = NULL;
    bool tiktok = false;

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if      (strcmp(a, "--audio") == 0) cfg->audio_only = true;
        else if (strcmp(a, "--tiktok") == 0) tiktok = true;
        else if (strcmp(a, "--1080") == 0) cfg->quality = PAPRIKA_QUALITY_1080;
        else if (strcmp(a, "--720")  == 0) cfg->quality = PAPRIKA_QUALITY_720;
        else if (strcmp(a, "--480")  == 0) cfg->quality = PAPRIKA_QUALITY_480;
        else if (strcmp(a, "--cli")  == 0) { *force_cli = true; }
        else if (strcmp(a, "--out")  == 0 && i + 1 < argc) {
            snprintf(cfg->output_dir, sizeof(cfg->output_dir), "%s", argv[++i]);
        }
        else if (strcmp(a, "--install-ytdlp") == 0)
            return paprika_install_ytdlp(stdout_cb, NULL) ? 0 : 1;
        else if (strcmp(a, "--install-ffmpeg") == 0)
            return paprika_install_ffmpeg(stdout_cb, NULL) ? 0 : 1;
        else if (strcmp(a, "--version") == 0) {
            puts(PAPRIKA_NAME " " PAPRIKA_VERSION);
            return 0;
        }
        else if (strcmp(a, "--help") == 0) {
            puts(PAPRIKA_NAME " " PAPRIKA_VERSION);
            puts("Usage: paprika                       (launch GUI)");
            puts("       paprika --cli                 (interactive text menu)");
            puts("       paprika [URL] [--audio] [--tiktok] [--1080|--720|--480] [--out DIR]");
            puts("       paprika --install-ytdlp | --install-ffmpeg");
            return 0;
        }
        else if (a[0] != '-') url = a;
    }

    if (!url) return -1;

    if (!paprika_ytdlp_present()) {
        fprintf(stderr, "yt-dlp not found. Run: paprika --install-ytdlp\n");
        return 1;
    }
    paprika_mkdir_p(cfg->output_dir);
    paprika_download_opts opts = { cfg->audio_only, cfg->quality, tiktok };
    return paprika_download(url, cfg->output_dir, &opts, stdout_cb, NULL) ? 0 : 1;
}

int main(int argc, char **argv)
{
    paprika_config cfg;
    paprika_config_load(&cfg);

    bool force_cli = false;
    if (argc > 1) {
        int rc = run_command_line(argc, argv, &cfg, &force_cli);
        if (rc >= 0) return rc;
    }

#ifdef PAPRIKA_HAS_GUI
    if (!force_cli) return paprika_gui_run(&cfg);
#endif
    return run_interactive_cli(&cfg);
}
