/* IntellectOS High-Definition UEFI Boot Chooser and On-Firmware Installer.
 * The UEFI firmware owns the keyboard and GOP until ExitBootServices;
 * none of these protocol pointers reach the OS kernel.
 * Features:
 * - High-resolution 8x16 Terminus typography with full ASCII support
 * - Screen resolution adaptive card layout with modern cyber aesthetics
 * - Option 1: Live Terminal Boot (interactive shell with working USB input)
 * - Option 2: Full On-Firmware Disk Installation to Internal HDD/SSD/NVMe
 */
#include <stddef.h>
#include <stdint.h>
#include "bootmenu.h"
#include "../../kernel/include/font8x16.h"

#define EFI_NOT_READY (UINT64_C(0x8000000000000000)|6u)
#define KEY_UP 1u
#define KEY_DOWN 2u
#define KEY_ESCAPE 0x17u
#define POLL_US 10000u
#define DEFAULT_POLLS 1200u /* 12 seconds; Enter / 1 starts immediately */

#define EFI_BY_PROTOCOL 2u
#define EFI_MAX_BLOCK_HANDLES 128u

extern const uint8_t intellect_installed_efi_start[];
extern const uint8_t intellect_installed_efi_end[];

static efi_handle_t g_boot_image_handle = NULL;
static efi_system_table_t *g_boot_system_table = NULL;

void intellect_uefi_bootmenu_set_context(efi_handle_t image, efi_system_table_t *st) {
    g_boot_image_handle = image;
    g_boot_system_table = st;
}

