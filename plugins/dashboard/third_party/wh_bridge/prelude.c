/* Build the prelude from /data/wh-widgets/.
 *
 * Generic loader head (loader.js, embedded via embed.S) + one spliced section
 * per widget folder:
 *   __wh.__begin({id,title,iconId,description,size}); <widget.js verbatim>; __wh.__end();
 * widget.js is compiled as part of the prelude (Hermes release blocks in-JS
 * eval), so a syntax error in one widget breaks the set -- validate offline.
 *
 * Hot reload: every widget section is still guarded by
 * `if(globalThis.__wh&&!globalThis.__wh.__loaded){...}` so repeated
 * evaluateJavaScript calls on an UNCHANGED prelude never re-register widgets
 * (that used to be a one-way latch: once __loaded was set, it stayed set for
 * the life of the ShellUI JS runtime, so an edited or newly added widget only
 * ever showed up after a full sign-out/sign-in). Now a hash of every widget's
 * id+config.json+widget.js is embedded as `__wh.__gen`; when it differs from
 * the generation already loaded in this JS runtime (because wh_bridge.elf was
 * re-run after a widget was added/edited/removed on disk), the widget
 * registry is cleared and __loaded is reset to 0, so the section below runs
 * again and picks up the change -- without requiring a fresh console
 * sign-out/sign-in, same as re-running the payload already did for the
 * detour/bridge themselves. */
#include "prelude.h"
#include "server.h"   /* WH_ASSET_URL */
#include "util.h"

/* embedded by embed.S, NUL-terminated so it can be treated as a C string */
extern const char wh_loader[];

int g_wok = 0, g_wbad = 0, g_wmore = 0;
char g_wfail[200] = "";

static void wh_app(char** buf, size_t* len, size_t* cap, const char* s, size_t n){
  if (*len + n + 1 > *cap) { while (*len + n + 1 > *cap) *cap *= 2; *buf = (char*)realloc(*buf, *cap); }
  memcpy(*buf + *len, s, n); *len += n; (*buf)[*len] = 0;
}
#define WHAPP(s) wh_app(&out,&len,&cap,(s),strlen(s))
static void wh_app_esc(char** buf, size_t* len, size_t* cap, const char* s){
  for (; *s; s++){ char c=*s; if (c=='\''||c=='\\'){ char e[2]={'\\',c}; wh_app(buf,len,cap,e,2); } else if (c=='\n'||c=='\r'){ } else wh_app(buf,len,cap,&c,1); }
}

/* install-time widget summary for the notification */
static void wh_fail_add(const char* name){
  g_wbad++;
  size_t l = strlen(g_wfail), n = strlen(name);
  if (l + n + 3 >= sizeof g_wfail) { g_wmore++; return; }
  if (l) strcat(g_wfail, ", ");
  strcat(g_wfail, name);
}

