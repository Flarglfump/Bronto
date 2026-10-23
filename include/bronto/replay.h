/*
 * Bronto Engine - recording and replaying a run, frame for frame.
 *
 * A run is deterministic: the same inputs on the same frames give the same
 * game (bmath.h's RNG is seeded, the clock is virtual, saved data starts
 * empty). So a recording only needs the inputs - which keys went down or up
 * on which frame - and replaying them plays the run again exactly, at any
 * speed, in a window or a browser.
 *
 * Both work by wrapping the platform: br_replay_platform() returns a
 * platform that forwards everything to the real one except
 *   - the clock: virtual, exactly 1/60 s a frame, so every frame's dt (and
 *     so every fixed step) is the same as when it was recorded;
 *   - the game's input: the real keyboard, mouse and gamepads are taken out
 *     of the events, and the recorded keys put in instead (when recording,
 *     the keys a program asks for with br_replay_key());
 *   - pacing: frames are held to real time at the chosen speed.
 * While replaying in a window, keys control playback (the real input never
 * reaches the game):
 *   Space pause / resume    . (period) one frame, while paused
 *   1 2 3 4  speed x1 x2 x4 x8      0  as fast as it goes
 *   N / ]  on to the next segment   P / [  back to this segment's start
 *   (or the one before, near its start) H  show / hide the status bar
 *
 * The file is text (".brec"):
 *   brec 1                 the format
 *   game <name>            for people; not checked
 *   seg <frame> <name>     a segment starts on that frame ("Overworld 1")
 *   k <frame> <+|-><key>   a key goes down (+) or up (-) on that frame
 *   h <frame> <hex>        the picture's hash at the end of that frame
 *   end <frames>           how many frames the run has
 * Replaying checks every recorded hash, so a replay that drifts says so.
 */
#ifndef BRONTO_REPLAY_H
#define BRONTO_REPLAY_H

#include "types.h"
#include "input.h"
#include "platform.h"

struct br_engine;

#define BR_REPLAY_MAX_SEGMENTS 64

typedef enum br_replay_mode { BR_REPLAY_OFF = 0, BR_REPLAY_RECORD, BR_REPLAY_PLAY } br_replay_mode;

/* ---- recording ---- */
/* Starts a recording (written by br_replay_finish). `hash_every`: how often
 * to keep the picture's hash, in frames (0 = 60). */
bool br_replay_record(const char *game, uint32_t hash_every);
/* Holds a key down (or lets it go) from the next frame on. */
void br_replay_key(br_key key, bool down);
/* A new segment starts with the next frame. */
void br_replay_segment(const char *name);

/* ---- replaying ---- */
/* Loads a recording (br_file_read_all, so it can come from a pack). */
bool br_replay_load(const char *path);
bool br_replay_load_text(const char *text, size_t size);
/* Starts playback at a segment (0 = the start): earlier frames run as fast
 * as they can, unseen. */
void br_replay_start_at_segment(int segment);

/* ---- either ---- */
/* The platform to give the engine (br_engine_config.platform): `inner` with
 * the clock, input and pacing replaced. `live`: a window to watch (paced to
 * real time, with the playback keys); false runs flat out (headless). */
const br_platform *br_replay_platform(const br_platform *inner, bool live);
/* Call after br_engine_init: the replay suspends and steps the engine. */
void br_replay_attach(struct br_engine *e);
/* After each frame (br_engine_loop's after_frame does it): runs extra frames
 * at speeds above x1, draws the status bar, ends the run after the last
 * recorded frame. Returns false once the replay has ended. */
bool br_replay_after_frame(struct br_engine *e);
/* Going back to an earlier segment starts the game again: the engine is
 * shut down and initialised again in place (the window stays), after `fn`
 * has put the program's own state back as it was at the start (the first
 * scene, for instance). Without a hook, going back is not offered. */
void br_replay_set_restart_hook(void (*fn)(struct br_engine *e));
/* Deletes every file in the game's saved-data folder (save.h): a run starts
 * from nothing saved, as it did when it was recorded. */
void br_replay_clear_saves(void);
/* Unwatched (live = false), only the frames whose pictures are hashed are
 * drawn, which is much faster. On: draw every frame anyway. */
void br_replay_draw_every_frame(bool on);
/* Writes a recording to `path` (record mode), and forgets everything. */
bool br_replay_finish(const char *path);

br_replay_mode br_replay_mode_now(void);
uint64_t br_replay_frame(void);          /* frames run so far */
uint64_t br_replay_length(void);         /* frames in the recording (play mode) */
int br_replay_segment_count(void);
int br_replay_segment_at(uint64_t frame);/* the segment that frame is in (-1: none) */
const char *br_replay_segment_name(int i);
uint64_t br_replay_segment_start(int i);
bool br_replay_ended(void);              /* every recorded frame has played */
/* Hashes compared while replaying, and how many differed (the first such
 * frame in *first, if any). */
uint32_t br_replay_mismatches(uint32_t *checked, uint64_t *first);
/* The hash of the frame just drawn (FNV-1a over the framebuffer). */
uint32_t br_replay_last_hash(void);

#endif
