#!/usr/bin/env python3
"""exlflux.py -- prepare un film pour le lecteur "disque" (badflux.asm) : le Pico ne
calcule rien, il sert deux fichiers octet par octet ; l'EXL100 fait tout.

    python3 exlflux.py film.mp4 film.exf --titre "MON FILM" [--midi partition.mid ...]
    python3 exlflux.py film.mp4 film.exf ... --uf2 badflux.rom firmware.uf2 [firmware_eprom.uf2]

Il faut ffmpeg, numpy, et exlvideo.py a cote de ce script. Ecrit :
  film.exf          le conteneur (en-tete EXLF + fichier 0 : le film, fichier 1 : le
                    son), a ranger a 2 Mo dans la flash du Pico ;
  film_1bit.wav     la musique telle que l'EXL100 la jouera ;
  avec --uf2        film_carte_lineaire.uf2 (et _eprom) : firmware + lecteur + conteneur.

LE FILM (fichier 0) : des listes, chacune precedee de son heure (24 bits, poids fort
d'abord, en pas de 256 unites de 13,02 us, soit 3,33 ms) : ADR lo hi ; GROUPES ; BRUT ;
FIN (les commandes du lecteur video). $FFFFFF : fin, retour au debut.
Pour choisir les images, ce script joue le role du Pico d'avant, mais a l'avance : il
simule l'EXL100 (le cout de chaque liste en cycles, plus le temps pris par la musique)
et, a chaque liste, prend l'image du moment ou la console sera prete. Si l'EXL100 est
en avance, il attend l'heure de la liste ; s'il est en retard, il enchaine.

LE SON (fichier 1) : la musique 1 bit d'une partition MIDI (melodie et basse en
impulsions, batterie en salves, comme exlvideo.py --midi), mise en liste pour la
minuterie de l'EXL100 : pour chaque interruption, l'inversion de la sortie cassette
($08 ou 0) et la recharge de la minuterie (unites de 13,02 us). Sans partition : du
silence (la minuterie sert quand meme d'horloge).
Options : --midi FICHIER, --midi-decalage S (defaut : calage automatique sur la bande
son), --midi-melodie 6,5, --midi-basse 9, --midi-batterie 2 (comme exlvideo.py),
--debut S, --duree S, --seuil B,H, --inverse, --ips N (images par seconde lues dans le film).
"""
import os, struct, sys
ICI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, ICI)
import exlvideo as xv

FMAGIC = 0x464C5845                       # "EXLF"
UNIT_US = 1e6 * 64 / 4915200              # 13,0208 us : une unite de la minuterie
CYC_PER_UNIT = 32                         # cycles du TMS7020 (2,4576 MHz)
UPS = 76800                               # unites par seconde
T0 = 16                                   # heure (unites) du premier debordement suivi
MIND = 10                                 # ecart minimal entre deux interruptions (unites)
DELAY_S = 0.02                            # la musique un peu apres l'heure de l'image (le temps de l'afficher)
FLASH_FILES = 0x200000                    # 2 Mo
FLASH_ROM = 0x100000

