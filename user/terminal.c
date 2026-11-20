/* Freestanding, reloc-free user process. No kernel pointers or drivers. */
#include <stddef.h>
#include <stdint.h>
static long sys3(uint64_t no,uint64_t a,uint64_t b,uint64_t c){
    register uint64_t nr __asm__("rax")=no;
    register uint64_t x __asm__("rdi")=a;
    register uint64_t y __asm__("rsi")=b;
    register uint64_t z __asm__("rdx")=c;
    __asm__ volatile("int $0x80":"+a"(nr):"D"(x),"S"(y),"d"(z):"rcx","r11","memory");
    return (long)nr;
}
static size_t slen(const char *s){size_t n=0;while(s[n])++n;return n;}
static void puts0(const char *s){(void)sys3(1u,1u,(uintptr_t)s,slen(s));}
static long getchar0(void){return sys3(2u,0u,0u,0u);}
static int equal(const char *a,const char *b){
    while(*a&&*b&&*a==*b){++a;++b;}return !*a&&!*b;
}
static int prefix(const char *s,const char *p){
    while(*p){if(*s++!=*p++)return 0;}return 1;
}
int main(void){
    char line[96];
    puts0("\nINTELLECT OS U1 - USER MODE TERMINAL\n");
    puts0("CPL3 / INT80 ABI / PS2 OR COM1 INPUT\n");
    puts0("type help. This is a bootstrap shell, not root login.\n\n");
    for(;;){
        puts0("intellect:/$ ");
        unsigned n=0;
        for(;;){
            long c=getchar0();
            if(c<0 || c>255)continue;
            if(c=='\r'||c=='\n'){puts0("\n");break;}
            if(c==8||c==127){
                if(n){--n;line[n]=0;puts0("\b");}
                continue;
            }
            if(c>=32&&c<=126&&n<sizeof(line)-1u){
                line[n++]=(char)c;line[n]=0;
                (void)sys3(1u,1u,(uintptr_t)&line[n-1],1u);
            }
        }
        line[n]=0;
        if(!n)continue;
        if(equal(line,"help"))puts0("COMMANDS: help about pwd echo <text> mem clear exit\n");
        else if(equal(line,"about"))puts0("IntellectOS native user process, no filesystem yet\n");
        else if(equal(line,"pwd"))puts0("/ (virtual bootstrap root)\n");
        else if(equal(line,"mem"))(void)sys3(4u,0u,0u,0u);
        else if(equal(line,"clear"))(void)sys3(3u,0u,0u,0u);
        else if(equal(line,"exit")){(void)sys3(5u,0u,0u,0u);return 0;}
        else if(prefix(line,"echo ")){puts0(line+5);puts0("\n");}
        else puts0("unknown command; type help\n");
    }
}
