/* Polled x86 i8042 scan-set-1 keyboard + optional COM1 fallback.
 * Deliberately no USB/xHCI claim. Interrupts are not enabled in U1.
 */
#include <stdint.h>
#include "serial64.h"
static unsigned shift_pressed, extended;
static inline uint8_t inb(uint16_t port) {
    uint8_t v;__asm__ volatile("inb %1,%0":"=a"(v):"Nd"(port));return v;
}
static const char keymap[128] = {
    [0x02]='1',[0x03]='2',[0x04]='3',[0x05]='4',[0x06]='5',
    [0x07]='6',[0x08]='7',[0x09]='8',[0x0a]='9',[0x0b]='0',
    [0x0c]='-',[0x0d]='=',[0x0e]='\b',[0x0f]='\t',
    [0x10]='q',[0x11]='w',[0x12]='e',[0x13]='r',[0x14]='t',
    [0x15]='y',[0x16]='u',[0x17]='i',[0x18]='o',[0x19]='p',
    [0x1a]='[',[0x1b]=']',[0x1c]='\n',
    [0x1e]='a',[0x1f]='s',[0x20]='d',[0x21]='f',[0x22]='g',
    [0x23]='h',[0x24]='j',[0x25]='k',[0x26]='l',[0x27]=';',
    [0x28]='\'',[0x29]='`',[0x2b]='\\',
    [0x2c]='z',[0x2d]='x',[0x2e]='c',[0x2f]='v',[0x30]='b',
    [0x31]='n',[0x32]='m',[0x33]=',',[0x34]='.',[0x35]='/',
    [0x39]=' '
};
static char shifted(char c) {
    if(c>='a'&&c<='z')return (char)(c-('a'-'A'));
    switch(c) {
      case '1':return '!';case '2':return '@';case '3':return '#';
      case '4':return '$';case '5':return '%';case '6':return '^';
      case '7':return '&';case '8':return '*';case '9':return '(';
      case '0':return ')';case '-':return '_';case '=':return '+';
      case '[':return '{';case ']':return '}';case ';':return ':';
      case '\'':return '"';case ',':return '<';case '.':return '>';
      case '/':return '?';case '`':return '~';case '\\':return '|';
      default:return c;
    }
}
int intellect_input64_poll_char(void) {
    const int serial=intellect_serial64_getc_nonblocking();
    if(serial>=0)return serial=='\r'?'\n':serial;
    const uint8_t status=inb(0x64u);
    if(!(status&1u))return -1;
    const uint8_t sc=inb(0x60u);
    if(status&0x20u)return -1; /* AUX: mouse, not keyboard */
    if(sc==0xe0u || sc==0xe1u) {extended=1u;return -1;}
    if(extended){extended=0u;return -1;}
    const uint8_t key=(uint8_t)(sc&0x7fu);
    if(key==0x2au || key==0x36u){shift_pressed=(sc&0x80u)?0u:1u;return -1;}
    if(sc&0x80u)return -1;
    char c=keymap[key];
    if(shift_pressed)c=shifted(c);
    return c?(int)(unsigned char)c:-1;
}
int intellect_input64_read_char(void) {
    for(;;){
        const int c=intellect_input64_poll_char();
        if(c>=0)return c;
        __asm__ volatile("pause");
    }
}
