/* Host test harness for input64: validates Set 1 and Set 2 scancode decoding,
 * shift states, break codes, enter/backspace/space, and floating bus guards.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_QUEUE 256
static uint8_t mock_ports[65536];
static uint8_t scan_queue[MAX_QUEUE];
static unsigned queue_head = 0, queue_tail = 0;

uint8_t intellect_input64_mock_inb(uint16_t port) {
    if (port == 0x64u) {
        if (mock_ports[0x64u] == 0xFFu) return 0xFFu;
        return (queue_head != queue_tail) ? (mock_ports[0x64u] | 0x01u) : (mock_ports[0x64u] & ~0x01u);
    }
    if (port == 0x60u) {
        if (queue_head != queue_tail) {
            uint8_t sc = scan_queue[queue_tail++];
            if (queue_tail >= MAX_QUEUE) queue_tail = 0;
            return sc;
        }
        return mock_ports[0x60u];
    }
    return mock_ports[port];
}

void intellect_input64_mock_outb(uint16_t port, uint8_t val) {
    mock_ports[port] = val;
}

int intellect_serial64_getc_nonblocking(void) {
    return -1; /* default: test keyboard path */
}

static void feed_scancode(uint8_t sc) {
    scan_queue[queue_head++] = sc;
    if (queue_head >= MAX_QUEUE) queue_head = 0;
}

#include "input64.h"

int main(void) {
    memset(mock_ports, 0, sizeof(mock_ports));
    mock_ports[0x60u] = 0;
    mock_ports[0x64u] = 0;

    intellect_input64_init();

    /* 1. Test Set 1 keys: 'h', 'e', 'l', 'p', '\n' */
    feed_scancode(0x23u); /* 'h' make */
    feed_scancode(0xA3u); /* 'h' break */
    feed_scancode(0x12u); /* 'e' make */
    feed_scancode(0x92u); /* 'e' break */
    feed_scancode(0x26u); /* 'l' make */
    feed_scancode(0xA6u); /* 'l' break */
    feed_scancode(0x19u); /* 'p' make */
    feed_scancode(0x99u); /* 'p' break */
    feed_scancode(0x1Cu); /* Enter make */
    feed_scancode(0x9Cu); /* Enter break */

    assert(intellect_input64_poll_char() == 'h');
    assert(intellect_input64_poll_char() == -1); /* break */
    assert(intellect_input64_poll_char() == 'e');
    assert(intellect_input64_poll_char() == -1); /* break */
    assert(intellect_input64_poll_char() == 'l');
    assert(intellect_input64_poll_char() == -1); /* break */
    assert(intellect_input64_poll_char() == 'p');
    assert(intellect_input64_poll_char() == -1); /* break */
    assert(intellect_input64_poll_char() == '\n');
    assert(intellect_input64_poll_char() == -1); /* break */

    /* 2. Test Set 1 Shift key: Left Shift (0x2A) + 'a' (0x1E) = 'A' */
    feed_scancode(0x2Au); /* Shift make */
    feed_scancode(0x1Eu); /* 'a' make */
    feed_scancode(0x9Eu); /* 'a' break */
    feed_scancode(0xAAu); /* Shift break */

    assert(intellect_input64_poll_char() == -1); /* shift */
    assert(intellect_input64_poll_char() == 'A');
    assert(intellect_input64_poll_char() == -1); /* 'a' break */
    assert(intellect_input64_poll_char() == -1); /* shift break */

    /* 3. Test Set 2 keys: USB keyboard native scancodes!
     * Break is 0xF0 <key>. Enter is 0x5A. Space is 0x29. Backspace is 0x66.
     * 'd' in Set 2 is 0x23, 'i' is 0x43, 's' is 0x1B, 'k' is 0x42.
     */
    feed_scancode(0x23u); /* 'd' in Set 2 */
    feed_scancode(0xF0u); /* break prefix -> activates Set 2 decoder! */
    feed_scancode(0x23u); /* 'd' released */
    feed_scancode(0x43u); /* 'i' */
    feed_scancode(0xF0u); feed_scancode(0x43u);
    feed_scancode(0x1Bu); /* 's' */
    feed_scancode(0xF0u); feed_scancode(0x1Bu);
    feed_scancode(0x42u); /* 'k' */
    feed_scancode(0xF0u); feed_scancode(0x42u);
    feed_scancode(0x29u); /* Space in Set 2 */
    feed_scancode(0xF0u); feed_scancode(0x29u);
    feed_scancode(0x5Au); /* Enter in Set 2 */
    feed_scancode(0xF0u); feed_scancode(0x5Au);

    /* First char was fed before Set 2 break; in Set 1 0x23 is 'h', but break 0xF0 switches to Set 2 */
    (void)intellect_input64_poll_char(); /* consumes 0x23 */
    assert(intellect_input64_poll_char() == -1); /* 0xF0 break prefix */
    assert(intellect_input64_poll_char() == -1); /* 0x23 break */
    assert(intellect_input64_poll_char() == 'i');
    assert(intellect_input64_poll_char() == -1); /* 0xF0 */
    assert(intellect_input64_poll_char() == -1); /* 0x43 */
    assert(intellect_input64_poll_char() == 's');
    assert(intellect_input64_poll_char() == -1); /* 0xF0 */
    assert(intellect_input64_poll_char() == -1); /* 0x1B */
    assert(intellect_input64_poll_char() == 'k');
    assert(intellect_input64_poll_char() == -1); /* 0xF0 */
    assert(intellect_input64_poll_char() == -1); /* 0x42 */
    assert(intellect_input64_poll_char() == ' ');
    assert(intellect_input64_poll_char() == -1); /* 0xF0 */
    assert(intellect_input64_poll_char() == -1); /* 0x29 */
    assert(intellect_input64_poll_char() == '\n');
    assert(intellect_input64_poll_char() == -1); /* 0xF0 */
    assert(intellect_input64_poll_char() == -1); /* 0x5A */

    /* 4. Test Set 2 Shift + Backspace */
    feed_scancode(0x12u); /* Set 2 Left Shift */
    feed_scancode(0x1Cu); /* Set 2 'a' -> 'A' */
    feed_scancode(0xF0u); feed_scancode(0x1Cu);
    feed_scancode(0xF0u); feed_scancode(0x12u); /* Shift released */
    feed_scancode(0x66u); /* Set 2 Backspace */
    feed_scancode(0xF0u); feed_scancode(0x66u);

    assert(intellect_input64_poll_char() == -1); /* shift */
    assert(intellect_input64_poll_char() == 'A');
    assert(intellect_input64_poll_char() == -1); /* 0xF0 */
    assert(intellect_input64_poll_char() == -1); /* 0x1C */
    assert(intellect_input64_poll_char() == -1); /* 0xF0 */
    assert(intellect_input64_poll_char() == -1); /* 0x12 shift release */
    assert(intellect_input64_poll_char() == '\b');
    assert(intellect_input64_poll_char() == -1); /* 0xF0 */
    assert(intellect_input64_poll_char() == -1); /* 0x66 */

    /* 5. Test Floating bus guard */
    mock_ports[0x64u] = 0xFFu;
    assert(intellect_input64_poll_char() == -1);

    puts("PASS: input64 Set 1 and Set 2 keyboard decoding verified");
    return 0;
}
