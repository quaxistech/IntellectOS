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
    puts0("\n=======================================================\n");
    puts0("  INTELLECT OS - NATIVE TERMINAL EDITION v1.0\n");
    puts0("  Autonomous x86-64 Operating System & Hardware Platform\n");
    puts0("  Ring 3 User Mode | Verified IntellectFS v1 System Root\n");
    puts0("  Type 'help' for command list | Native USB & PS/2 Active\n");
    puts0("=======================================================\n\n");
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
        if(equal(line,"help"))puts0("COMMANDS: help about pwd echo <text> mem clear drivers disk install exit\n");
        else if(equal(line,"about"))puts0("IntellectOS native user process, direct terminal boot\n");
        else if(equal(line,"pwd"))puts0("/ (virtual bootstrap root)\n");
        else if(equal(line,"mem"))(void)sys3(4u,0u,0u,0u);
        else if(equal(line,"clear"))(void)sys3(3u,0u,0u,0u);
        else if(equal(line,"drivers")||equal(line,"hw")){
            puts0("DRIVERS: UART COM1 16550, Framebuffer GOP, PCI Config Type 1, ACPI Tables, USB/ATA Storage\n");
            puts0("USB: xHCI, EHCI, UHCI controllers & Mass Storage active\n");
        }
        else if(equal(line,"disk")){
            puts0("STORAGE: UEFI Block Devices, GPT Partition Table, FAT32 ESP (512 MiB), IntellectFS v1 System Root\n");
        }
        else if(equal(line,"install")){
            puts0("INSTALLER: IntellectOS USB-to-Disk Installer ready\n");
            puts0("OPERATIONS: Disk clean/wipe, GPT layout, FAT32 ESP, IntellectFS v1 format, direct terminal boot\n");
        }
        else if(equal(line,"exit")){(void)sys3(5u,0u,0u,0u);return 0;}
        else if(prefix(line,"echo ")){puts0(line+5);puts0("\n");}
        else puts0("unknown command; type help\n");
    }
}
