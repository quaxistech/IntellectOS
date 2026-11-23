/* Freestanding x86-64 Polled xHCI USB HID Keyboard Driver.
 * Discovers USB xHCI controller via PCI, performs safe BIOS handoff,
 * configures device slots, endpoints and polls USB HID keystrokes.
 * Fully freestanding: no firmware services, no heap, no interrupt handlers.
 */
#include <stdint.h>
#include <stddef.h>
#include "bootinfo.h"
#include "hardware64.h"
#include "xhci64.h"
#include "serial64.h"

typedef struct {
    uint64_t param;
    uint32_t status;
    uint32_t control;
} __attribute__((packed)) trb_t;

_Static_assert(sizeof(trb_t) == 16, "TRB size must be 16 bytes");

#define XHCI_MAX_SLOTS 16u
#define XHCI_RING_SIZE 64u

/* Static DMA-safe buffers aligned to 64 bytes */
static uint64_t g_dcbaap[XHCI_MAX_SLOTS + 1u] __attribute__((aligned(64)));
static trb_t g_cmd_ring[XHCI_RING_SIZE] __attribute__((aligned(64)));
static trb_t g_event_ring[XHCI_RING_SIZE] __attribute__((aligned(64)));
static uint64_t g_erst[2] __attribute__((aligned(64)));

static uint8_t g_input_ctx[2048] __attribute__((aligned(64)));
static uint8_t g_device_ctx[2048] __attribute__((aligned(64)));
static trb_t g_ep0_ring[XHCI_RING_SIZE] __attribute__((aligned(64)));
static trb_t g_intr_ring[XHCI_RING_SIZE] __attribute__((aligned(64)));
static uint8_t g_hid_report[8] __attribute__((aligned(64)));

static uintptr_t g_mmio = 0;
static uintptr_t g_op = 0;
static uintptr_t g_rt = 0;
static uintptr_t g_db = 0;

static unsigned g_cmd_idx;
static unsigned g_cmd_cycle;
static unsigned g_event_idx;
static unsigned g_event_cycle;

static uint32_t g_active_slot;
static uint32_t g_active_ep;
static int g_xhci_ready;
static uint8_t g_prev_key;
static unsigned g_intr_cycle;

static inline void outl_pci(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %w1" : : "a"(val), "Nd"(port) : "memory");
}
static inline uint32_t inl_pci(uint16_t port) {
    uint32_t v;
    __asm__ volatile("inl %w1, %0" : "=a"(v) : "Nd"(port) : "memory");
    return v;
}
static uint32_t pci_read(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off) {
    uint32_t addr = UINT32_C(0x80000000) | ((uint32_t)bus << 16) |
                    ((uint32_t)dev << 11) | ((uint32_t)fn << 8) | (off & 0xFCu);
    outl_pci(0xCF8, addr);
    return inl_pci(0xCFC);
}
static void pci_write(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint32_t val) {
    uint32_t addr = UINT32_C(0x80000000) | ((uint32_t)bus << 16) |
                    ((uint32_t)dev << 11) | ((uint32_t)fn << 8) | (off & 0xFCu);
    outl_pci(0xCF8, addr);
    outl_pci(0xCFC, val);
}

#define REG_USBCMD  (*(volatile uint32_t *)(g_op + 0x00))
#define REG_USBSTS  (*(volatile uint32_t *)(g_op + 0x04))
#define REG_CRCR    (*(volatile uint64_t *)(g_op + 0x18))
#define REG_DCBAAP  (*(volatile uint64_t *)(g_op + 0x30))
#define REG_CONFIG  (*(volatile uint32_t *)(g_op + 0x38))

#define REG_IMAN    (*(volatile uint32_t *)(g_rt + 0x20 + 0x00))
#define REG_ERSTSZ  (*(volatile uint32_t *)(g_rt + 0x20 + 0x08))
#define REG_ERSTBA  (*(volatile uint64_t *)(g_rt + 0x20 + 0x10))
#define REG_ERDP    (*(volatile uint64_t *)(g_rt + 0x20 + 0x18))

