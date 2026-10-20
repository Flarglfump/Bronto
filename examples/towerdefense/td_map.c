/*
 * POP DEFENSE - WHERE things are: the path and the buildable ground.
 *
 * The path is a smooth curve through a handful of control points
 * (Catmull-Rom spline), sampled once into a dense polyline. Balloons store
 * only how far along it they are; td_map_point turns that into a position.
 */
#include "td.h"
#include <string.h>

#define SAMPLES_PER_SEGMENT 20

/* The meadow: three lanes that snake down the field. Off-screen at both
 * ends so balloons glide in and out. */
static const br_vec2 k_control[] = {
    { -30, 62 }, { 60, 62 }, { 150, 46 }, { 250, 66 }, { 345, 48 }, { 412, 70 },
    { 432, 122 }, { 398, 162 }, { 300, 152 }, { 200, 170 }, { 112, 150 }, { 58, 178 },
    { 50, 236 }, { 96, 270 }, { 196, 262 }, { 288, 282 }, { 362, 262 }, { 420, 276 },
    { 446, 322 }, { 440, 400 },
};

static const td_obstacle k_obstacles[] = {
    { TD_OBSTACLE_POND, { 236, 326 }, 26 },
    { TD_OBSTACLE_POND, { 262, 316 }, 18 },
    { TD_OBSTACLE_TREE, { 22, 112 }, 12 },  { TD_OBSTACLE_TREE, { 30, 330 }, 14 },
    { TD_OBSTACLE_TREE, { 462, 20 }, 13 },  { TD_OBSTACLE_TREE, { 222, 112 }, 11 },
    { TD_OBSTACLE_TREE, { 120, 330 }, 10 }, { TD_OBSTACLE_TREE, { 380, 340 }, 12 },
    { TD_OBSTACLE_TREE, { 336, 208 }, 10 }, { TD_OBSTACLE_TREE, { 14, 20 }, 10 },
    { TD_OBSTACLE_ROCK, { 160, 214 }, 7 },  { TD_OBSTACLE_ROCK, { 300, 104 }, 6 },
    { TD_OBSTACLE_ROCK, { 466, 190 }, 6 },
};

static br_vec2 catmull_rom(br_vec2 p0, br_vec2 p1, br_vec2 p2, br_vec2 p3, float t) {
    float t2 = t * t, t3 = t2 * t;
    br_vec2 r;
    r.x = 0.5f * (2.0f * p1.x + (p2.x - p0.x) * t + (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 +
                  (3.0f * p1.x - p0.x - 3.0f * p2.x + p3.x) * t3);
    r.y = 0.5f * (2.0f * p1.y + (p2.y - p0.y) * t + (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 +
                  (3.0f * p1.y - p0.y - 3.0f * p2.y + p3.y) * t3);
    return r;
}

void td_map_build(td_map *m) {
    int n = (int)BR_ARRAY_COUNT(k_control), i, s;
    memset(m, 0, sizeof(*m));
    m->name = "Meadow Lanes";
    m->path_radius = 12.0f;
    for (i = 0; i < n - 1; ++i) {
        br_vec2 p0 = k_control[i > 0 ? i - 1 : 0], p1 = k_control[i];
        br_vec2 p2 = k_control[i + 1], p3 = k_control[i + 2 < n ? i + 2 : n - 1];
        for (s = 0; s < SAMPLES_PER_SEGMENT; ++s)
            m->points[m->point_count++] = catmull_rom(p0, p1, p2, p3, (float)s / SAMPLES_PER_SEGMENT);
    }
    m->points[m->point_count++] = k_control[n - 1];
    for (i = 1; i < m->point_count; ++i)
        m->along[i] = m->along[i - 1] + br_v2_dist(m->points[i - 1], m->points[i]);
    m->length = m->along[m->point_count - 1];
    m->obstacle_count = (int)BR_ARRAY_COUNT(k_obstacles);
    memcpy(m->obstacles, k_obstacles, sizeof(k_obstacles));
}

/* Index of the polyline segment containing path distance `dist`. */
static int segment_at(const td_map *m, float dist) {
    int lo = 0, hi = m->point_count - 1;
    while (hi - lo > 1) {
        int mid = (lo + hi) / 2;
        if (m->along[mid] <= dist) lo = mid; else hi = mid;
    }
    return lo;
}

br_vec2 td_map_point(const td_map *m, float dist) {
    int i;
    float span;
    if (dist <= 0) return m->points[0];
    if (dist >= m->length) return m->points[m->point_count - 1];
    i = segment_at(m, dist);
    span = m->along[i + 1] - m->along[i];
    return br_v2_lerp(m->points[i], m->points[i + 1], span > 0 ? (dist - m->along[i]) / span : 0.0f);
}

br_vec2 td_map_direction(const td_map *m, float dist) {
    int i = segment_at(m, br_clampf(dist, 0, m->length));
    return br_v2_norm(br_v2_sub(m->points[i + 1], m->points[i]));
}

static float point_segment_distance(br_vec2 p, br_vec2 a, br_vec2 b) {
    br_vec2 ab = br_v2_sub(b, a), ap = br_v2_sub(p, a);
    float len2 = ab.x * ab.x + ab.y * ab.y;
    float t = len2 > 0 ? br_clampf((ap.x * ab.x + ap.y * ab.y) / len2, 0, 1) : 0.0f;
    return br_v2_dist(p, br_v2_add(a, br_v2_scale(ab, t)));
}

float td_map_distance_to_path(const td_map *m, br_vec2 p) {
    float best = 1e30f;
    int i;
    for (i = 0; i + 1 < m->point_count; ++i) {
        float d = point_segment_distance(p, m->points[i], m->points[i + 1]);
        if (d < best) best = d;
    }
    return best;
}

/* A tower of `radius` fits at `p`: on the map, off the path, clear of scenery. */
bool td_map_buildable(const td_map *m, br_vec2 p, float radius) {
    int i;
    if (p.x < radius || p.y < radius || p.x > TD_MAP_W - radius || p.y > TD_MAP_H - radius) return false;
    if (td_map_distance_to_path(m, p) < m->path_radius + radius) return false;
    for (i = 0; i < m->obstacle_count; ++i)
        if (br_v2_dist(p, m->obstacles[i].pos) < m->obstacles[i].radius + radius) return false;
    return true;
}
