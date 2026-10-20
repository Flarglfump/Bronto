/*
 * Bronto Engine - pack files: a whole project folder in one file.
 *
 * A built game is the Bronto player with its project packed in: the
 * scenes, scripts, images and sounds, read from the pack instead of from
 * loose files. Once a pack is mounted, br_file_read_all() (which the
 * asset cache, scene loading and scripts all use) finds relative paths in
 * it first, so nothing else has to know.
 *
 * A pack can be its own file (game.brpak) or be appended to another file
 * - the player executable - and is found from the end of the file:
 *
 *   [file data ...][index][trailer: "BRONTOPK", version, count, index offset, pack size]
 *
 * Numbers are little-endian; offsets count from the start of the pack.
 */
#ifndef BRONTO_PACK_H
#define BRONTO_PACK_H

#include "types.h"

#define BR_PACK_MAGIC "BRONTOPK"
#define BR_PACK_VERSION 1

/* Return false to leave a file (or a whole folder) out of a pack. */
typedef bool (*br_pack_filter)(const char *rel_path, bool is_dir, void *user);

typedef struct br_pack_stats { uint32_t files; uint64_t bytes; } br_pack_stats;

/* Packs every file under `folder` (hidden ones aren't included) into
 * `out_path`, or adds the pack to the end of it with `append`. Paths in
 * the pack are relative to `folder`, with '/' between parts. */
bool br_pack_create(const char *folder, const char *out_path, bool append, br_pack_filter filter, void *user,
                    br_pack_stats *stats, char *err, size_t err_size);

/* Mounts a pack file, or the pack at the end of any file (an executable).
 * One pack at a time: mounting another unmounts the first. */
bool br_pack_mount(const char *path);
void br_pack_unmount(void);
bool br_pack_mounted(void);
bool br_pack_contains(const char *rel_path);
/* A file from the mounted pack, br_alloc'd and NUL terminated like
 * br_file_read_all, or NULL. */
void *br_pack_read(const char *rel_path, size_t *size);
uint32_t br_pack_file_count(void);
const char *br_pack_file_name(uint32_t index);

#endif /* BRONTO_PACK_H */