#define REG_DB(i)   (*(volatile uint32_t *)(g_db + (i) * 4u))

static int send_command(trb_t *cmd, trb_t *event_out) {
    cmd->control = (cmd->control & ~1u) | g_cmd_cycle;
    g_cmd_ring[g_cmd_idx] = *cmd;
    if (++g_cmd_idx >= XHCI_RING_SIZE - 1u) {
        trb_t link = {(uint64_t)(uintptr_t)g_cmd_ring, 0, (6u << 10) | (1u << 1) | g_cmd_cycle};
        g_cmd_ring[g_cmd_idx] = link;
        g_cmd_idx = 0;
        g_cmd_cycle ^= 1u;
    }
    REG_DB(0) = 0u;

    for (unsigned timeout = 500000u; timeout > 0u; --timeout) {
        trb_t *ev = &g_event_ring[g_event_idx];
        if ((ev->control & 1u) == g_event_cycle) {
            uint32_t type = (ev->control >> 10) & 0x3Fu;
            if (type == 33u) { /* Command Completion Event */
                if (event_out) *event_out = *ev;
                if (++g_event_idx >= XHCI_RING_SIZE) {
                    g_event_idx = 0;
                    g_event_cycle ^= 1u;
                }
                REG_ERDP = (uint64_t)(uintptr_t)&g_event_ring[g_event_idx] | (1u << 3);
                return ((ev->status >> 24) & 0xFFu) == 1u ? 1 : 0;
            }
            if (++g_event_idx >= XHCI_RING_SIZE) {
                g_event_idx = 0;
                g_event_cycle ^= 1u;
            }
            REG_ERDP = (uint64_t)(uintptr_t)&g_event_ring[g_event_idx] | (1u << 3);
        }
        for (volatile int d = 0; d < 100; ++d);
    }
    return 0;
}

