/* hookpoint.h -- runtime discovery of the hook point: evaluateJavaScript and
 * its JSI vtable slot in libhermes, found by content so no firmware-specific
 * offsets are needed. Validated on FW 13.20 (matches 0xe890 / 0x26c358). */
#ifndef WH_HOOKPOINT_H
#define WH_HOOKPOINT_H

#include "common.h"

/* FW 13.20 reference offsets: used only to label a match in the log; the real
 * addresses are discovered at runtime by signature (see wh_discover). */
#define OFF_EVALUATE_JS  0x0000e890u
#define OFF_VTABLE_SLOT2 0x0026c358u
#define WH_MOD_CAP       0x00800000u   /* hard cap when walking the module image */

/* The first 11 bytes of evaluateJavaScript's prologue -- checked before
 * hooking, as a sanity check on top of the signature match. */
extern const uint8_t EXPECTED_PROLOGUE[11];

/* Finds evaluateJavaScript and its vtable slot in the target process's
 * libhermes. Returns 0 (WH_OK-equivalent) on success and fills *eval_out /
 * *slot_out, or a small positive rc (see install.c's rc table) on failure. */
int wh_discover(pid_t pid, uintptr_t base, uintptr_t* eval_out, uintptr_t* slot_out);

#endif /* WH_HOOKPOINT_H */
