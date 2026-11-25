/*
 * exlvcodec.h -- codec video noir et blanc d'EXLCART (decodeur pour le Pico,
 * encodeur pour le PC). Aucune dependance au materiel : se compile aussi sur PC.
 *
 * Image : 320 x 240 pixels, 1 bit par pixel, 40 octets par ligne, bit 7 = pixel
 * de gauche (le meme ordre que les plans du TMS3556). 1 = blanc.
 *
 * Chaque image est codee par rapport a la precedente (la premiere, par rapport
 * a une image noire), avec un codeur arithmetique binaire (celui de LZMA :
 * probabilites sur 11 bits, adaptation 1/32) et trois niveaux :
 *   1. pour chaque ligne : a-t-elle change ? (4 contextes)
 *   2. pour chaque groupe de 8 pixels d'une ligne changee : a-t-il change ? (32)
 *   3. pour chaque pixel d'un groupe change : sa valeur, predite par ses voisins
 *      deja connus dans l'image et par l'image precedente (8192 contextes).
 * Les probabilites s'adaptent tout au long de la video (pas de remise a zero
 * entre les images). Bad Apple, 6955 images : environ 1,3 Mo.
 *
 * Flux en flash (petit-boutiste) : en-tete vhdr_t de 64 octets, puis les donnees
 * du codeur arithmetique.
 */
#ifndef EXLVCODEC_H
#define EXLVCODEC_H
#include <stdint.h>
#include <string.h>

#define VW        320u
#define VH        240u
#define VG        40u                              /* groupes (octets) par ligne */
#define VFRAME    (VG * VH)                        /* 9600 octets par image */
#define VMAGIC    0x564C5845u                      /* "EXLV" */
#define VVERSION  1u                               /* noir et blanc */
#define VVERSION_COUL 2u                           /* 8 couleurs : 3 plans (bleu, vert, rouge) */

typedef struct {
    uint32_t magic;          /* "EXLV" */
    uint16_t version;        /* 1 : noir et blanc ; 2 : 8 couleurs */
    uint16_t width, height;  /* 320, 240 */
    uint16_t fps_milli;      /* images par seconde x 1000 (29970 pour 29,97) */
    uint32_t frames;         /* nombre d'images */
    uint32_t size;           /* octets de donnees apres l'en-tete */
    uint32_t sum;            /* somme des octets de donnees (controle) */
    uint8_t  title[40];      /* titre affiche (ASCII, complete par des espaces) */
} vhdr_t;                    /* 64 octets */

