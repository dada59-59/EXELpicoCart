#!/usr/bin/env python3
"""make_wizord_db9.py -- fabrique wizord_db9.rom (32 Ko) a partir de la ROM Wizord 8 Ko.
   python3 make_wizord_db9.py wizord_exl100.rom wizord_db9.rom
- $6000-$7FFF : Wizord d'origine, sauf le vecteur timer de jeu ($61AF : $740F -> $5000)
- $5000       : wz_patch (voir wz_patch.asm) et sa table
- $3FE0-$3FE3 : octets manette a $00 (EXLCART les reecrit) ; $3FE4 : "EXLJ"
Sur une vraie EPROM 27C256, l'octet manette reste $00 : Wizord se joue comme avant."""
import sys, hashlib
src = open(sys.argv[1], 'rb').read()
assert len(src) == 0x2000 and src[0x1FFC:0x2000] == bytes([0xAA, 0x8C, 0x60, 0x00]), "ROM Wizord 8 Ko attendue"
assert src[0x01AE:0x01B2] == bytes([0x88, 0x74, 0x0F, 0x01]), "pas la version de Wizord analysee ($61AE)"
assert src[0x039D:0x03A5] == bytes([0x76, 0x08, 0x30, 0x6F, 0x12, 0x03, 0x14, 0x04]), "gestionnaire $639D inattendu"
img = bytearray([0xFF]) * 0x8000
img[0x6000:0x8000] = src
img[0x61AF:0x61B1] = bytes([0x50, 0x00])            # BRTIME de jeu -> wz_tick
TAB = 0x5028
def j(op, at, tgt):                                 # saut relatif court
    o = tgt - (at + 2); assert -128 <= o < 128; return [op, o & 0xFF]
DONE = 0x5025
code = ([0x8A, 0x3F, 0xE0, 0x23, 0x1F, 0xC0, 0xAA, TAB >> 8, TAB & 0xFF, 0x8D, 0xC7, 0xF0]
        + j(0xE2, 0x500C, DONE) + [0x8B, 0xC7, 0xF0, 0xC0, 0x8A, 0xC0, 0x00, 0x2D, 0x63]
        + j(0xE6, 0x5017, DONE) + [0x8A, 0xC0, 0x01, 0x2D, 0x9D]
        + j(0xE6, 0x501E, DONE) + [0xD1, 0x03, 0x8E, 0x63, 0x9D, 0x8C, 0x74, 0x0F])
assert 0x5000 + len(code) == TAB
def tabcode(v):
    for bit, c in ((0x10, 0x20), (0x08, 0x81), (0x04, 0x83), (0x01, 0x80), (0x02, 0x82)):
        if v & bit: return c
    return 0x04
img[0x5000:0x5000 + len(code)] = bytes(code)
img[TAB:TAB + 32] = bytes(tabcode(v) for v in range(32))
img[0x3FE0:0x3FE8] = bytes([0, 0, 0, 0]) + b"EXLJ"
open(sys.argv[2], 'wb').write(img)
print("%s : %d octets, somme %08X" % (sys.argv[2], len(img), sum(img) & 0xFFFFFFFF))
