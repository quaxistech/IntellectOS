#ifndef INTELLECT_FONT5X7_H
#define INTELLECT_FONT5X7_H
#include <stdint.h>
/* Original 5x7 uppercase bitmap glyphs, stored by scanline (bit 4 is leftmost). */
static const uint8_t font_upper[26][7] = {
 {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
 {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
 {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
 {14,17,16,23,17,17,14}, {17,17,17,31,17,17,17},
 {31,4,4,4,4,4,31}, {7,2,2,2,18,18,12},
 {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17},
 {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
 {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
 {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
 {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
 {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31}
};
static const uint8_t font_digits[10][7] = {
 {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
 {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
 {2,6,10,18,31,2,2}, {31,16,30,1,1,17,14},
 {6,8,16,30,17,17,14}, {31,1,2,4,8,8,8},
 {14,17,17,14,17,17,14}, {14,17,17,15,1,2,12}
};
static inline uint8_t intellect_glyph_row(char ch, unsigned y) {
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch-'a'+'A');
    if (ch >= 'A' && ch <= 'Z') return font_upper[(unsigned)(ch-'A')][y];
    if (ch >= '0' && ch <= '9') return font_digits[(unsigned)(ch-'0')][y];
    switch(ch) {
      case '-': return y==3 ? 31 : 0;
      case '_': return y==6 ? 31 : 0;
      case ':': return (y==2 || y==5) ? 4 : 0;
      case '.': return y==6 ? 4 : 0;
      case '/': return (uint8_t)(1u << (y<=4 ? y : 4));
      case '=': return (y==2 || y==4) ? 31 : 0;
      case '+': return y==3 ? 31 : (y==1 || y==2 || y==4 || y==5) ? 4 : 0;
      case '[': return y==0 || y==6 ? 12 : 8;
      case ']': return y==0 || y==6 ? 6 : 2;
      case '(': return y==0 || y==6 ? 2 : 4;
      case ')': return y==0 || y==6 ? 8 : 4;
      case ',': return y==6 ? 4 : 0;
      case '?': return (const uint8_t[]){14,17,1,2,4,0,4}[y];
      case ' ': return 0;
      default: return y==0 || y==6 ? 31 : 17;
    }
}

#endif
