/*
 * Paprika Tool — raylib + raygui front-end.
 *
 * Same four tabs as the original Rust/egui app:
 *   YouTube · TikTok · Autocut · Settings
 *
 * Each long-running operation is run in a worker thread (job.c) and the
 * main thread polls the job's ring buffer once per frame to render a
 * scrolling log + progress bar.
 */
#include "paprika.h"

#include "raylib.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN_W           880
#define WIN_H           620
#define SIDEBAR_W       180
#define MAIN_X          (SIDEBAR_W + 16)
#define MAIN_W          (WIN_W - MAIN_X - 16)

#define LOG_MAX_LINES   200

/* ── per-tab state ───────────────────────────────────────────────────────── */

typedef enum {
    TAB_YOUTUBE = 0,
    TAB_TIKTOK,
    TAB_AUTOCUT,
    TAB_SETTINGS,
    TAB_COUNT
} tab_id;

typedef struct {
    char    log[LOG_MAX_LINES][PAPRIKA_JOB_MAX_LINE];
    int     log_count;
    paprika_job *job;
} log_panel;

static void log_panel_init(log_panel *p)
{
    p->log_count = 0;
    p->job = paprika_job_new();
}

static void log_panel_drain(log_panel *p)
{
    if (!p->job) return;
    char buf[16][PAPRIKA_JOB_MAX_LINE];
    size_t n;
    while ((n = paprika_job_drain(p->job, buf, 16)) > 0) {
        for (size_t i = 0; i < n; ++i) {
            if (p->log_count == LOG_MAX_LINES) {
                memmove(p->log[0], p->log[1],
                        sizeof(p->log[0]) * (LOG_MAX_LINES - 1));
                p->log_count--;
            }
            snprintf(p->log[p->log_count++], PAPRIKA_JOB_MAX_LINE, "%s", buf[i]);
        }
    }
}

static void log_panel_clear(log_panel *p)
{
    p->log_count = 0;
}

/* Draw the last visible lines of `p->log` inside `bounds`. */
static void log_panel_draw(log_panel *p, Rectangle bounds)
{
    DrawRectangleRec(bounds, (Color){ 18, 20, 28, 255 });
    DrawRectangleLinesEx(bounds, 1, (Color){ 60, 66, 90, 255 });

    int line_h = 14;
    int max_lines = ((int)bounds.height - 12) / line_h;
    int start = p->log_count > max_lines ? p->log_count - max_lines : 0;

    for (int i = start; i < p->log_count; ++i) {
        int y = (int)bounds.y + 6 + (i - start) * line_h;
        const char *line = p->log[i];
        Color c = (Color){ 200, 206, 220, 255 };
        if (strstr(line, "[paprika]"))            c = (Color){ 247, 208, 44, 255 };
        else if (strstr(line, "ERROR") ||
                 strstr(line, "error") ||
                 strstr(line, "failed"))          c = (Color){ 240, 90,  80, 255 };
        else if (strstr(line, "100%"))            c = (Color){  90, 220, 120, 255 };
        DrawText(line, (int)bounds.x + 8, y, 10, c);
    }
}

/* ── tab states ──────────────────────────────────────────────────────────── */

typedef struct {
    char  url[2048];
    bool  url_edit;
    log_panel panel;
} download_tab;

typedef struct {
    char  input[PAPRIKA_PATH_MAX];   bool input_edit;
    char  output[PAPRIKA_PATH_MAX];  bool output_edit;
    char  db_buf[16];                bool db_edit;
    char  fade_buf[16];              bool fade_edit;
    log_panel panel;
} autocut_tab;

typedef struct {
    char  out_dir_edit_buf[PAPRIKA_PATH_MAX];
    bool  out_dir_edit;
    int   quality_active;
    bool  audio_only;
    log_panel panel;
} settings_tab;

typedef struct {
    paprika_config *cfg;
    tab_id          current;
    download_tab    youtube;
    download_tab    tiktok;
    autocut_tab     autocut;
    settings_tab    settings;
    bool            ytdlp_present;
    bool            ffmpeg_present;
    int             tool_check_timer;
} app_state;