int intellect_xhci64_init(const intellect_hw64_snapshot_t *hw) {
    if (g_xhci_ready) return 1;
    if (!hw || hw->pci_count == 0u) return 0;

    const intellect_hw64_pci_t *dev = NULL;
    for (uint16_t i = 0; i < hw->pci_count; ++i) {
        if (hw->pci[i].class_code == 0x0Cu &&
            hw->pci[i].subclass == 0x03u &&
            hw->pci[i].prog_if == 0x30u) {
            dev = &hw->pci[i];
            break;
        }
    }
    if (!dev) return 0;

    /* Read BAR0 and BAR1 */
    uint32_t bar0 = pci_read(dev->bus, dev->device, dev->function, 0x10);
    uint32_t bar1 = pci_read(dev->bus, dev->device, dev->function, 0x14);
    uint64_t mmio = (bar0 & 0x04u) ? (((uint64_t)bar1 << 32) | (bar0 & ~0x0Fu)) : (bar0 & ~0x0Fu);
    if (!mmio || mmio >= UINT64_C(0x100000000)) return 0;

    /* Enable Bus Master and Memory Space */
    uint32_t pci_cmd = pci_read(dev->bus, dev->device, dev->function, 0x04);
    pci_write(dev->bus, dev->device, dev->function, 0x04, pci_cmd | 0x06u);

    g_mmio = (uintptr_t)mmio;
    uint8_t caplength = *(volatile uint8_t *)g_mmio;
    uint32_t rtsoff = *(volatile uint32_t *)(g_mmio + 0x18) & ~0x1Fu;
    uint32_t dboff = *(volatile uint32_t *)(g_mmio + 0x14) & ~0x03u;
    uint32_t hcsparams1 = *(volatile uint32_t *)(g_mmio + 0x04);
    uint32_t max_slots = hcsparams1 & 0xFFu;
    uint32_t max_ports = (hcsparams1 >> 24) & 0xFFu;
    uint32_t hccparams1 = *(volatile uint32_t *)(g_mmio + 0x10);
    uint32_t xecp = (hccparams1 >> 16) & 0xFFFFu;

    g_op = g_mmio + caplength;
    g_rt = g_mmio + rtsoff;
    g_db = g_mmio + dboff;

    /* BIOS Handoff: check USBLEGSUP semaphore */
    if (xecp != 0u) {
        uint32_t legsup = pci_read(dev->bus, dev->device, dev->function, (uint8_t)(xecp * 4u));
        if (legsup & (1u << 16)) {
            pci_write(dev->bus, dev->device, dev->function, (uint8_t)(xecp * 4u), legsup | (1u << 24));
            for (unsigned t = 10000u; t > 0u; --t) {
                if (!(pci_read(dev->bus, dev->device, dev->function, (uint8_t)(xecp * 4u)) & (1u << 16))) break;
                for (volatile int d = 0; d < 100; ++d);
            }
            /* Disable SMI on OS Ownership */
            pci_write(dev->bus, dev->device, dev->function, (uint8_t)(xecp * 4u + 4u), 0u);
        }
    }

    /* Stop Controller */
    REG_USBCMD &= ~1u;
    for (unsigned t = 100000u; t > 0u; --t) {
        if (REG_USBSTS & 1u) break;
        for (volatile int d = 0; d < 100; ++d);
    }

    /* Reset Controller */
    REG_USBCMD |= 2u;
    for (unsigned t = 500000u; t > 0u; --t) {
        if (!(REG_USBCMD & 2u) && !(REG_USBSTS & (1u << 11))) break;
        for (volatile int d = 0; d < 100; ++d);
    }

    /* Configure Max Slots */
    if (max_slots > XHCI_MAX_SLOTS) max_slots = XHCI_MAX_SLOTS;
    REG_CONFIG = (REG_CONFIG & ~0xFFu) | (max_slots & 0xFFu);

    /* Zero static buffers */
    for (size_t z = 0; z < sizeof(g_dcbaap); ++z) ((uint8_t *)g_dcbaap)[z] = 0;
    for (size_t z = 0; z < sizeof(g_cmd_ring); ++z) ((uint8_t *)g_cmd_ring)[z] = 0;
    for (size_t z = 0; z < sizeof(g_event_ring); ++z) ((uint8_t *)g_event_ring)[z] = 0;
    for (size_t z = 0; z < sizeof(g_ep0_ring); ++z) ((uint8_t *)g_ep0_ring)[z] = 0;
    for (size_t z = 0; z < sizeof(g_intr_ring); ++z) ((uint8_t *)g_intr_ring)[z] = 0;

    g_cmd_idx = 0; g_cmd_cycle = 1;
    g_event_idx = 0; g_event_cycle = 1;
    g_intr_cycle = 1;

    REG_DCBAAP = (uint64_t)(uintptr_t)g_dcbaap;
    REG_CRCR = (uint64_t)(uintptr_t)g_cmd_ring | 1u;

    g_erst[0] = (uint64_t)(uintptr_t)g_event_ring;
    g_erst[1] = XHCI_RING_SIZE;
    REG_ERSTSZ = 1u;
    REG_ERSTBA = (uint64_t)(uintptr_t)g_erst;
    REG_ERDP = (uint64_t)(uintptr_t)g_event_ring;
    REG_IMAN |= 2u;

    /* Start Controller */
    REG_USBCMD |= 1u;
    for (unsigned t = 100000u; t > 0u; --t) {
        if (!(REG_USBSTS & 1u)) break;
        for (volatile int d = 0; d < 100; ++d);
    }

    /* Scan Root Hub Ports for connected USB Keyboard */
    for (uint32_t p = 1u; p <= max_ports && p <= 16u; ++p) {
        volatile uint32_t *portsc = (volatile uint32_t *)(g_op + 0x400 + (p - 1u) * 0x10);
        uint32_t sc = *portsc;
        if (!(sc & 1u)) continue; /* No device connected */

        /* Reset port */
        *portsc = (sc & ~0x02u) | (1u << 4);
        for (unsigned t = 100000u; t > 0u; --t) {
            if (*portsc & (1u << 21)) break;
            for (volatile int d = 0; d < 100; ++d);
        }
        *portsc |= (1u << 21); /* Clear PRC */

        uint32_t speed = (*portsc >> 10) & 0x0Fu;
        if (speed == 0u || speed > 4u) continue;

        /* Send Enable Slot command */
        trb_t cmd = {0, 0, (9u << 10)};
        trb_t ev = {0, 0, 0};
        if (!send_command(&cmd, &ev)) continue;

        uint32_t slot = (ev.control >> 24) & 0xFFu;
        if (!slot || slot > XHCI_MAX_SLOTS) continue;

        /* Assign Device Context */
        for (size_t z = 0; z < sizeof(g_device_ctx); ++z) g_device_ctx[z] = 0;
        g_dcbaap[slot] = (uint64_t)(uintptr_t)g_device_ctx;

        /* Setup Input Context for Address Device */
        for (size_t z = 0; z < sizeof(g_input_ctx); ++z) g_input_ctx[z] = 0;
        uint32_t *in_ctrl = (uint32_t *)g_input_ctx;
        in_ctrl[1] = 0x03u; /* Add Slot Context (bit 0) and EP0 Context (bit 1) */

        uint32_t *slot_ctx = (uint32_t *)(g_input_ctx + 32);
        slot_ctx[0] = (1u << 27) | (speed << 20);
        slot_ctx[1] = (p << 16);

        uint32_t *ep0_ctx = (uint32_t *)(g_input_ctx + 64);
        uint32_t max_pkt = (speed == 1u ? 64u : (speed == 2u ? 8u : 64u));
        ep0_ctx[1] = (4u << 3) | (max_pkt << 16); /* EP Type 4 (Control), Max Packet */
        ep0_ctx[2] = (uint32_t)(uintptr_t)g_ep0_ring | 1u;
        ep0_ctx[3] = (uint32_t)((uint64_t)(uintptr_t)g_ep0_ring >> 32);
        ep0_ctx[4] = 8u;

        cmd.param = (uint64_t)(uintptr_t)g_input_ctx;
        cmd.status = 0;
        cmd.control = (11u << 10) | (slot << 24); /* Address Device */
        if (!send_command(&cmd, &ev)) continue;

        /* Configure Interrupt IN Endpoint (EP index 3 = EP1 IN) */
        for (size_t z = 0; z < sizeof(g_input_ctx); ++z) g_input_ctx[z] = 0;
        in_ctrl[1] = (1u << 0) | (1u << 3); /* Add Slot Context (bit 0) and EP3 (bit 3) */
        slot_ctx[0] = (3u << 27) | (speed << 20); /* Context Entries = 3 */
        slot_ctx[1] = (p << 16);

        uint32_t *ep3_ctx = (uint32_t *)(g_input_ctx + 32 + 3 * 32);
        ep3_ctx[0] = (6u << 16); /* Interval = 6 (8 ms) */
        ep3_ctx[1] = (7u << 3) | (8u << 16) | (3u << 1); /* EP Type 7 (Interrupt IN), 8B max packet */
        ep3_ctx[2] = (uint32_t)(uintptr_t)g_intr_ring | 1u;
        ep3_ctx[3] = (uint32_t)((uint64_t)(uintptr_t)g_intr_ring >> 32);
        ep3_ctx[4] = 8u;

        cmd.param = (uint64_t)(uintptr_t)g_input_ctx;
        cmd.status = 0;
        cmd.control = (12u << 10) | (slot << 24); /* Configure Endpoint */
        if (!send_command(&cmd, &ev)) continue;

        /* Queue first Normal TRB on Interrupt IN ring */
        g_intr_ring[0].param = (uint64_t)(uintptr_t)g_hid_report;
        g_intr_ring[0].status = 8u;
        g_intr_ring[0].control = (1u << 10) | (1u << 5) | 1u; /* Normal TRB, IOC, Cycle = 1 */
        REG_DB(slot) = 3u; /* Ring doorbell for EP index 3 */

        g_active_slot = slot;
        g_active_ep = 3u;
        g_xhci_ready = 1;
        break;
    }

    return g_xhci_ready;
}

