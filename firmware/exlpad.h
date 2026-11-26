/*
 * exlpad.h -- une manette USB "HID generique" (manettes SNES USB, manettes premier prix,
 * 8BitDo en mode DInput, DualShock 4, DualSense...) sur le port USB du Pico, en hote.
 *
 * Contrairement au clavier et a la souris, une manette n'a pas de format de rapport
 * standard : elle decrit ses rapports dans son "descripteur de rapport". pad_parse le
 * lit et retient ou sont les axes X / Y (Generic Desktop 0x30 / 0x31), la croix
 * (chapeau 0x39, ou les usages D-pad 0x90-0x93) et les boutons 1 a 16 (page 9).
 * pad_decode lit ensuite chaque rapport :
 *   - directions : la croix, ou le stick gauche (au-dela du quart de sa course,
 *     compte depuis le repos ; voir pad_decode) ;
 *   - boutons 1 a 4 (les boutons de face) : le feu ;
 *   - les autres boutons : des touches du clavier EXL100, envoyees par l'emetteur
 *     comme le clavier USB (PAD_KEYS) : 5 a 8 (L1 R1 L2 R2) -> 1 2 3 4,
 *     9 (Select / Share) -> ESPACE, 10 (Start / Options) -> ENTREE,
 *     11 a 16 -> 5 6 7 8 9 0.
 * (Numerotation des manettes SNES USB, DualShock 4 / DualSense et de la plupart des
 * manettes DInput ; elle varie un peu d'une marque a l'autre.)
 * Les directions et le feu ont le format de la DB9 (bit0 haut, bit1 bas, bit2 gauche,
 * bit3 droite, bit4 feu) : ils sont combines a la DB9, pour l'infrarouge comme pour
 * la zone d'echange. Seules les collections d'application "joystick", "gamepad" ou
 * "multi-axes" sont lues : les autres interfaces HID (touches multimedia d'un
 * clavier...) sont ignorees. Les manettes Xbox (XInput) ne sont pas des HID : non prises.
 */
#ifndef EXLPAD_H
#define EXLPAD_H
#include <stdint.h>
#include <string.h>
#include "exlkbd.h"

#define PAD_NBTN 16

typedef struct { uint16_t off; uint8_t size, rid, sign; int32_t lmin, lmax; } pad_field_t;   /* size 0 : absent */
#define PAD_NAX 4                                 /* champs "X" (ou "Y") retenus au plus */
typedef struct {
    uint8_t ok, rid;                              /* rid : numero de rapport (0 : aucun) */
    uint8_t nax[2];                               /* nombre de champs X, Y */
    pad_field_t ax[2][PAD_NAX];                   /* X, Y (plusieurs : manettes SNES USB) */
    pad_field_t hat, dp[4], btn[PAD_NBTN];        /* dp : D-pad haut, bas, droite, gauche */
    /* au fil des rapports : axes trouves loin du centre au premier rapport */
    uint8_t seen, odd[2][PAD_NAX];
} pad_layout_t;

/* l'etat de la manette, partage avec l'emetteur et la zone d'echange (meme coeur) */
typedef struct {
    volatile uint8_t joy;                         /* format DB9 */
    volatile uint8_t on;                          /* une manette est branchee */
    uint16_t keys;                                /* boutons 5..16 du rapport precedent */
    int8_t held;                                  /* bouton dont la touche est tenue, -1 : aucun */
    uint32_t kgen;                                /* g_kbd.gen de cette touche */
} pad_state_t;
extern pad_state_t g_pad;

