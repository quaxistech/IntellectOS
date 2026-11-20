/* IntellectOS C1 kernel diagnostic monitor; NOT a user-mode shell.
 * Commands never parse or execute arbitrary code, touch disks or allocate RAM.
 * The pure parser is testable in a normal host process.
 */
#include <stddef.h>
#include <stdint.h>
#include "kmonitor64.h"
#include "pmm64.h"
#include "vmm64.h"
#include "serial64.h"

static void ch(intellect_monitor64_t *m, char c) {
    m->output(m->ctx, c);
}
static void txt(intellect_monitor64_t *m, const char *s) {
    while (*s) ch(m, *s++);
}
static int eq(const char *a,const char *b) {
    while (*a && *b && *a==*b) {++a;++b;}
    return *a==*b;
}
static void number(intellect_monitor64_t *m, uint64_t n) {
    char b[21]; unsigned count=0;
    do { b[count++]=(char)('0'+(n%10u)); n/=10u; } while(n);
    while(count) ch(m,b[--count]);
}
static void hex64(intellect_monitor64_t *m, uint64_t n) {
    static const char hexdigits[]="0123456789ABCDEF";
    txt(m,"0x");
    for(int shift=60;shift>=0;shift-=4)
        ch(m,hexdigits[(n>>(unsigned)shift)&15u]);
}
static void prompt(intellect_monitor64_t *m) { txt(m,"intellect# "); }

static void cpu(intellect_monitor64_t *m) {
    uint32_t a=0,b=0,c=0,d=0;
    __asm__ volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(0u),"c"(0u));
    txt(m,"CPUID MAX_LEAF=");number(m,a);txt(m," VENDOR=");
    const uint32_t words[3]={b,d,c};
    for(unsigned i=0;i<3u;++i)
        for(unsigned j=0;j<4u;++j)
            ch(m,(char)(words[i]>>(8u*j)));
    txt(m,"\n");
}

static void command(intellect_monitor64_t *m) {
    char *s=m->line;
    while (*s==' ' || *s=='\t') ++s;
    size_t len=0;
    while(s[len]) ++len;
    while(len && (s[len-1]==' '||s[len-1]=='\t')) s[--len]=0;
    if (len==0u) return;
    if(eq(s,"help")) {
        txt(m,"COMMANDS: help about boot mem vmm cpu clear echo <text> halt\n");
        txt(m,"NOTE: kernel diagnostic monitor; no files, users or processes yet.\n");
    } else if(eq(s,"about")) {
        txt(m,"IntellectOS C1 x86-64 native kernel, BootInfo ABI v1\n");
        txt(m,"COM1 38400 8N1, QEMU interactive monitor, no UEFI services\n");
    } else if(eq(s,"boot")) {
        const intellect_bootinfo_t *boot=m->boot;
        txt(m,"BOOTINFO MAGIC=");hex64(m,boot->magic);
        txt(m," VERSION=");number(m,boot->version);
        txt(m," METHOD=");number(m,boot->boot_method);
        txt(m," MAP_RANGES=");number(m,boot->memory_map_count);
        txt(m," VIDEO=");number(m,boot->framebuffer_width);
        ch(m,'x');number(m,boot->framebuffer_height);ch(m,'\n');
    } else if(eq(s,"mem")) {
        intellect_pmm_stats_t s64=intellect_pmm_stats();
        txt(m,"PMM MANAGED=");number(m,s64.managed_pages);
        txt(m," FREE=");number(m,s64.free_pages);
        txt(m," ALLOCATED=");number(m,s64.allocated_pages);
        txt(m," PAGE_BYTES=4096\n");
    } else if(eq(s,"vmm")) {
        txt(m,"VMM SCRATCH WINDOW=");
        txt(m,intellect_vmm64_ready()?"READY":"NOT_READY");
        txt(m,"; NOT global W^X or process isolation\n");
    } else if(eq(s,"cpu")) {
        cpu(m);
    } else if(eq(s,"clear")) {
        txt(m,"\x1b[2J\x1b[H");
    } else if(eq(s,"halt")) {
        txt(m,"CPU HALT REQUESTED; reset QEMU to start again\n");
        m->stop_requested=1u;
    } else if(eq(s,"echo") ||
              (s[0]=='e' && s[1]=='c' && s[2]=='h' && s[3]=='o' && s[4]==' ')) {
        if(s[4]==' ') txt(m,s+5);
        ch(m,'\n');
    } else {
        txt(m,"UNKNOWN COMMAND; type help\n");
    }
}

void intellect_monitor64_start(intellect_monitor64_t *m,
                              const intellect_bootinfo_t *boot,
                              intellect_monitor64_output_t output, void *ctx) {
    m->boot=boot;m->output=output;m->ctx=ctx;
    m->length=0u;m->line[0]=0;m->stop_requested=0u;
    m->last_was_cr=0u;m->dropping_line=0u;
    txt(m,"\nINTELLECTOS C1 / EARLY SERIAL MONITOR\n");
    txt(m,"UEFI -> native kernel / BootInfo v1 / COM1 38400\n");
    txt(m,"type help\n");
    prompt(m);
}
void intellect_monitor64_feed(intellect_monitor64_t *m, char c) {
    if(m->stop_requested) return;
    if(c=='\r' || c=='\n') {
        if(c=='\n' && m->last_was_cr) {m->last_was_cr=0u;return;}
        m->last_was_cr=(c=='\r');
        ch(m,'\n');
        if(m->dropping_line) txt(m,"ERROR: line too long (max 127 bytes)\n");
        else {m->line[m->length]=0; command(m);}
        m->length=0;m->line[0]=0;m->dropping_line=0u;
        if(!m->stop_requested) prompt(m);
        return;
    }
    m->last_was_cr=0u;
    if(c=='\b' || (unsigned char)c==127u) {
        if(!m->dropping_line && m->length) {
            --m->length;m->line[m->length]=0;
            txt(m,"\b \b");
        }
        return;
    }
    if(c<' ' || c>'~') return; /* reject control / escape sequences */
    if(m->dropping_line) return;
    if(m->length + 1u >= sizeof(m->line)) {
        m->dropping_line=1u;
        return;
    }
    m->line[m->length++]=c;
    ch(m,c);
}

static void serial_output(void *ctx, char c) {
    (void)ctx;
    intellect_serial64_putc(c);
}
int intellect_monitor64_run(const intellect_bootinfo_t *boot) {
    if(!intellect_serial64_init()) return 0;
    intellect_monitor64_t monitor;
    intellect_monitor64_start(&monitor,boot,serial_output,NULL);
    while(!monitor.stop_requested) {
        int c=intellect_serial64_getc_nonblocking();
        if(c>=0) intellect_monitor64_feed(&monitor,(char)c);
        else __asm__ volatile("pause");
    }
    return 1;
}
