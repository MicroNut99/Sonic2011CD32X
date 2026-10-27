// REV R4C: Sonic CD's save data in the Sega CD's backup RAM (MicroNut99's option 2): the Sub-CPU's file SONICCD32X_,
// 512 bytes = 256 words (cd32x/sub_main.c, commands 56 load / 57 get a word / 58 put a word / 59 save).
//
// The game keeps everything that lasts in its save slots 0-173 (measured on the PC with SCD32X_SAVELOG over the title,
// the menu, save select, time attack, the stage select, sound test, D.A. Garden, help, the credits and Tails's unlock
// screen; slots 7168 on are the stages' scratch, never saved):
//     0-31   four save files, 8 slots each - slot 2 the score and slot 6 the next extra life's score need 32 bits
//    32-47   options (32, 35, 36, 38, 39 used; 33 / 34 the engine's volumes)
//    48-173  the time attack table: 7 zones x 3 acts x 3 records, each a time in hundredths of a second (at most
//            9'59"99 = 59,999: unsigned 16 bits) and a word beside it
// Packed (big-endian words): 0 'SC', 1 version 1, 2 a checksum of the words after it, then slots 0-173 in order - the
// two 32-bit slots of each save file as two words (high, low), the time attack times unsigned 16-bit, every other slot
// a signed 16-bit word: 3 + 174 + 8 = 185 words. A value outside its word's range is clamped (none seen).
// Plain C++, no engine dependencies: the SH2 engine (plat_mars.cpp) and the PC check (host/plat_host.cpp) include it.
#ifndef SCD_SAVE_H
#define SCD_SAVE_H
#include <stdint.h>

/* REV R6 (MicroNut99, 26 Oct 2026: "find a way to make the min save possible using the SegaCD's.  If you have to
   split them into more than one game save, if that work") - SCD_SAVE_MIN=1 splits the one 8-block file into the two
   smallest files that hold it, so that what must never fail is as small as the BIOS can allocate and stands alone in
   a backup RAM that other games have filled (docs/R5_SAVE_MINIMUM.md):

     file 0  SONICCD32X_   2 blocks (128 bytes)  header + the four save files + the options (59 words used of 64)
     file 1  SONICCD32XT   5 blocks (320 bytes)  header + the time attack table (129 words used of 160)

   7 blocks instead of 8, and the progress survives a backup RAM too full for the records.  Each file has its own
   header and checksum, and file 1 is written only when the records change: a failed write of the records leaves the
   progress as it was.  A version-1 file (the single 8-block one) is still read and carried over once (migration,
   scd_save_unpack_v1) so nobody loses a save to the new layout.  COMM2 of commands 56 / 59 carries the file number
   (cd32x/sub_main.c).  Without the switch everything below is exactly as it was. */
#if defined(SCD_SAVE_MIN) && SCD_SAVE_MIN
#define SCD_SAVE_FILES 2
#define SCD_SAVE_WORDS 64                 /* file 0: 2 blocks */
#define SCD_SAVE_WORDS_T 160              /* file 1: 5 blocks */
#define SCD_SAVE_SPLIT_SLOT 48            /* slots 0-47 in file 0, 48-173 in file 1 */
#define SCD_SAVE_VERSION 2
#else
#define SCD_SAVE_FILES 1
#define SCD_SAVE_WORDS 256
#define SCD_SAVE_VERSION 1
#endif
#define SCD_SAVE_SLOTS 174
#define SCD_SAVE_MAGIC 0x5343 /* 'SC' */
#define SCD_SAVE_V1_WORDS 256                 /* the version-1 file, for the migration */

enum { SCD_SAVE_S16, SCD_SAVE_U16, SCD_SAVE_S32 };
static inline int scd_save_kind(int slot)
{
    if (slot < 32)
        return ((slot & 7) == 2 || (slot & 7) == 6) ? SCD_SAVE_S32 : SCD_SAVE_S16;
    if (slot >= 48 && ((slot - 48) & 1) == 0)
        return SCD_SAVE_U16;
    return SCD_SAVE_S16;
}
static inline uint16_t scd_save_sum(const uint16_t *w, int n)
{
    uint32_t a = 0x5AC3, b = 0;
    for (int i = 0; i < n; ++i) {
        a += w[i];
        b += a;
    }
    return (uint16_t)(a ^ (b >> 3) ^ (b << 7));
}
#if SCD_SAVE_FILES == 1  /* REV R6: with SCD_SAVE_MIN these would pack 185 words into 64 - use the _f pair */
/* the save slots -> the file's words; returns how many words are used (the rest are 0) */
static inline int scd_save_pack(const int *ram, uint16_t *w)
{
    int n = 3;
    for (int s = 0; s < SCD_SAVE_SLOTS; ++s) {
        int v = ram[s];
        switch (scd_save_kind(s)) {
            case SCD_SAVE_S32:
                w[n++] = (uint16_t)((uint32_t)v >> 16);
                w[n++] = (uint16_t)v;
                break;
            case SCD_SAVE_U16: w[n++] = (uint16_t)(v < 0 ? 0 : v > 0xFFFF ? 0xFFFF : v); break;
            default: w[n++] = (uint16_t)(int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v); break;
        }
    }
    for (int i = n; i < SCD_SAVE_WORDS; ++i) w[i] = 0;
    w[0] = SCD_SAVE_MAGIC;
    w[1] = SCD_SAVE_VERSION;
    w[2] = scd_save_sum(w + 3, SCD_SAVE_WORDS - 3);
    return n;
}
/* the file's words -> the save slots; false (slots unchanged) when the words are not a save of this version */
static inline bool scd_save_unpack(const uint16_t *w, int *ram)
{
    if (w[0] != SCD_SAVE_MAGIC || w[1] != SCD_SAVE_VERSION || w[2] != scd_save_sum(w + 3, SCD_SAVE_WORDS - 3))
        return false;
    int n = 3;
    for (int s = 0; s < SCD_SAVE_SLOTS; ++s) {
        switch (scd_save_kind(s)) {
            case SCD_SAVE_S32:
                ram[s] = (int)((uint32_t)w[n] << 16 | w[n + 1]);
                n += 2;
                break;
            case SCD_SAVE_U16: ram[s] = w[n++]; break;
            default: ram[s] = (int16_t)w[n++]; break;
        }
    }
    return true;
}
#endif
#if SCD_SAVE_FILES == 2
/* REV R6: the version-2 files' checksum.  scd_save_sum (version 1's, above, unchanged) lets some single-bit flips
   through - its final XOR of a and b can cancel them (found with 200 random files, 26 Oct 2026).  The new files are a
   new version, so they use FNV-1a folded to 16 bits, which catches a flipped bit with probability 1 - 2^-16. */
