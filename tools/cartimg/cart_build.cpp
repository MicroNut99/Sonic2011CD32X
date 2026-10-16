// PORT32X S6a: the console cart image builder.
//   cart_build --check                     layout self-check (field coverage, padding, round trip)
//   cart_build <native image> <out.bin>     one stage: PC image (written by the host build after a
//                                          stage load, SCD32X_CARTIMG_OUT) -> big-endian SH2 image
// SH2 image: "SCCB", u32 version 2, u32 part count, then per part 5 x u32 (offset, bytes, element
// size, elements, CRC32 of the part's bytes - S6b), then the parts, each 16-byte aligned. All numbers big-endian. Parts that are
// arrays of many elements are trimmed after their last non-zero element (the SH2 reads only what
// the scripts and loaders used); single-struct parts (chunks, collision) are kept whole, because
// their fields sit at fixed offsets inside the struct. S6a2: the chunk part is written as separate
// trimmed arrays (u32 n, u16 tileIndex[n], u8 direction[n], plane[n], solidity A[n], B[n]).
#include "cart_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

struct Field {
    int kind, offset, size, count;
};
struct Element {
    int kind, size;
};
static const Field fields[] = {
#define F(kind, type, off, size, count) { kind, (int)(off), size, (int)(count) },
    SCD_CART_FIELDS(F)
#undef F
};
static const Element elements[] = {
#define E(kind, type) { kind, (int)sizeof(type) },
    SCD_CART_ELEMENTS(E)
#undef E
};
static const int nFields   = sizeof(fields) / sizeof(fields[0]);
static const int nElements = sizeof(elements) / sizeof(elements[0]);
static const char *partNames[SCD_CART_COUNT] = { "scriptcode", "jumptable", "chunks", "collision", "scriptframes", "animframes", "animlist",
                                                 "arctan",     "stagelist", "typenames", "globalnames", "animfiles", "font", "trig",
                                                 "gfxdata",    "tilegfx",   "blend" };

static int element_size(int kind)
{
    for (int i = 0; i < nElements; ++i)
        if (elements[i].kind == kind)
            return elements[i].size;
    return 0;
}

// swap every multi-byte scalar of every element (the same call converts both ways)
static void swap_part(int kind, unsigned char *p, size_t bytes)
{
    int es = element_size(kind);
    for (size_t e = 0; e + es <= bytes; e += es)
        for (int f = 0; f < nFields; ++f) {
            if (fields[f].kind != kind || fields[f].size == 1)
                continue;
            unsigned char *q = p + e + fields[f].offset;
            for (int c = 0; c < fields[f].count; ++c, q += fields[f].size) {
                if (fields[f].size == 2) {
                    unsigned char t = q[0];
                    q[0]            = q[1];
                    q[1]            = t;
                }
                else {
                    unsigned char t0 = q[0], t1 = q[1];
                    q[0] = q[3], q[1] = q[2], q[2] = t1, q[3] = t0;
                }
            }
        }
}

static int check()
{
    int bad = 0;
    for (int k = 0; k < SCD_CART_COUNT; ++k) {
        int es = element_size(k);
        if (!es) {
            printf("part %s: no element type\n", partNames[k]);
            bad++;
            continue;
        }
        std::vector<int> cover(es, 0);
        for (int f = 0; f < nFields; ++f)
            if (fields[f].kind == k)
                for (int b = 0; b < fields[f].size * fields[f].count; ++b)
                    if (fields[f].offset + b < es)
                        cover[fields[f].offset + b]++;
                    else
                        bad++, printf("part %s: field at %d runs past the element (%d bytes)\n", partNames[k], fields[f].offset, es);
        int pad = 0;
        for (int b = 0; b < es; ++b) {
            if (cover[b] > 1)
                bad++, printf("part %s: byte %d covered twice\n", partNames[k], b);
            pad += cover[b] == 0;
        }
        printf("  %-12s element %6d bytes, padding %d\n", partNames[k], es, pad);
    }
    // round trip: swapping twice gives the original
    for (int k = 0; k < SCD_CART_COUNT; ++k) {
        int es = element_size(k) * 3;
        std::vector<unsigned char> a(es), b;
        for (int i = 0; i < es; ++i) a[i] = (unsigned char)(i * 37 + k);
        b = a;
        swap_part(k, b.data(), b.size());
        swap_part(k, b.data(), b.size());
        if (a != b)
            bad++, printf("part %s: round trip differs\n", partNames[k]);
    }
    // a known value: the first int of the script code, 0x11223344, must become 11 22 33 44
    {
        int v = 0x11223344;
        unsigned char b[4];
        memcpy(b, &v, 4);
        swap_part(SCD_CART_SCRIPTCODE, b, 4);
        if (b[0] != 0x11 || b[3] != 0x44)
            bad++, printf("big-endian conversion wrong\n");
    }
    printf("layout self-check: %s\n", bad ? "FAILED" : "OK");
    return bad ? 1 : 0;
}

