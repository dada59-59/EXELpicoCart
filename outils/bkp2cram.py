#!/usr/bin/env python3
"""bkp2cram.py -- sauvegarde Exelmemoire .BKP -> image .cram ($8000-$BFFF, DCEXEL, Jexel),
et l'inverse.
    python3 bkp2cram.py CB5QUAD.BKP cb5quad.cram
    python3 bkp2cram.py --inverse cb5quad.cram CB5QUAD.BKP

BKP : 512 octets d'en-tete, puis 64 blocs [01 00][256 octets] ; les octets se
suivent de $BFFF vers $8000 (memoire lue en descendant) ; le reste du fichier est
du bourrage. Dans l'autre sens, l'en-tete est celui des sauvegardes fournies
(FD 00 00 40 01 ... 01 21), le bourrage est fait de zeros (fichier de 17 408 octets,
comme EXLPICO)."""
import sys
BKP_HEAD = bytes([0xFD, 0x00, 0x00, 0x40, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x21])
def bkp2cram(d):
    if len(d) < 0x200 + 64 * 258: raise ValueError('BKP trop court')
    s = bytearray()
    for k in range(64):
        o = 0x200 + k * 258
        if d[o] != 1 or d[o+1] != 0: raise ValueError('bloc %d : en-tete %02X %02X' % (k, d[o], d[o+1]))
        s += d[o+2:o+258]
    return bytes(reversed(s))
def cram2bkp(c):
    if len(c) != 0x4000: raise ValueError('une image .cram fait 16 384 octets')
    r = bytes(reversed(c)); d = bytearray(0x4400); d[:16] = BKP_HEAD
    for k in range(64):
        o = 0x200 + k * 258
        d[o] = 1; d[o+2:o+258] = r[k*256:k*256+256]
    return bytes(d)
if __name__ == '__main__':
    a = sys.argv[1:]
    if a and a[0] == '--inverse': open(a[2], 'wb').write(cram2bkp(open(a[1], 'rb').read()))
    elif len(a) == 2: open(a[1], 'wb').write(bkp2cram(open(a[0], 'rb').read()))
    else: sys.exit(__doc__)
