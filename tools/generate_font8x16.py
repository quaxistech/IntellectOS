#!/usr/bin/env python3
"""Generate kernel/include/font8x16.h from standard TerminusBold16 console font."""
import gzip
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FONT_PATH = Path('/usr/share/consolefonts/Lat15-TerminusBold16.psf.gz')

def main():
    if not FONT_PATH.exists():
        raise FileNotFoundError(f"Console font not found: {FONT_PATH}")
    with gzip.open(FONT_PATH, 'rb') as f:
        data = f.read()

    lines = []
    lines.append('/* IntellectOS High-Definition 8x16 Monospace Console Font */')
    lines.append('/* Clean Terminus-based bitmap font with full ASCII 32..126 support */')
    lines.append('#ifndef INTELLECT_FONT8X16_H')
    lines.append('#define INTELLECT_FONT8X16_H')
    lines.append('#include <stdint.h>')
    lines.append('')
    lines.append('static const uint8_t font8x16_glyphs[95][16] = {')

    for ch in range(32, 127):
        offset = 4 + ch * 16
        glyph = data[offset:offset + 16]
        comment = f'/* {ch} 0x{ch:02X} \'{chr(ch)}\' */' if ch not in (92, 34) else f'/* {ch} 0x{ch:02X} */'
        byte_str = ', '.join(f'0x{b:02x}' for b in glyph)
        lines.append(f'  {{{byte_str}}}, {comment}')

    lines.append('};')
    lines.append('')
    lines.append('static inline uint8_t intellect_glyph8x16(char ch, unsigned row) {')
    lines.append('    if (row >= 16u) return 0u;')
    lines.append('    if ((unsigned char)ch < 32u || (unsigned char)ch > 126u) return 0u;')
    lines.append('    return font8x16_glyphs[(unsigned char)ch - 32u][row];')
    lines.append('}')
    lines.append('')
    lines.append('#endif /* INTELLECT_FONT8X16_H */')
    lines.append('')

    out = '\n'.join(lines)
    dest1 = ROOT / 'kernel/include/font8x16.h'
    dest2 = ROOT / 'kernel/src64/font8x16.h'
    dest1.write_text(out)
    dest2.write_text(out)
    print(f"Wrote font headers: {dest1} and {dest2}")

if __name__ == '__main__':
    main()
