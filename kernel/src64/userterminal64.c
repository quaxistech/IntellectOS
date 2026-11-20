/* U1: one static, strictly isolated ring3 program, with bounded int80 API.
 * This is a bootstrap runtime, not a complete process manager or IVX loader.
 * No arbitrary files are executed. Interrupts remain disabled.
 */
#include <stddef.h>
#include <stdint.h>
#include "bootinfo.h"
#include "pmm64.h"
#include "userterminal64.h"
#include "terminalfb64.h"

#define P  UINT64_C(0x001)
#define W  UINT64_C(0x002)
#define U  UINT64_C(0x004)
#define NX UINT64_C(0x8000000000000000)
#define ADDR UINT64_C(0x000FFFFFFFFFF000)
#define USER_PML4_INDEX 1u
#define SYS_MAX_WRITE 256u
#define EFAULT 14
#define EINVAL 22
#define ENOSYS 38

extern const uint8_t intellect_user_terminal_begin[], intellect_user_terminal_end[];
extern void intellect_u1_enter(uint64_t user_rip, uint64_t user_stack);
extern __attribute__((noreturn)) void intellect_u1_resume_kernel(void);
extern int intellect_input64_read_char(void);

/* Order matches GPR pushes in usergate64.S (top of saved trap frame). */
typedef struct {
    uint64_t rax,rbx,rcx,rdx,rsi,rdi,rbp,r8,r9,r10,r11,r12,r13,r14,r15;
    uint64_t rip,cs,rflags,rsp,ss;
} u1_frame_t;

static uint64_t owned[5];  /* PDPT, PD, PT, executable code, stack */
static uint64_t bootstrap_cr3_u1;
static uint8_t syscall_stack[16384] __attribute__((aligned(4096)));
static int user_active;

static void zero4096(uint64_t p) {
    uint64_t *d=(uint64_t *)(uintptr_t)p;
    for(unsigned i=0;i<512u;++i)d[i]=0;
}
static void print_text(const char *s) {
    size_t n=0;while(s[n])++n;
    intellect_terminalfb64_write(s,n);
}
static void print_num(uint64_t n) {
    char b[24];unsigned i=0;
    do {b[i++]=(char)('0'+n%10u);n/=10u;}while(n);
    while(i)intellect_terminalfb64_write(&b[--i],1u);
}
static void release_owned(void) {
    uint64_t *pml4=(uint64_t *)(uintptr_t)bootstrap_cr3_u1;
    if (bootstrap_cr3_u1 && (pml4[USER_PML4_INDEX]&P)) {
        pml4[USER_PML4_INDEX]=0;
        /* Drop TLB entries before the backing pages become allocatable. */
        __asm__ volatile("mov %0,%%cr3"::"r"(bootstrap_cr3_u1):"memory");
    }
    for(unsigned i=0;i<5u;++i) if(owned[i]) {
        zero4096(owned[i]);
        (void)intellect_pmm_free_page(owned[i]);
        owned[i]=0;
    }
    bootstrap_cr3_u1=0;
}

/* Only int 0x80 from CPL3 is permitted. The saved frame never exposes IO
 * privilege. A negative return is encoded as signed two's complement. */
