// PORT32X S6a: the layout of every cart image part, in one place.
// Compiled by the PC image builder, by the PC layout probe and by sh-elf-g++ (the SH2 layout probe),
// so all three use the same description. Each entry is one field of the part's element type:
//   X(kind, element type, field offset, scalar size, scalar count)
// Scalars of size 2 and 4 are byte-swapped for the big-endian SH2; size-1 fields are copied.
// Every byte of an element must be covered by exactly one field (the PC check proves it), so any
// padding would show up - and the SH2 probe proves the SH2 compiler agrees on sizes and offsets.
#ifndef SCD_CART_LAYOUT_H
#define SCD_CART_LAYOUT_H
#include <stddef.h>
#include "../../engine/RetroEngine.hpp"

#define SCD_N(type, member) (int)(sizeof(((type *)0)->member) / sizeof(((type *)0)->member[0]))
#define SCD_S(type, member) (int)sizeof(((type *)0)->member[0])

// element type of each part (count = cart_size(kind) / sizeof(element))
#define SCD_CART_ELEMENTS(E)                     \
    E(SCD_CART_SCRIPTCODE, int)                  \
    E(SCD_CART_JUMPTABLE, int)                   \
    E(SCD_CART_CHUNKS, Tiles128x128)             \
    E(SCD_CART_COLLISION, CollisionMasks)        \
    E(SCD_CART_SCRIPTFRAMES, SpriteFrame)        \
    E(SCD_CART_ANIMFRAMES, SpriteFrame)          \
    E(SCD_CART_ANIMLIST, SpriteAnimation)        \
    E(SCD_CART_ARCTAN, byte)                     \
    E(SCD_CART_STAGELIST, SceneInfo)             \
    E(SCD_CART_TYPENAMES, char)                  \
    E(SCD_CART_GLOBALNAMES, char)                \
    E(SCD_CART_ANIMFILES, AnimationFile)         \
    E(SCD_CART_FONT, FontCharacter)              \
    E(SCD_CART_TRIG, int)                        \
    E(SCD_CART_GFXDATA, byte)                    \
    E(SCD_CART_TILEGFX, byte)                    \
    E(SCD_CART_BLEND, ushort)

// the fields of each element type
#define SCD_CART_FIELDS(X)                                                                                            \
    X(SCD_CART_SCRIPTCODE, int, 0, 4, 1)                                                                               \
    X(SCD_CART_JUMPTABLE, int, 0, 4, 1)                                                                                \
    X(SCD_CART_CHUNKS, Tiles128x128, offsetof(Tiles128x128, gfxDataPos), 4, SCD_N(Tiles128x128, gfxDataPos))           \
    X(SCD_CART_CHUNKS, Tiles128x128, offsetof(Tiles128x128, tileIndex), 2, SCD_N(Tiles128x128, tileIndex))             \
    X(SCD_CART_CHUNKS, Tiles128x128, offsetof(Tiles128x128, direction), 1, SCD_N(Tiles128x128, direction))             \
    X(SCD_CART_CHUNKS, Tiles128x128, offsetof(Tiles128x128, visualPlane), 1, SCD_N(Tiles128x128, visualPlane))         \
    X(SCD_CART_CHUNKS, Tiles128x128, offsetof(Tiles128x128, collisionFlags), 1, (int)sizeof(((Tiles128x128 *)0)->collisionFlags)) \
    X(SCD_CART_COLLISION, CollisionMasks, offsetof(CollisionMasks, floorMasks), 1, SCD_N(CollisionMasks, floorMasks)) \
    X(SCD_CART_COLLISION, CollisionMasks, offsetof(CollisionMasks, lWallMasks), 1, SCD_N(CollisionMasks, lWallMasks)) \
    X(SCD_CART_COLLISION, CollisionMasks, offsetof(CollisionMasks, rWallMasks), 1, SCD_N(CollisionMasks, rWallMasks)) \
    X(SCD_CART_COLLISION, CollisionMasks, offsetof(CollisionMasks, roofMasks), 1, SCD_N(CollisionMasks, roofMasks))   \
    X(SCD_CART_COLLISION, CollisionMasks, offsetof(CollisionMasks, angles), 4, SCD_N(CollisionMasks, angles))         \
    X(SCD_CART_COLLISION, CollisionMasks, offsetof(CollisionMasks, flags), 1, SCD_N(CollisionMasks, flags))           \
    X(SCD_CART_SCRIPTFRAMES, SpriteFrame, offsetof(SpriteFrame, sprX), 4, 6)                                           \
    X(SCD_CART_SCRIPTFRAMES, SpriteFrame, offsetof(SpriteFrame, sheetID), 1, 2)                                        \
    X(SCD_CART_ANIMFRAMES, SpriteFrame, offsetof(SpriteFrame, sprX), 4, 6)                                             \
    X(SCD_CART_ANIMFRAMES, SpriteFrame, offsetof(SpriteFrame, sheetID), 1, 2)                                          \
    X(SCD_CART_ANIMLIST, SpriteAnimation, offsetof(SpriteAnimation, name), 1, 16 + 4)                                  \
    X(SCD_CART_ANIMLIST, SpriteAnimation, offsetof(SpriteAnimation, frameListOffset), 4, 1)                            \
    X(SCD_CART_ARCTAN, byte, 0, 1, 1)                                                                                  \
    X(SCD_CART_STAGELIST, SceneInfo, 0, 1, 3 * 0x40 + 1)                                                               \
    X(SCD_CART_TYPENAMES, char, 0, 1, 1)                                                                               \
    X(SCD_CART_GLOBALNAMES, char, 0, 1, 1)                                                                             \
    X(SCD_CART_ANIMFILES, AnimationFile, offsetof(AnimationFile, fileName), 1, 0x20)                                   \
    X(SCD_CART_ANIMFILES, AnimationFile, offsetof(AnimationFile, animCount), 4, 3)                                     \
    X(SCD_CART_FONT, FontCharacter, offsetof(FontCharacter, id), 4, 1)                                                 \
    X(SCD_CART_FONT, FontCharacter, offsetof(FontCharacter, srcX), 2, 7)                                               \
    X(SCD_CART_TRIG, int, 0, 4, 1)                                                                                     \
    X(SCD_CART_GFXDATA, byte, 0, 1, 1)                                                                                 \
    X(SCD_CART_TILEGFX, byte, 0, 1, 1)                                                                                 \
    X(SCD_CART_BLEND, ushort, 0, 2, 1)

// the numbers both compilers must agree on: per element type its size, per field its offset
#define SCD_LAYOUT_ELEMENT_INT(kind, type) (int)(kind), (int)sizeof(type),
#define SCD_LAYOUT_FIELD_INT(kind, type, off, size, count) (int)(kind), (int)(off), (int)(size), (int)(count),
#define SCD_LAYOUT_PROBE_VALUES SCD_CART_ELEMENTS(SCD_LAYOUT_ELEMENT_INT) - 1, SCD_CART_FIELDS(SCD_LAYOUT_FIELD_INT) - 2

#endif