/* fonctions chaudes : en RAM sur le Pico (pas d'attente de la flash), non inlinees */
#ifndef VHOT
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define VHOT(f) __attribute__((noinline, section(".time_critical." #f))) f
#else
#define VHOT(f) f
#endif
#endif

#define PB 11u                                     /* bits de probabilite */
#define PMOVE 5u
#define PONE (1u << PB)

typedef struct {
    uint16_t pl[4];                                /* ligne changee ? */
    uint16_t pg[32];                               /* groupe change ? */
    uint16_t pp[8192];                             /* pixel */
} vmodel_t;

static inline void vm_init(vmodel_t *m) {
    for (unsigned i = 0; i < 4; i++) m->pl[i] = PONE / 2;
    for (unsigned i = 0; i < 32; i++) m->pg[i] = PONE / 2;
    for (unsigned i = 0; i < 8192; i++) m->pp[i] = PONE / 2;
}

/* contexte d'un pixel : 13 voisins deja connus.
 *   image courante C : x-1, x-2 (ligne y) ; x-1, x, x+1 (ligne y-1) ; x (ligne y-2)
 *   image precedente P : x-1, x, x+1 (ligne y) ; x-1, x (y-1) ; x, x+1 (y+1)
 * Pour aller vite, chaque ligne voisine est lue une fois par groupe dans une
 * "fenetre" de 24 bits (octets g-1, g, g+1) : le pixel k du groupe y est le bit 15-k.
 * Hors de l'image : 0. */
typedef struct { uint32_t c1, c2, p0, p1, p2; } vwin_t;

static inline uint32_t vw_row(const uint8_t *row, int g) {
    if (!row) return 0;
    return (g > 0 ? (uint32_t)row[g - 1] << 16 : 0u) | (uint32_t)row[g] << 8 | (g < (int)VG - 1 ? row[g + 1] : 0u);
}
static inline void vw_init(vwin_t *w, const uint8_t *C, const uint8_t *P, int y, int g) {
    w->c1 = vw_row(y > 0 ? C + (y - 1) * VG : 0, g);
    w->c2 = vw_row(y > 1 ? C + (y - 2) * VG : 0, g);
    w->p0 = vw_row(P + y * VG, g);
    w->p1 = vw_row(y > 0 ? P + (y - 1) * VG : 0, g);
    w->p2 = vw_row(y < (int)VH - 1 ? P + (y + 1) * VG : 0, g);
}
/* left : bit 0 = pixel x-1, bit 1 = pixel x-2 (ligne courante) */
static inline uint32_t vctx_k(const vwin_t *w, uint32_t left, int k) {
    const int s = 14 - k;                          /* bit de x+1 ; x en s+1, x-1 en s+2 */
    return left
         | ((w->c1 >> s) & 7u) << 2               /* C y-1 : x+1, x, x-1 */
         | ((w->c2 >> (s + 1)) & 1u) << 5         /* C y-2 : x */
         | ((w->p0 >> s) & 7u) << 6               /* P y   : x+1, x, x-1 */
         | ((w->p1 >> (s + 1)) & 3u) << 9         /* P y-1 : x, x-1 */
         | ((w->p2 >> s) & 3u) << 11;             /* P y+1 : x+1, x */
}

/* contexte d'un groupe : last = groupe g-1 de cette ligne change ; up = carte de la
 * ligne y-1 de cette image (ou NULL) ; chp = ligne y de l'image precedente ; pv = octet P */
static inline uint32_t vgctx(uint32_t last, const uint8_t *up, const uint8_t *chp, uint8_t pv, int g) {
    return  last
          | (up ? (uint32_t)up[g] : 0u) << 1
          | (uint32_t)chp[g] << 2
          | (uint32_t)(pv != 0 && pv != 0xFF) << 3
          | (up && g < (int)VG - 1 ? (uint32_t)up[g + 1] : 0u) << 4;
}
static inline uint32_t vlctx(const uint8_t *lch, const uint8_t *lcp, int y) {
    return (y > 0 ? lch[y - 1] : 0u) | lcp[y] << 1;
}

/* ------------------------------------------------------------------ decodeur */
typedef struct {
    const uint8_t *in;
    uint32_t range, code;
} vdec_t;

static inline void vd_init(vdec_t *d, const uint8_t *in) {
    d->in = in; d->range = 0xFFFFFFFFu; d->code = 0;
    for (int i = 0; i < 5; i++) d->code = (d->code << 8) | *d->in++;
}
static inline uint32_t vd_bit(vdec_t *d, uint16_t *p) {
    uint32_t bound = (d->range >> PB) * *p, b;
    if (d->code < bound) { d->range = bound; *p += (uint16_t)((PONE - *p) >> PMOVE); b = 0; }
    else { d->range -= bound; d->code -= bound; *p -= (uint16_t)(*p >> PMOVE); b = 1; }
    if (d->range < (1u << 24)) { d->range <<= 8; d->code = (d->code << 8) | *d->in++; }
    return b;
}

/* etat d'un decodage : deux images (P precedente, C en cours), cartes de changements */
typedef struct {
    vdec_t d;
    vmodel_t m;
    uint32_t np;                     /* plans : 1 (noir et blanc) ou 3 (couleur) */
    uint8_t *P, *C;                  /* VFRAME * np octets chacune ; en couleur, groupe par
                                        groupe : bleu, vert, rouge (l'ordre du TMS3556) */
    uint8_t *chg, *chp;              /* VFRAME octets : groupes changes (cette image, precedente) */
    uint8_t lch[VH], lcp[VH];        /* lignes changees */
} vstate_t;

/* debut de video : image precedente noire, aucun changement precedent */
static inline void vs_start(vstate_t *s, const uint8_t *data, uint32_t np) {
    vd_init(&s->d, data);
    vm_init(&s->m);
    s->np = np;
    memset(s->P, 0, VFRAME * np); memset(s->C, 0, VFRAME * np);
    memset(s->chp, 0, VFRAME); memset(s->chg, 0, VFRAME);
    memset(s->lcp, 0, VH); memset(s->lch, 0, VH);
}

/* un bit, sur l'etat du decodeur en variables locales (rng, code, in) */
#define VD_BIT(b, pp) do {                                                          \
        uint32_t _p = *(pp), _bd = (rng >> PB) * _p;                                 \
        if (code < _bd) { rng = _bd; *(pp) = (uint16_t)(_p + ((PONE - _p) >> PMOVE)); (b) = 0; } \
        else { rng -= _bd; code -= _bd; *(pp) = (uint16_t)(_p - (_p >> PMOVE)); (b) = 1; }        \
        if (rng < (1u << 24)) { rng <<= 8; code = (code << 8) | *in++; }            \
    } while (0)

/* decode la ligne y de l'image courante (C) ; meme parcours que vd_bit */
static void VHOT(vs_line)(vstate_t *s, int y) {
    uint8_t *c = s->C + y * VG, *cg = s->chg + y * VG;
    const uint8_t *p = s->P + y * VG, *chp = s->chp + y * VG, *up = y > 0 ? cg - VG : 0;
    uint32_t rng = s->d.range, code = s->d.code, b;
    const uint8_t *in = s->d.in;
    VD_BIT(b, &s->m.pl[vlctx(s->lch, s->lcp, y)]);
    s->lch[y] = (uint8_t)b;
    if (!b) {
        memcpy(c, p, VG); memset(cg, 0, VG);
    } else {
        const uint8_t *c1 = y > 0 ? c - VG : 0, *c2 = y > 1 ? c - 2 * VG : 0;
        const uint8_t *p1 = y > 0 ? p - VG : 0, *p2 = y < (int)VH - 1 ? p + VG : 0;
        uint16_t *pg = s->m.pg, *pp = s->m.pp;
        uint32_t last = 0;
        for (int g = 0; g < (int)VG; g++) {
            uint32_t ch;
            VD_BIT(ch, &pg[vgctx(last, up, chp, p[g], g)]);
            cg[g] = (uint8_t)ch;
            last = ch;
            if (!ch) { c[g] = p[g]; continue; }
            vwin_t w;
            w.c1 = vw_row(c1, g); w.c2 = vw_row(c2, g); w.p0 = vw_row(p, g);
            w.p1 = vw_row(p1, g); w.p2 = vw_row(p2, g);
            uint32_t left = g > 0 ? c[g - 1] & 3u : 0u, v = 0;
            for (int k = 0; k < 8; k++) {
                VD_BIT(b, &pp[vctx_k(&w, left, k)]);
                v = v << 1 | b;
                left = (left << 1 | b) & 3u;
            }
            c[g] = (uint8_t)v;
        }
    }
    s->d.range = rng; s->d.code = code; s->d.in = in;
}

/* ---- couleur : 3 plans. Un groupe change si l'un de ses 3 octets change ; ses
 * pixels sont codes plan par plan (bleu, vert, rouge), avec pour contexte :
 *   meme plan : x-1, x-2 (ligne y) ; x+1, x, x-1 (ligne y-1) ; dans l'image
 *   precedente x+1, x, x-1 (ligne y) et x (ligne y+1) ; les plans deja connus du
 *   meme pixel (vert : bleu ; rouge : bleu et vert) ; le numero du plan. */
static inline uint32_t vw_row3(const uint8_t *row, int g, int k) {
    if (!row) return 0;
    return (g > 0 ? (uint32_t)row[3 * (g - 1) + k] << 16 : 0u) | (uint32_t)row[3 * g + k] << 8
         | (g < (int)VG - 1 ? row[3 * (g + 1) + k] : 0u);
}
static inline uint32_t vctx3(uint32_t left, uint32_t c1, uint32_t p0, uint32_t p2, uint32_t cross, int k, int i) {
    const int s = 14 - i;
    return left | ((c1 >> s) & 7u) << 2 | ((p0 >> s) & 7u) << 5 | ((p2 >> (s + 1)) & 1u) << 8
         | cross << 9 | (uint32_t)k << 11;
}
static inline uint32_t vmixed3(const uint8_t *q) {           /* groupe ni tout noir ni tout allume */
    return (q[0] != 0 && q[0] != 0xFF) || (q[1] != 0 && q[1] != 0xFF) || (q[2] != 0 && q[2] != 0xFF);
}

static void VHOT(vs_line3)(vstate_t *s, int y) {
    const int L = 3 * VG;
    uint8_t *c = s->C + y * L, *cg = s->chg + y * VG;
    const uint8_t *p = s->P + y * L, *chp = s->chp + y * VG, *up = y > 0 ? cg - VG : 0;
    uint32_t rng = s->d.range, code = s->d.code, b;
    const uint8_t *in = s->d.in;
    VD_BIT(b, &s->m.pl[vlctx(s->lch, s->lcp, y)]);
    s->lch[y] = (uint8_t)b;
    if (!b) {
        memcpy(c, p, (size_t)L); memset(cg, 0, VG);
    } else {
        const uint8_t *c1 = y > 0 ? c - L : 0, *p2 = y < (int)VH - 1 ? p + L : 0;
        uint16_t *pg = s->m.pg, *pp = s->m.pp;
        uint32_t last = 0;
        for (int g = 0; g < (int)VG; g++) {
            uint32_t ch;
            VD_BIT(ch, &pg[vgctx(last, up, chp, vmixed3(p + 3 * g) ? 0x55 : 0, g)]);
            cg[g] = (uint8_t)ch;
            last = ch;
            uint8_t *q = c + 3 * g;
            if (!ch) { q[0] = p[3 * g]; q[1] = p[3 * g + 1]; q[2] = p[3 * g + 2]; continue; }
            for (int k = 0; k < 3; k++) {
                uint32_t w1 = vw_row3(c1, g, k), w0 = vw_row3(p, g, k), w2 = vw_row3(p2, g, k);
                uint32_t left = g > 0 ? c[3 * (g - 1) + k] & 3u : 0u, v = 0;
                for (int i = 0; i < 8; i++) {
                    uint32_t cross = k == 0 ? 0u : k == 1 ? (uint32_t)(q[0] >> (7 - i)) & 1u
                                   : ((uint32_t)(q[0] >> (7 - i)) & 1u) | ((uint32_t)(q[1] >> (7 - i)) & 1u) << 1;
                    VD_BIT(b, &pp[vctx3(left, w1, w0, w2, cross, k, i)]);
                    v = v << 1 | b;
                    left = (left << 1 | b) & 3u;
                }
                q[k] = (uint8_t)v;
            }
        }
    }
    s->d.range = rng; s->d.code = code; s->d.in = in;
}

/* une ligne, selon le nombre de plans */
static inline void vs_decode_line(vstate_t *s, int y) { if (s->np == 3) vs_line3(s, y); else vs_line(s, y); }

/* fin d'image : C devient la precedente */
static inline void vs_end(vstate_t *s) {
    uint8_t *t = s->P; s->P = s->C; s->C = t;
    t = s->chp; s->chp = s->chg; s->chg = t;
    memcpy(s->lcp, s->lch, VH);
}

/* ------------------------------------------------------------------ encodeur (PC) */
#ifdef EXLV_ENCODER
typedef struct {
    uint64_t low;
    uint32_t range, cache_size;
    uint8_t cache;
    uint8_t *out; size_t n, cap;
} venc_t;

static void ve_byte(venc_t *e, uint8_t b) {
    if (e->n == e->cap) { e->cap = e->cap ? e->cap * 2 : 1 << 20; e->out = (uint8_t *)realloc(e->out, e->cap); }
    e->out[e->n++] = b;
}
static void ve_shift(venc_t *e) {
    if ((uint32_t)e->low < 0xFF000000u || (e->low >> 32) != 0) {
        uint8_t t = e->cache;
        do { ve_byte(e, (uint8_t)(t + (uint8_t)(e->low >> 32))); t = 0xFF; } while (--e->cache_size);
        e->cache = (uint8_t)((uint32_t)e->low >> 24);
    }
    e->cache_size++;
    e->low = (uint64_t)((uint32_t)e->low << 8);
}
static void ve_init(venc_t *e) { memset(e, 0, sizeof *e); e->range = 0xFFFFFFFFu; e->cache_size = 1; }
static void ve_bit(venc_t *e, uint16_t *p, uint32_t b) {
    uint32_t bound = (e->range >> PB) * *p;
    if (!b) { e->range = bound; *p += (uint16_t)((PONE - *p) >> PMOVE); }
    else { e->low += bound; e->range -= bound; *p -= (uint16_t)(*p >> PMOVE); }
    while (e->range < (1u << 24)) { e->range <<= 8; ve_shift(e); }
}
static void ve_flush(venc_t *e) { for (int i = 0; i < 5; i++) ve_shift(e); }

/* code l'image N (meme parcours que vs_line) ; s->C doit contenir l'image N */
static void ve_frame(venc_t *e, vstate_t *s) {
    for (int y = 0; y < (int)VH; y++) {
        uint8_t *c = s->C + y * VG, *cg = s->chg + y * VG;
        const uint8_t *p = s->P + y * VG;
        uint32_t lc = memcmp(c, p, VG) != 0;
        ve_bit(e, &s->m.pl[vlctx(s->lch, s->lcp, y)], lc);
        s->lch[y] = (uint8_t)lc;
        if (!lc) { memset(cg, 0, VG); continue; }
        const uint8_t *chp = s->chp + y * VG, *up = y > 0 ? cg - VG : 0;
        uint32_t last = 0;
        for (int g = 0; g < (int)VG; g++) {
            uint32_t ch = c[g] != p[g];
            ve_bit(e, &s->m.pg[vgctx(last, up, chp, p[g], g)], ch);
            cg[g] = (uint8_t)ch;
            last = ch;
            if (!ch) continue;
            vwin_t w;
            vw_init(&w, s->C, s->P, y, g);
            uint32_t left = g > 0 ? c[g - 1] & 3u : 0u;
            for (int k = 0; k < 8; k++) {
                uint32_t b = (uint32_t)(c[g] >> (7 - k)) & 1u;
                ve_bit(e, &s->m.pp[vctx_k(&w, left, k)], b);
                left = (left << 1 | b) & 3u;
            }
        }
    }
}
/* couleur : meme parcours que vs_line3 */
static void ve_frame3(venc_t *e, vstate_t *s) {
    const int L = 3 * VG;
    for (int y = 0; y < (int)VH; y++) {
        uint8_t *c = s->C + y * L, *cg = s->chg + y * VG;
        const uint8_t *p = s->P + y * L;
        uint32_t lc = memcmp(c, p, (size_t)L) != 0;
        ve_bit(e, &s->m.pl[vlctx(s->lch, s->lcp, y)], lc);
        s->lch[y] = (uint8_t)lc;
        if (!lc) { memset(cg, 0, VG); continue; }
        const uint8_t *chp = s->chp + y * VG, *up = y > 0 ? cg - VG : 0;
        const uint8_t *c1 = y > 0 ? c - L : 0, *p2 = y < (int)VH - 1 ? p + L : 0;
        uint32_t last = 0;
        for (int g = 0; g < (int)VG; g++) {
            const uint8_t *q = c + 3 * g;
            uint32_t ch = memcmp(q, p + 3 * g, 3) != 0;
            ve_bit(e, &s->m.pg[vgctx(last, up, chp, vmixed3(p + 3 * g) ? 0x55 : 0, g)], ch);
            cg[g] = (uint8_t)ch;
            last = ch;
            if (!ch) continue;
            for (int k = 0; k < 3; k++) {
                uint32_t w1 = vw_row3(c1, g, k), w0 = vw_row3(p, g, k), w2 = vw_row3(p2, g, k);
                uint32_t left = g > 0 ? c[3 * (g - 1) + k] & 3u : 0u;
                for (int i = 0; i < 8; i++) {
                    uint32_t cross = k == 0 ? 0u : k == 1 ? (uint32_t)(q[0] >> (7 - i)) & 1u
                                   : ((uint32_t)(q[0] >> (7 - i)) & 1u) | ((uint32_t)(q[1] >> (7 - i)) & 1u) << 1;
                    uint32_t b = (uint32_t)(q[k] >> (7 - i)) & 1u;
                    ve_bit(e, &s->m.pp[vctx3(left, w1, w0, w2, cross, k, i)], b);
                    left = (left << 1 | b) & 3u;
                }
            }
        }
    }
}
#endif
#endif