# --------------------------------------------------------------- la musique
def sound_stream(pulses, dur, hold_s=2.0):
    """impulsions 1 bit (debut, fin en us depuis le debut du film, exlvideo.onebit_pulses)
    -> (fichier 1, heures (unites) des interruptions, heures des inversions).
    Le fichier 1 : pour chaque interruption, l'inversion ($08 ou 0) puis la recharge.
    Comme le lecteur : debordement 0 a T0, le suivant T0 plus tard (recharge 15 au
    depart) ; la recharge lue a l'interruption j fixe l'ecart entre j+1 et j+2."""
    E, last = [], -10 ** 9
    for a, b in pulses:
        for t in (a, b):
            u = T0 + int(round((t * 1e-6 + DELAY_S) * UPS))
            if u < last + MIND: u = last + MIND                # ecart minimal : un peu plus tard
            E.append(u); last = u
    end = T0 + int((dur + hold_s + 3.0) * UPS)                # le film, sa fin tenue, et de la marge
    U = [T0, 2 * T0]
    m = {0: 0, 1: 0}
    out = bytearray()
    ei, j = 0, 0
    while U[j + 1] < end:
        U1 = U[j + 1]
        while ei < len(E) and E[ei] <= U1:                    # deja passe (debut) : saute
            ei += 1
        rem = (E[ei] - U1) if ei < len(E) else 10 ** 9
        if rem > 256:
            D, m2 = (256 if rem > 512 else max(MIND, rem // 2)), 0
        else:
            D, m2 = max(MIND, rem), 8
            ei += 1
        out += bytes((m[j], D - 1))
        U.append(U1 + D); m[j + 2] = m2
        j += 1
    isr = U[:j]
    tog = [U[k] for k in range(j) if m[k]]
    return bytes(out), isr, tog


def engine_wav(tog_units, dur, path, rate=22050):
    import numpy as np
    n = int(dur * rate) + 2
    acc = np.zeros(n + 2)
    sg = 1.0
    for t in tog_units:
        x = t * UNIT_US * 1e-6 * rate; i = int(x); fr = x - i
        if i >= n: break
        acc[i] += sg * (1 - fr); acc[i + 1] += sg * fr; sg = -sg
    y = np.cumsum(acc)[:n]
    y = y - np.convolve(y, np.ones(441) / 441, 'same')
    y = np.convolve(y, np.ones(3) / 3, 'same')
    y = y / (np.max(np.abs(y)) + 1e-9) * 0.8
    pcm = (y * 32767).astype('<i2').tobytes()
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 36 + len(pcm)) + b'WAVEfmt '
                + struct.pack('<IHHIIHH', 16, 1, 1, rate, rate * 2, 2, 16) + b'data' + struct.pack('<I', len(pcm)) + pcm)


# --------------------------------------------------------------- le film
C_ADR, C_END, C_RAW, G_FIRST, G_MAX, G_SIZE, R_MAX, R_SIZE = 0, 3, 6, 9, 27, 9, 50, 5
TEXT_VRAM, GRAPH_VRAM, STRIDE = 0x0600, 0x0652, 122
# cout en cycles (badflux.asm, table du manuel) : mesure par test_flux.py
COST = dict(adr=106, gcmd=28, group=41, rcmd=50, raw=21, end=0, list=420)


class Lists:
    """listes du lecteur (comme le Builder du Pico), avec leur cout en cycles"""
    def __init__(self):
        self.sent = bytearray(240 * 40)
        self.text = [(0xF8, 0x60)] * 40

    def build(self, img, text=None):
        s, c = bytearray(), COST['list']
        def adr(a):
            nonlocal c
            s.extend((C_ADR, a & 255, a >> 8)); c += COST['adr']
        if text is not None:
            diff = [i for i in range(40) if text[i] != self.text[i]]
            if diff:
                a, b = diff[0], diff[-1]
                adr(TEXT_VRAM + 2 * a)
                data = [x for cc in text[a:b + 1] for x in cc]
                i = 0
                while i < len(data):
                    n = min(R_MAX, len(data) - i)
                    s.extend((C_RAW, (R_MAX - n) * R_SIZE)); s.extend(data[i:i + n]); c += COST['rcmd'] + n * COST['raw']
                    i += n
                self.text = list(text)
        for y in range(240):
            row, old = img[y * 40:(y + 1) * 40], self.sent[y * 40:(y + 1) * 40]
            if row == old:
                continue
            ch = [g for g in range(40) if row[g] != old[g]]
            runs = [[ch[0], ch[0]]]
            for g in ch[1:]:
                if g - runs[-1][1] - 1 <= 2: runs[-1][1] = g
                else: runs.append([g, g])
            for a, b in runs:
                adr(GRAPH_VRAM + y * STRIDE + 3 * a)
                i = a
                while i <= b:
                    n = min(G_MAX, b - i + 1)
                    s.append(G_FIRST + (G_MAX - n) * G_SIZE); s.extend(row[i:i + n]); c += COST['gcmd'] + n * COST['group']
                    i += n
            self.sent[y * 40:(y + 1) * 40] = row
        s.append(C_END)
        return bytes(s), c


def title_text(title):
    yellow, t = 0x78, [(0xF8, 0x60)] * 40
    for i, ch in enumerate(title.upper()[:25]):
        o = ord(ch)
        t[1 + i] = (yellow, o if 0x20 < o < 0x60 else 0x60)
    return t


