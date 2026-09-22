#define _GNU_SOURCE
#include "corex_fatbin_runtime.h"

#include <elf.h>
#include <inttypes.h>
#include <link.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define WRAPPER_MAGIC 0x54535a58u      /* XZST */
#define WRAPPER_VERSION 1u
#define FATBIN_MAGIC 0x20160329u
#define FATBIN_VERSION 0x00010000u
#define ILUVATAR_OSABI 0x42u
#define ILUVATAR_MACHINE 0x00f8u
#define MAX_FATBIN_BYTES ((size_t)256u * 1024u * 1024u)

typedef struct {
    uintptr_t object_base;
    uintptr_t seg_begin;
    uintptr_t seg_end;
    char object_name[256];
    int found;
} ObjSpan;

typedef struct {
    uintptr_t begin;
    uintptr_t end;
    ObjSpan out;
} SpanQuery;

static void set_err(char *err, size_t cap, const char *fmt, ...)
{
    if (!err || cap == 0) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, cap, fmt, ap);
    va_end(ap);
}

static uint16_t rd16(const unsigned char *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const unsigned char *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t rd64(const unsigned char *p)
{
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

static int add_uptr(uintptr_t a, size_t b, uintptr_t *out)
{
    if (b > UINTPTR_MAX - a) return 0;
    *out = a + (uintptr_t)b;
    return 1;
}

static int add_u64(uint64_t a, uint64_t b, uint64_t *out)
{
    if (UINT64_MAX - a < b) return 0;
    *out = a + b;
    return 1;
}

static int mul_u64(uint64_t a, uint64_t b, uint64_t *out)
{
    if (a && b > UINT64_MAX / a) return 0;
    *out = a * b;
    return 1;
}

static int span_cb(struct dl_phdr_info *info, size_t info_size, void *opaque)
{
    (void)info_size;
    SpanQuery *q = (SpanQuery *)opaque;

    for (ElfW(Half) i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        if (ph->p_type != PT_LOAD || !(ph->p_flags & PF_R)) continue;

        uintptr_t begin = (uintptr_t)info->dlpi_addr + (uintptr_t)ph->p_vaddr;
        uintptr_t end = 0;
        if (!add_uptr(begin, (size_t)ph->p_filesz, &end)) continue;

        if (q->begin >= begin && q->end <= end) {
            q->out.object_base = (uintptr_t)info->dlpi_addr;
            q->out.seg_begin = begin;
            q->out.seg_end = end;
            snprintf(q->out.object_name, sizeof(q->out.object_name), "%s",
                     (info->dlpi_name && info->dlpi_name[0]) ? info->dlpi_name : "<main>");
            q->out.found = 1;
            return 1;
        }
    }
    return 0;
}

static int locate_file_backed_span(const void *ptr, size_t len, ObjSpan *out,
                                   char *err, size_t err_cap)
{
    if (!ptr || !out || len == 0) {
        set_err(err, err_cap, "invalid span query");
        return 0;
    }

    uintptr_t begin = (uintptr_t)ptr;
    uintptr_t end = 0;
    if (!add_uptr(begin, len, &end)) {
        set_err(err, err_cap, "span overflow");
        return 0;
    }

    SpanQuery q;
    memset(&q, 0, sizeof(q));
    q.begin = begin;
    q.end = end;
    dl_iterate_phdr(span_cb, &q);

    if (!q.out.found) {
        set_err(err, err_cap,
                "span [%p,+%zu) not inside readable file-backed PT_LOAD", ptr, len);
        return 0;
    }

    *out = q.out;
    return 1;
}

static int same_object(const ObjSpan *a, const ObjSpan *b)
{
    return a->object_base == b->object_base &&
           strcmp(a->object_name, b->object_name) == 0;
}

static int range_end(uint64_t off, uint64_t len, uint64_t limit, uint64_t *end)
{
    if (!add_u64(off, len, end)) return 0;
    return *end <= limit;
}

static int valid_iluvatar_elf(const unsigned char *base, size_t avail, size_t *exact)
{
    if (avail < 64) return 0;
    if (memcmp(base, "\x7f" "ELF", 4) != 0) return 0;
    if (base[EI_CLASS] != ELFCLASS64 || base[EI_DATA] != ELFDATA2LSB ||
        base[EI_VERSION] != EV_CURRENT || base[EI_OSABI] != ILUVATAR_OSABI)
        return 0;
    if (rd16(base + 18) != ILUVATAR_MACHINE || rd32(base + 20) != EV_CURRENT)
        return 0;

    uint64_t phoff = rd64(base + 32), shoff = rd64(base + 40);
    uint16_t ehsize = rd16(base + 52), phentsize = rd16(base + 54), phnum = rd16(base + 56);
    uint16_t shentsize = rd16(base + 58), shnum = rd16(base + 60);

    if (ehsize != 64 || (phnum && phentsize < 56) || (shnum && shentsize < 64))
        return 0;

    uint64_t end = ehsize, bytes = 0, table_end = 0;
    if (phnum) {
        if (!mul_u64(phentsize, phnum, &bytes) || !range_end(phoff, bytes, avail, &table_end))
            return 0;
        if (table_end > end) end = table_end;
    }
    if (shnum) {
        if (!mul_u64(shentsize, shnum, &bytes) || !range_end(shoff, bytes, avail, &table_end))
            return 0;
        if (table_end > end) end = table_end;
    }

    for (uint16_t i = 0; i < phnum; ++i) {
        const unsigned char *ph = base + phoff + (uint64_t)i * phentsize;
        uint64_t off = rd64(ph + 8), filesz = rd64(ph + 32), e = 0;
        if (!range_end(off, filesz, avail, &e)) return 0;
        if (e > end) end = e;
    }
    for (uint16_t i = 0; i < shnum; ++i) {
        const unsigned char *sh = base + shoff + (uint64_t)i * shentsize;
        uint32_t type = rd32(sh + 4);
        uint64_t off = rd64(sh + 24), size = rd64(sh + 32), e = 0;
        if (type == SHT_NOBITS) continue;
        if (!range_end(off, size, avail, &e)) return 0;
        if (e > end) end = e;
    }

    if (end < 64 || end > avail || end > SIZE_MAX) return 0;
    *exact = (size_t)end;
    return 1;
}

static int extract_one(const unsigned char *fatbin, size_t fatbin_size,
                       size_t *off_out, size_t *size_out,
                       char *err, size_t err_cap)
{
    size_t count = 0, found_off = 0, found_size = 0;

    for (size_t off = 0; off + 4 <= fatbin_size; ++off) {
        if (memcmp(fatbin + off, "\x7f" "ELF", 4) != 0) continue;
        size_t exact = 0;
        if (!valid_iluvatar_elf(fatbin + off, fatbin_size - off, &exact)) continue;
        ++count;
        found_off = off;
        found_size = exact;
        if (exact && off + exact > off) off += exact - 1;
    }

    if (count == 0) {
        set_err(err, err_cap, "no valid Iluvatar ELF64 in bounded fatbin");
        return 0;
    }
    if (count != 1) {
        set_err(err, err_cap, "ambiguous fatbin: %zu valid Iluvatar ELF64 images", count);
        return 0;
    }

    *off_out = found_off;
    *size_out = found_size;
    return 1;
}

int corex_image_from_compiler_wrapper(const void *wrapper,
                                      CorexLoadableImage *out,
                                          char *err, size_t err_cap)
{
    if (err && err_cap) err[0] = '\0';
    if (!wrapper || !out) {
        set_err(err, err_cap, "null wrapper/out");
        return -1;
    }
    memset(out, 0, sizeof(*out));

    ObjSpan wrapper_obj;
    if (!locate_file_backed_span(wrapper, 24, &wrapper_obj, err, err_cap)) return -2;

    const unsigned char *w = (const unsigned char *)wrapper;
    if (rd32(w) != WRAPPER_MAGIC || rd32(w + 4) != WRAPPER_VERSION) {
        set_err(err, err_cap, "CoreX wrapper mismatch magic=0x%08x version=%u",
                rd32(w), rd32(w + 4));
        return -3;
    }

    const unsigned char *fatbin = NULL;
    memcpy(&fatbin, w + 8, sizeof(fatbin));
    if (!fatbin) {
        set_err(err, err_cap, "wrapper payload pointer is null");
        return -4;
    }

    ObjSpan header_obj;
    if (!locate_file_backed_span(fatbin, 24, &header_obj, err, err_cap)) return -5;
    if (!same_object(&wrapper_obj, &header_obj)) {
        set_err(err, err_cap, "wrapper and payload belong to different ELF objects");
        return -6;
    }

    if (rd32(fatbin) != FATBIN_MAGIC || rd32(fatbin + 4) != FATBIN_VERSION) {
        set_err(err, err_cap, "CoreX fatbin header mismatch magic=0x%08x version=0x%08x",
                rd32(fatbin), rd32(fatbin + 4));
        return -7;
    }

    uint64_t total64 = 0;
    if (!add_u64(rd64(fatbin + 8), 16, &total64) || total64 < 24 ||
        total64 > MAX_FATBIN_BYTES || total64 > SIZE_MAX) {
        set_err(err, err_cap, "invalid observed CoreX-4.4 fatbin span=%" PRIu64, total64);
        return -8;
    }

    size_t fatbin_size = (size_t)total64;
    ObjSpan full_obj;
    if (!locate_file_backed_span(fatbin, fatbin_size, &full_obj, err, err_cap)) return -9;
    if (!same_object(&wrapper_obj, &full_obj)) {
        set_err(err, err_cap, "full fatbin span belongs to different ELF object");
        return -10;
    }

    size_t image_off = 0, image_size = 0;
    if (!extract_one(fatbin, fatbin_size, &image_off, &image_size, err, err_cap)) return -11;

    out->fatbin_bytes = fatbin;
    out->fatbin_size = fatbin_size;
    out->image_bytes = fatbin + image_off;
    out->image_size = image_size;
    out->image_offset = image_off;
    out->object_base = wrapper_obj.object_base;
    snprintf(out->object_name, sizeof(out->object_name), "%s", wrapper_obj.object_name);
    return 0;
}
