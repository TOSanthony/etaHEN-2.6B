/* One install into SceShellUI: attach, build the cave image, map it in the
 * target, write and verify it, then point the JSI vtable slot at the detour. */
#include "install.h"
#include "cave.h"
#include "hookpoint.h"
#include "prelude.h"
#include "remote.h"
#include "util.h"
#include "wh_detour.h"

/* embedded by embed.S: full bridge HBC, swapped in for the hub on any firmware
 * (the matcher detects the hub by content, not by exact bytes) */
extern const uint8_t bridge_hbc_start[];
extern const uint8_t bridge_hbc_end[];

int wh_install_once(pid_t pid, int want_dump, int* reload_out){
  return wh_install_once_g(pid, want_dump, reload_out, 0);
}
int wh_install_once_g(pid_t pid, int want_dump, int* reload_out, uint64_t* gen_out){
  *reload_out = 0;
  size_t blen = (size_t)(bridge_hbc_end - bridge_hbc_start);
  uint64_t bhash = fnv1a64(bridge_hbc_start, blen);
  LOG("start fw=%08x bridge=%zuB magic %02x%02x%02x%02x\n", kernel_get_fw_version(), blen,
      bridge_hbc_start[0],bridge_hbc_start[1],bridge_hbc_start[2],bridge_hbc_start[3]);
  if (blen < 0x100000) return 1;
  char* prelude = 0; size_t plen = 0;
  build_prelude(&prelude, &plen, gen_out);
  if (!prelude || !plen) { free(prelude); return 1; }
  size_t imgcap = CV_PRELUDE + plen + 1;
  size_t cavesz = (imgcap + 0xfff) & ~((size_t)0xfff);
  uint8_t* img = (uint8_t*)calloc(1, imgcap);
  uint8_t* back = (uint8_t*)malloc(imgcap);
  if (!img || !back) { free(img); free(back); free(prelude); return 1; }
  LOG("prelude %zuB cave %zuB\n", plen, cavesz);
  LOG("pid=%d\n", pid);
  if (sys_ptrace(PT_ATTACH, pid, 0, 0)) { free(img); free(back); free(prelude); return 2; }
  if (waitpid(pid, 0, 0) == -1) { sys_ptrace(PT_DETACH,pid,0,0); free(img); free(back); free(prelude); return 3; }
  LOG("attached\n");
  int rc = 0, reload = 0; uint32_t handle = 0; uintptr_t base = 0, scave = 0, hcave = 0, counter = 0, old_h = 0, fn2 = 0, vslot = 0, dcave = 0;
  if (want_dump) LOG("dump mode ON (%s present): large non-hub bundles will be staged\n", WH_DUMP_FLAG);
  if (kernel_dynlib_handle(pid, "libhermes-0.11.sprx", &handle)) rc = 10;
  else if (!(base = kernel_dynlib_mapbase_addr(pid, handle))) rc = 11;
  else {
    uint64_t slotv=0; uint8_t pro[11]={0};
    int drc = wh_discover(pid, base, &fn2, &vslot);
    if (drc) rc = (drc == 43 || drc == 44) ? 13 : drc;   /* 43/44 = can't find hook point (unsupported FW) */
    else if (remote_read(pid,vslot,&slotv,8)||remote_read(pid,fn2,pro,11)) rc=12;
    else if (memcmp(pro,EXPECTED_PROLOGUE,11)) rc=13;
    if (rc==0 && slotv != fn2) {
      /* Already hooked. Replace it only if the slot points at OUR cave: our detour
       * prologue (first 8 bytes, stable across detour versions) + the "WHBRIDGE"
       * signature at the fixed offset CV_SIG. This recognises our own earlier
       * builds even when the detour's internal layout changed. Anything else: refuse. */
      uint8_t oc[CV_PRELUDE];
      if (remote_read(pid,(uintptr_t)slotv,oc,sizeof oc)) rc=15;
      else if (memcmp(oc+CV_DET, WH_C_DET, 8) || memcmp(oc+CV_SIG, "WHBRIDGE", 8)) rc=15;
      else {
        reload = 1;
        if (get64(oc+CV_SIG+8) == blen && get64(oc+CV_SIG+16) == bhash)
          old_h = get64(oc+CV_BDATA+WH_C_DAIMM);   /* identical bridge already in ShellUI */
        LOG("reload: replacing our hook at %#lx (bridge %s)\n", (long)slotv, old_h ? "reused" : "re-uploaded");
        /* Detour counters from the previous session (see WH_C_CTR_A..D): [0]=slot calls
         * [8]=prelude evaluated [16]=bridge chosen (hub recognised) [24]=bundle returned.
         * swapped=0 with calls>0 means the hub bundle was evaluated WITHOUT the swap. */
        uint64_t pv[4] = {0,0,0,0};
        if (!remote_read(pid,(uintptr_t)slotv+CV_CTR,pv,sizeof pv))
          LOG("prev: calls=%llu prelude=%llu swapped=%llu hub_eval=%llu%s\n",
              (unsigned long long)pv[0],(unsigned long long)pv[1],(unsigned long long)pv[2],(unsigned long long)pv[3],
              (pv[0] && !pv[2]) ? " (hook fired but the hub was NOT swapped: matcher missed)" : "");
        else LOG("prev: counters unreadable\n");
      }
    }
    if (rc==0) {
      uintptr_t g = gadget_of(pid);
      size_t hpg = (blen + 0xfff) & ~((size_t)0xfff);
      if (!g) rc=20;
      else if (!(scave = remote_mmap(pid,g,cavesz,1))) rc=21;
      else if (old_h) hcave = old_h;
      else if (!(hcave = remote_mmap(pid,g,hpg,0))) rc=22;
      else if (remote_write_chunked(pid,hcave,bridge_hbc_start,blen)) rc=23;
      if (rc==0 && want_dump) {
        /* 64 MB staging in ShellUI + a header whctl scans for ("WHDUMPH") */
        dcave = remote_mmap(pid, g, WH_DCAVE_SZ, 0);
        if (!dcave) LOG("dump: staging mmap failed (continuing without dump)\n");
        else {
          uint8_t hdr[0x40]; memset(hdr,0,sizeof hdr);
          memcpy(hdr,"WHDUMPH",8); put64(hdr+8,(uint64_t)WH_DUMP_CAP); put64(hdr+16,0);
          if (remote_write_d(pid,dcave,hdr,sizeof hdr)) { LOG("dump: header write failed\n"); dcave=0; }
          else LOG("dump: staging %zuMB @ %#lx (cap %d)\n", WH_DCAVE_SZ>>20, (long)dcave, WH_DUMP_CAP);
        }
      }
      if (rc==0) {
        uintptr_t S = scave; counter = S + CV_CTR;
        memcpy(img+CV_DET, WH_C_DET, sizeof WH_C_DET);
        put64(img+CV_DET+WH_C_SHARED, S+CV_PSHARED);
        put64(img+CV_DET+WH_C_URL,    S+CV_URL);
        put64(img+CV_DET+WH_C_ORIG_A, fn2);
        put64(img+CV_DET+WH_C_ORIG_B, fn2);
        put64(img+CV_DET+WH_C_BRIDGE, S+CV_BSHARED);
        put64(img+CV_DET+WH_C_MATCHFN, S+CV_MATCH);
        /* choose_buffer helper + the hub signature it scans for (content detection) */
        memcpy(img+CV_MATCH, WH_C_MATCH, sizeof WH_C_MATCH);
        put64(img+CV_MATCH+WH_M_NEEDLE, S+CV_NEEDLE);
        put64(img+CV_MATCH+WH_M_DUMPHDR, dcave);   /* 0 => dump disabled (block skipped) */
        memcpy(img+CV_NEEDLE, WH_NEEDLE, WH_NEEDLE_LEN);
        put64(img+CV_DET+WH_C_CTR_A,  S+CV_CTR);
        put64(img+CV_DET+WH_C_CTR_B,  S+CV_CTR);
        put64(img+CV_DET+WH_C_CTR_C,  S+CV_CTR);
        put64(img+CV_DET+WH_C_CTR_D,  S+CV_CTR);
        memcpy(img+CV_PSIZE, WH_C_SIZE, sizeof WH_C_SIZE); put64(img+CV_PSIZE+WH_C_SZIMM, (uint64_t)plen);
        memcpy(img+CV_PDATA, WH_C_DATA, sizeof WH_C_DATA); put64(img+CV_PDATA+WH_C_DAIMM, S+CV_PRELUDE);
        memcpy(img+CV_DTOR,  WH_C_DTOR, sizeof WH_C_DTOR);
        put64(img+CV_PVT+0, S+CV_DTOR); put64(img+CV_PVT+8, S+CV_DTOR); put64(img+CV_PVT+16, S+CV_PSIZE); put64(img+CV_PVT+24, S+CV_PDATA);
        put64(img+CV_PBUF, S+CV_PVT);
        put64(img+CV_PCTRL+0,0); put64(img+CV_PCTRL+8,0x40000000); put64(img+CV_PCTRL+16,0x40000000);
        put64(img+CV_PSHARED+0, S+CV_PBUF); put64(img+CV_PSHARED+8, S+CV_PCTRL);
        img[CV_URL]=(uint8_t)(2u<<1); img[CV_URL+1]='w'; img[CV_URL+2]='h';
        memcpy(img+CV_BSIZE, WH_C_SIZE, sizeof WH_C_SIZE); put64(img+CV_BSIZE+WH_C_SZIMM, (uint64_t)blen);
        memcpy(img+CV_BDATA, WH_C_DATA, sizeof WH_C_DATA); put64(img+CV_BDATA+WH_C_DAIMM, hcave);
        put64(img+CV_BVT+0, S+CV_DTOR); put64(img+CV_BVT+8, S+CV_DTOR); put64(img+CV_BVT+16, S+CV_BSIZE); put64(img+CV_BVT+24, S+CV_BDATA);
        put64(img+CV_BBUF, S+CV_BVT);
        put64(img+CV_BCTRL+0,0); put64(img+CV_BCTRL+8,0x40000000); put64(img+CV_BCTRL+16,0x40000000);
        put64(img+CV_BSHARED+0, S+CV_BBUF); put64(img+CV_BSHARED+8, S+CV_BCTRL);
        memcpy(img+CV_SIG, "WHBRIDGE", 8); put64(img+CV_SIG+8, (uint64_t)blen);
        put64(img+CV_SIG+16, bhash); put64(img+CV_SIG+24, WH_SIG_VER);
        memcpy(img+CV_PRELUDE, prelude, plen+1);
        size_t imglen = CV_PRELUDE + plen + 1;
        if (remote_write_d(pid, scave, img, imglen)) rc=25;
        else {
          if (remote_read(pid,scave,back,imglen)||memcmp(back,img,imglen)) rc=26;
          else {
            int prot = kernel_get_vmem_protection(pid,vslot,8);
            kernel_set_vmem_protection(pid,vslot,8,prot|PROT_READ|PROT_WRITE);
            uint64_t ns = S+CV_DET;
            if (remote_write_d(pid,vslot,&ns,8)) rc=27;
            else { uint64_t c=0; remote_read(pid,vslot,&c,8); if(c!=ns) rc=28; else LOG("INSTALLED slot2->%#lx bridge %zuB @ %#lx%s\n",(long)ns,blen,(long)hcave,reload ? (old_h ? " (reload, bridge reused)" : " (reload)") : ""); }
            if (prot>=0) kernel_set_vmem_protection(pid,vslot,8,prot);
          }
        }
      }
    }
  }
  LOG("ADDRS base=%#lx scave=%#lx hcave=%#lx counter=%#lx vslot2=%#lx\n",
      (long)base,(long)scave,(long)hcave,(long)counter,(long)vslot);
  if (sys_ptrace(PT_DETACH,pid,0,0)) LOG("detach failed\n"); else LOG("detached\n");
  LOG("done rc=%d\n", rc);
  if (rc) {
    const char* why = rc==10 ? "libhermes not loaded in ShellUI" : rc==11 ? "libhermes base not found"
      : rc==12 ? "could not read ShellUI memory" : rc==13 ? "could not locate evaluateJavaScript (unsupported firmware?)"
      : rc==15 ? "evaluateJavaScript is hooked by something else; left untouched"
      : rc>=40 && rc<=42 ? "could not scan libhermes to find the hook point"
      : rc>=20 && rc<=24 ? "could not allocate/write memory in ShellUI" : rc>=25 && rc<=26 ? "code cave write/verify failed"
      : rc>=27 ? "could not update the hook pointer" : "unknown";
    LOG("install failed (rc=%d): %s\n", rc, why);
  }
  free(img); free(back); free(prelude);
  *reload_out = reload;
  return rc;
}
