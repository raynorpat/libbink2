#ifndef LIBBINK2_FILTER_H
#define LIBBINK2_FILTER_H

/* Post-reconstruction stages that RAD's decoder runs and FFmpeg's does not:
 * pixel deblocking (DB), DC-only block reconstruction (DC) and the inter
 * residual edge filter (R). See docs/rad-filter-design.md. */

#define BINK2_STAGE_DEBLOCK  0
#define BINK2_STAGE_DCRECON  1
#define BINK2_STAGE_RESIDUAL 2

/* Returns 1 when libbink2 reproduces RAD's behaviour for this stage on this
 * revision. `version` is the fourcc's last character, as in
 * Bink2Video.version ('f' for KB2f). Any other stage returns 0. */
int bink2_filter_covers(int version, int stage);

/* Deblocking taps across an edge, p1 p0 | q0 q1. */
#define BINK2_DB_TAP_P1 1u
#define BINK2_DB_TAP_P0 2u
#define BINK2_DB_TAP_Q0 4u
#define BINK2_DB_TAP_Q1 8u
#define BINK2_DB_TAPS_ALL 15u

/* Tap mask for an edge where at least one side is an intra macroblock,
 * from the AC-count classes (0..3) of block P (left or top) and block Q
 * (right or bottom). Two class-3 blocks give 0. */
unsigned bink2_filter_db_class_mask(int class_p, int class_q);

/* 1 when two motion vectors differ by 2 or more in either component, which
 * makes an edge between two non-intra macroblocks full strength. */
int bink2_filter_mv_differ(int ax, int ay, int bx, int by);

/* Filters one 8-line deblocking edge segment in place. `q0` is the first
 * pixel on the Q side of line 0. `across` is the distance from one pixel to
 * the next across the edge (1 for a vertical edge, the plane stride for a
 * horizontal one) and `along` the distance between lines. The footprint is
 * two pixels on each side; `mask` picks the taps that are written. */
void bink2_filter_db_edge(unsigned char *q0, int across, int along,
                          unsigned mask);

#endif