int intellect_xhci64_is_ready(void) {
    return g_xhci_ready;
}

static const char hid_to_ascii[128] = {
    [0x04]='a',[0x05]='b',[0x06]='c',[0x07]='d',[0x08]='e',
    [0x09]='f',[0x0a]='g',[0x0b]='h',[0x0c]='i',[0x0d]='j',
    [0x0e]='k',[0x0f]='l',[0x10]='m',[0x11]='n',[0x12]='o',
    [0x13]='p',[0x14]='q',[0x15]='r',[0x16]='s',[0x17]='t',
    [0x18]='u',[0x19]='v',[0x1a]='w',[0x1b]='x',[0x1c]='y',
    [0x1d]='z',
    [0x1e]='1',[0x1f]='2',[0x20]='3',[0x21]='4',[0x22]='5',
    [0x23]='6',[0x24]='7',[0x25]='8',[0x26]='9',[0x27]='0',
    [0x28]='\n',[0x2a]='\b',[0x2b]='\t',[0x2c]=' ',
    [0x2d]='-',[0x2e]='=',[0x2f]='[',[0x30]=']',[0x31]='\\',
    [0x33]=';',[0x34]='\'',[0x35]='`',[0x36]=',',[0x37]='.',
    [0x38]='/'
};

static char hid_shifted(char c) {
    if (c >= 'a' && c <= 'z') return (char)(c - ('a' - 'A'));
    switch (c) {
      case '1': return '!'; case '2': return '@'; case '3': return '#';
      case '4': return '$'; case '5': return '%'; case '6': return '^';
      case '7': return '&'; case '8': return '*'; case '9': return '(';
      case '0': return ')'; case '-': return '_'; case '=': return '+';
      case '[': return '{'; case ']': return '}'; case ';': return ':';
      case '\'': return '"'; case ',': return '<'; case '.': return '>';
      case '/': return '?'; case '`': return '~'; case '\\': return '|';
      default: return c;
    }
}