/* ── styling ─────────────────────────────────────────────────────────────── */

static void apply_theme(void)
{
    /* Warm "paprika" tones. */
    GuiSetStyle(DEFAULT, BACKGROUND_COLOR,    0x14181fff);
    GuiSetStyle(DEFAULT, BASE_COLOR_NORMAL,   0x1e2433ff);
    GuiSetStyle(DEFAULT, BASE_COLOR_FOCUSED,  0x2a3146ff);
    GuiSetStyle(DEFAULT, BASE_COLOR_PRESSED,  0xc24a18ff);
    GuiSetStyle(DEFAULT, BORDER_COLOR_NORMAL, 0x3a4258ff);
    GuiSetStyle(DEFAULT, BORDER_COLOR_FOCUSED,0xe46008ff);
    GuiSetStyle(DEFAULT, BORDER_COLOR_PRESSED,0xf7d02cff);
    GuiSetStyle(DEFAULT, TEXT_COLOR_NORMAL,   0xc9d1e6ff);
    GuiSetStyle(DEFAULT, TEXT_COLOR_FOCUSED,  0xffffffff);
    GuiSetStyle(DEFAULT, TEXT_COLOR_PRESSED,  0xffffffff);
    GuiSetStyle(DEFAULT, LINE_COLOR,          0x3a4258ff);
    GuiSetStyle(DEFAULT, TEXT_SIZE,           14);
    GuiSetStyle(BUTTON,  BORDER_WIDTH,        2);
}

/* ── sidebar ─────────────────────────────────────────────────────────────── */

static void draw_sidebar(app_state *s)
{
    DrawRectangle(0, 0, SIDEBAR_W, WIN_H, (Color){ 14, 16, 24, 255 });
    DrawRectangle(SIDEBAR_W - 1, 0, 1, WIN_H, (Color){ 60, 66, 90, 255 });

    /* Logo / brand. */
    DrawText("PAPRIKA",       18, 22, 22, (Color){ 247, 208, 44, 255 });
    DrawText("TOOL",          18, 46, 22, (Color){ 228, 96,  8, 255 });
    DrawText("v" PAPRIKA_VERSION, 22, 72, 10, (Color){ 110, 118, 150, 255 });

    static const char *labels[TAB_COUNT] = {
        "  YouTube", "  TikTok", "  Autocut", "  Settings"
    };
    static const Color accents[TAB_COUNT] = {
        { 60, 130, 255, 255 },   /* youtube blue  */
        { 247, 208, 44, 255 },   /* tiktok yellow */
        { 228, 96,   8, 255 },   /* autocut orange*/
        { 110, 118, 150, 255 }   /* settings grey */
    };

    int y = 110;
    for (int i = 0; i < TAB_COUNT; ++i) {
        Rectangle r = { 12, (float)y, SIDEBAR_W - 24, 38 };
        bool sel = (s->current == (tab_id)i);

        Vector2 m = GetMousePosition();
        bool hovered = CheckCollisionPointRec(m, r);

        if (sel) {
            DrawRectangleRec(r, (Color){ 30, 35, 52, 255 });
            DrawRectangle((int)r.x, (int)r.y, 3, (int)r.height, accents[i]);
        } else if (hovered) {
            DrawRectangleRec(r, (Color){ 26, 30, 44, 255 });
        }
        Color tc = sel ? (Color){ 255, 255, 255, 255 }
                       : (Color){ 175, 182, 205, 255 };
        DrawText(labels[i], (int)r.x + 12, (int)r.y + 12, 14, tc);

        if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            s->current = (tab_id)i;
        }
        y += 46;
    }

    /* Tool status footer. */
    int fy = WIN_H - 60;
    Color ok = { 90, 220, 120, 255 };
    Color no = { 240, 90,  80, 255 };
    DrawText("yt-dlp", 18, fy, 11, (Color){ 175, 182, 205, 255 });
    DrawCircle(SIDEBAR_W - 28, fy + 5, 5, s->ytdlp_present ? ok : no);
    DrawText("ffmpeg", 18, fy + 22, 11, (Color){ 175, 182, 205, 255 });
    DrawCircle(SIDEBAR_W - 28, fy + 27, 5, s->ffmpeg_present ? ok : no);
}

