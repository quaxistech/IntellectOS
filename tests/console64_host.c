/* Deterministic host integration tests for early kernel monitor parser. */
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "kmonitor64.h"
#include "pmm64.h"

static char text[20000];
static size_t used;
static void emit(void *ctx, char ch) {
    (void)ctx;
    assert(used+1<sizeof(text));
    text[used++]=ch;text[used]=0;
}
static void reset(void) {used=0;text[0]=0;}
static void feed(intellect_monitor64_t *m,const char *line) {
    while(*line) intellect_monitor64_feed(m,*line++);
}
intellect_pmm_stats_t intellect_pmm_stats(void) {
    const intellect_pmm_stats_t out={128u,120u,8u};
    return out;
}
int intellect_vmm64_ready(void) {return 1;}
int intellect_serial64_init(void) {return 0;}
void intellect_serial64_putc(char c) {(void)c;}
int intellect_serial64_getc_nonblocking(void) {return -1;}
int main(void) {
    intellect_bootinfo_t b={0};
    b.magic=INTELLECT_BOOTINFO_MAGIC;
    b.version=1;b.boot_method=INTELLECT_BOOT_UEFI;
    b.memory_map_count=42;
    b.framebuffer_width=1920;b.framebuffer_height=1080;
    intellect_monitor64_t m;
    intellect_monitor64_start(&m,&b,emit,0);
    assert(strstr(text,"INTELLECTOS C1")!=0);
    reset();feed(&m,"help\r\n");
    assert(strstr(text,"COMMANDS: help about boot mem vmm cpu clear echo <text> halt")!=0);
    assert(strstr(text,"UNKNOWN COMMAND")==0);
    reset();feed(&m,"mem\r");
    assert(strstr(text,"PMM MANAGED=128 FREE=120 ALLOCATED=8")!=0);
    reset();feed(&m,"vmm\n");
    assert(strstr(text,"VMM SCRATCH WINDOW=READY")!=0);
    reset();feed(&m,"boot\n");
    assert(strstr(text,"MAP_RANGES=42 VIDEO=1920x1080")!=0);
    reset();feed(&m,"echo 123\b4\r");
    assert(strstr(text,"124\n")!=0);
    reset();feed(&m,"unknown\n");
    assert(strstr(text,"UNKNOWN COMMAND")!=0);
    reset();
    for(unsigned i=0;i<130u;++i) intellect_monitor64_feed(&m,'A');
    intellect_monitor64_feed(&m,'\n');
    assert(strstr(text,"line too long")!=0);
    reset();feed(&m,"halt\r");
    assert(m.stop_requested==1u);
    reset();feed(&m,"help\r");
    assert(used==0);
    puts("PASS: C1 serial command parser + overflow + CRLF + halt");
    return 0;
}