static inline int32_t pad_item(const uint8_t *p, int n, int sign) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++) v |= (uint32_t)p[i] << (8 * i);
    if (sign && n && n < 4 && (v >> (8 * n - 1) & 1u)) v |= ~0u << (8 * n);
    return (int32_t)v;
}
/* le descripteur de rapport -> ou sont les commandes ; renvoie 1 si c'est une manette */
static inline int pad_parse(const uint8_t *d, uint32_t len, pad_layout_t *L) {
    memset(L, 0, sizeof *L);
    uint32_t page = 0, rsize = 0, rcount = 0, rid = 0, depth = 0, app = 0;
    int32_t lmin = 0, lmax = 0;
    uint32_t usages[16], nus = 0, umin = 0, umax = 0, hasmm = 0;
    uint8_t ids[8] = {0}; uint16_t offs[8] = {0}; uint32_t nid = 1;   /* rapport 0 d'abord */
    uint32_t cur = 0;                                                  /* index dans ids/offs */
    for (uint32_t i = 0; i < len;) {
        const uint8_t b = d[i];
        if (b == 0xFE) {                          /* element long : saute */
            if (i + 2 >= len) break;
            i += 3u + d[i + 1]; continue;
        }
        const int n = (b & 3) == 3 ? 4 : (b & 3);
        if (i + 1 + (uint32_t)n > len) break;
        const uint8_t *p = d + i + 1;
        const uint32_t uval = (uint32_t)pad_item(p, n, 0);
        switch (b & 0xFC) {
        case 0x04: page = uval; break;                                  /* Usage Page */
        case 0x14: lmin = pad_item(p, n, 1); break;                     /* Logical Minimum */
        case 0x24:                                                      /* Logical Maximum */
            lmax = pad_item(p, n, 1);
            if (lmax < lmin) lmax = (int32_t)uval;                      /* (code sans signe) */
            break;
        case 0x74: rsize = uval; break;                                 /* Report Size */
        case 0x94: rcount = uval; break;                                /* Report Count */
        case 0x84: {                                                    /* Report ID */
            rid = uval & 0xFFu;
            uint32_t k = 0;
            while (k < nid && ids[k] != rid) k++;
            if (k == nid && nid < 8) { ids[nid] = (uint8_t)rid; offs[nid] = 0; nid++; }
            cur = k < 8 ? k : 7;
            break;
        }
        case 0x08: if (nus < 16) usages[nus++] = n == 4 ? uval : (page << 16) | uval; break;   /* Usage */
        case 0x18: umin = n == 4 ? uval : (page << 16) | uval; hasmm = 1; break;               /* Usage Min */
        case 0x28: umax = n == 4 ? uval : (page << 16) | uval; hasmm = 1; break;               /* Usage Max */
        case 0xA0:                                                      /* Collection */
            if (depth == 0) app = (uval == 1 && nus) ? usages[0] : 0;  /* collection d'application */
            depth++; nus = 0; hasmm = 0; break;
        case 0xC0: if (depth) depth--; break;                           /* End Collection */
        case 0x80: {                                                    /* Input */
            const int constant = uval & 1, variable = (uval >> 1) & 1;
            const int padapp = app == 0x10004u || app == 0x10005u || app == 0x10008u;
            for (uint32_t k = 0; k < rcount && k < 64; k++) {      /* (les longs blocs : donnees du fabricant) */
                uint32_t u = 0;
                if (nus) u = usages[k < nus ? k : nus - 1];
                else if (hasmm) u = (umin + k <= umax) ? umin + k : umax;
                const uint32_t off = offs[cur] + k * rsize;
                if (padapp && !constant && variable && rsize && rsize <= 32 && off < 0xFFFF) {
                    pad_field_t f = { (uint16_t)off, (uint8_t)rsize, (uint8_t)rid, (uint8_t)(lmin < 0), lmin, lmax };
                    pad_field_t *t = 0;
                    if (u == 0x10030u || u == 0x10031u) {
                        const int a = u == 0x10031u;
                        if (L->nax[a] < PAD_NAX) L->ax[a][L->nax[a]++] = f;
                    } else if (u == 0x10039u) t = &L->hat;
                    else if (u >= 0x10090u && u <= 0x10093u) t = &L->dp[u - 0x10090u];
                    else if ((u >> 16) == 9 && (u & 0xFFFF) >= 1 && (u & 0xFFFF) <= PAD_NBTN) t = &L->btn[(u & 0xFFFF) - 1];
                    if (t && !t->size) *t = f;
                }
            }
            const uint32_t end = offs[cur] + (rcount < 0x10000u && rsize < 0x10000u ? rcount * rsize : 0xFFFFu);
            offs[cur] = (uint16_t)(end < 0xFFFFu ? end : 0xFFFFu);
            nus = 0; hasmm = 0; break;
        }
        case 0x90: case 0xB0: nus = 0; hasmm = 0; break;               /* Output, Feature */
        }
        i += 1u + (uint32_t)n;
    }
    /* le rapport de la manette : celui du premier X (ou de la croix) ; les autres champs
     * doivent y etre (une manette n'a qu'un rapport d'entree utile) */
    const pad_field_t *ref = L->nax[0] ? &L->ax[0][0] : L->hat.size ? &L->hat : L->dp[0].size ? &L->dp[0] : 0;
    if (!ref) return 0;
    L->rid = ref->rid;
    for (int a = 0; a < 2; a++) {
        uint8_t m = 0;
        for (int k = 0; k < L->nax[a]; k++) if (L->ax[a][k].rid == L->rid) L->ax[a][m++] = L->ax[a][k];
        L->nax[a] = m;
    }
    pad_field_t *all[] = { &L->hat, &L->dp[0], &L->dp[1], &L->dp[2], &L->dp[3] };
    for (unsigned k = 0; k < sizeof all / sizeof all[0]; k++) if (all[k]->size && all[k]->rid != L->rid) all[k]->size = 0;
    for (int k = 0; k < PAD_NBTN; k++) if (L->btn[k].size && L->btn[k].rid != L->rid) L->btn[k].size = 0;
    L->ok = (L->nax[0] && L->nax[1]) || L->hat.size || L->dp[0].size;
    return L->ok;
}
static inline int32_t pad_get(const pad_field_t *f, const uint8_t *r, uint32_t len) {
    uint32_t v = 0;
    for (uint32_t k = 0; k < f->size; k++) {
        const uint32_t bit = f->off + k;
        if ((bit >> 3) < len && (r[bit >> 3] >> (bit & 7) & 1u)) v |= 1u << k;
    }
    if (f->sign && f->size < 32 && (v >> (f->size - 1) & 1u)) v |= ~0u << f->size;
    return (int32_t)v;
}
/* un rapport -> directions et feu (format DB9) ; *keys = boutons 5..16 (bit 0 = bouton 5).
 * Un axe que le premier rapport trouve loin du centre (octet fixe, comme les "X" en
 * trop des manettes SNES USB, ou stick tenu au branchement) est ignore tant qu'il
 * n'est pas passe par le centre. */