int intellect_u1_syscall_dispatch(u1_frame_t *f) {
    if(!f || !user_active || (f->cs&3u)!=3u || (f->ss&3u)!=3u) {
        print_text("U1: invalid syscall privilege frame\n");
        return 1;
    }
    const uint64_t no=f->rax;
    f->rax=(uint64_t)(int64_t)-ENOSYS;
    if(no==INTELLECT_U1_SYS_WRITE) {
        if(f->rdi!=1u && f->rdi!=2u) {f->rax=(uint64_t)(int64_t)-EINVAL;return 0;}
        if(f->rdx>SYS_MAX_WRITE) {f->rax=(uint64_t)(int64_t)-EINVAL;return 0;}
        if(!intellect_u1_user_range(f->rsi,f->rdx)) {f->rax=(uint64_t)(int64_t)-EFAULT;return 0;}
        const char *s=(const char *)(uintptr_t)f->rsi;
        intellect_terminalfb64_write(s,(size_t)f->rdx);
        f->rax=f->rdx;
        return 0;
    }
    if(no==INTELLECT_U1_SYS_READ) {
        if(f->rdi!=0u) {f->rax=(uint64_t)(int64_t)-EINVAL;return 0;}
        f->rax=(uint64_t)(unsigned)intellect_input64_read_char();
        return 0;
    }
    if(no==INTELLECT_U1_SYS_CLEAR) {
        intellect_terminalfb64_clear();
        f->rax=0;return 0;
    }
    if(no==INTELLECT_U1_SYS_INFO) {
        const intellect_pmm_stats_t s=intellect_pmm_stats();
        print_text("U1 MEMORY: MANAGED=");print_num(s.managed_pages);
        print_text(" FREE=");print_num(s.free_pages);
        print_text(" ALLOCATED=");print_num(s.allocated_pages);
        print_text(" PAGE_BYTES=4096\n");
        f->rax=0;return 0;
    }
    if(no==INTELLECT_U1_SYS_EXIT) {
        print_text("U1: user terminal exited; returning to kernel\n");
        user_active=0;
        return 1;
    }
    return 0;
}
__attribute__((noreturn)) void intellect_u1_abort_fault(uint64_t vector,
        uint64_t error,uint64_t rip,uint64_t cr2) {
    (void)error;(void)rip;(void)cr2;
    user_active=0;
    print_text("U1: user process fault (vector ");print_num(vector);
    print_text("); kernel intact\n");
    intellect_u1_resume_kernel();
}
int intellect_u1_terminal_run(const intellect_bootinfo_t *boot,uint64_t cr3) {
    for(unsigned i=0;i<5u;++i)owned[i]=0;
    bootstrap_cr3_u1=0;user_active=0;
    if(!boot || !cr3 || (cr3&4095u) || cr3>=INTELLECT_PMM_LIMIT) return 0;
    if(!intellect_terminalfb64_init(boot))return 0;
    uintptr_t begin=0,end=0;
    __asm__ volatile("leaq intellect_user_terminal_begin(%%rip),%0" : "=r"(begin));
    __asm__ volatile("leaq intellect_user_terminal_end(%%rip),%0" : "=r"(end));
    if(end<=begin || end-begin>4096u) return 0;
    bootstrap_cr3_u1=cr3;
    uint64_t *pml4=(uint64_t *)(uintptr_t)cr3;
    if(pml4[USER_PML4_INDEX]!=0) return 0;
    for(unsigned i=0;i<5u;++i) {
        owned[i]=intellect_pmm_alloc_page();
        if(!owned[i]) {release_owned();return 0;}
        zero4096(owned[i]);
    }
    /* Immutable executable bytes are copied into a fresh private frame.
     * Identity alias is kernel-only; user code page is RX, stack RW+NX.
     */
    const uint8_t *src=(const uint8_t *)begin;
    uint8_t *code=(uint8_t *)(uintptr_t)owned[3];
    for(size_t i=0;i<end-begin;++i)code[i]=src[i];
    uint64_t *pdpt=(uint64_t *)(uintptr_t)owned[0];
    uint64_t *pd=(uint64_t *)(uintptr_t)owned[1];
    uint64_t *pt=(uint64_t *)(uintptr_t)owned[2];
    pdpt[0]=owned[1]|P|W|U;
    pd[0]=owned[2]|P|W|U;
    pt[0]=owned[3]|P|U;
    pt[1]=owned[4]|P|W|U|NX;
    /* Commit one PML4 root after all subordinate tables are ready. */
    pml4[USER_PML4_INDEX]=owned[0]|P|W|U;
    __asm__ volatile("mov %0,%%cr3"::"r"(cr3):"memory");
    const uintptr_t syscall_top=(uintptr_t)syscall_stack+sizeof(syscall_stack);
    intellect64_user_tss_set_rsp0((uint64_t)syscall_top);
    intellect64_user_trap_install();
    print_text("U1: Ring3 user address space installed (RX code / RW+NX stack)\n");
    user_active=1;
    intellect_u1_enter(INTELLECT_USER64_BASE,INTELLECT_USER64_BASE+8192u-16u);
    /* The only return path is explicit exit or a recoverable user fault. */
    user_active=0;
    release_owned();
    return 1;
}
