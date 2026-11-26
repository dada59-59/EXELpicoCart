#!/usr/bin/env python3
"""make_tennis_db9.py -- Tennis (Exelvision) + manette DB9 d'EXLCART
   python3 make_tennis_db9.py tennis_exl100.rom tennis_db9.rom [tn_patch.asm]

Assemble tn_patch.asm (petit assembleur integre, limite aux instructions de ce
fichier : le resultat est identique a celui de TASM), le place en $7F60 et la
zone d'echange EXLJ en $7FE0 (zone libre $7F5C-$7FFB de la ROM d'origine),
puis fait pointer BRTIME sur tn_tick au lieu de $0E07 (voir tn_patch.asm).
La ROM produite garde la forme de l'originale (31,5 Ko, de $0200 a $7FFF).
Sur une vraie EPROM, l'octet manette reste a $00 : Tennis se joue comme avant."""
import sys, os, re

def assemble(path):
    lines = open(path, encoding='latin-1').read().splitlines()
    sym = {'A': None, 'B': None}
    sym.update({'R%d' % n: n for n in range(128)})

    def ev(e):
        t = re.sub(r'\$([0-9A-Fa-f]+)', lambda m: str(int(m.group(1), 16)), e.strip())
        t = re.sub(r'\b([A-Za-z_]\w*)\b', lambda m: str(sym[m.group(1)]), t)
        return int(eval(t, {}, {}))

    def reg(x):
        m = re.fullmatch(r'R(\d+)', x.strip())
        if not m: raise ValueError('registre attendu : ' + x)
        return int(m.group(1))

    def enc(op, a, pc, final):
        """-> octets de l'instruction (adresses a 0 en premiere passe)"""
        def val(e):
            try: return ev(e)
            except KeyError:
                if final: raise
                return 0
        def w(e): v = val(e); return [v >> 8 & 0xFF, v & 0xFF]
        def rel(e, ln):
            if not final: return 0
            o = val(e.lstrip('@')) - (pc + ln)
            if not -128 <= o < 128: raise ValueError('saut trop loin : %s' % e)
            return o & 0xFF
        J = {'jmp': 0xE0, 'jn': 0xE1, 'jeq': 0xE2, 'jz': 0xE2, 'jc': 0xE3, 'jhs': 0xE3,
             'jp': 0xE4, 'jpz': 0xE5, 'jne': 0xE6, 'jnz': 0xE6, 'jl': 0xE7, 'jnc': 0xE7}
        if op == 'rets': return [0x0A]
        if op in J: return [J[op], rel(a[0], 2)]
        if op == 'call' and a[0].startswith('*'): return [0x9E, reg(a[0][1:])]
        if op == 'cmpa' and a[0].startswith('@'): return [0x8D] + w(a[0][1:])
        if op in ('call', 'br', 'lda', 'sta'):
            x = a[0]
            m = re.fullmatch(r'@(.+)\(B\)', x)
            if m and op in ('lda', 'sta'): return [{'lda': 0xAA, 'sta': 0xAB}[op]] + w(m.group(1))
            if x.startswith('@'): return [{'call': 0x8E, 'br': 0x8C, 'lda': 0x8A, 'sta': 0x8B}[op]] + w(x[1:])
        if op in ('push', 'pop') and a[0] in ('A', 'B'):
            return [{('push', 'A'): 0xB8, ('pop', 'A'): 0xB9, ('push', 'B'): 0xC8, ('pop', 'B'): 0xC9}[op, a[0]]]
        if op == 'btjz' and a[0].startswith('%'):
            return [0x77, val(a[0][1:]) & 0xFF, reg(a[1]), rel(a[2], 4)]
        ALU = {'mov': 2, 'and': 3, 'or': 4, 'xor': 5, 'add': 8, 'sub': 0xA, 'cmp': 0xD}
        if op in ALU:
            lo = ALU[op]; s, d = a
            if s == 'B' and d == 'A': return [0x60 | lo]
            if op == 'mov' and s == 'A' and d == 'B': return [0xC0]
            if op == 'mov' and s in ('A', 'B'): return [0xD0 if s == 'A' else 0xD1, reg(d)]
            if s.startswith('%'):
                v = val(s[1:])
                if final and not 0 <= v <= 0xFF: raise ValueError('octet attendu : ' + s)
                if d == 'A': return [0x20 | lo, v & 0xFF]
                if d == 'B': return [0x50 | lo, v & 0xFF]
                return [0x70 | lo, v & 0xFF, reg(d)]
            if d == 'A': return [0x10 | lo, reg(s)]
            if d == 'B': return [0x30 | lo, reg(s)]
            return [0x40 | lo, reg(s), reg(d)]
        raise NotImplementedError('%s %s' % (op, ','.join(a)))

    out = {}
    for final in (False, True):
        pc = 0
        for n, raw in enumerate(lines, 1):
            line = raw.split(';', 1)[0].rstrip()
            if not line.strip() or line.lstrip().startswith('#'): continue
            m = re.match(r'^([A-Za-z_]\w*):?\s*(.*)$', line) if not line[0].isspace() else None
            label, rest = (m.group(1), m.group(2)) if m else (None, line.strip())
            parts = rest.split(None, 1)
            op = parts[0].lower() if parts else ''
            args = parts[1].strip() if len(parts) > 1 else ''
            try:
                if op == '.equ':
                    try: sym[label] = ev(args)
                    except KeyError:
                        if final: raise
                    continue
                if label: sym[label] = pc
                if not op: continue
                if op == '.org': pc = ev(args); continue
                if op == '.end': break
                if op == '.byte':
                    for x in args.split(','):
                        if final: out[pc] = ev(x) & 0xFF
                        pc += 1
                    continue
                if op == '.text':
                    for ch in re.fullmatch(r'"(.*)"', args).group(1):
                        out[pc] = ord(ch); pc += 1
                    continue
                a = [x.strip() for x in args.split(',')] if args else []
                b = enc(op, a, pc, final)
                if final:
                    for k, v in enumerate(b): out[pc + k] = v
                pc += len(b)
            except Exception as e:
                raise SystemExit('%s ligne %d : %s (%s)' % (path, n, e, raw.strip()))
    return out, sym

