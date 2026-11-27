/* Shared EN/FR text selection for plugin UIs.
 * French when the console system language is French (PS5 ids 2 / 22),
 * English otherwise. Use TR("français", "English"). */
#ifndef PLUGIN_LANG_H
#define PLUGIN_LANG_H

extern int sceSystemServiceParamGetInt(int param_id, int *value);

static inline int plugin_lang_is_fr(void) {
    static int cached = -1;
    if (cached < 0) {
        int lang = 1;
        if (sceSystemServiceParamGetInt(1, &lang) < 0) return 0; /* retry next call */
        cached = (lang == 2 || lang == 22);
    }
    return cached;
}

#define TR(fr, en) (plugin_lang_is_fr() ? (fr) : (en))

#endif
