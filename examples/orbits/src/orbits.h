/* Orbits - the game's own component (src/orbits.c). */
#ifndef ORBITS_H
#define ORBITS_H

#include "bronto/bronto.h"

#define ORBIT_BOOST 3.0f              /* how much faster everything goes while Space is held */

typedef struct Orbit {
    br_entity center;                 /* what it goes round (none: the scene's origin) */
    float radius;                     /* pixels */
    float speed;                      /* degrees a second (negative: the other way) */
    float angle;                      /* where it is now, degrees */
} Orbit;

#endif
