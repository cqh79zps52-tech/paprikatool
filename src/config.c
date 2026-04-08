#include "paprika.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void config_path(char *out, size_t cap)
{
    char dir[PAPRIKA_PATH_MAX];
    if (!paprika_config_dir(dir, sizeof(dir))) { snprintf(out, cap, "paprika.cfg"); return; }
    paprika_path_join(out, cap, dir, "paprika.cfg");
}

const char *paprika_quality_label(paprika_quality q)
{
    switch (q) {
    case PAPRIKA_QUALITY_BEST: return "Best";
    case PAPRIKA_QUALITY_1080: return "1080p";
    case PAPRIKA_QUALITY_720:  return "720p";
    case PAPRIKA_QUALITY_480:  return "480p";
    }
    return "Best";
}

static paprika_quality parse_quality(const char *s)
{
    if (!s) return PAPRIKA_QUALITY_BEST;
    if (strcmp(s, "1080p") == 0) return PAPRIKA_QUALITY_1080;
    if (strcmp(s, "720p")  == 0) return PAPRIKA_QUALITY_720;
    if (strcmp(s, "480p")  == 0) return PAPRIKA_QUALITY_480;
    return PAPRIKA_QUALITY_BEST;
}

void paprika_config_load(paprika_config *cfg)
{
    /* Defaults. */
    paprika_default_download_dir(cfg->output_dir, sizeof(cfg->output_dir));
    cfg->audio_only = false;
    cfg->quality    = PAPRIKA_QUALITY_BEST;

    char path[PAPRIKA_PATH_MAX];
    config_path(path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[PAPRIKA_PATH_MAX + 64];
    while (fgets(line, sizeof(line), f)) {
        paprika_chomp(line);
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;

        if (strcmp(key, "output_dir") == 0) {
            snprintf(cfg->output_dir, sizeof(cfg->output_dir), "%s", val);
        } else if (strcmp(key, "audio_only") == 0) {
            cfg->audio_only = (strcmp(val, "true") == 0 || strcmp(val, "1") == 0);
        } else if (strcmp(key, "quality") == 0) {
            cfg->quality = parse_quality(val);
        }
    }
    fclose(f);
}

void paprika_config_save(const paprika_config *cfg)
{
    char path[PAPRIKA_PATH_MAX];
    config_path(path, sizeof(path));

    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "output_dir=%s\n", cfg->output_dir);
    fprintf(f, "audio_only=%s\n", cfg->audio_only ? "true" : "false");
    fprintf(f, "quality=%s\n", paprika_quality_label(cfg->quality));
    fclose(f);
}