/* ── shared download panel ───────────────────────────────────────────────── */

static void draw_download_tab(app_state *s, download_tab *t,
                              const char *title, bool tiktok)
{
    int x = MAIN_X;
    int y = 20;

    DrawText(title, x, y, 24, (Color){ 247, 208, 44, 255 });
    y += 38;

    DrawText("Video URL", x, y, 12, (Color){ 175, 182, 205, 255 });
    y += 18;

    Rectangle url_box = { (float)x, (float)y, MAIN_W - 100, 32 };
    if (GuiTextBox(url_box, t->url, sizeof(t->url), t->url_edit)) {
        t->url_edit = !t->url_edit;
    }
    Rectangle paste_btn = { url_box.x + url_box.width + 6, (float)y, 90, 32 };
    if (GuiButton(paste_btn, "Paste")) {
        const char *clip = GetClipboardText();
        if (clip) snprintf(t->url, sizeof(t->url), "%s", clip);
    }
    y += 44;

    /* Output directory readout. */
    char info[PAPRIKA_PATH_MAX + 32];
    snprintf(info, sizeof(info), "Output: %s", s->cfg->output_dir);
    DrawText(info, x, y, 11, (Color){ 130, 138, 165, 255 });
    y += 16;

    char qline[64];
    snprintf(qline, sizeof(qline), "Quality: %s%s",
             paprika_quality_label(s->cfg->quality),
             s->cfg->audio_only ? "  ·  audio-only (mp3)" : "");
    DrawText(qline, x, y, 11, (Color){ 130, 138, 165, 255 });
    y += 22;

    bool running = paprika_job_state_get(t->panel.job) == PAPRIKA_JOB_RUNNING;

    Rectangle dl_btn = { (float)x, (float)y, 200, 40 };
    GuiSetState(running || strlen(t->url) == 0 ? STATE_DISABLED : STATE_NORMAL);
    if (GuiButton(dl_btn, running ? "#191#  Downloading..." : "#191#  Download")) {
        paprika_download_opts opts = {
            .audio_only = s->cfg->audio_only,
            .quality    = s->cfg->quality,
            .tiktok     = tiktok
        };
        paprika_mkdir_p(s->cfg->output_dir);
        log_panel_clear(&t->panel);
        paprika_job_start_download(t->panel.job, t->url, s->cfg->output_dir, &opts);
    }
    GuiSetState(STATE_NORMAL);

    Rectangle clr_btn = { dl_btn.x + dl_btn.width + 10, (float)y, 110, 40 };
    if (GuiButton(clr_btn, "Clear log")) log_panel_clear(&t->panel);

    y += 52;

    /* Progress bar. */
    float p = paprika_job_progress(t->panel.job);
    if (p < 0) p = 0;
    Rectangle pbar = { (float)x, (float)y, MAIN_W, 18 };
    char prog_text[16];
    snprintf(prog_text, sizeof(prog_text), "%d%%", (int)(p * 100));
    GuiProgressBar(pbar, "", prog_text, &p, 0.0f, 1.0f);
    y += 30;

    /* Log area fills the rest. */
    Rectangle log = { (float)x, (float)y, MAIN_W, (float)(WIN_H - y - 20) };
    log_panel_draw(&t->panel, log);
}

/* ── autocut tab ─────────────────────────────────────────────────────────── */

