#ifndef LIBBINK2_FILTER_H
#define LIBBINK2_FILTER_H

/* Post-reconstruction stages that RAD's decoder runs and FFmpeg's does not:
 * pixel deblocking (DB), DC-only block reconstruction (DC) and the inter
 * residual edge filter (R). See docs/rad-filter-design.md. */

#include <stdint.h>

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

struct Bink2Video;

/* Deblocks the frame in cur in place: every enabled vertical edge of Y, U
 * and V, then every enabled horizontal edge, from the block info words,
 * motion vectors, frame flags and edge arrays of the frame just decoded.
 * Rows from the 16-aligned height on and the alpha plane are not touched. */
void bink2_filter_deblock(struct Bink2Video *v);

/* ---- Stage R: the residual edge filter of motion+residual macroblocks ---- */

#define BINK2_RES_VERTICAL   1u
#define BINK2_RES_HORIZONTAL 2u

/* The edge directions stage R filters in the frame v is decoding: none
 * unless filters are on, the revision is covered, frame flag 0x2000 is set
 * and 0x40000 is clear; 0x4000 removes the vertical edges and 0x8000 the
 * horizontal ones. */
unsigned bink2_filter_residual_dirs(const struct Bink2Video *v);

/* The word stage R uses for an inter block: the class from its number of
 * decoded AC symbols, shifted by BINK2_INFO_CLASS_SHIFT, over its DC value
 * (the v for which coefficient 0 is v * 8 + 32) in 13-bit two's
 * complement. */
unsigned bink2_filter_residual_word(int dc, int symbols);

/* Filters one 8-line edge of an int16 residual in place. `r2` is the first
 * sample on the B (right or bottom) side of line 0; `across` and `along`
 * are as for bink2_filter_db_edge. wa and wb are the words of blocks A and
 * B. Arithmetic is 16-bit two's complement with no clamping. */
void bink2_filter_residual_edge(int16_t *r2, int across, int along,
                                unsigned wa, unsigned wb);

/* Filters the internal 8x8 edges of an n x n block residual (n = 4 for
 * luma, 2 for chroma) in the directions `dirs`: every vertical edge, then
 * every horizontal one. words[by * n + bx] belongs to block (bx, by). The
 * macroblock's outer edges are never touched. */
void bink2_filter_residual(int16_t *res, int stride, int n,
                           const unsigned *words, unsigned dirs);

#endif