static inline uint16_t scd_save_sum2(const uint16_t *w, int n)
{
    uint32_t h = 0x811C9DC5u;
    for (int i = 0; i < n; ++i) {
        h ^= w[i];
        h *= 0x01000193u;
    }
    return (uint16_t)(h ^ (h >> 16));
}
/* REV R6: the words of one file.  file 0: slots 0..SCD_SAVE_SPLIT_SLOT-1, file 1: the rest.  Returns the words used. */
static inline int scd_save_words_of(int file) { return file ? SCD_SAVE_WORDS_T : SCD_SAVE_WORDS; }
static inline int scd_save_slot_first(int file) { return file ? SCD_SAVE_SPLIT_SLOT : 0; }
static inline int scd_save_slot_last(int file) { return file ? SCD_SAVE_SLOTS : SCD_SAVE_SPLIT_SLOT; }
static inline int scd_save_pack_f(int file, const int *ram, uint16_t *w)
{
    const int words = scd_save_words_of(file);
    int n = 3;
    for (int s = scd_save_slot_first(file); s < scd_save_slot_last(file); ++s) {
        int v = ram[s];
        switch (scd_save_kind(s)) {
            case SCD_SAVE_S32:
                w[n++] = (uint16_t)((uint32_t)v >> 16);
                w[n++] = (uint16_t)v;
                break;
            case SCD_SAVE_U16: w[n++] = (uint16_t)(v < 0 ? 0 : v > 0xFFFF ? 0xFFFF : v); break;
            default: w[n++] = (uint16_t)(int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v); break;
        }
    }
    for (int i = n; i < words; ++i) w[i] = 0;
    w[0] = (uint16_t)(SCD_SAVE_MAGIC + file);          /* 'SC' / 'SD': a file read as the other one is rejected */
    w[1] = SCD_SAVE_VERSION;
    w[2] = scd_save_sum2(w + 3, words - 3);
    return n;
}
static inline bool scd_save_unpack_f(int file, const uint16_t *w, int *ram)
{
    const int words = scd_save_words_of(file);
    if (w[0] != (uint16_t)(SCD_SAVE_MAGIC + file) || w[1] != SCD_SAVE_VERSION ||
        w[2] != scd_save_sum2(w + 3, words - 3))
        return false;                                  /* the other slots are left as they are */
    int n = 3;
    for (int s = scd_save_slot_first(file); s < scd_save_slot_last(file); ++s) {
        switch (scd_save_kind(s)) {
            case SCD_SAVE_S32:
                ram[s] = (int)((uint32_t)w[n] << 16 | w[n + 1]);
                n += 2;
                break;
            case SCD_SAVE_U16: ram[s] = w[n++]; break;
            default: ram[s] = (int16_t)w[n++]; break;
        }
    }
    return true;
}
/* REV R6 migration: the version-1 file (one file, 256 words, every slot) - read once, then written as the two */
static inline bool scd_save_unpack_v1(const uint16_t *w, int *ram)
{
    if (w[0] != SCD_SAVE_MAGIC || w[1] != 1 || w[2] != scd_save_sum(w + 3, SCD_SAVE_V1_WORDS - 3))
        return false;
    int n = 3;
    for (int s = 0; s < SCD_SAVE_SLOTS; ++s) {
        switch (scd_save_kind(s)) {
            case SCD_SAVE_S32:
                ram[s] = (int)((uint32_t)w[n] << 16 | w[n + 1]);
                n += 2;
                break;
            case SCD_SAVE_U16: ram[s] = w[n++]; break;
            default: ram[s] = (int16_t)w[n++]; break;
        }
    }
    return true;
}
#endif
#endif