static inline uint8_t pad_decode(pad_layout_t *L, const uint8_t *r, uint32_t len, uint16_t *keys) {
    *keys = 0;
    if (L->rid) { if (!len || r[0] != L->rid) return 0xFF; r++; len--; }   /* 0xFF : pas ce rapport */
    uint8_t v = 0;
    for (int a = 0; a < 2; a++) {
        for (int k = 0; k < L->nax[a]; k++) {
            const pad_field_t *f = &L->ax[a][k];
            const int64_t val = pad_get(f, r, len), lo = f->lmin, hi = f->lmax > f->lmin ? f->lmax : f->lmin + 1;
            const int64_t c = (lo + hi) / 2, q = (hi - lo) / 4;
            const int near = val > c - q && val < c + q;
            if (!L->seen) L->odd[a][k] = !near;
            else if (near) L->odd[a][k] = 0;
            if (L->odd[a][k]) continue;
            if (val <= c - q) v |= a ? 0x01 : 0x04;   /* haut / gauche */
            if (val >= c + q) v |= a ? 0x02 : 0x08;   /* bas / droite */
        }
    }
    L->seen = 1;
    if (L->hat.size) {
        const int32_t h = pad_get(&L->hat, r, len) - L->hat.lmin;
        const int32_t span = L->hat.lmax - L->hat.lmin;
        static const uint8_t H8[8] = { 0x01, 0x09, 0x08, 0x0A, 0x02, 0x06, 0x04, 0x05 };
        static const uint8_t H4[4] = { 0x01, 0x08, 0x02, 0x04 };
        if (span == 7 && h >= 0 && h <= 7) v |= H8[h];
        else if (span == 3 && h >= 0 && h <= 3) v |= H4[h];
    }
    static const uint8_t DP[4] = { 0x01, 0x02, 0x08, 0x04 };
    for (int k = 0; k < 4; k++) if (L->dp[k].size && pad_get(&L->dp[k], r, len)) v |= DP[k];
    for (int k = 0; k < PAD_NBTN; k++) {
        if (!L->btn[k].size || !pad_get(&L->btn[k], r, len)) continue;
        if (k < 4) v |= 0x10; else *keys |= (uint16_t)(1u << (k - 4));
    }
    return v;
}
/* boutons 5..16 -> touches du clavier EXL100 : 1 2 3 4 ESPACE ENTREE 5 6 7 8 9 0 */
static const uint8_t PAD_KEYS[12] = { 20, 28, 29, 52, 6, 59, 60, 21, 53, 22, 30, 23 };

static inline void pad_reset(pad_state_t *p, kbd_state_t *k) {
    if (p->held >= 0 && k->gen == p->kgen) k->down = 0;   /* la touche tenue : relachee */
    p->joy = 0; p->keys = 0; p->held = -1;
}
/* l'etat lu d'une manette (HID ou XInput) -> g_pad (directions, feu) et la touche du
 * clavier (k) : un bouton nouvellement appuye envoie sa touche, le relacher la relache. */
static inline void pad_apply(pad_state_t *p, kbd_state_t *k, uint8_t v, uint16_t keys) {
    p->joy = v;
    if (p->held >= 0 && !(keys >> p->held & 1u)) {
        if (k->gen == p->kgen) k->down = 0;       /* (sauf si le clavier a pris la main) */
        p->held = -1;
    }
    const uint16_t nw = keys & (uint16_t)~p->keys;
    if (nw) {
        int b = 0;
        while (!(nw >> b & 1u)) b++;
        k->code = PAD_KEYS[b]; k->npre = 0; k->usage = 0; k->down = 1;
        k->gen++;
        p->held = (int8_t)b; p->kgen = k->gen;
    }
    p->keys = keys;
}
/* un rapport d'une manette HID */
static inline void pad_report(pad_state_t *p, kbd_state_t *k, pad_layout_t *L, const uint8_t *r, uint32_t len) {
    uint16_t keys;
    const uint8_t v = pad_decode(L, r, len, &keys);
    if (v == 0xFF) return;                        /* un autre rapport de l'appareil */
    pad_apply(p, k, v, keys);
}
#endif
