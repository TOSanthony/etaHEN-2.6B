/* Embeds the Welcome Hub bridge bytecode (bridge.hbc) and the JS loader head
 * (loader.js, NUL-terminated) -- same blobs as TOSanthony/dashboard embed.S. */
#ifndef WH_EMBED_DIR
#error "WH_EMBED_DIR must be defined by CMake"
#endif

__asm__(".section .rodata\n"
        ".balign 16\n"
        ".global bridge_hbc_start\n"
        ".global bridge_hbc_end\n"
        "bridge_hbc_start:\n"
        ".incbin \"" WH_EMBED_DIR "/bridge.hbc\"\n"
        "bridge_hbc_end:\n"
        ".balign 16\n"
        ".global wh_loader\n"
        "wh_loader:\n"
        ".incbin \"" WH_EMBED_DIR "/loader.js\"\n"
        ".byte 0\n"
        ".previous\n");
