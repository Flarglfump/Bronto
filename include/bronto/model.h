/*
 * Bronto Engine - models with skeletons and animations (glTF 2.0).
 *
 * glTF is what Blender and most 3D tools export: a .gltf (JSON, with its data
 * in a .bin file next to it or inside as base64) or a .glb (the same in one
 * binary file). Bronto reads, with no library:
 *
 *   - meshes (triangles: positions, normals, texture coordinates), each part
 *     with its material's colour and base-colour texture (PNG, inside or beside);
 *   - the node tree (translation, rotation, scale or a matrix per node);
 *   - skins: vertices bound to up to four joints each, with weights;
 *   - animations: translation, rotation and scale keys per node, stepped or
 *     linear (cubic splines are read as linear between their keys).
 *
 * A Mesh component with shape `model` and a .gltf / .glb `file` draws one; an
 * Animator component (components3d.h) plays its animations, blending from one
 * to the next. Skinning is done on the CPU each frame.
 *
 * Models face +z in glTF's convention, while Bronto's "forward" is -z: turn a
 * character 180 degrees about y (or turn it in the tool that exported it).
 */
#ifndef BRONTO_MODEL_H
#define BRONTO_MODEL_H

#include "render3d.h"

typedef struct br_model_part {
    br_mesh mesh;               /* in its node's space (skinned: in the skeleton's) */
    int node;                   /* the node it hangs on (-1: none) */
    int skin;                   /* the skin moving it (-1: none) */
    uint8_t (*joints)[4];       /* per vertex: skin joints (indices into the skin's list) */
    float (*weights)[4];        /* ... and how much each counts */
    br_color color;             /* the material's base colour */
    br_image *texture;          /* its base-colour texture (owned by the model; may be NULL) */
} br_model_part;

typedef struct br_model_node {
    char name[48];
    int parent;                 /* -1: a root */
    br_vec3 t, s;               /* at rest */
    br_quat r;
} br_model_node;

typedef struct br_model_skin {
    int joint_count;
    int *joints;                /* node index of each joint */
    br_mat4 *inverse_bind;      /* per joint */
} br_model_skin;

enum { BR_MODEL_T = 0, BR_MODEL_R, BR_MODEL_S };
typedef struct br_model_channel {
    int node, path;             /* path: BR_MODEL_T, _R or _S */
    bool step;                  /* hold each key until the next (else linear) */
    int count;                  /* keys */
    float *times;
    float *values;              /* 3 per key (T, S) or 4 (R: x y z w) */
} br_model_channel;

typedef struct br_model_clip {
    char name[48];
    float duration;             /* seconds */
    br_model_channel *channels;
    int channel_count;
} br_model_clip;

typedef struct br_model {
    br_model_part *parts;
    int part_count;
    br_model_node *nodes;
    int node_count;
    int *order;                 /* the nodes, parents before children */
    br_model_skin *skins;
    int skin_count;
    br_model_clip *clips;
    int clip_count;
    br_image *images;           /* (the textures' storage) */
    int image_count;
    br_mesh merged;             /* every part in its rest pose, as one mesh (colliders, picking, bounds) */
} br_model;

/* A pose: each node's translation, rotation and scale. */
typedef struct br_model_pose { br_vec3 *t, *s; br_quat *r; int count; } br_model_pose;

bool br_model_load(br_model *m, const char *path, char *err, size_t err_size);
/* `dir`: where files it names (.bin, images) are ("" or NULL: none allowed). */
bool br_model_load_mem(br_model *m, const void *data, size_t size, const char *dir, char *err, size_t err_size);
void br_model_destroy(br_model *m);
bool br_model_is_file(const char *path);          /* .gltf or .glb */
int  br_model_find_clip(const br_model *m, const char *name);   /* -1: none */

bool br_model_pose_init(br_model_pose *p, const br_model *m);   /* at rest */
void br_model_pose_destroy(br_model_pose *p);
void br_model_pose_rest(br_model_pose *p, const br_model *m);
/* The clip at `time` seconds (wrapped round with loop, else held at the end). */
void br_model_pose_sample(br_model_pose *p, const br_model *m, int clip, float time, bool loop);
/* From `a` towards `b` by t (0..1), into a. */
void br_model_pose_blend(br_model_pose *a, const br_model_pose *b, float t);
/* Every node's matrix in the model's space, for a pose (`out`: node_count of them). */
void br_model_node_matrices(const br_model *m, const br_model_pose *p, br_mat4 *out);
/* A part as the pose has it: its vertices in the model's space, into `out`
 * (made the first time, reused after). `nodes`: from br_model_node_matrices. */
bool br_model_part_posed(const br_model *m, int part, const br_mat4 *nodes, br_mesh *out);

#endif /* BRONTO_MODEL_H */