BASE = 0x0200
def build(src, patch_asm):
    assert len(src) == 0x8000 - BASE and src[-4:] == bytes([0xAA, 0x8C, 0x02, 0x01]), "ROM Tennis de 32256 octets attendue (pied AA 8C 02 01)"
    rd = lambda a, n: list(src[a - BASE:a - BASE + n])
    attendu = {0x026D: [0x88, 0x0E, 0x07, 0x01, 0x8B, 0xC0, 0x04], 0x0E07: [0xD3, 0x42, 0x76, 0x40, 0x33],
               0x0D00: [0xB5, 0xB8, 0x12, 0x03, 0x14, 0x04], 0x0D33: [0x73, 0x0F, 0x43, 0x2D, 0x80],
               0x0C22: [0x12, 0x03, 0x77, 0x10, 0x45]}
    for a, b in attendu.items():
        assert rd(a, len(b)) == b, "pas la version de Tennis analysee ($%04X)" % a
    code, sym = assemble(patch_asm)
    img = bytearray(src)
    for a, v in code.items():
        assert 0x7F5C <= a <= 0x7FFB and img[a - BASE] == 0xFF, "patch hors de la zone libre ($%04X)" % a
        img[a - BASE] = v
    t = sym['tn_tick']
    img[0x026E - BASE:0x0270 - BASE] = bytes([t >> 8, t & 0xFF])
    return img, code, sym

if __name__ == '__main__':
    asm = sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.path.dirname(os.path.abspath(__file__)), 'tn_patch.asm')
    img, code, sym = build(open(sys.argv[1], 'rb').read(), asm)
    open(sys.argv[2], 'wb').write(img)
    print("%s : %d octets ($0200-$7FFF) ; patch $%04X-$%04X, zone d'echange EXLJ $%04X ; somme %08X"
          % (sys.argv[2], len(img), min(code), max(a for a in code if a < sym['exl_joy']), sym['exl_joy'],
             sum(img) & 0xFFFFFFFF))