def film_stream(frames, fps, title, isr_units, isr_cost, hold_s=2.0, progress=True, plan=None):
    """-> (octets du fichier 0, nombre de listes) en simulant l'EXL100 ; plan (liste) recoit,
    pour chaque liste : (heure, image, debut prevu, fin prevue, cout) en cycles"""
    import numpy as np
    N = len(frames)
    isr = np.array(isr_units, dtype=np.float64) * CYC_PER_UNIT    # en cycles
    def advance(t, work):
        """heure (cycles) ou "work" cycles du programme principal sont faits, depuis t"""
        end = t + work
        for _ in range(8):
            k = np.searchsorted(isr, end) - np.searchsorted(isr, t)
            e2 = t + work + k * isr_cost
            if e2 <= end + 1: break
            end = e2
        return end
    L = Lists()
    out = bytearray()
    cyc_s = CYC_PER_UNIT * UPS                                    # cycles par seconde
    # liste 0 : le titre, a l'heure 0
    s, c = L.build(bytes(L.sent), title_text(title))
    out += b'\0\0\0' + s
    ready = advance(0, c)
    if plan is not None: plan.append((0, -1, 0, ready, c))
    f_prev, shown, last_ts = -1, 0, 0
    while True:
        f = max(f_prev + 1, int((ready + 0.012 * cyc_s) / cyc_s * fps))
        if f >= N: break
        due = int(f / fps * UPS) + T0
        ts = max((due + 255) // 256, last_ts)                    # en pas de 256 unites (3,33 ms)
        start = max(ready, ts * 256 * CYC_PER_UNIT)
        s, c = L.build(frames[f].tobytes())
        out += struct.pack('>I', ts)[1:] + s
        ready = advance(start, c)
        if plan is not None: plan.append((ts, f, start, ready, c))
        f_prev, last_ts = f, ts
        shown += 1
        if progress and shown % 200 == 0:
            sys.stderr.write('\r  liste %d (image %d / %d, %.1f s)' % (shown, f, N, f / fps))
    if progress: sys.stderr.write('\n')
    end_ts = last_ts + int(hold_s * UPS / 256)
    out += struct.pack('>I', end_ts)[1:] + bytes([C_END])        # la derniere image, tenue
    out += b'\xFF\xFF\xFF'
    return bytes(out), shown


# --------------------------------------------------------------- conteneur et UF2
def container(f0, f1, title):
    off0 = 64; off1 = (off0 + len(f0) + 3) & ~3
    t = title.upper().encode('latin-1', 'replace')[:28].ljust(28, b' ')
    hd = struct.pack('<IHH', FMAGIC, 1, 2) + struct.pack('<III', off0, len(f0), sum(f0) & 0xFFFFFFFF) \
        + struct.pack('<III', off1, len(f1), sum(f1) & 0xFFFFFFFF) + t
    assert len(hd) == 60
    hd += struct.pack('<I', sum(hd) & 0xFFFFFFFF)
    return hd + f0 + b'\0' * (off1 - off0 - len(f0)) + f1


def uf2_assemble(regions):
    """regions : [(adresse flash, octets)] -> UF2 par secteurs de 4 Ko complets, numerotes"""
    sectors = {}
    for addr, data in regions:
        for o in range(0, len(data), 256):
            a = addr + o
            sa = a & ~0xFFF
            sec = sectors.setdefault(sa, bytearray(b'\xFF' * 4096))
            chunk = data[o:o + 256]
            sec[a - sa:a - sa + len(chunk)] = chunk
    blocks = []
    for sa in sorted(sectors):
        for o in range(0, 4096, 256):
            blocks.append((0x10000000 + sa + o, bytes(sectors[sa][o:o + 256])))
    out = bytearray()
    for i, (a, d) in enumerate(blocks):
        out += struct.pack('<IIIIIIII', 0x0A324655, 0x9E5D5157, 0x2000, a, 256, i, len(blocks), xv.FAMILY_RP2040)
        out += d + b'\0' * (476 - 256) + struct.pack('<I', 0x0AB16F30)
    return bytes(out)


def uf2_regions(path):
    u = open(path, 'rb').read(); r = []
    for o in range(0, len(u), 512):
        a, n = struct.unpack_from('<II', u, o + 12)
        r.append((a - 0x10000000, u[o + 32:o + 32 + n]))
    return r


# --------------------------------------------------------------- principal
def main(argv):
    if len(argv) < 2 or argv[0] in ('-h', '--help'):
        print(__doc__); return 2
    src, dst = argv[0], argv[1]
    opt = {'titre': None, 'debut': None, 'duree': None, 'ips': None, 'seuil': '96,160', 'images': None,
           'midi': None, 'midi-decalage': None, 'midi-melodie': None, 'midi-basse': None, 'midi-batterie': None,
           'cout-son': '190'}
    flags, uf2 = set(), None
    i = 2
    while i < len(argv):
        a = argv[i]
        if a == '--uf2':
            uf2 = argv[i + 1:]; break
        if a == '--inverse': flags.add('inverse'); i += 1; continue
        if a.startswith('--') and a[2:] in opt and i + 1 < len(argv):
            opt[a[2:]] = argv[i + 1]; i += 2; continue
        print('option inconnue :', a); return 2
    import numpy as np
    if opt['images']:                                   # images deja pretes (9600 octets chacune)
        frames = np.fromfile(opt['images'], np.uint8).reshape(-1, 240, 40)
        fps = float(opt['ips'] or 29.97)
    else:
        lo, hi = (int(x) for x in opt['seuil'].split(','))
        fps = float(opt['ips']) if opt['ips'] else min(30.0, xv.video_fps(src))
        print('lecture de %s (ffmpeg, %.2f images/s)...' % (src, fps))
        gray = xv.frames_from_video(src, opt['debut'], opt['duree'], opt['ips'] or (None if xv.video_fps(src) <= 30 else 30))
        frames = xv.binarize(gray, lo, hi, 'inverse' in flags)
    title = opt['titre'] or os.path.splitext(os.path.basename(src))[0]
    dur = len(frames) / fps
    print('%d images (%.1f s)' % (len(frames), dur))
    # la musique : impulsions 1 bit (comme exlvideo.py --midi), puis la liste du son
    P = []
    if opt['midi']:
        tracks, names = xv.midi_read(opt['midi'])
        mel, bass, drums = xv.midi_roles(tracks)
        pick = lambda k, d: [int(x) for x in opt[k].split(',') if x != ''] if opt[k] is not None else d
        mel, bass, drums = pick('midi-melodie', mel), pick('midi-basse', bass), pick('midi-batterie', drums)
        if opt['midi-decalage'] in (None, 'auto'):
            off = xv.midi_offset_auto(src, tracks, drums, opt['debut'])
            print('decalage de la partition : %.3f s (calage automatique)' % off)
        else:
            off = float(opt['midi-decalage'])
        P = xv.onebit_pulses(tracks, mel, bass, drums, off, dur)
    f1, isr_u, tog_u = sound_stream(P, dur)
    print('musique : %d interruptions (%.0f par seconde), %d inversions, %.0f Ko' % (len(isr_u), len(isr_u) / dur, len(tog_u), len(f1) / 1024))
    engine_wav(tog_u, dur, os.path.splitext(dst)[0] + '_1bit.wav')
    print('film : listes (simulation de l\'EXL100)...')
    f0, shown = film_stream(frames, fps, title, isr_u, int(opt['cout-son']))
    print('film : %d listes (%.1f images par seconde en moyenne), %.1f Mo' % (shown, shown / dur, len(f0) / 1e6))
    blob = container(f0, f1, title)
    open(dst, 'wb').write(blob)
    print('%s : %.1f Mo (a ranger a 2 Mo dans la flash ; 14 Mo au plus)' % (dst, len(blob) / 1e6))
    if len(blob) > 0x1000000 - FLASH_FILES:
        print('ATTENTION : trop gros pour la flash de 16 Mo')
    if uf2:
        rom = open(uf2[0], 'rb').read()
        for k, fw in enumerate(uf2[1:]):
            name = os.path.splitext(dst)[0] + ('_carte_eprom.uf2' if 'eprom' in os.path.basename(fw) else '_carte_lineaire.uf2')
            regs = uf2_regions(fw) + [(FLASH_ROM, rom), (FLASH_FILES, blob)]
            open(name, 'wb').write(uf2_assemble(regs))
            print('%s : firmware + lecteur + film' % name)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
