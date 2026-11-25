#!/usr/bin/env python3
"""exlvideo.py -- convertit une video en fichier .exv (320 x 240, noir et blanc ou
8 couleurs) pour EXELpicoCart, et fabrique le .uf2 qui la range dans la flash du Pico.

    python3 exlvideo.py film.mp4 film.exv [options]
    python3 exlvideo.py film.exv nouveau.exv --midi partition.mid --midi-decalage S
                      (garde la video deja codee, change seulement la musique)

Il faut ffmpeg (dans le PATH) et numpy (pip install numpy). Options :
    --titre "TEXTE"   titre affiche en haut de l'ecran (25 caracteres ; par defaut : nom du fichier)
    --debut S         commencer a S secondes          --duree S   garder S secondes
    --ips N           images par seconde gardees (par defaut : celles du film, 30 au plus)
    --seuil B,H       noir sous B, blanc a partir de H (0-255, defaut 96,160) ; entre les
                      deux, le pixel garde sa couleur de l'image precedente (moins de
                      scintillement, moins de travail pour l'EXL100)
    --inverse         echange noir et blanc
    --couleur         8 couleurs (celles du TMS3556) au lieu du noir et blanc : tramage
                      ordonne 4 x 4, avec ces reglages :
      --saturation S  (defaut 1.8) et --contraste C (defaut 1.15), appliques avant ;
      --hysteresis H  (defaut 16) : un point dont la valeur est a moins de H de son
                      seuil de tramage garde sa couleur de l'image precedente (moins
                      de scintillement, beaucoup moins de travail pour l'EXL100)
    --midi FICHIER    ajoute une musique 1 bit, jouee par la sortie cassette de l'EXL100
                      (reliee au son du televiseur) d'apres une partition MIDI : une
                      melodie et une basse en impulsions, et la batterie. Ecrit aussi
                      film_1bit.wav, pour l'ecouter avant. Avec :
      --midi-decalage S   ou commence la partition dans le film (secondes ; par defaut
                          "auto" : calage des grosses caisses sur la bande son du film)
      --midi-melodie 6,5  pistes de la melodie, par priorite (par defaut : devinees ;
      --midi-basse 9      la liste des pistes est affichee)
      --midi-batterie 2
    --uf2             ecrit aussi film_video_seule.uf2 (la video seule, a 8 Mo dans la flash)
    --images FICHIER  ecrit aussi les images noir et blanc, a la suite (9600 octets chacune)

La video est reduite pour tenir dans 320 x 240 (bandes noires si besoin). Le format
du fichier (en-tete, codeur arithmetique) est decrit dans exlvcodec.h : ce script
produit exactement les memes octets que l'encodeur C du firmware.
Duree : environ une minute de calcul par minute de film (Bad Apple, 3 min 52 : 1,3 Mo).
"""
import os, struct, subprocess, sys

VW, VH, VG = 320, 240, 40
VFRAME = VG * VH
VMAGIC = 0x564C5845
PB, PMOVE, PONE = 11, 5, 1 << 11
FLASH_VIDEO = 0x10000000 + 0x800000
FAMILY_RP2040 = 0xE48BFF56


# ------------------------------------------------------------------ images
def frames_from_video(path, debut, duree, ips, couleur=False, sat=1.8, con=1.15):
    import numpy as np
    vf = 'scale=%d:%d:force_original_aspect_ratio=decrease:flags=area,' % (VW, VH)
    if couleur:
        vf += 'eq=saturation=%s:contrast=%s,' % (sat, con)
    vf += 'pad=%d:%d:(ow-iw)/2:(oh-ih)/2:color=black,format=%s' % (VW, VH, 'rgb24' if couleur else 'gray')
    if ips:
        vf = 'fps=%s,' % ips + vf
    cmd = ['ffmpeg', '-loglevel', 'error']
    if debut: cmd += ['-ss', str(debut)]
    cmd += ['-i', path]
    if duree: cmd += ['-t', str(duree)]
    cmd += ['-vf', vf, '-f', 'rawvideo', '-pix_fmt', 'rgb24' if couleur else 'gray', '-']
    raw = subprocess.run(cmd, stdout=subprocess.PIPE, check=True).stdout
    px = VW * VH * (3 if couleur else 1)
    n = len(raw) // px
    a = np.frombuffer(raw[:n * px], np.uint8)
    return a.reshape(n, VH, VW, 3) if couleur else a.reshape(n, VH, VW)


