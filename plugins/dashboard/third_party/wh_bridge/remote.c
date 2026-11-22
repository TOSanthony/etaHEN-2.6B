#include "remote.h"
#include "util.h"

int sys_ptrace(int req, pid_t pid, caddr_t addr, int data) {
  uint8_t pc[16]; memset(pc, 0xff, sizeof pc);
  pid_t me = getpid(); uint8_t caps[16]; uint64_t authid; int ret;
  if (!(authid = kernel_get_ucred_authid(me))) return -1;
  if (kernel_get_ucred_caps(me, caps)) return -1;
  if (kernel_set_ucred_authid(me, 0x4800000000010003l)) return -1;
  if (kernel_set_ucred_caps(me, pc)) return -1;
  ret = (int)__syscall(SYS_ptrace, req, pid, addr, data);
  kernel_set_ucred_authid(me, authid); kernel_set_ucred_caps(me, caps);
  return ret;
}
static int ptg(pid_t pid, struct reg *r){ return sys_ptrace(PT_GETREGS, pid,(caddr_t)r,0); }
static int pts(pid_t pid, struct reg *r){ return sys_ptrace(PT_SETREGS, pid,(caddr_t)r,0); }
int remote_read(pid_t pid, uintptr_t a, void *o, size_t l) { return mdbg_copyout(pid, (intptr_t)a, o, l); }

int remote_write_d(pid_t pid, uintptr_t a, const void *s, size_t l) {
  struct ptrace_io_desc iod = {0}; iod.piod_op = PIOD_WRITE_D; iod.piod_offs=(void*)a; iod.piod_addr=(void*)s; iod.piod_len=l;
  return sys_ptrace(PT_IO, pid, (caddr_t)&iod, 0);
}
int remote_write_chunked(pid_t pid, uintptr_t a, const uint8_t *s, size_t l) {
  const size_t CH=0x10000;
  for (size_t o=0;o<l;o+=CH){ size_t n=l-o; if(n>CH)n=CH; if(remote_write_d(pid,a+o,s+o,n)){LOG("chunk @%#zx fail\n",o);return -1;} }
  return 0;
}

#define SYS_mmap 477
uintptr_t gadget_of(pid_t pid){ intptr_t a=kernel_dynlib_resolve(pid,0x1,"HoLVWNanBBc"); if(!a)a=kernel_dynlib_resolve(pid,0x2001,"HoLVWNanBBc"); return a?(uintptr_t)a+0xa:0; }
static int remote_syscall(pid_t pid, uintptr_t g, uint64_t sn, uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5, uint64_t *ret){
  struct reg bak,jmp; if(ptg(pid,&bak))return -1; jmp=bak;
  jmp.r_rax=(int64_t)sn; jmp.r_rdi=a0;jmp.r_rsi=a1;jmp.r_rdx=a2;jmp.r_r10=a3;jmp.r_r8=a4;jmp.r_r9=a5; jmp.r_rip=(int64_t)g;
  if(pts(pid,&jmp))return -1; int rc=-1;
  for(int i=0;i<1000;i++){ if(sys_ptrace(PT_STEP,pid,(caddr_t)1,0))break; if(waitpid(pid,0,0)==-1)break; if(ptg(pid,&jmp))break; if((uint64_t)jmp.r_rsp>(uint64_t)bak.r_rsp){*ret=(uint64_t)jmp.r_rax;rc=0;break;} }
  if(pts(pid,&bak))rc=-1; return rc;
}
uintptr_t remote_mmap(pid_t pid, uintptr_t g, size_t len, int exec){
  uint64_t ret=0; if(remote_syscall(pid,g,SYS_mmap,0,len,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,(uint64_t)-1,0,&ret))return 0;
  if(ret==(uint64_t)MAP_FAILED||!ret)return 0;
  if(exec&&kernel_mprotect(pid,(intptr_t)ret,len,PROT_READ|PROT_WRITE|PROT_EXEC))return 0;
  return (uintptr_t)ret;
}
