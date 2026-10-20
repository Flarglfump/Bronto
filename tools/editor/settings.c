/*
 * BRONTO EDITOR - preferences, and opening scripts in a text editor.
 *
 * Preferences live in the user's settings folder (br_fs_config_dir:
 * %APPDATA%\Bronto, ~/Library/Application Support/Bronto, ~/.config/Bronto)
 * as "key = value" lines in editor.cfg, so they apply to every project.
 *
 * The script editor is a command line with {file} and {line} in it, for
 * example  code -g "{file}:{line}". Empty means the system's default program
 * for .bs files.
 */
#include "editor.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const struct { const char *label, *command; } k_presets[] = {
    { "Default", "" },
    { "VS Code", "code -g \"{file}:{line}\"" },
    { "Sublime Text", "subl \"{file}:{line}\"" },
    { "Notepad++", "notepad++ -n{line} \"{file}\"" },
    { "Zed", "zed \"{file}:{line}\"" },
};

static bool prefs_path(char *out, size_t size) {
    char dir[ED_PATH_MAX];
    if (!br_fs_config_dir("Bronto", dir, sizeof(dir))) return false;
    br_path_join(out, size, dir, "editor.cfg");
    return true;
}

void ed_prefs_load(editor *ed) {
    char path[ED_PATH_MAX];
    char *text, *line;
    size_t size;
    if (ed->headless || !prefs_path(path, sizeof(path)) || !(text = (char *)br_file_read_all(path, &size))) return;
    for (line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
        char *eq = strchr(line, '=');
        size_t n;
        if (line[0] == '#' || !eq) continue;
        *eq = 0;
        n = strlen(line);
        while (n && line[n - 1] == ' ') line[--n] = 0;
        eq++;
        while (*eq == ' ') eq++;
        n = strlen(eq);
        while (n && (eq[n - 1] == '\r' || eq[n - 1] == ' ')) eq[--n] = 0;
        if (strcmp(line, "script_editor") == 0) snprintf(ed->prefs.script_editor, sizeof(ed->prefs.script_editor), "%s", eq);
    }
    br_free(text);
}

bool ed_prefs_save(editor *ed) {
    char path[ED_PATH_MAX], text[600];
    if (ed->headless) return true;                     /* the self-test never touches your settings */
    if (!prefs_path(path, sizeof(path))) return false;
    snprintf(text, sizeof(text), "# Bronto editor settings\nscript_editor = %s\n", ed->prefs.script_editor);
    return br_file_write_all(path, text, strlen(text));
}

/* The command with {file} and {line} filled in ({file} added at the end if missing). */
void ed_script_command(const char *tmpl, const char *file, int line, char *out, size_t size) {
    size_t o = 0;
    const char *p = tmpl;
    bool had_file = strstr(tmpl, "{file}") != NULL;
    if (size == 0) return;
    while (*p && o + 1 < size) {
        if (strncmp(p, "{file}", 6) == 0) { o += (size_t)snprintf(out + o, size - o, "%s", file); p += 6; }
        else if (strncmp(p, "{line}", 6) == 0) { o += (size_t)snprintf(out + o, size - o, "%d", line); p += 6; }
        else out[o++] = *p++;
        if (o >= size) o = size - 1;
    }
    out[o] = 0;
    if (!had_file) snprintf(out + o, size - o, " \"%s\"", file);
}

bool ed_open_script(editor *ed, const char *rel, int line) {
    char full[ED_PATH_MAX], abs[ED_PATH_MAX], cmd[1600];
    ed_asset_path(ed, rel, full, sizeof(full));
    if (!br_fs_absolute(full, abs, sizeof(abs))) snprintf(abs, sizeof(abs), "%s", full);
    if (line < 1) line = 1;
    if (ed->prefs.script_editor[0]) {
        ed_script_command(ed->prefs.script_editor, abs, line, cmd, sizeof(cmd));
        if (br_launch(ed->engine, cmd)) { ed_status(ed, "Opened %s (Play picks up your changes)", rel); return true; }
        ed_status(ed, "Couldn't run \"%s\" - check Settings > Script editor", cmd);
        return false;
    }
    if (br_open_path(ed->engine, abs)) { ed_status(ed, "Opened %s in your editor; Play picks up your changes", rel); return true; }
    ed_status(ed, "Couldn't open %s - pick a script editor in Settings", rel);
    return false;
}

/* ---- the Settings dialog ------------------------------------------------------- */

void ed_settings_open(editor *ed) {
    snprintf(ed->settings_field, sizeof(ed->settings_field), "%s", ed->prefs.script_editor);
    ed->settings_visible = true;
}

void ed_settings_dialog(editor *ed, br_rect screen) {
    br_ui *ui = &ed->ui;
    br_rect box = br_rect_make(floorf(screen.x + (screen.w - 520) / 2), floorf(screen.y + (screen.h - 300) / 2), 520, 300);
    char example[700];
    int i;
    if (!ed->settings_visible) return;
    br_fill_rect(ui->r, screen, br_rgba(0, 0, 0, 140));
    br_ui_set_disabled(ui, false);
    br_ui_panel_begin(ui, "settings", box);
    br_ui_heading(ui, "Settings");
    br_ui_label(ui, "Script editor: the program that opens .bs files");
    br_ui_text_field(ui, "Command", ed->settings_field, sizeof(ed->settings_field));
    br_ui_label_colored(ui, "{file} is the script's path, {line} the line to show.", ui->style.text_dim);
    br_ui_label_colored(ui, "Empty: your system's default program for .bs files.", ui->style.text_dim);
    br_ui_spacing(ui, 4);
    br_ui_label(ui, "Presets:");
    br_ui_columns(ui, (int)BR_ARRAY_COUNT(k_presets));
    for (i = 0; i < (int)BR_ARRAY_COUNT(k_presets); ++i) {
        char label[48];
        snprintf(label, sizeof(label), "%s##preset", k_presets[i].label);
        if (br_ui_toggle(ui, label, strcmp(ed->settings_field, k_presets[i].command) == 0))
            snprintf(ed->settings_field, sizeof(ed->settings_field), "%s", k_presets[i].command);
    }
    br_ui_spacing(ui, 4);
    if (ed->settings_field[0]) {
        char cmd[600];
        ed_script_command(ed->settings_field, "/path/to/player.bs", 12, cmd, sizeof(cmd));
        snprintf(example, sizeof(example), "Runs: %s", cmd);
    } else {
        snprintf(example, sizeof(example), "Opens the file with the system's default program.");
    }
    br_ui_label_colored(ui, example, ui->style.text_dim);
    br_ui_spacing(ui, 10);
    br_ui_columns(ui, 2);
    if (br_ui_button(ui, "Save##settings")) {
        snprintf(ed->prefs.script_editor, sizeof(ed->prefs.script_editor), "%s", ed->settings_field);
        ed->settings_visible = false;
        if (ed_prefs_save(ed)) ed_status(ed, "Settings saved");
        else ed_status(ed, "Couldn't save the settings file");
    }
    if (br_ui_button(ui, "Cancel##settings") || br_key_pressed(&ed->engine->input, BR_KEY_ESCAPE)) ed->settings_visible = false;
    br_draw_rect(ui->r, box, ui->style.accent);
    br_ui_panel_end(ui);
}