int intellect_xhci64_poll_char(void) {
    if (!g_xhci_ready || !g_active_slot) return -1;

    trb_t *ev = &g_event_ring[g_event_idx];
    if ((ev->control & 1u) != g_event_cycle) return -1;

    uint32_t type = (ev->control >> 10) & 0x3Fu;
    uint32_t slot = (ev->control >> 24) & 0xFFu;

    int ret_char = -1;

    if (type == 32u && slot == g_active_slot) { /* Transfer Event */
        uint8_t mod = g_hid_report[0];
        uint8_t key = g_hid_report[2];
        int shift = (mod & 0x22u) != 0;

        if (key != 0 && key != g_prev_key && key < 128u) {
            char ch = hid_to_ascii[key];
            if (ch) {
                ret_char = shift ? (int)(unsigned char)hid_shifted(ch) : (int)(unsigned char)ch;
            }
        }
        g_prev_key = key;

        /* Re-queue Normal TRB */
        g_intr_ring[0].control = (1u << 10) | (1u << 5) | g_intr_cycle;
        REG_DB(g_active_slot) = g_active_ep;
    }

    if (++g_event_idx >= XHCI_RING_SIZE) {
        g_event_idx = 0;
        g_event_cycle ^= 1u;
    }
    REG_ERDP = (uint64_t)(uintptr_t)&g_event_ring[g_event_idx] | (1u << 3);

    return ret_char;
}