static void draw_autocut_tab(app_state *s)
{
    autocut_tab *t = &s->autocut;
    int x = MAIN_X;
    int y = 20;

    DrawText("Autocut", x, y, 24, (Color){ 228, 96, 8, 255 });
    y += 38;
    DrawText("Strip silence from an audio file using ffmpeg.",
             x, y, 11, (Color){ 130, 138, 165, 255 });
    y += 24;

    DrawText("Input file", x, y, 12, (Color){ 175, 182, 205, 255 });
    y += 18;
    Rectangle in_box = { (float)x, (float)y, MAIN_W, 30 };
    if (GuiTextBox(in_box, t->input, sizeof(t->input), t->input_edit))
        t->input_edit = !t->input_edit;
    y += 40;

    DrawText("Output file", x, y, 12, (Color){ 175, 182, 205, 255 });
    y += 18;
    Rectangle out_box = { (float)x, (float)y, MAIN_W, 30 };
    if (GuiTextBox(out_box, t->output, sizeof(t->output), t->output_edit))
        t->output_edit = !t->output_edit;
    y += 40;

    DrawText("Threshold (dB)", x, y, 12, (Color){ 175, 182, 205, 255 });
    DrawText("Fade-in (ms)",  x + 160, y, 12, (Color){ 175, 182, 205, 255 });
    y += 18;
    Rectangle db_box   = { (float)x,        (float)y, 140, 30 };
    Rectangle fade_box = { (float)x + 160,  (float)y, 140, 30 };
    if (GuiTextBox(db_box, t->db_buf, sizeof(t->db_buf), t->db_edit))
        t->db_edit = !t->db_edit;
    if (GuiTextBox(fade_box, t->fade_buf, sizeof(t->fade_buf), t->fade_edit))
        t->fade_edit = !t->fade_edit;
    y += 44;

    bool running = paprika_job_state_get(t->panel.job) == PAPRIKA_JOB_RUNNING;
    bool ready   = strlen(t->input) > 0 && strlen(t->output) > 0;

    Rectangle run_btn = { (float)x, (float)y, 200, 38 };
    GuiSetState(running || !ready ? STATE_DISABLED : STATE_NORMAL);
    if (GuiButton(run_btn, running ? "#149#  Processing..." : "#149#  Run autocut")) {
        float db   = (float)atof(t->db_buf);
        if (db == 0.0f) db = -40.0f;
        int   fade = atoi(t->fade_buf);
        if (fade <= 0) fade = 50;
        log_panel_clear(&t->panel);
        paprika_job_start_autocut(t->panel.job, t->input, t->output, db, fade);
    }
    GuiSetState(STATE_NORMAL);
    y += 50;

    Rectangle log = { (float)x, (float)y, MAIN_W, (float)(WIN_H - y - 20) };
    log_panel_draw(&t->panel, log);
}

/* ── settings tab ────────────────────────────────────────────────────────── */

static void draw_settings_tab(app_state *s)
{
    settings_tab *t = &s->settings;
    int x = MAIN_X;
    int y = 20;

    DrawText("Settings", x, y, 24, (Color){ 175, 182, 205, 255 });
    y += 38;

    DrawText("Output directory", x, y, 12, (Color){ 175, 182, 205, 255 });
    y += 18;
    Rectangle dir_box = { (float)x, (float)y, MAIN_W, 30 };
    if (GuiTextBox(dir_box, t->out_dir_edit_buf,
                   sizeof(t->out_dir_edit_buf), t->out_dir_edit)) {
        t->out_dir_edit = !t->out_dir_edit;
        snprintf(s->cfg->output_dir, sizeof(s->cfg->output_dir),
                 "%s", t->out_dir_edit_buf);
        paprika_config_save(s->cfg);
    }
    y += 44;

    DrawText("Video quality", x, y, 12, (Color){ 175, 182, 205, 255 });
    y += 18;
    Rectangle combo = { (float)x, (float)y, 220, 30 };
    int prev_q = t->quality_active;
    GuiComboBox(combo, "Best;1080p;720p;480p", &t->quality_active);
    if (prev_q != t->quality_active) {
        s->cfg->quality = (paprika_quality)t->quality_active;
        paprika_config_save(s->cfg);
    }
    y += 44;

    Rectangle cb = { (float)x, (float)y, 22, 22 };
    bool prev_a = t->audio_only;
    GuiCheckBox(cb, "  Audio only (extract MP3)", &t->audio_only);
    if (prev_a != t->audio_only) {
        s->cfg->audio_only = t->audio_only;
        paprika_config_save(s->cfg);
    }
    y += 44;

    DrawLineEx((Vector2){ (float)x, (float)y },
               (Vector2){ (float)(x + MAIN_W), (float)y },
               1.0f, (Color){ 60, 66, 90, 255 });
    y += 14;

    DrawText("Bundled tools", x, y, 14, (Color){ 247, 208, 44, 255 });
    y += 22;

    bool running = paprika_job_state_get(t->panel.job) == PAPRIKA_JOB_RUNNING;

    Rectangle yt_btn = { (float)x, (float)y, 220, 36 };
    GuiSetState(running ? STATE_DISABLED : STATE_NORMAL);
    if (GuiButton(yt_btn, "Install / update yt-dlp")) {
        log_panel_clear(&t->panel);
        paprika_job_start_install_ytdlp(t->panel.job);
    }
    Rectangle ff_btn = { yt_btn.x + 230, (float)y, 220, 36 };
    if (GuiButton(ff_btn, "Install / update ffmpeg")) {
        log_panel_clear(&t->panel);
        paprika_job_start_install_ffmpeg(t->panel.job);
    }
    GuiSetState(STATE_NORMAL);
    y += 48;

    Rectangle log = { (float)x, (float)y, MAIN_W, (float)(WIN_H - y - 20) };
    log_panel_draw(&t->panel, log);
}