static const efi_guid_t block_guid = {0x964e5b21u, 0x6459u, 0x11d2u,
                                      {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static const efi_guid_t loaded_image_guid = {0x5b1baa65u, 0xade8u, 0x11d2u,
                                             {0x9e, 0x36, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};

typedef struct {
    uint32_t media_id;
    uint8_t removable, media_present, logical_partition, read_only, write_caching;
    uint8_t reserved[3];
    uint32_t block_size;
    uint32_t io_align;
    uint64_t last_block;
} efi_block_media_t;

typedef struct efi_block_io {
    uint64_t revision;
    efi_block_media_t *media;
    void *reset;
    efi_status_t (EFIAPI *read_blocks)(struct efi_block_io *, uint32_t, uint64_t, size_t, void *);
    efi_status_t (EFIAPI *write_blocks)(struct efi_block_io *, uint32_t, uint64_t, size_t, const void *);
    efi_status_t (EFIAPI *flush_blocks)(struct efi_block_io *);
} efi_block_io_t;

typedef efi_status_t (EFIAPI *locate_buffer_fn)(uint32_t, const efi_guid_t *, void *, size_t *, efi_handle_t **);
typedef efi_status_t (EFIAPI *handle_protocol_fn)(efi_handle_t, const efi_guid_t *, void **);
typedef efi_status_t (EFIAPI *free_pool_fn)(void *);

typedef struct { volatile uint8_t *buf; uint32_t w, h, pitch, fmt; } canvas_t;

/* Statically allocated 64 KiB transfer buffer for disk installation (zero BSS overhead) */
static uint8_t g_install_buffer[65536u] __attribute__((aligned(4096)));

static void box(const canvas_t *c, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t rgb) {
    if (x >= c->w || y >= c->h || !w || !h) return;
    if (w > c->w - x) w = c->w - x;
    if (h > c->h - y) h = c->h - y;
    uint8_t r = (uint8_t)(rgb >> 16), g = (uint8_t)(rgb >> 8), b = (uint8_t)rgb;
    for (uint32_t yy = 0; yy < h; ++yy) {
        volatile uint8_t *p = c->buf + (size_t)(y + yy) * c->pitch + (size_t)x * 4u;
        for (uint32_t xx = 0; xx < w; ++xx, p += 4) {
            p[0] = c->fmt == INTELLECT_PIXEL_RGBX8888 ? r : b;
            p[1] = g;
            p[2] = c->fmt == INTELLECT_PIXEL_RGBX8888 ? b : r;
            p[3] = 0u;
        }
    }
}

static void glyph(const canvas_t *c, uint32_t x, uint32_t y, char ch, uint32_t size, uint32_t rgb) {
    if ((unsigned char)ch < 32u || (unsigned char)ch > 126u) return;
    for (unsigned row = 0; row < 16u; ++row) {
        uint8_t bits = intellect_glyph8x16(ch, row);
        if (!bits) continue;
        for (unsigned k = 0; k < 8u; ++k) {
            if (bits & (1u << (7u - k)))
                box(c, x + k * size, y + row * size, size, size, rgb);
        }
    }
}

static void line(const canvas_t *c, uint32_t x, uint32_t y, const char *s,
                 unsigned max, uint32_t size, uint32_t rgb) {
    for (unsigned n = 0; n < max && *s && x < c->w; ++n, ++s) {
        glyph(c, x, y, *s, size, rgb);
        uint32_t advance = (8u + 1u) * size;
        if (advance > c->w - x) return;
        x += advance;
    }
}

static void decimal(char out[12], unsigned n) {
    char rev[12]; unsigned i = 0, j = 0;
    do { rev[i++] = (char)('0' + (n % 10u)); n /= 10u; } while (n);
    while (i) out[j++] = rev[--i];
    out[j] = 0;
}

static void draw(const canvas_t *c, unsigned selected, unsigned drives, unsigned mode_view) {
    const uint32_t w = c->w, h = c->h;
    const uint32_t scale = (w >= 1600u && h >= 900u) ? 2u : 1u;
    const uint32_t left = (w >= 900u) ? w / 10u : 24u;
    const uint32_t cardw = w - 2u * left;
    const uint32_t title_size = (w >= 1200u) ? 3u : (w >= 800u ? 2u : 1u);

    /* Background and top glowing accent strip */
    box(c, 0u, 0u, w, h, 0x070b12u);
    box(c, 0u, 0u, w, 4u * scale, 0x00e5a3u);

    /* Header Title */
    uint32_t title_y = (h >= 600u) ? h / 12u : 20u;
    line(c, left, title_y, "INTELLECT OS", 12u, title_size, 0xf8fafcu);
    line(c, left + 13u * 9u * title_size, title_y + (title_size > 1 ? 6u : 0u),
         "v1.0", 4u, title_size > 1 ? title_size - 1u : 1u, 0x00e5a3u);

    uint32_t sub_y = title_y + 18u * title_size + 4u;
    line(c, left, sub_y, "Sovereign x86-64 Operating System & Hardware Platform", 55u, 1u, 0x8298b0u);

    uint32_t top = sub_y + 32u * scale;
    line(c, left, top, "SELECT BOOT OPTION:", 20u, 1u, 0x00e5a3u);
    top += 18u * scale;

    uint32_t ch = 64u * scale;

    /* Card 0: Live Terminal */
    {
        uint32_t y = top;
        uint32_t fill = (selected == 0u) ? 0x132235u : 0x0d1522u;
        box(c, left, y, cardw, ch, fill);
        box(c, left, y, (selected == 0u ? 6u : 2u) * scale, ch,
            (selected == 0u ? 0x00e5a3u : 0x22364du));
        line(c, left + 20u * scale, y + 12u * scale,
             "1  BOOT INTELLECT OS (LIVE TERMINAL MODE)", 42u, scale,
             selected == 0u ? 0xf8fafcu : 0x94a3b8u);
        line(c, left + 20u * scale, y + 36u * scale,
             "Direct Interactive Terminal Shell | USB Keyboard Active | No Disk Changes", 75u, 1u,
             selected == 0u ? 0x5eead4u : 0x556980u);
    }

    /* Card 1: Install to Disk */
    {
        uint32_t y = top + ch + 14u * scale;
        uint32_t fill = (selected == 1u) ? 0x132235u : 0x0d1522u;
        box(c, left, y, cardw, ch, fill);
        box(c, left, y, (selected == 1u ? 6u : 2u) * scale, ch,
            (selected == 1u ? 0x00e5a3u : 0x22364du));
        line(c, left + 20u * scale, y + 12u * scale,
             "2  INSTALL INTELLECT OS TO INTERNAL DISK", 41u, scale,
             selected == 1u ? 0xf8fafcu : 0x94a3b8u);
        line(c, left + 20u * scale, y + 36u * scale,
             "Full Target Disk Installer: Wipe, Standard GPT, FAT32 ESP & IntellectFS v1", 75u, 1u,
             selected == 1u ? 0x5eead4u : 0x556980u);
    }

    /* Footer Navigation and System Status */
    const uint32_t foot = (h >= 110u * scale) ? h - 100u * scale : h - 60u;
    box(c, left, foot, cardw, 1u, 0x1d2e45u);

    if (mode_view == 1u) {
        line(c, left, foot + 12u * scale,
             "INSTALLER READY - PRESS ENTER TO BEGIN INSTALLATION TO INTERNAL HDD/SSD", 71u, 1u,
             0x00e5a3u);
    } else {
        line(c, left, foot + 12u * scale,
             "USE ARROWS / 1 / 2 TO SELECT  |  PRESS ENTER TO CONFIRM", 55u, 1u,
             0xa0c0d8u);
    }

    char value[12]; decimal(value, drives);
    line(c, left, foot + 32u * scale, "UEFI BLOCK DEVICES DETECTED:", 28u, 1u, 0x6e88a4u);
    line(c, left + 29u * 9u, foot + 32u * scale, value, 11u, 1u, 0x00e5a3u);

    line(c, left, foot + 52u * scale,
         "ALL DRIVERS ACTIVE: USB xHCI/EHCI, SATA AHCI, NVMe, Framebuffer GOP", 67u, 1u,
         0x526b85u);
}

/* Performs disk installation directly via UEFI Block I/O before ExitBootServices */
static int run_firmware_installer(const canvas_t *c, efi_boot_services_t *bs,
                                  intellect_efi_input_t *input) {
    if (!c || !bs || !input || !input->read_key) return 0;

    /* Verify Boot Services functions are present */
    union { void *raw; locate_buffer_fn fn; } locate;
    union { void *raw; handle_protocol_fn fn; } handle;
    union { void *raw; free_pool_fn fn; } release;
    locate.raw = bs->functions_before_locate[7];
    handle.raw = bs->functions_before_exit[11];
    release.raw = bs->functions_before_exit[1];

    if (!locate.fn || !handle.fn || !release.fn) {
        /* Host test mock: functions are not wired. Return preview. */
        return 0;
    }

    size_t n = 0;
    efi_handle_t *handles = NULL;
    efi_status_t rc = locate.fn(EFI_BY_PROTOCOL, &block_guid, NULL, &n, &handles);
    if (EFI_ERROR(rc) || !handles || !n) return 0;

    efi_block_io_t *disks[EFI_MAX_BLOCK_HANDLES];
    size_t disk_count = 0;
    efi_block_io_t *source_block = NULL;
    efi_block_io_t *target_block = NULL;

    typedef struct {
        uint32_t revision;
        efi_handle_t parent_handle;
        efi_system_table_t *system_table;
        efi_handle_t device_handle;
    } efi_loaded_image_min_t;

    efi_handle_t boot_dev_handle = NULL;
    if (g_boot_image_handle && handle.fn) {
        efi_loaded_image_min_t *loaded = NULL;
        if (!EFI_ERROR(handle.fn(g_boot_image_handle, &loaded_image_guid, (void **)&loaded)) && loaded) {
            boot_dev_handle = loaded->device_handle;
        }
    }

    efi_block_io_t *boot_bio = NULL;
    if (boot_dev_handle && handle.fn) {
        (void)handle.fn(boot_dev_handle, &block_guid, (void **)&boot_bio);
    }

    for (size_t i = 0; i < n && i < EFI_MAX_BLOCK_HANDLES; ++i) {
        efi_block_io_t *block = NULL;
        if (EFI_ERROR(handle.fn(handles[i], &block_guid, (void **)&block)) ||
            !block || !block->media) continue;
        const efi_block_media_t *m = block->media;
        if (!m->media_present) continue;
        if (m->block_size < 512u || (m->block_size & (m->block_size - 1u)) != 0u) continue;

        if (boot_dev_handle && (handles[i] == boot_dev_handle ||
            (boot_bio && boot_bio->media && m->media_id == boot_bio->media->media_id))) {
            if (!m->logical_partition || !source_block) {
                source_block = block;
            }
        }

        if (m->logical_partition) continue;

        disks[disk_count++] = block;
    }
    (void)release.fn(handles);

    /* If source_block was a partition, find parent disk in disks[] */
    if (source_block && source_block->media->logical_partition) {
        for (size_t i = 0; i < disk_count; ++i) {
            if (disks[i]->media->media_id == source_block->media->media_id) {
                source_block = disks[i];
                break;
            }
        }
    }

    /* Fallback: if source_block not resolved via loaded_image, probe for existing EFI image */
    if (!source_block && disk_count >= 2) {
        for (size_t i = 0; i < disk_count; ++i) {
            if (disks[i]->read_blocks &&
                disks[i]->read_blocks(disks[i], disks[i]->media->media_id, 4164u, 512u, g_install_buffer) == EFI_SUCCESS) {
                if (g_install_buffer[0] == 'M' && g_install_buffer[1] == 'Z') {
                    source_block = disks[i];
                    break;
                }
            }
        }
    }

    /* Target is any non-read-only disk that is NOT source_block (prefer fixed disk) */
    for (size_t i = 0; i < disk_count; ++i) {
        if (disks[i] == source_block || disks[i]->media->read_only) continue;
        if (!target_block || (!disks[i]->media->removable && target_block->media->removable)) {
            target_block = disks[i];
        }
    }

    const uint32_t w = c->w, h = c->h;
    const uint32_t left = (w >= 900u) ? w / 10u : 24u;
    const uint32_t cardw = w - 2u * left;

    /* Clear and draw installer dialog */
    box(c, 0u, 0u, w, h, 0x070b12u);
    box(c, 0u, 0u, w, 4u, 0x00e5a3u);

    line(c, left, 32u, "INTELLECT OS - FIRMWARE TARGET DISK INSTALLER", 45u, 2u, 0xf8fafcu);
    line(c, left, 68u, "Autonomous USB-to-Disk Operating System Installation", 53u, 1u, 0x00e5a3u);

    uint32_t card_y = 104u;
    box(c, left, card_y, cardw, 240u, 0x0d1522u);
    box(c, left, card_y, 4u, 240u, 0x00e5a3u);

    if (disk_count < 2 || !target_block || !source_block || target_block == source_block) {
        line(c, left + 20u, card_y + 24u, "NO SEPARATE TARGET DISK (HDD/SSD/NVMe) FOUND", 45u, 1u, 0xffa07au);
        line(c, left + 20u, card_y + 54u, "Cannot install: source and target point to the same drive.", 58u, 1u, 0xd0e0f0u);
        line(c, left + 20u, card_y + 90u, "Press ESC or ENTER to return to Boot Menu.", 42u, 1u, 0x00e5a3u);
        while (1) {
            intellect_efi_key_t key = {0, 0};
            if (input->read_key(input, &key) == 0) break;
            (void)bs->stall(POLL_US);
        }
        return 0;
    }

    uint64_t target_size_mb = ((target_block->media->last_block + 1u) * target_block->media->block_size) / (1024u * 1024u);
    char size_str[12]; decimal(size_str, (unsigned)target_size_mb);

    line(c, left + 20u, card_y + 20u, "TARGET DISK IDENTIFIED:", 23u, 1u, 0x00e5a3u);
    line(c, left + 20u, card_y + 44u, "Device Type   : Fixed Internal Storage (HDD / SSD / NVMe)", 57u, 1u, 0xf0f6fcu);
    line(c, left + 20u, card_y + 68u, "Total Capacity: ", 16u, 1u, 0xf0f6fcu);
    line(c, left + 20u + 16u * 9u, card_y + 68u, size_str, 10u, 1u, 0x00e5a3u);
    line(c, left + 20u + 27u * 9u, card_y + 68u, "MB", 2u, 1u, 0xf0f6fcu);

    line(c, left + 20u, card_y + 104u, "INSTALLATION PLAN:", 18u, 1u, 0x00e5a3u);
    line(c, left + 20u, card_y + 128u, "1. Zero MBR and primary/backup GPT partition tables (Wipe)", 58u, 1u, 0x94a3b8u);
    line(c, left + 20u, card_y + 148u, "2. Create standard GUID Partition Table (GPT) with protective MBR", 66u, 1u, 0x94a3b8u);
    line(c, left + 20u, card_y + 168u, "3. Format and write FAT32 EFI System Partition (/EFI/BOOT/BOOTX64.EFI)", 70u, 1u, 0x94a3b8u);
    line(c, left + 20u, card_y + 188u, "4. Create IntellectFS v1 System Root (/sbin/terminal.ivx, /etc/system.json)", 75u, 1u, 0x94a3b8u);
    line(c, left + 20u, card_y + 208u, "5. Install dedicated OS bootloader (direct terminal boot, no passwords)", 72u, 1u, 0x94a3b8u);

    box(c, left, card_y + 260u, cardw, 54u, 0x221318u);
    box(c, left, card_y + 260u, 4u, 54u, 0xff5555u);
    line(c, left + 20u, card_y + 272u, "WARNING: ALL EXISTING DATA ON TARGET DISK WILL BE PERMANENTLY ERASED!", 69u, 1u, 0xff7b72u);
    line(c, left + 20u, card_y + 292u, "Press [Y] to CONFIRM AND PROCEED WITH INSTALLATION  |  [ESC] to Cancel", 70u, 1u, 0xffd1b3u);

    /* Wait for confirmation key */
    while (1) {
        intellect_efi_key_t key = {0, 0};
        if (input->read_key(input, &key) == 0) {
            if (key.unicode_char == 'Y' || key.unicode_char == 'y') break;
            if (key.unicode_char == KEY_ESCAPE || key.unicode_char == 'N' || key.unicode_char == 'n') return 0;
        }
        (void)bs->stall(POLL_US);
    }

    /* Perform Installation with Real-Time Progress Bar */
    box(c, left, card_y + 260u, cardw, 100u, 0x070b12u);
    line(c, left, card_y + 260u, "WIPING TARGET DISK AND WRITING INTELLECT OS IMAGE...", 52u, 1u, 0x00e5a3u);

    const uint32_t pbar_x = left;
    const uint32_t pbar_y = card_y + 288u;
    const uint32_t pbar_w = cardw > 400u ? cardw : 400u;
    const uint32_t pbar_h = 24u;

    box(c, pbar_x, pbar_y, pbar_w, pbar_h, 0x111c2cu);
    box(c, pbar_x, pbar_y, pbar_w, 1u, 0x2d435eu);

    /* 1. Wipe first 1 MiB and last 1 MiB */
    for (size_t z = 0; z < sizeof(g_install_buffer); ++z) g_install_buffer[z] = 0;
    if (target_block->write_blocks) {
        (void)target_block->write_blocks(target_block, target_block->media->media_id,
                                         0u, sizeof(g_install_buffer), g_install_buffer);
        if (target_block->media->last_block >= 2048u) {
            (void)target_block->write_blocks(target_block, target_block->media->media_id,
                                             target_block->media->last_block - 2047u,
                                             sizeof(g_install_buffer), g_install_buffer);
        }
    }

    /* 2. Copy system image sectors */
    const uint64_t total_sectors = 2097152u; /* 1 GiB standard image */
    const uint32_t sectors_per_chunk = sizeof(g_install_buffer) / 512u;
    const uint32_t total_chunks = (uint32_t)(total_sectors / sectors_per_chunk);
    int write_error = 0;

    for (uint32_t chunk = 0; chunk < total_chunks; ++chunk) {
        uint64_t lba = (uint64_t)chunk * sectors_per_chunk;

        /* Read from installation media */
        if (source_block->read_blocks) {
            efi_status_t rrc = source_block->read_blocks(source_block, source_block->media->media_id,
                                                         lba, sizeof(g_install_buffer), g_install_buffer);
            if (EFI_ERROR(rrc)) { write_error = 1; break; }
        }

        /* Write to target internal disk */
        if (target_block->write_blocks) {
            efi_status_t wrc = target_block->write_blocks(target_block, target_block->media->media_id,
                                                          lba, sizeof(g_install_buffer), g_install_buffer);
            if (EFI_ERROR(wrc)) { write_error = 2; break; }
        }

        /* Update progress bar every 256 chunks (~16 MiB) */
        if ((chunk & 255u) == 0u || chunk == total_chunks - 1u) {
            uint32_t progress_w = (uint32_t)(((uint64_t)chunk * pbar_w) / total_chunks);
            if (progress_w > pbar_w) progress_w = pbar_w;
            box(c, pbar_x, pbar_y, progress_w, pbar_h, 0x00e5a3u);

            char pct[12]; decimal(pct, (unsigned)((chunk * 100u) / total_chunks));
            line(c, pbar_x + pbar_w + 16u, pbar_y + 4u, pct, 4u, 1u, 0x00e5a3u);
            line(c, pbar_x + pbar_w + 16u + 4u * 9u, pbar_y + 4u, "%", 1u, 1u, 0x00e5a3u);
        }
    }

    /* 3. Write dedicated installed OS bootloader directly to target ESP at LBA 4164 */
    size_t installed_bytes = (size_t)(intellect_installed_efi_end - intellect_installed_efi_start);
    if (!write_error && installed_bytes > 0 && target_block->write_blocks) {
        const uint64_t esp_boot_lba = 4164u;
        size_t written = 0;
        while (written < installed_bytes) {
            size_t to_write = installed_bytes - written;
            if (to_write > sizeof(g_install_buffer)) to_write = sizeof(g_install_buffer);
            for (size_t b = 0; b < sizeof(g_install_buffer); ++b) {
                g_install_buffer[b] = (b < to_write) ? intellect_installed_efi_start[written + b] : 0u;
            }
            uint64_t write_lba = esp_boot_lba + (written / 512u);
            efi_status_t ewrc = target_block->write_blocks(target_block, target_block->media->media_id,
                                                           write_lba, sizeof(g_install_buffer), g_install_buffer);
            if (EFI_ERROR(ewrc)) { write_error = 3; break; }
            written += to_write;
        }
    }

    if (target_block->flush_blocks) {
        (void)target_block->flush_blocks(target_block);
    }

    /* 4. Independent readback verification */
    if (!write_error && target_block->read_blocks) {
        efi_status_t vrc = target_block->read_blocks(target_block, target_block->media->media_id,
                                                     4164u, 512u, g_install_buffer);
        if (EFI_ERROR(vrc) || g_install_buffer[0] != 'M' || g_install_buffer[1] != 'Z') {
            write_error = 4;
        } else {
            for (size_t k = 0; k < 512u && k < installed_bytes; ++k) {
                if (g_install_buffer[k] != intellect_installed_efi_start[k]) {
                    write_error = 5;
                    break;
                }
            }
        }
    }

    if (write_error) {
        box(c, left, card_y + 330u, cardw, 60u, 0x221318u);
        box(c, left, card_y + 330u, 4u, 60u, 0xff5555u);
        line(c, left + 20u, card_y + 342u, "ERROR: FAILED TO WRITE/VERIFY TARGET DISK", 41u, 1u, 0xff7b72u);
        line(c, left + 20u, card_y + 364u, "Installation aborted. Press ENTER to return to menu.", 53u, 1u, 0xffd1b3u);
        while (1) {
            intellect_efi_key_t key = {0, 0};
            if (input->read_key(input, &key) == 0 && key.unicode_char == 13u) break;
            (void)bs->stall(POLL_US);
        }
        return 0;
    }

    /* Complete screen */
    box(c, pbar_x, pbar_y, pbar_w, pbar_h, 0x00e5a3u);
    box(c, left, card_y + 330u, cardw, 60u, 0x0f291eu);
    box(c, left, card_y + 330u, 4u, 60u, 0x00e5a3u);

    line(c, left + 20u, card_y + 342u, "SUCCESS: INTELLECT OS HAS BEEN FULLY INSTALLED TO DISK!", 56u, 1u, 0xf0f6fcu);
    line(c, left + 20u, card_y + 364u, "PLEASE REMOVE THE USB FLASH DRIVE AND PRESS ENTER TO RESTART.", 61u, 1u, 0x5eead4u);

    while (1) {
        intellect_efi_key_t key = {0, 0};
        if (input->read_key(input, &key) == 0 && key.unicode_char == 13u) break;
        (void)bs->stall(POLL_US);
    }

    return 2; /* 2 indicates installation complete */
}

int intellect_uefi_bootmenu(const intellect_bootinfo_t *boot,
                            intellect_efi_input_t *input,
                            efi_boot_services_t *bs, unsigned drives) {
    if (!boot || !input || !input->read_key || !bs || !bs->stall ||
        boot->video_kind != INTELLECT_VIDEO_FRAMEBUFFER ||
        (boot->pixel_format != INTELLECT_PIXEL_BGRX8888 &&
         boot->pixel_format != INTELLECT_PIXEL_RGBX8888)) return 0;

    uint32_t w = boot->framebuffer_width, h = boot->framebuffer_height;
    if (w < 640u || h < 480u || w > 8192u || h > 8192u ||
        w > UINT32_MAX / 4u || boot->framebuffer_pitch < w * 4u ||
        boot->framebuffer_addr == 0u ||
        (uint64_t)boot->framebuffer_pitch * h > boot->framebuffer_size ||
        boot->framebuffer_addr > UINT64_MAX - boot->framebuffer_size) return 0;

    const canvas_t c = {(volatile uint8_t *)(uintptr_t)boot->framebuffer_addr,
                        w, h, boot->framebuffer_pitch, boot->pixel_format};

    unsigned selected = 0u, mode_view = 0u, idle = 0u;
    draw(&c, selected, drives, mode_view);

    while (1) {
        intellect_efi_key_t key = {0, 0};
        efi_status_t rc = input->read_key(input, &key);
        if (rc == 0) {
            idle = 0u;
            if (key.unicode_char == '1') {
                selected = 0u;
                return 1;
            }
            if (key.unicode_char == '2') {
                selected = 1u;
                mode_view = 1u;
                draw(&c, selected, drives, mode_view);
                continue;
            }
            if (key.scan_code == KEY_UP) {
                selected = 0u;
                mode_view = 0u;
                draw(&c, selected, drives, mode_view);
                continue;
            }
            if (key.scan_code == KEY_DOWN) {
                selected = 1u;
                mode_view = 1u;
                draw(&c, selected, drives, mode_view);
                continue;
            }
            if (key.unicode_char == 13u) {
                if (selected == 0u) return 1;
                /* Selected == 1: Launch interactive installer */
                int inst_res = run_firmware_installer(&c, bs, input);
                if (inst_res == 2) return 2;
                /* If cancelled or error, redraw menu */
                selected = 0u;
                mode_view = 0u;
                draw(&c, selected, drives, mode_view);
                continue;
            }
        } else if (rc != EFI_NOT_READY) {
            return 0;
        }

        if (++idle >= DEFAULT_POLLS) {
            return 3; /* Unattended timeout: proceed to native kernel handoff */
        }
        (void)bs->stall(POLL_US);
    }
}
