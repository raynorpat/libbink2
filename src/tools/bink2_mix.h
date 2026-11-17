#ifndef LIBBINK2_MIX_H
#define LIBBINK2_MIX_H

#include "bink2.h"

/**
 * Mix every enabled audio track into interleaved stereo s16. Returns the
 * number of stereo sample frames written, capped at capacity.
 */
unsigned long bink2_mix_frame(const bink2 object, short *out,
                              unsigned long capacity, unsigned char tracks);

#endif