def video_fps(path):
    try:
        out = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'v:0', '-show_entries',
                              'stream=r_frame_rate', '-of', 'csv=p=0', path],
                             stdout=subprocess.PIPE, check=True).stdout.decode().strip()
        a, b = out.split('/')
        return float(a) / float(b)
    except Exception:
        return 30.0


def binarize(gray, lo, hi, inverse):
    """niveaux de gris -> images 1 bit compactees (N, 240, 40), avec hysteresis"""
    import numpy as np
    out = np.zeros((len(gray), VH, VG), np.uint8)
    prev = np.zeros((VH, VW), bool)
    for i, g in enumerate(gray):
        b = prev.copy()
        b[g >= hi] = True
        b[g < lo] = False
        prev = b
        out[i] = np.packbits(~b if inverse else b, axis=1)
    return out


BAYER4 = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def colorize(rgb, hyst):
    """couleurs -> images 8 couleurs (N, 240, 120) : pour chaque groupe de 8 pixels,
    les octets bleu, vert, rouge (l'ordre du TMS3556). Tramage ordonne 4 x 4, chaque
    composante seuillee a part ; hysteresis : pres du seuil, on garde l'image d'avant."""
    import numpy as np
    t = (np.tile(np.array(BAYER4), (VH // 4, VW // 4)) + 0.5) * 16       # seuils 8 .. 248
    t = t[..., None]
    out = np.zeros((len(rgb), VH, 3 * VG), np.uint8)
    prev = None
    for i, f in enumerate(rgb):
        f = f.astype(np.int16)
        q = f > t
        if prev is not None and hyst:
            q = np.where(np.abs(f - t) < hyst, prev, q)
        prev = q
        planes = [np.packbits(q[..., c], axis=1) for c in (2, 1, 0)]  # bleu, vert, rouge
        out[i] = np.stack(planes, -1).reshape(VH, 3 * VG)
    return out


# ------------------------------------------------------------------ codeur
class Encoder:
    def __init__(self):
        self.low, self.rng, self.cache, self.csize = 0, 0xFFFFFFFF, 0, 1
        self.out = bytearray()

    def shift(self):
        low = self.low
        if low < 0xFF000000 or low >= 1 << 32:
            carry = low >> 32
            t = self.cache
            while True:
                self.out.append((t + carry) & 0xFF)
                t = 0xFF
                self.csize -= 1
                if not self.csize: break
            self.cache = (low >> 24) & 0xFF
        self.csize += 1
        self.low = (low & 0x00FFFFFF) << 8

    def run(self, probs, ctxs, bits):
        """code la suite de decisions (contextes, bits) -- la boucle chaude"""
        low, rng = self.low, self.rng
        for c, b in zip(ctxs, bits):
            p = probs[c]
            bound = (rng >> PB) * p
            if b:
                low += bound; rng -= bound; probs[c] = p - (p >> PMOVE)
            else:
                rng = bound; probs[c] = p + ((PONE - p) >> PMOVE)
            while rng < 0x1000000:
                rng = (rng << 8) & 0xFFFFFFFF
                self.low = low; self.shift(); low = self.low
        self.low, self.rng = low, rng

    def flush(self):
        for _ in range(5): self.shift()


def frame_decisions(C, P, chp, lcp):
    """decisions d'une image, dans l'ordre du decodeur (vs_line) :
    -> (contextes, bits, carte des groupes changes, lignes changees).
    Contextes : 0-3 lignes, 4-35 groupes, 36- pixels (meme numerotation que exlvcodec.h)."""
    import numpy as np
    ch = (C != P)                                         # (240, 40)
    lch = ch.any(axis=1)
    # lignes
    up_l = np.concatenate([[False], lch[:-1]])
    lctx = up_l.astype(np.int32) | (lcp.astype(np.int32) << 1)
    # groupes
    chi = ch.astype(np.int32)
    last = np.zeros_like(chi); last[:, 1:] = chi[:, :-1]
    up = np.zeros_like(chi); up[1:] = chi[:-1]
    upr = np.zeros_like(chi); upr[1:, :-1] = chi[:-1, 1:]
    mixed = ((P != 0) & (P != 0xFF)).astype(np.int32)
    gctx = last | up << 1 | chp.astype(np.int32) << 2 | mixed << 3 | upr << 4
    # pixels : voisins dans C (image courante) et P (precedente), 0 hors de l'image
    cb = np.unpackbits(C, axis=1).astype(np.int32)        # (240, 320)
    pb = np.unpackbits(P, axis=1).astype(np.int32)
    def sh(a, dy, dx):                                    # a[y-dy, x-dx]
        r = np.zeros_like(a)
        H, W = a.shape
        r[max(dy, 0):H + min(dy, 0), max(dx, 0):W + min(dx, 0)] = a[max(-dy, 0):H + min(-dy, 0), max(-dx, 0):W + min(-dx, 0)]
        return r
    pctx = (sh(cb, 0, 1) | sh(cb, 0, 2) << 1
            | sh(cb, 1, -1) << 2 | sh(cb, 1, 0) << 3 | sh(cb, 1, 1) << 4
            | sh(cb, 2, 0) << 5
            | sh(pb, 0, -1) << 6 | pb << 7 | sh(pb, 0, 1) << 8
            | sh(pb, 1, 0) << 9 | sh(pb, 1, 1) << 10
            | sh(pb, -1, -1) << 11 | sh(pb, -1, 0) << 12)
    ctxs, bits = [], []
    for y in range(VH):
        ctxs.append(int(lctx[y])); bits.append(int(lch[y]))
        if not lch[y]:
            continue
        for g in range(VG):
            ctxs.append(4 + int(gctx[y, g])); bits.append(int(chi[y, g]))
            if chi[y, g]:
                x = g * 8
                ctxs.extend((36 + pctx[y, x:x + 8]).tolist())
                bits.extend(cb[y, x:x + 8].tolist())
    return ctxs, bits, chi.astype(np.uint8), lch


def frame_decisions3(C, P, chp, lcp):
    """couleur : meme parcours que vs_line3 (exlvcodec.h). C, P : (240, 120)"""
    import numpy as np
    C3 = C.reshape(VH, VG, 3); P3 = P.reshape(VH, VG, 3)
    ch = (C3 != P3).any(axis=2)
    lch = ch.any(axis=1)
    up_l = np.concatenate([[False], lch[:-1]])
    lctx = up_l.astype(np.int32) | (lcp.astype(np.int32) << 1)
    chi = ch.astype(np.int32)
    last = np.zeros_like(chi); last[:, 1:] = chi[:, :-1]
    up = np.zeros_like(chi); up[1:] = chi[:-1]
    upr = np.zeros_like(chi); upr[1:, :-1] = chi[:-1, 1:]
    mixed = ((P3 != 0) & (P3 != 0xFF)).any(axis=2).astype(np.int32)
    gctx = last | up << 1 | chp.astype(np.int32) << 2 | mixed << 3 | upr << 4
    def sh(a, dy, dx):
        r = np.zeros_like(a)
        H, W = a.shape
        r[max(dy, 0):H + min(dy, 0), max(dx, 0):W + min(dx, 0)] = a[max(-dy, 0):H + min(-dy, 0), max(-dx, 0):W + min(-dx, 0)]
        return r
    cb = [np.unpackbits(C3[:, :, k], axis=1).astype(np.int32) for k in range(3)]   # (240, 320) par plan
    pb = [np.unpackbits(P3[:, :, k], axis=1).astype(np.int32) for k in range(3)]
    pctx = []
    for k in range(3):
        c, p = cb[k], pb[k]
        cross = 0 if k == 0 else (cb[0] if k == 1 else cb[0] | cb[1] << 1)
        pctx.append(sh(c, 0, 1) | sh(c, 0, 2) << 1
                    | sh(c, 1, -1) << 2 | sh(c, 1, 0) << 3 | sh(c, 1, 1) << 4
                    | sh(p, 0, -1) << 5 | p << 6 | sh(p, 0, 1) << 7
                    | sh(p, -1, 0) << 8 | cross << 9 | k << 11)
    ctxs, bits = [], []
    for y in range(VH):
        ctxs.append(int(lctx[y])); bits.append(int(lch[y]))
        if not lch[y]:
            continue
        for g in range(VG):
            ctxs.append(4 + int(gctx[y, g])); bits.append(int(chi[y, g]))
            if chi[y, g]:
                x = g * 8
                for k in range(3):
                    ctxs.extend((36 + pctx[k][y, x:x + 8]).tolist())
                    bits.extend(cb[k][y, x:x + 8].tolist())
    return ctxs, bits, chi.astype(np.uint8), lch


def encode(frames, progress=True):
    import numpy as np
    probs = [PONE // 2] * (4 + 32 + 8192)
    e = Encoder()
    couleur = frames.shape[-1] == 3 * VG
    P = np.zeros(frames.shape[1:], np.uint8)
    chp = np.zeros((VH, VG), np.uint8)
    lcp = np.zeros(VH, bool)
    n = len(frames)
    for i in range(n):
        C = frames[i]
        ctxs, bits, chp, lcp = (frame_decisions3 if couleur else frame_decisions)(C, P, chp, lcp)
        e.run(probs, ctxs, bits)
        P = C
        if progress and (i % 100 == 0 or i == n - 1):
            sys.stderr.write('\r  image %d / %d  (%d octets)' % (i + 1, n, len(e.out)))
    e.flush()
    if progress: sys.stderr.write('\n')
    return bytes(e.out)


def header(data, nframes, fps, title, couleur=False):
    t = title.upper().encode('latin-1', 'replace')[:40].ljust(40, b' ')
    return struct.pack('<IHHHHIII', VMAGIC, 2 if couleur else 1, VW, VH, int(round(fps * 1000)), nframes, len(data),
                       sum(data) & 0xFFFFFFFF) + t


# ------------------------------------------------------------------ musique 1 bit
# La sortie cassette de l'EXL100 (bit 3 du port B du TMS7020) est reliee au son du
# televiseur : un seul bit, que le lecteur inverse sous interruption de la minuterie.
# On joue une partition MIDI : une melodie et une basse en impulsions etroites (la
# methode des musiques "beeper" des micros 8 bits), plus la batterie (grosse caisse,
# caisse claire, cymbales) en salves. Le resultat est une suite d'inversions datees
# (bloc "EXL1"), que le Pico fait jouer a la minuterie de l'EXL100.
BMAGIC = 0x314C5845                                    # "EXL1"
B_MIN_US = 120                                         # ecart minimal entre deux inversions
B_WAKE_US = 30000                                      # au plus 30 ms entre deux evenements


def midi_read(path):
    """fichier MIDI (format 0 ou 1) -> liste de pistes : [(debut s, fin s, note, canal), ...], noms"""
    d = open(path, 'rb').read()
    if d[:4] != b'MThd':
        raise SystemExit('%s : ce n\'est pas un fichier MIDI' % path)
    hl = struct.unpack('>I', d[4:8])[0]
    fmt, ntr, div = struct.unpack('>HHH', d[8:14])
    if div & 0x8000:
        raise SystemExit('MIDI en temps SMPTE : non pris en charge')
    p = 8 + hl
    raw, names, tempos = [], [], []
    for _ in range(ntr):
        while d[p:p + 4] != b'MTrk':
            p += 8 + struct.unpack('>I', d[p + 4:p + 8])[0]
        ln = struct.unpack('>I', d[p + 4:p + 8])[0]
        q, end, tick, st, evs, name = p + 8, p + 8 + ln, 0, 0, [], ''
        while q < end:
            dt = 0
            while True:
                b = d[q]; q += 1; dt = dt << 7 | (b & 0x7F)
                if b < 0x80: break
            tick += dt
            b = d[q]
            if b == 0xFF:
                typ = d[q + 1]; q += 2; n = 0
                while True:
                    c = d[q]; q += 1; n = n << 7 | (c & 0x7F)
                    if c < 0x80: break
                if typ == 0x51: tempos.append((tick, int.from_bytes(d[q:q + 3], 'big')))
                if typ == 0x03 and not name: name = d[q:q + n].decode('latin-1')
                q += n; continue
            if b in (0xF0, 0xF7):
                q += 1; n = 0
                while True:
                    c = d[q]; q += 1; n = n << 7 | (c & 0x7F)
                    if c < 0x80: break
                q += n; continue
            if b & 0x80: st = b; q += 1
            k = st >> 4
            a1 = d[q]; q += 1
            a2 = 0
            if k not in (0xC, 0xD): a2 = d[q]; q += 1
            if k == 9 and a2: evs.append((tick, 1, st & 15, a1))
            elif k == 8 or (k == 9 and not a2): evs.append((tick, 0, st & 15, a1))
        raw.append(evs); names.append(name)
        p = end
    tempos.sort()
    if not tempos or tempos[0][0] != 0: tempos.insert(0, (0, 500000))
    def sec(t):
        s, pt, pv = 0.0, 0, tempos[0][1]
        for tt, tv in tempos[1:]:
            if tt >= t: break
            s += (tt - pt) * pv / 1e6 / div; pt, pv = tt, tv
        return s + (t - pt) * pv / 1e6 / div
    tracks = []
    for evs in raw:
        on, ns = {}, []
        for tick, kind, ch, note in sorted(evs, key=lambda e: (e[0], e[1])):
            if kind:
                on.setdefault((ch, note), []).append(tick)
            elif on.get((ch, note)):
                ns.append((sec(on[(ch, note)].pop(0)), sec(tick), note, ch))
        for (ch, note), l in on.items():                  # notes jamais relachees
            ns += [(sec(t), sec(t) + 0.1, note, ch) for t in l]
        tracks.append(sorted(ns))
    return tracks, names


def midi_roles(tracks):
    """pistes par defaut : batterie (canal 10), basse (la plus grave), melodie (la plus
    fournie entre do 4 et do 6)"""
    drums = [i for i, t in enumerate(tracks) if t and all(n[3] == 9 for n in t)]
    tone = [i for i, t in enumerate(tracks) if t and i not in drums]
    avg = lambda i: sum(n[2] for n in tracks[i]) / len(tracks[i])
    bass = [min(tone, key=avg)] if tone else []
    mel = sorted((i for i in tone if i not in bass),
                 key=lambda i: -sum(1 for n in tracks[i] if 60 <= n[2] <= 84))[:1]
    return mel, bass, drums


def mono_line(tracks, prio, low):
    """une voix : a chaque instant, la note de la premiere piste (par priorite) qui joue ;
    dans une piste, la plus aigue (melodie) ou la plus grave (basse)"""
    cuts = sorted({x for i in prio for n in tracks[i] for x in n[:2]})
    seg = []
    for a, b in zip(cuts, cuts[1:]):
        m = (a + b) / 2
        for i in prio:
            act = [n for n in tracks[i] if n[0] <= m < n[1]]
            if act:
                n = min(act, key=lambda n: n[2]) if low else max(act, key=lambda n: n[2])
                key = (i, n[0], n[2])
                if seg and seg[-1][3] == key and abs(seg[-1][1] - a) < 1e-6:
                    seg[-1][1] = b
                else:
                    seg.append([a, b, n[2], key])
                break
    out = []
    for k, (a, b, note, key) in enumerate(seg):          # petite coupure entre deux notes liees
        if k + 1 < len(seg) and seg[k + 1][0] - b < 0.012:
            b = max(a + 0.02, b - 0.012)
        out.append((a, b, note))
    return out


def _freq(note, lo, hi):
    f = 440.0 * 2 ** ((note - 69) / 12)
    while f > hi: f /= 2
    while f < lo: f *= 2
    return f


def onebit_pulses(tracks, mel, bass, drums, offset, duration, seed=1):
    """impulsions (debut, fin) en microsecondes depuis le debut du film"""
    import random
    rnd = random.Random(seed)
    P = []
    # melodie : impulsions de 28 % de la periode (220-900 Hz)
    for a, b, note in mono_line(tracks, mel, False):
        f = _freq(note, 220, 900); T = 1e6 / f; w = 0.28 * T
        t = (a + offset) * 1e6
        while t < (b + offset) * 1e6:
            P.append((t, t + w)); t += T
    # basse : impulsions de 900 us (55-220 Hz)
    for a, b, note in mono_line(tracks, bass, True):
        f = _freq(note, 55, 220); T = 1e6 / f; w = min(900.0, 0.2 * T)
        t = (a + offset) * 1e6
        while t < (b + offset) * 1e6:
            P.append((t, t + w)); t += T
    # batterie
    for i in drums:
        for a, b, note, ch in tracks[i]:
            t = (a + offset) * 1e6
            if note in (35, 36):                            # grosse caisse : bip qui descend
                for dt, w in ((0, 2500), (5500, 2200), (12000, 1800), (20000, 1400), (30000, 1000)):
                    P.append((t + dt, t + dt + w))
            elif note in (37, 38, 39, 40):                  # caisse claire, claquement : bruit 60 ms
                u = t
                while u < t + 60000:
                    w = rnd.uniform(120, 260) * (1 - (u - t) / 90000)
                    P.append((u, u + max(w, B_MIN_US))); u += w + rnd.uniform(130, 420)
            elif note in (49, 52, 55, 57):                  # cymbale : bruit 180 ms, de plus en plus rare
                u = t
                while u < t + 180000:
                    P.append((u, u + B_MIN_US)); u += B_MIN_US + rnd.uniform(150, 600) * (1 + (u - t) / 40000)
            elif note in (42, 44, 46):                      # charleston : un claquement
                P.append((t, t + 140))
            elif note in (41, 43, 45, 47, 48, 50):          # toms : bip
                for dt, w in ((0, 1600), (4500, 1300), (10000, 1000)):
                    P.append((t + dt, t + dt + w))
    end = duration * 1e6
    P = sorted((max(0.0, a), min(b, end)) for a, b in P if a < end)
    # union, puis ecarts et largeurs minimales (le temps de l'interruption)
    U = []
    for a, b in P:
        if b - a < B_MIN_US: b = a + B_MIN_US
        if U and a - U[-1][1] < B_MIN_US:
            U[-1][1] = max(U[-1][1], b)
        else:
            U.append([a, b])
    return [(int(round(a)), int(round(b))) for a, b in U]


def onebit_block(pulses, duration):
    """bloc EXL1 : en-tete de 20 octets (signature, version, 0, nombre d'evenements, duree
    en ms, somme des octets), puis un mot de 16 bits par evenement : bits 0-14 = ecart en
    us depuis le precedent (le premier : depuis le debut du film), bit 15 = inverser la
    sortie (0 : simple reveil, pour les silences de plus de 30 ms)."""
    ev, last = [], 0
    for a, b in pulses:
        for t in (a, b):
            dt = t - last
            while dt > B_WAKE_US:
                ev.append(B_WAKE_US); dt -= B_WAKE_US
            ev.append(0x8000 | max(dt, 1)); last = t
    data = struct.pack('<%dH' % len(ev), *ev)
    if len(data) % 4: data += b'\0\0'
    return struct.pack('<IHHIII', BMAGIC, 1, 0, len(ev), int(duration * 1000), sum(data) & 0xFFFFFFFF) + data, len(ev)


def onebit_wav(pulses, duration, path, rate=22050):
    """apercu : la sortie 1 bit, sans continu, adoucie comme par un haut-parleur de televiseur"""
    import numpy as np
    n = int(duration * rate) + 2
    acc = np.zeros(n + 2)
    for a, b in pulses:                                    # aire exacte de chaque impulsion
        for t, sg in ((a, 1.0), (b, -1.0)):
            x = t * rate / 1e6; i = int(x); fr = x - i
            if i < n:
                acc[i] += sg * (1 - fr); acc[i + 1] += sg * fr
    y = np.cumsum(acc)[:n]
    y = y - np.convolve(y, np.ones(441) / 441, 'same')     # condensateur de liaison
    y = np.convolve(y, np.ones(3) / 3, 'same')             # un peu moins d'aigus
    y = y / (np.max(np.abs(y)) + 1e-9) * 0.8
    pcm = (y * 32767).astype('<i2').tobytes()
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 36 + len(pcm)) + b'WAVEfmt '
                + struct.pack('<IHHIIHH', 16, 1, 1, rate, rate * 2, 2, 16) + b'data' + struct.pack('<I', len(pcm)) + pcm)


def midi_offset_auto(src, tracks, drums, debut=None):
    """decalage (s) de la partition dans le film : d'abord grossierement (le volume de la
    bande son contre le nombre de notes jouees, par dixiemes de seconde : les couplets, les
    ponts et les silences), puis finement (grosses caisses contre attaques graves)"""
    import numpy as np
    cmd = ['ffmpeg', '-loglevel', 'error']
    if debut: cmd += ['-ss', str(debut)]
    cmd += ['-i', src, '-vn', '-ac', '1', '-ar', '11025', '-f', 's16le', '-']
    x = np.frombuffer(subprocess.run(cmd, stdout=subprocess.PIPE, check=True).stdout, np.int16).astype(np.float32)
    sr = 11025
    if len(x) < sr:
        return 0.0
    # grossier : 100 ms
    nb = len(x) // 1102
    A = np.log10(np.sqrt((x[:nb * 1102].reshape(nb, 1102) ** 2).mean(1)) + 1.0)
    end = max(n[1] for t in tracks for n in t) if any(tracks) else 0
    B = np.zeros(int(end * 10) + 2)
    for t in tracks:
        for a, b_, note, ch in t:
            B[int(a * 10):int(b_ * 10) + 1] += 1
    B = np.log1p(B)
    za = (A - A.mean()) / (A.std() + 1e-9)                  # le silence de la bande son : tres negatif
    best = (-1e18, 0)
    for k in range(-300, 301):                              # -30 .. +30 s : la partition placee en k
        lo, hi = max(0, k), min(len(za), k + len(B))
        if hi - lo < 50: continue
        sc = float((za[lo:hi] * B[lo - k:hi - k]).sum())
        if sc > best[0]: best = (sc, k)
    coarse = best[1] / 10.0
    # fin : attaques graves, a 5 ms pres, autour du decalage grossier
    hop, win = 55, 512
    n = len(x) // hop
    spec = np.empty(n); w = np.hanning(win); xp = np.pad(x, (0, win))
    for i0 in range(0, n, 4096):
        idx = np.arange(i0, min(n, i0 + 4096))[:, None] * hop + np.arange(win)
        spec[i0:i0 + len(idx)] = np.abs(np.fft.rfft(np.take(xp, idx) * w, axis=1))[:, 1:12].sum(1)
    env = np.maximum(0, np.diff(np.log1p(spec), prepend=0))
    env = np.maximum(env - np.convolve(env, np.ones(100) / 100, 'same'), 0)
    kick = np.array([n_[0] for i in drums for n_ in tracks[i] if n_[2] in (35, 36)])
    if not len(kick):
        kick = np.array([n_[0] for i in drums for n_ in tracks[i]])
    if not len(kick):
        return coarse
    fr = hop / sr
    best = (-1, coarse)
    for off in np.arange(coarse - 0.25, coarse + 0.25, fr):
        idx = ((kick + off) / fr).astype(int)
        idx = idx[(idx >= 0) & (idx < n - 1)]
        sc = env[idx].sum() + env[idx + 1].sum()
        if sc > best[0]: best = (sc, off)
    return float(best[1])


# ------------------------------------------------------------------ uf2
def uf2(data, addr):
    """blocs UF2 par SECTEURS de 4 Ko complets (comme EXLPICO : la ROM de demarrage
    du RP2040 efface d'apres le numero du bloc)"""
    data = bytes(data) + b'\xFF' * (-len(data) % 4096)
    blocks = []
    for off in range(0, len(data), 256):
        blocks.append((addr + off, data[off:off + 256]))
    out = bytearray()
    for i, (a, d) in enumerate(blocks):
        out += struct.pack('<IIIIIIII', 0x0A324655, 0x9E5D5157, 0x2000, a, 256, i, len(blocks), FAMILY_RP2040)
        out += d + b'\0' * (476 - 256) + struct.pack('<I', 0x0AB16F30)
    return bytes(out)


def add_music(blob, src, dst, opt, nframes, fps):
    """partition MIDI -> bloc EXL1 ajoute a la video ; apercu .wav a cote de dst"""
    tracks, names = midi_read(opt['midi'])
    mel, bass, drums = midi_roles(tracks)
    pick = lambda k, d: [int(x) for x in opt[k].split(',') if x != ''] if opt[k] is not None else d
    mel, bass, drums = pick('midi-melodie', mel), pick('midi-basse', bass), pick('midi-batterie', drums)
    for i in mel + bass + drums:
        if not 0 <= i < len(tracks):
            raise SystemExit('piste %d inexistante (le fichier en a %d)' % (i, len(tracks)))
    print('partition %s : %d pistes' % (opt['midi'], len(tracks)))
    for i, t in enumerate(tracks):
        if t:
            r = 'melodie' if i in mel else 'basse' if i in bass else 'batterie' if i in drums else ''
            print('  piste %2d : %4d notes  %-28s %s' % (i, len(t), names[i][:28], r))
    if opt['midi-decalage'] in (None, 'auto'):
        if src.lower().endswith('.exv'):
            raise SystemExit('--midi-decalage : donner les secondes (la bande son de la video est necessaire pour "auto")')
        off = midi_offset_auto(src, tracks, drums, opt['debut'])
        print('decalage de la partition : %.3f s (calage automatique sur la bande son)' % off)
    else:
        off = float(opt['midi-decalage'])
    dur = nframes / fps
    P = onebit_pulses(tracks, mel, bass, drums, off, dur)
    blk, nev = onebit_block(P, dur)
    print('musique 1 bit : %d inversions (%.0f par seconde en moyenne), %d octets' % (2 * len(P), 2 * len(P) / dur, len(blk)))
    wav = os.path.splitext(dst)[0] + '_1bit.wav'
    onebit_wav(P, dur, wav)
    print('%s : apercu de la musique' % wav)
    return blob + b'\0' * (-len(blob) % 4) + blk


def main(argv):
    if len(argv) < 2 or argv[0] in ('-h', '--help'):
        print(__doc__); return 2
    src, dst = argv[0], argv[1]
    opt = {'titre': None, 'debut': None, 'duree': None, 'ips': None, 'seuil': '96,160', 'images': None,
           'saturation': '1.8', 'contraste': '1.15', 'hysteresis': '16', 'midi': None, 'midi-decalage': None,
           'midi-melodie': None, 'midi-basse': None, 'midi-batterie': None}
    flags = set()
    i = 2
    while i < len(argv):
        a = argv[i]
        if a in ('--inverse', '--uf2', '--couleur'):
            flags.add(a[2:]); i += 1; continue
        if a.startswith('--') and a[2:] in opt and i + 1 < len(argv):
            opt[a[2:]] = argv[i + 1]; i += 2; continue
        print('option inconnue :', a); return 2
    if src.lower().endswith('.exv'):                     # video deja codee : on change seulement la musique
        old = open(src, 'rb').read()
        if struct.unpack_from('<I', old, 0)[0] != VMAGIC:
            print('%s : pas un fichier .exv' % src); return 1
        _, ver, _, _, fpsm, nframes, size, _ = struct.unpack_from('<IHHHHIII', old, 0)
        blob = old[:64 + size]
        if opt['midi']:
            blob = add_music(blob, src, dst, opt, nframes, fpsm / 1000.0)
        open(dst, 'wb').write(blob)
        print('%s : %d images, %d octets%s' % (dst, nframes, len(blob), ', avec musique' if opt['midi'] else ', sans musique'))
        hd, data = blob, b''
    else:
        lo, hi = (int(x) for x in opt['seuil'].split(','))
        fps = float(opt['ips']) if opt['ips'] else min(30.0, video_fps(src))
        title = opt['titre'] or os.path.splitext(os.path.basename(src))[0]
        couleur = 'couleur' in flags
        print('lecture de %s (ffmpeg, %.2f images/s%s)...' % (src, fps, ', couleur' if couleur else ''))
        gray = frames_from_video(src, opt['debut'], opt['duree'], opt['ips'] or (None if video_fps(src) <= 30 else 30),
                                 couleur, opt['saturation'], opt['contraste'])
        if not len(gray):
            print('aucune image lue'); return 1
        if couleur:
            print('%d images (%.1f s), passage en 8 couleurs (tramage 4 x 4, hysteresis %s)...' % (len(gray), len(gray) / fps, opt['hysteresis']))
            frames = colorize(gray, int(opt['hysteresis']))
        else:
            print('%d images (%.1f s), passage en noir et blanc (seuils %d / %d)...' % (len(gray), len(gray) / fps, lo, hi))
            frames = binarize(gray, lo, hi, 'inverse' in flags)
        if opt['images']:
            frames.tofile(opt['images'])
        print('codage...')
        data = encode(frames)
        blob = header(data, len(frames), fps, title, couleur) + data
        if opt['midi']:
            blob = add_music(blob, src, dst, opt, len(frames), fps)
        open(dst, 'wb').write(blob)
        hd, data = blob, b''
        print('%s : %d images, %d octets, titre "%s"' % (dst, len(frames), len(blob), title.upper()[:25]))
    if 'uf2' in flags:
        u = os.path.splitext(dst)[0] + '_video_seule.uf2'
        open(u, 'wb').write(uf2(hd + data, FLASH_VIDEO))
        print('%s : la video seule, a 8 Mo dans la flash du Pico' % u)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