void build_prelude(char** outbuf, size_t* outlen, uint64_t* gen_out){
  size_t cap = 16384, len = 0;
  char* out = (char*)malloc(cap); out[0] = 0;
  /* Widget sections are built into their own buffer so their combined bytes
   * can be hashed into a generation id before being spliced into `out` --
   * see the hot-reload note above. */
  size_t wcap = 16384, wlen = 0;
  char* wbuf = (char*)malloc(wcap); wbuf[0] = 0;
#define WAPP(s) wh_app(&wbuf,&wlen,&wcap,(s),strlen(s))
  int count = 0;
  DIR* d = opendir(WH_WIDGETS_DIR);
  if (d){
    struct dirent* e;
    while ((e = readdir(d))){
      if (e->d_name[0] == '.') continue;
      char cpath[600], wpath[600];
      snprintf(cpath, sizeof cpath, "%s/%s/config.json", WH_WIDGETS_DIR, e->d_name);
      snprintf(wpath, sizeof wpath, "%s/%s/widget.js", WH_WIDGETS_DIR, e->d_name);
      char* cfg=0; size_t cl=0;
      if (wh_read_file(cpath,&cfg,&cl)) {
        /* Only a folder that looks like a widget (has widget.js) is worth a
         * message; plain files (wh.log) and unrelated folders are ignored. */
        struct stat ws;
        if (!stat(wpath, &ws)) {
          LOG("skip %s: config.json missing or empty\n", e->d_name); wh_fail_add(e->d_name);
        }
        continue;
      }
      char* js=0; size_t jl=0;
      if (wh_read_file(wpath,&js,&jl)) { LOG("skip %s: widget.js missing or empty\n", e->d_name); wh_fail_add(e->d_name); free(cfg); continue; }
      if (!strchr(cfg, '{')) { LOG("skip %s: config.json is not a JSON object\n", e->d_name); wh_fail_add(e->d_name); free(cfg); free(js); continue; }
      char id[128]={0}, title[160]={0}, iconId[64]={0}, desc[400]={0};
      if (wh_json_str(cfg,"id",id,sizeof id)) { strncpy(id, e->d_name, sizeof id-1); LOG("%s: no \"id\" in config.json, using folder name\n", e->d_name); }
      if (wh_json_str(cfg,"title",title,sizeof title)) strncpy(title, id, sizeof title-1);
      /* "glyph": white PNG in the widget folder, served by the asset server and
       * registered as IconPS id "wh:<id>" (the default iconId when one is given) */
      char glyph[80]={0};
      int dir_ok = wh_name_ok(e->d_name, strlen(e->d_name));
      if (!wh_json_str(cfg,"glyph",glyph,sizeof glyph)) {
        if (!dir_ok) { LOG("%s: folder name not servable ([A-Za-z0-9._-]), glyph ignored\n", id); glyph[0]=0; }
        else if (!wh_name_ok(glyph,strlen(glyph)) || !wh_img_type(glyph)) { LOG("%s: glyph \"%s\" ignored (must be a .png/.jpg file name)\n", id, glyph); glyph[0]=0; }
        else { struct stat gs; char gp[600]; snprintf(gp, sizeof gp, "%s/%s/%s", WH_WIDGETS_DIR, e->d_name, glyph);
               if (stat(gp, &gs)) LOG("%s: glyph file %s not found (stock fallback will show)\n", id, glyph); }
      } else glyph[0]=0;
      if (wh_json_str(cfg,"iconId",iconId,sizeof iconId)) {
        if (glyph[0]) snprintf(iconId, sizeof iconId, "wh:%s", id);
        else strncpy(iconId, "app_welcome", sizeof iconId-1);
      }
      wh_json_str(cfg,"description",desc,sizeof desc);   /* optional; stays "" */
      char iconFb[64]={0};                                 /* stock id if wh: glyphs unavailable */
      wh_json_str(cfg,"iconFallback",iconFb,sizeof iconFb);
      double sz = wh_json_num(cfg,"size",1);
      char num[48]; snprintf(num, sizeof num, "',size:%g},'", sz);
      WAPP("\nif(globalThis.__wh&&!globalThis.__wh.__loaded){globalThis.__wh.__begin({id:'");
      wh_app_esc(&wbuf,&wlen,&wcap, id);
      WAPP("',title:'");
      wh_app_esc(&wbuf,&wlen,&wcap, title);
      WAPP("',iconId:'");
      wh_app_esc(&wbuf,&wlen,&wcap, iconId);
      WAPP("',description:'");
      wh_app_esc(&wbuf,&wlen,&wcap, desc);
      WAPP("',iconFallback:'");
      wh_app_esc(&wbuf,&wlen,&wcap, iconFb);
      if (dir_ok) {                          /* base URL for this widget's images */
        WAPP("',assetBase:'" WH_ASSET_URL);
        wh_app_esc(&wbuf,&wlen,&wcap, e->d_name);
        WAPP("/");
      }
      if (glyph[0]) { WAPP("',glyph:'"); wh_app_esc(&wbuf,&wlen,&wcap, glyph); }
      WAPP(num);
      wh_app_esc(&wbuf,&wlen,&wcap, cfg);       /* raw config.json; parsed by the loader */
      WAPP("');\n");
      WAPP("try{\n");
      wh_app(&wbuf,&wlen,&wcap, js, jl);
      WAPP("\n}catch(e){globalThis.__wh.__fail(e);}\nglobalThis.__wh.__end();}\n");
      free(cfg); free(js); count++;
      LOG("widget: id=%s size=%g js=%zuB\n", id, sz, jl);
    }
    closedir(d);
  } else {
    LOG("build_prelude: opendir(%s) failed\n", WH_WIDGETS_DIR);
  }
  /* Generation id: a hash of every widget's exact id+config.json+widget.js
   * bytes, in directory order. Appending/editing/removing a widget and
   * re-running the payload changes this, which is what lets the check below
   * tell a genuine change apart from the hub simply re-evaluating the same
   * unchanged bundle again (readdir() order is stable within one run but
   * not guaranteed across runs; that only flips the hash needlessly on an
   * unrelated run, never missing a real change, so it's fine for this). */
  uint64_t gen = fnv1a64((const uint8_t*)wbuf, wlen);
  if (gen_out) *gen_out = gen;
  char genhex[20]; snprintf(genhex, sizeof genhex, "%016llx", (unsigned long long)gen);
  WHAPP(wh_loader);
  /* If this generation differs from the one already loaded in this JS
   * runtime, drop the latch and the widget registry so the sections below
   * run again and replace the stale entries -- see the hot-reload note
   * above. Identical generation (the hub re-evaluating the same unchanged
   * bundle) leaves everything untouched, same as before. */
  WHAPP("\nif(globalThis.__wh){var __whg='");
  WHAPP(genhex);
  WHAPP("';if(globalThis.__wh.__gen!==__whg){globalThis.__wh.__gen=__whg;globalThis.__wh.__loaded=0;"
        "globalThis.__wh_widgets={};globalThis.__wh_cfgs={};globalThis.__wh_order=[];}}\n");
  wh_app(&out,&len,&cap, wbuf, wlen);
  free(wbuf);
  /* mark loaded only after every section ran, so all widgets load in the
   * first eval where the generation is new, and never re-run on later evals
   * of that same generation */
  WHAPP("\nif(globalThis.__wh){globalThis.__wh.__loaded=1;globalThis.__wh.__flush();}\n");
  g_wok = count;
  LOG("build_prelude: widgets=%d failed=%d total=%zuB gen=%s\n", count, g_wbad, len, genhex);
  *outbuf = out; *outlen = len;
}
#undef WHAPP
#undef WAPP
