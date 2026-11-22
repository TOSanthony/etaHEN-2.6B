/* cave.h -- byte-offset layout of the "code cave" we build and write into
 * SceShellUI (see install.c). Matches wh_detour.h's detour code, which reads
 * these same structures at runtime. Pure macros: no .c file needed. */
#ifndef WH_CAVE_H
#define WH_CAVE_H

#include "wh_detour.h"   /* WH_DUMP_SHIFT, WH_DUMP_HDR, WH_C_*, WH_NEEDLE */

#define CV_DET     0x000
#define CV_PSIZE   0x100
#define CV_PDATA   0x120
#define CV_DTOR    0x140
#define CV_PVT     0x160
#define CV_PBUF    0x1a0
#define CV_PCTRL   0x1c0
#define CV_PSHARED 0x1e0
#define CV_URL     0x200
#define CV_BSIZE   0x220
#define CV_BDATA   0x240
#define CV_BVT     0x260
#define CV_BBUF    0x2a0
#define CV_BCTRL   0x2c0
#define CV_BSHARED 0x2e0
#define CV_CTR     0x300
#define CV_SIG     0x320   /* "WHBRIDGE", bridge len, bridge FNV-1a64, version */
#define CV_NEEDLE  0x340   /* hub signature string ("widget:customTemplates") */
#define CV_MATCH   0x360   /* choose_buffer helper (content-detects the hub) */
#define CV_PRELUDE 0x500   /* spliced loader + widget JS */
#define WH_SIG_VER 1

#define WH_DUMP_FLAG "/data/wh-widgets/.wh_dump"
#define WH_DUMP_CAP  8
#define WH_DUMP_SLOT ((size_t)1 << WH_DUMP_SHIFT)
#define WH_DCAVE_SZ  (WH_DUMP_HDR + (size_t)WH_DUMP_CAP * WH_DUMP_SLOT)

#endif /* WH_CAVE_H */