/* ── lifecycle ───────────────────────────────────────────────────────────── */

static void state_init(app_state *s, paprika_config *cfg)
{
    memset(s, 0, sizeof(*s));
    s->cfg     = cfg;
    s->current = TAB_YOUTUBE;

    log_panel_init(&s->youtube.panel);
    log_panel_init(&s->tiktok.panel);
    log_panel_init(&s->autocut.panel);
    log_panel_init(&s->settings.panel);

    snprintf(s->autocut.db_buf,   sizeof(s->autocut.db_buf),   "-40");
    snprintf(s->autocut.fade_buf, sizeof(s->autocut.fade_buf), "50");

    snprintf(s->settings.out_dir_edit_buf,
             sizeof(s->settings.out_dir_edit_buf),
             "%s", cfg->output_dir);
    s->settings.quality_active = (int)cfg->quality;
    s->settings.audio_only     = cfg->audio_only;

    s->ytdlp_present  = paprika_ytdlp_present();
    s->ffmpeg_present = paprika_ffmpeg_present();
}

static void state_free(app_state *s)
{
    paprika_job_free(s->youtube.panel.job);
    paprika_job_free(s->tiktok.panel.job);
    paprika_job_free(s->autocut.panel.job);
    paprika_job_free(s->settings.panel.job);
}

int paprika_gui_run(paprika_config *cfg)
{
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIGHDPI | FLAG_MSAA_4X_HINT);
    InitWindow(WIN_W, WIN_H, "Paprika Tool");
    SetTargetFPS(60);
    SetExitKey(0); /* don't exit on ESC */

    apply_theme();

    app_state s;
    state_init(&s, cfg);

    while (!WindowShouldClose()) {
        /* Drain background workers each frame. */
        log_panel_drain(&s.youtube.panel);
        log_panel_drain(&s.tiktok.panel);
        log_panel_drain(&s.autocut.panel);
        log_panel_drain(&s.settings.panel);

        /* Re-probe tool availability roughly twice a second. */
        if (++s.tool_check_timer >= 30) {
            s.tool_check_timer = 0;
            s.ytdlp_present  = paprika_ytdlp_present();
            s.ffmpeg_present = paprika_ffmpeg_present();
        }

        BeginDrawing();
        ClearBackground((Color){ 20, 24, 32, 255 });

        draw_sidebar(&s);

        switch (s.current) {
        case TAB_YOUTUBE:  draw_download_tab(&s, &s.youtube, "YouTube",  false); break;
        case TAB_TIKTOK:   draw_download_tab(&s, &s.tiktok,  "TikTok",   true);  break;
        case TAB_AUTOCUT:  draw_autocut_tab(&s);  break;
        case TAB_SETTINGS: draw_settings_tab(&s); break;
        default: break;
        }

        EndDrawing();
    }

    paprika_config_save(cfg);
    state_free(&s);
    CloseWindow();
    return 0;
}