// the standard CRC32 (reflected, 0xEDB88320) - md/cart_verify.c computes the same on the SH2
static unsigned crc32_of(const unsigned char *p, size_t n)
{
    unsigned c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

static void put32(std::vector<unsigned char> &v, size_t at, unsigned x)
{
    v[at] = (unsigned char)(x >> 24), v[at + 1] = (unsigned char)(x >> 16), v[at + 2] = (unsigned char)(x >> 8), v[at + 3] = (unsigned char)x;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--check"))
        return check();
    if (argc != 3) {
        fprintf(stderr, "usage: cart_build --check | cart_build <native image> <out.bin>\n");
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f)
        return fprintf(stderr, "cannot open %s\n", argv[1]), 1;
    std::vector<unsigned char> in;
    {
        unsigned char buf[65536];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) in.insert(in.end(), buf, buf + n);
        fclose(f);
    }
    if (in.size() < 4 || memcmp(in.data(), "SCCI", 4))
        return fprintf(stderr, "%s: not a PC cart image\n", argv[1]), 1;
    size_t pos = 4;
    std::vector<std::vector<unsigned char>> parts(SCD_CART_COUNT);
    for (int k = 0; k < SCD_CART_COUNT; ++k) {
        unsigned len;
        if (pos + 4 > in.size())
            return fprintf(stderr, "%s: truncated\n", argv[1]), 1;
        memcpy(&len, &in[pos], 4);
        pos += 4;
        if (pos + len > in.size())
            return fprintf(stderr, "%s: truncated\n", argv[1]), 1;
        parts[k].assign(in.begin() + pos, in.begin() + pos + len);
        pos += len;
    }
    std::string stage = "?";
    if (pos + 4 <= in.size() && !memcmp(&in[pos], "STAG", 4))
        stage.assign((const char *)&in[pos + 4], strnlen((const char *)&in[pos + 4], in.size() - pos - 4));

    std::vector<unsigned char> out(12 + 20 * SCD_CART_COUNT, 0);
    memcpy(out.data(), "SCCB", 4);
    put32(out, 4, 2);
    put32(out, 8, SCD_CART_COUNT);
    printf("%-14s", stage.c_str());
    for (int k = 0; k < SCD_CART_COUNT; ++k) {
        int es      = element_size(k);
        size_t n    = parts[k].size() / es;
        size_t used = n;
        if (n > 2) { // trim after the last non-zero element
            used = 0;
            for (size_t e = 0; e < n; ++e)
                for (int b = 0; b < es; ++b)
                    if (parts[k][e * es + b]) {
                        used = e + 1;
                        break;
                    }
        }
        std::vector<unsigned char> p(parts[k].begin(), parts[k].begin() + used * es);
        swap_part(k, p.data(), p.size());
        if (k == SCD_CART_CHUNKS) { // S6a2: separate trimmed arrays, no gfxDataPos (it is tileIndex << 8)
            const Tiles128x128 *t = (const Tiles128x128 *)parts[k].data();
            unsigned c            = 0;
            for (int i = 0; i < CHUNKTILE_COUNT; ++i)
                if (t->tileIndex[i] || t->direction[i] || t->visualPlane[i] || t->collisionFlags[0][i] || t->collisionFlags[1][i])
                    c = (unsigned)i + 1;
            p.assign(4, 0);
            p[0] = (unsigned char)(c >> 24), p[1] = (unsigned char)(c >> 16), p[2] = (unsigned char)(c >> 8), p[3] = (unsigned char)c;
            for (unsigned i = 0; i < c; ++i) p.push_back((unsigned char)(t->tileIndex[i] >> 8)), p.push_back((unsigned char)t->tileIndex[i]);
            for (unsigned i = 0; i < c; ++i) p.push_back(t->direction[i]);
            for (unsigned i = 0; i < c; ++i) p.push_back(t->visualPlane[i]);
            for (unsigned i = 0; i < c; ++i) p.push_back(t->collisionFlags[0][i]);
            for (unsigned i = 0; i < c; ++i) p.push_back(t->collisionFlags[1][i]);
            es = 1, used = p.size();
        }
        while (out.size() % 16) out.push_back(0);
        put32(out, 12 + 20 * k, (unsigned)out.size());
        put32(out, 16 + 20 * k, (unsigned)p.size());
        put32(out, 20 + 20 * k, (unsigned)es);
        put32(out, 24 + 20 * k, (unsigned)used);
        put32(out, 28 + 20 * k, crc32_of(p.data(), p.size()));
        out.insert(out.end(), p.begin(), p.end());
        printf(" %s %zu", partNames[k], p.size());
    }
    printf(" total %zu\n", out.size());
    f = fopen(argv[2], "wb");
    if (!f)
        return fprintf(stderr, "cannot write %s\n", argv[2]), 1;
    fwrite(out.data(), 1, out.size(), f);
    fclose(f);
    return 0;
}
