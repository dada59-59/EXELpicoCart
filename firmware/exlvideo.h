/*
 * exlvideo.h -- mode VIDEO d'EXLCART (mode ROM unique) : le Pico decode une
 * video noir et blanc rangee dans sa flash et la fait afficher par l'EXL100.
 *
 * La ROM (le lecteur, par exemple badapple.asm) reserve une zone "EXLV" :
 *   +0  V_FIFO  le Pico y presente l'octet suivant de la liste apres CHAQUE lecture
 *   +1  V_STAT  0 : liste pas prete ; 1 : prete
 *   +2  V_PRES  $A5 : video presente ; $5A : firmware video mais pas de video valide
 *   +3  V_SYNC  lecture = "image affichee, la suivante svp"
 *   +4  "EXLV"  +8 version (1)  +9 V_RST (lecture = redemarrage, ecran vide)
 *   +10 table des commandes (JT), +12 deplacements ADR / FIN / BRUT,
 *   +15 groupes : premier deplacement, nombre maxi, taille d'un pas,
 *   +18 table des octets bruts (RT), +20 nombre maxi, taille d'un pas,
 *   +22 adresse VRAM de la ligne de texte, +24 de la premiere ligne bitmap,
 *   +26 octets par ligne bitmap, +27 lignes bitmap, +28 groupes par ligne,
 *   +29 attribut du texte, +30 glyphe vide.
 * Version 3 (musique 1 bit) : +31 V_SON (le Pico y met 1), +32 S_MASK, +33 S_REL
 *   (lus par l'interruption de la minuterie du lecteur), +34 type (1), +35 prediviseur.
 * Les mots sont rangees poids fort d'abord (comme .word sur TMS7000).
 *
 * La liste d'une image (preparee par le coeur 0, servie par le coeur 1) :
 *   ADR lo hi | GROUPES n octets | BRUT n octets | ... | FIN
 * Le coeur 0 compare l'image du moment avec ce que l'EXL100 affiche deja
 * (vb->sent) et n'envoie que les groupes de 8 pixels qui changent.
 *
 * Le coeur 1 sert le bus avec exactement les instructions de la ROM unique
 * pendant l'acces ; apres l'acces (bus relache) :
 *   V_FIFO : la case V_FIFO recoit l'octet suivant de la liste ;
 *   V_STAT : relit le pointeur de liste publie par le coeur 0 ;
 *   V_SYNC, V_RST : V_STAT = 0 et signal au coeur 0.
 * Le coeur 0 decode la video au rythme de ses images (horloge du Pico : le
 * film garde sa vitesse, l'EXL100 saute les images qu'il n'a pas le temps
 * d'afficher) et prepare la liste a chaque demande.
 *
 * Flash : en-tete vhdr_t (exlvcodec.h) a 8 Mo, puis les donnees.
 */
#ifndef EXLVIDEO_H
#define EXLVIDEO_H
#include <stdint.h>
#include <string.h>
#include "exlpins.h"
#include "exlvcodec.h"

#define VID_FLASH_OFFSET  0x00800000u              /* 8 Mo : apres la bibliotheque */
#define VID_MAX_SIZE      (0x00800000u - 64u)
#define VID_PRESENT       0xA5u
#define VID_NOVIDEO       0x5Au
#define VID_STREAM_CAP    12544u                   /* noir et blanc, pire cas : 240 x 50 + texte + FIN */
#define VID_GAP_MERGE3    1u                       /* couleur : un groupe inchange coute 63 cycles */
#define VID_HOLD_US       2000000u                 /* derniere image tenue 2 s avant de reboucler */
#define VID_GAP_MERGE     2u                       /* trou de 2 groupes au plus : reecrit */

typedef struct {
    uint32_t fifo, stat, pres, sync, rst;           /* adresses dans la ROM */
    uint16_t text, graph;                           /* adresses en VRAM */
    uint8_t  c_adr, c_end, c_raw, g_first, g_max, g_size, r_max, r_size;
    uint8_t  stride, lines, groups, attr, blank;
    uint32_t son;                                   /* zone version 3 : V_SON (1 = musique 1 bit), 0 : aucune */
    uint32_t smask, srel;                           /* zone version 3 : S_MASK, S_REL (lus par l'interruption) */
} vzone_t;

/* zone EXLV dans la ROM (octets normaux), ou 0 si absente / incoherente */
static inline int vz_find(const uint8_t *r, uint32_t limit, vzone_t *z) {
    for (uint32_t a = 0; a + 36 <= limit; a++) {
        const uint8_t *p = r + a;
        if (p[4] != 'E' || p[5] != 'X' || p[6] != 'L' || p[7] != 'V' || p[0] | p[1] | p[2] | p[3]) continue;
        if (p[8] != 1 && p[8] != 3) continue;
        z->son = 0; z->smask = 0; z->srel = 0;
        if (p[8] == 3 && p[34] == 1) {               /* version 3 : musique 1 bit (sortie cassette) */
            z->son = a + 31; z->smask = a + 32; z->srel = a + 33;
        }
        z->fifo = a; z->stat = a + 1; z->pres = a + 2; z->sync = a + 3; z->rst = a + 9;
        z->c_adr = p[12]; z->c_end = p[13]; z->c_raw = p[14];
        z->g_first = p[15]; z->g_max = p[16]; z->g_size = p[17];
        z->r_max = p[20]; z->r_size = p[21];
        z->text = (uint16_t)(p[22] << 8 | p[23]); z->graph = (uint16_t)(p[24] << 8 | p[25]);
        z->stride = p[26]; z->lines = p[27]; z->groups = p[28]; z->attr = p[29]; z->blank = p[30];
        if (z->lines != VH || z->groups != VG || z->g_max < 1 || z->r_max < 1 || z->stride < 3 * VG
            || (uint32_t)z->g_first + (uint32_t)(z->g_max - 1) * z->g_size > 255
            || (uint32_t)(z->r_max - 1) * z->r_size > 255) return 0;
        return 1;
    }
    return 0;
}

/* en-tete de video valide (sans la somme : verifiee a part, vh_sum_ok) */
static inline int vh_ok(const vhdr_t *h) {
    return h->magic == VMAGIC && (h->version == VVERSION || h->version == VVERSION_COUL) && h->width == VW && h->height == VH
        && h->frames >= 1 && h->size >= 5 && h->size <= VID_MAX_SIZE && h->fps_milli >= 1000 && h->fps_milli <= 60000;
}
static inline uint32_t vh_planes(const vhdr_t *h) { return h->version == VVERSION_COUL ? 3u : 1u; }
static inline int vh_sum_ok(const vhdr_t *h) {
    const uint8_t *d = (const uint8_t *)h + sizeof *h;
    uint32_t s = 0;
    for (uint32_t i = 0; i < h->size; i++) s += d[i];
    return s == h->sum;
}


/* ---------------------------------------------------------------- la musique 1 bit
 * La sortie cassette de l'EXL100 (bit 3 du port B, P6) est reliee au son du televiseur.
 * Le lecteur fait tourner la minuterie 1 du TMS7020 ; a chaque debordement, son
 * interruption (appelee par le moniteur via BRTIME, $C004) fait :
 *     lda @S_MASK ; xorp A,P6     -> $08 : inverse la sortie, 0 : rien
 *     lda @S_REL  ; movp A,P2     -> valeur de recharge de la minuterie
 * Le decompteur se recharge tout seul au debordement, avec la valeur ecrite AVANT : la
 * valeur lue a l'interruption j fixe l'ecart entre les debordements j+1 et j+2. Le
 * temps de reponse de l'interruption ne s'accumule donc pas.
 * Le coeur 1 date chaque lecture de S_MASK et, apres celle de S_REL, presente l'entree
 * suivante de la file (masque, recharge) que le coeur 0 prepare quelques ms en avance.
 * Le coeur 0 mesure d'abord la minuterie, a vide : 16 ecarts avec la recharge 255, puis
 * 48 avec 63, ce qui donne la duree d'une "unite" et la regle exacte (ecart = recharge
 * + 1 + delta unites ; delta = 0 d'apres la documentation, mesure quand meme). Puis il
 * place chaque inversion a l'heure voulue, en suivant la video (meme origine, V.t0) et
 * en se recalant sur les lectures datees.
 *
 * Bloc "EXL1" juste apres les donnees video (aligne sur 4) : en-tete bhdr_t, puis un mot
 * de 16 bits par evenement : bits 0-14 = ecart en us depuis le precedent (le premier :
 * depuis le debut du film), bit 15 = inverser la sortie (0 : simple reveil).
 */
#define BMAGIC      0x314C5845u                      /* "EXL1" */
#define SB_Q        64u                              /* file (puissance de 2) */
#define SB_DEPTH    56u                              /* entrees preparees en avance, au plus */
#define SB_AHEAD_US 12000u                           /* ... et pas plus de 12 ms (au moins 4) */
#define SB_CAL_REL  255u                             /* recharge a vide (et celle du lecteur au depart) */
#define SB_CAL_REL2 63u                              /* mesure : NA ecarts a 255, puis NB a 63 */
#define SB_NA       16u
#define SB_NB       48u
#define SB_MIN_US   100u                             /* ecart minimal entre deux debordements */
#define SB_LATE_US  3000u                            /* evenement en retard de plus : saute */
#define SB_RESYNC_US 1500u                           /* ecart de plus : on se recale d'un coup */
#define SB_DELAY_US 20000u                           /* l'image s'affiche ~20 ms apres son heure */
#define SB_TOGGLE   0x08u                            /* bit 3 du port B */

typedef struct {
    uint32_t magic;
    uint16_t version, res;
    uint32_t nev, ms, sum;                           /* evenements, duree, somme des octets */
} bhdr_t;                                            /* 20 octets */

static inline const bhdr_t *sb_find(const vhdr_t *h) {
    uintptr_t p = ((uintptr_t)h + sizeof *h + h->size + 3u) & ~(uintptr_t)3u;
    const bhdr_t *b = (const bhdr_t *)p;
    if (b->magic != BMAGIC || b->version != 1 || b->nev == 0 || b->nev > 0x1000000u) return 0;
    return b;
}
static inline const uint16_t *sb_ev(const bhdr_t *b) { return (const uint16_t *)(b + 1); }
static inline int sb_sum_ok(const bhdr_t *b) {
    const uint8_t *d = (const uint8_t *)(b + 1);
    uint32_t s = 0, n = (b->nev * 2u + 3u) & ~3u;
    for (uint32_t i = 0; i < n; i++) s += d[i];
    return s == b->sum;
}

typedef struct {
    const bhdr_t *h;
    const uint16_t *ev;
    uint32_t ei, et;                 /* prochain evenement, son heure (us depuis le debut du film) */
    int64_t t0;                      /* origine du film (us du Pico) */
    int started;
    uint32_t qn;                     /* prochaine entree a remplir (= numero de debordement) */
    uint32_t seen;                   /* lectures deja traitees */
    int phase;                       /* 0 : attend la 1re lecture ; 1 : mesure ; 2 : en marche */
    uint32_t kA, kB; int64_t tA, tB; /* mesure : premiere lecture ; entrees >= kB a SB_CAL_REL2 */
    uint32_t u16;                    /* duree d'une unite de la minuterie (us << 16) */
    int32_t delta;                   /* ecart = recharge + 1 + delta */
    uint32_t mind;                   /* ecart minimal en unites */
    uint64_t C[SB_Q];                /* unites cumulees au debordement j */
    uint8_t  m[SB_Q];                /* masque decide pour le debordement j */
    int64_t  o16; uint64_t Co;       /* heure (us << 16) du debordement ou C = Co */
    int64_t  tm16; uint64_t Cm;      /* point d'appui de la mesure de l'unite */
    cell_t   qm[SB_Q], qr[SB_Q];     /* la file servie par le coeur 1 */
    uint32_t resyncs, late, toggles; /* statistiques */
} sbit_t;

static inline void sb_put(sbit_t *S, uint32_t j, uint32_t mask, uint32_t rel) {
    S->qm[j & (SB_Q - 1u)] = ENC(mask); S->qr[j & (SB_Q - 1u)] = ENC(rel);
}
/* (re)demarrage du lecteur : k = prochain debordement que la console lira */
static inline void sb_reset(sbit_t *S, uint32_t k) {
    S->qn = k; S->seen = k; S->phase = 0; S->started = 0;
    for (uint32_t j = 0; j < SB_Q; j++) sb_put(S, k + j, 0, SB_CAL_REL);
    S->qn = k + 4u;
}
static inline int64_t sb_pred(const sbit_t *S, uint32_t j) {
    return S->o16 + (int64_t)((S->C[j & (SB_Q - 1u)] - S->Co) * S->u16);
}
static inline void sb_next_event(sbit_t *S) {
    if (++S->ei < S->h->nev) S->et += S->ev[S->ei] & 0x7FFFu;
}
/* la lecture datee de l'interruption k (t : us du Pico) */
static inline void sb_read(sbit_t *S, uint32_t k, int64_t t) {
    if (S->phase == 0) {             /* l'ecart k -> k+1 suit la recharge de l'entree k-1 */
        S->kA = k; S->tA = t; S->phase = 1;
        S->kB = (int32_t)(S->qn - (k + SB_NA)) > 0 ? S->qn : k + SB_NA;
        return;
    }
    if (S->phase == 1) {
        if (k == S->kB + 1u) S->tB = t;
        if (k != S->kB + 1u + SB_NB) return;
        const int64_t nA = (int64_t)(S->kB + 1u - S->kA), nB = SB_NB;
        const int64_t a16 = ((S->tB - S->tA) << 16) / nA, b16 = ((t - S->tB) << 16) / nB;   /* us << 16 */
        int64_t u = (a16 - b16) / (int64_t)(SB_CAL_REL - SB_CAL_REL2);
        if (u < (1 << 16)) u = 1 << 16;
        S->u16 = (uint32_t)u;
        int64_t d = (b16 + u / 2) / u - (int64_t)(SB_CAL_REL2 + 1);       /* arrondi */
        S->delta = d < -2 ? -2 : d > 2 ? 2 : (int32_t)d;
        S->mind = (uint32_t)((((uint64_t)SB_MIN_US << 16) + S->u16 - 1) / S->u16);
        if ((int32_t)S->mind < 1 + S->delta) S->mind = (uint32_t)(1 + S->delta);
        const uint32_t D2 = (uint32_t)((int32_t)SB_CAL_REL2 + 1 + S->delta);
        for (uint32_t j = k; j != S->qn + 2u; j++) {           /* entrees deja servies : a 63 */
            S->C[j & (SB_Q - 1u)] = (uint64_t)(j - k) * D2;
            S->m[j & (SB_Q - 1u)] = 0;
        }
        S->Co = 0; S->o16 = t << 16; S->Cm = 0; S->tm16 = t << 16;
        S->phase = 2;
        return;
    }
    if ((int32_t)(S->qn - k) > (int32_t)SB_Q - 2 || (int32_t)(S->qn - k) < 0) return;
    const uint64_t Ck = S->C[k & (SB_Q - 1u)];
    const int64_t t16 = t << 16, r = t16 - sb_pred(S, k);
    if (r > ((int64_t)SB_RESYNC_US << 16) || r < -((int64_t)SB_RESYNC_US << 16)) {
        S->o16 = t16; S->Co = Ck; S->tm16 = t16; S->Cm = Ck; S->resyncs++;  /* interruption perdue ? */
        return;
    }
    S->o16 += r < 0 ? r / 4 : r / 64;                  /* suit les lectures les plus promptes : un retard
                                                        d'interruption ne decale pas les suivantes */
    if (Ck - S->Cm > 0 && (uint64_t)(Ck - S->Cm) * S->u16 > ((uint64_t)500000 << 16)) {   /* l'unite, sur 0,5 s et plus */
        const int64_t p = sb_pred(S, k);
        S->u16 = (uint32_t)((t16 - S->tm16) / (int64_t)(Ck - S->Cm));
        S->o16 = p; S->Co = Ck;
    }
}
/* prepare les entrees jusqu'a SB_DEPTH devant le debordement k et SB_AHEAD_US devant
 * l'heure now (au moins 4) ; t0 : origine du film (us du Pico) */
static inline void sb_fill(sbit_t *S, uint32_t k, int64_t now, int64_t t0) {
    if (!S->started || t0 != S->t0) {                  /* debut du film, ou il reboucle */
        S->t0 = t0; S->started = 1; S->ei = 0; S->et = S->ev[0] & 0x7FFFu;
    }
    while ((int32_t)(k + SB_DEPTH - S->qn) > 0) {
        const uint32_t j = S->qn;
        if (S->phase < 2) {
            if ((int32_t)(S->qn - k) >= 8) break;
            sb_put(S, j, 0, S->phase == 1 && (int32_t)(j - S->kB) >= 0 ? SB_CAL_REL2 : SB_CAL_REL); S->qn++; continue;
        }
        const uint32_t i1 = (j + 1u) & (SB_Q - 1u), i2 = (j + 2u) & (SB_Q - 1u);
        const int64_t U1 = sb_pred(S, j + 1u);
        if ((int32_t)(S->qn - k) >= 4 && U1 > ((now + SB_AHEAD_US) << 16)) break;
        const uint32_t DMAX = (uint32_t)(256 + S->delta);
        uint32_t D = DMAX, m2 = 0;
        while (S->ei < S->h->nev) {
            const int64_t T = (S->t0 + SB_DELAY_US + (int64_t)S->et) << 16, rem = T - U1;
            if (rem < -((int64_t)SB_LATE_US << 16)) { S->late++; sb_next_event(S); continue; }
            int64_t ru = rem > 0 ? (rem + S->u16 / 2) / S->u16 : 0;
            if (ru > DMAX) { D = ru > 2 * (int64_t)DMAX ? DMAX : (uint32_t)(ru / 2); if (D < S->mind) D = S->mind; break; }
            D = ru < S->mind ? S->mind : (uint32_t)ru;
            if (S->ev[S->ei] & 0x8000u) { m2 = SB_TOGGLE; S->toggles++; }
            sb_next_event(S);
            break;
        }
        sb_put(S, j, S->m[j & (SB_Q - 1u)], (uint32_t)((int32_t)D - 1 - S->delta));
        S->C[i2] = S->C[i1] + D; S->m[i2] = (uint8_t)m2;
        S->qn++;
    }
}

/* ---------------------------------------------------------------- la liste */
typedef struct {
    const vzone_t *z;
    uint32_t np;                     /* plans : 1 (noir et blanc) ou 3 (couleur) */
    uint8_t *sent;                   /* ce que l'EXL100 affiche (apres la derniere liste) : VFRAME * np */
    uint8_t text[80];                /* sa ligne de texte (attribut, code) x 40 */
    cell_t *buf; uint32_t n, cap;    /* la liste en cours, deja codee pour le service */
    int partial;                     /* la derniere liste n'a pas tout envoye (place) */
} vbuild_t;

static inline void vb_reset(vbuild_t *b) {
    memset(b->sent, 0, VFRAME * b->np);
    for (int i = 0; i < 40; i++) { b->text[2 * i] = b->z->attr; b->text[2 * i + 1] = b->z->blank; }
}
static inline void vb_put(vbuild_t *b, uint8_t v) { if (b->n < b->cap) b->buf[b->n++] = ENC(v); }
static inline void vb_adr(vbuild_t *b, uint32_t a) { vb_put(b, b->z->c_adr); vb_put(b, (uint8_t)a); vb_put(b, (uint8_t)(a >> 8)); }

static void vb_raw(vbuild_t *b, const uint8_t *d, uint32_t len) {
    const vzone_t *z = b->z;
    uint32_t i = 0;
    while (i < len) {
        uint32_t k = len - i < z->r_max ? len - i : z->r_max;
        vb_put(b, z->c_raw); vb_put(b, (uint8_t)((z->r_max - k) * z->r_size));
        for (uint32_t j = 0; j < k; j++) vb_put(b, d[i + j]);
        i += k;
    }
}
static inline int vb_mono3(const uint8_t *q) { return q[0] == q[1] && q[1] == q[2]; }

/* couleur : une ligne. Dans chaque passage, les groupes "noir ou blanc" (3 plans
 * egaux) par paquets de 2 ou plus partent en GROUPES (41 cycles par groupe pour
 * l'EXL100), les autres en BRUT (3 octets, 63 cycles). */
static void vb_line3(vbuild_t *b, const uint8_t *row, uint8_t *old, uint32_t y) {
    const vzone_t *z = b->z;
    uint32_t g = 0;
    while (g < VG) {
        if (!memcmp(row + 3 * g, old + 3 * g, 3)) { g++; continue; }
        uint32_t s = g, e = g;
        for (uint32_t k = g + 1; k < VG; k++) {
            if (memcmp(row + 3 * k, old + 3 * k, 3)) {
                if (k - e - 1 <= VID_GAP_MERGE3) e = k; else break;
            }
        }
        vb_adr(b, z->graph + y * z->stride + 3u * s);
        uint32_t i = s;
        while (i <= e) {
            uint32_t m = i;
            while (m <= e && vb_mono3(row + 3 * m)) m++;
            if (m - i >= 2) {                                    /* noir ou blanc : GROUPES */
                while (i < m) {
                    uint32_t k = m - i < z->g_max ? m - i : z->g_max;
                    vb_put(b, (uint8_t)(z->g_first + (z->g_max - k) * z->g_size));
                    for (uint32_t j = 0; j < k; j++) vb_put(b, row[3 * (i + j)]);
                    i += k;
                }
                continue;
            }
            uint32_t j = i;                                      /* couleur : BRUT */
            while (j <= e && !(vb_mono3(row + 3 * j) && j + 1 <= e && vb_mono3(row + 3 * (j + 1)))) j++;
            vb_raw(b, row + 3 * i, 3 * (j - i));
            i = j;
        }
        memcpy(old + 3 * s, row + 3 * s, 3 * (e - s + 1));
        g = e + 1;
    }
}

/* liste complete : texte (si change), lignes bitmap qui changent, FIN.
 * img : VFRAME * np octets ; text : 80 octets ou NULL. Renvoie le nombre d'octets. */
static uint32_t vb_build(vbuild_t *b, const uint8_t *img, const uint8_t *text) {
    const vzone_t *z = b->z;
    b->n = 0;
    if (text) {
        int a = -1, e = -1;
        for (int i = 0; i < 40; i++)
            if (text[2 * i] != b->text[2 * i] || text[2 * i + 1] != b->text[2 * i + 1]) { if (a < 0) a = i; e = i; }
        if (a >= 0) {
            vb_adr(b, z->text + 2u * (uint32_t)a);
            vb_raw(b, text + 2 * a, 2u * (uint32_t)(e - a + 1));
            memcpy(b->text, text, 80);
        }
    }
    b->partial = 0;
    if (b->np == 3) {
        for (uint32_t y = 0; y < VH; y++) {
            if (b->n + 200u > b->cap) { b->partial = 1; break; }   /* liste pleine : la suite a la prochaine */
            const uint8_t *row = img + y * 3 * VG;
            uint8_t *old = b->sent + y * 3 * VG;
            if (memcmp(row, old, 3 * VG)) vb_line3(b, row, old, y);
        }
        vb_put(b, z->c_end);
        return b->n;
    }
    for (uint32_t y = 0; y < VH; y++) {
        if (b->n + 64u > b->cap) { b->partial = 1; break; }   /* (jamais en pratique) */
        const uint8_t *row = img + y * VG;
        uint8_t *old = b->sent + y * VG;
        if (!memcmp(row, old, VG)) continue;         /* ligne inchangee */
        uint32_t g = 0;
        while (g < VG) {
            if (row[g] == old[g]) { g++; continue; }
            uint32_t s = g, e = g;                   /* un passage : de s a e inclus */
            for (uint32_t k = g + 1; k < VG; k++) {
                if (row[k] != old[k]) {
                    if (k - e - 1 <= VID_GAP_MERGE) e = k; else break;
                }
            }
            vb_adr(b, z->graph + y * z->stride + 3u * s);
            uint32_t i = s;
            while (i <= e) {
                uint32_t k = e - i + 1 < z->g_max ? e - i + 1 : z->g_max;
                vb_put(b, (uint8_t)(z->g_first + (z->g_max - k) * z->g_size));
                for (uint32_t j = 0; j < k; j++) vb_put(b, row[i + j]);
                i += k;
            }
            memcpy(old + s, row + s, e - s + 1);
            g = e + 1;
        }
    }
    vb_put(b, z->c_end);
    return b->n;
}

/* ligne de texte : titre a gauche (jaune), temps et images/s a droite (blanc) */
static inline uint8_t vt_code(const vzone_t *z, char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    return (c > ' ' && c < 0x60) ? (uint8_t)c : z->blank;
}
static void vt_make(const vzone_t *z, uint8_t *t, const uint8_t *title, uint32_t secs, uint32_t ips) {
    const uint8_t bank = z->attr & 0x1F, yellow = (uint8_t)(0x60 | bank), white = (uint8_t)(0xE0 | bank);
    char s[41];
    for (int i = 0; i < 40; i++) s[i] = ' ';
    s[40] = 0;
    for (int i = 0; i < 25; i++) s[1 + i] = (char)title[i];   /* colonnes 1-25 */
    /* "m:ss  nn IM/S" sur les colonnes 27-39 */
    char *q = s + 27;
    uint32_t m = secs / 60 % 10, ss = secs % 60;
    q[0] = (char)('0' + m); q[1] = ':'; q[2] = (char)('0' + ss / 10); q[3] = (char)('0' + ss % 10);
    if (ips > 99) ips = 99;
    q[5] = ips >= 10 ? (char)('0' + ips / 10) : ' '; q[6] = (char)('0' + ips % 10);
    q[8] = 'I'; q[9] = 'M'; q[10] = '/'; q[11] = 'S';
    for (int i = 0; i < 40; i++) { t[2 * i] = i < 27 ? yellow : white; t[2 * i + 1] = vt_code(z, s[i]); }
}

/* ---------------------------------------------------------------- le lecteur (coeur 0) */
typedef struct {
    const vhdr_t *h;
    const uint8_t *data;
    vstate_t s;                      /* decodeur : P = derniere image complete */
    uint32_t k;                      /* prochaine image a decoder */
    uint32_t line;                   /* ligne en cours de decodage (0 : entre deux images) */
    uint32_t t0, t_end;              /* debut du film, fin (derniere image tenue) */
    int have_new;                    /* une image complete pas encore envoyee */
    uint32_t shown, shown_sec, ips, sec_mark, secs;   /* compteurs pour la ligne de texte */
    uint8_t text[80];
} vplay_t;

static inline uint32_t vp_due(const vplay_t *v, uint32_t k) {
    return v->t0 + (uint32_t)((uint64_t)k * 1000000000u / v->h->fps_milli);
}
static inline void vp_restart(vplay_t *v, uint32_t now) {
    vs_start(&v->s, v->data, vh_planes(v->h));
    v->k = 0; v->line = 0; v->t0 = now; v->have_new = 0;
}

/* ================================================================ materiel (Pico) */
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "hardware/structs/sio.h"
#include "hardware/structs/timer.h"
#include "hardware/timer.h"
#include "hardware/sync.h"
#include "exlbanks.h"                             /* img_pool[] : memoire de travail */

extern cell_t rom[ROM_SIZE];
static vzone_t VZ;
static vplay_t V;
static vbuild_t VB;
static volatile uint32_t g_vreq, g_vrst;          /* poses par le service (V_SYNC, V_RST) */
static const cell_t *volatile g_vsp;              /* liste publiee : octet 2 et suivants */
static int g_vrun;                                /* le lecteur a demarre (V_RST lu) */
static int g_vok;                                 /* video valide dans la flash */
static sbit_t SB;                                 /* musique 1 bit (coeur 0 ; la file est lue par le coeur 1) */
static volatile uint32_t g_sk;                    /* prochaine interruption de la minuterie (coeur 1) */
static volatile uint32_t g_sqn;                   /* entrees de la file pretes (coeur 0) */
static volatile uint32_t g_st[SB_Q];              /* heure de la lecture de S_MASK, par interruption */
static int g_son;                                 /* musique presente et lecteur version 3 */

#ifdef EXL_SINGLE_CORE_TEST
static void vid_step(void);
#define VID_AFTER_ACCESS()  vid_step()            /* essai sur un coeur : le travail entre deux acces */
#else
#define VID_AFTER_ACCESS()  ((void)0)
#endif

/* service : pendant l'acces, les instructions de la ROM unique ; apres, la liste */
static void __noinline __not_in_flash_func(serve_video)(void) {
    cell_t *const img = rom;
    const cell_t *sp = g_vsp;
#ifdef EXL_PINOUT_EPROM
    interp_hw_t *ip = interp0_hw;                 /* configure par pins_interp_init */
    const uint32_t b2 = (uint32_t)img >> 1;
    ip->base[2] = b2;
    const uint32_t hF = b2 + ADDR_TO_IDX(VZ.fifo), hS = b2 + ADDR_TO_IDX(VZ.stat);
    const uint32_t hY = b2 + ADDR_TO_IDX(VZ.sync), hR = b2 + ADDR_TO_IDX(VZ.rst);
    cell_t *const cF = img + ADDR_TO_IDX(VZ.fifo), *const cS = img + ADDR_TO_IDX(VZ.stat);
    const uint32_t hM = VZ.smask ? b2 + ADDR_TO_IDX(VZ.smask) : 0xFFFFFFFFu;
    const uint32_t hL = VZ.srel ? b2 + ADDR_TO_IDX(VZ.srel) : 0xFFFFFFFFu;
    cell_t *const cM = img + ADDR_TO_IDX(VZ.smask), *const cL = img + ADDR_TO_IDX(VZ.srel);
    const cell_t q0m = ENC(0), q0r = ENC(SB_CAL_REL);
    for (;;) {
        uint32_t p, h;
        while ((p = sio_hw->gpio_in) & CS_MASK) ;          /* repos */
        do {                                                /* l'acces */
            ip->accum[0] = p;
            h = ip->peek[2];
            sio_hw->gpio_out = SERVE_CELL(h) << DATA_SHIFT;
            sio_hw->gpio_oe_set = DATA_MASK;
        } while (!((p = sio_hw->gpio_in) & CS_MASK));
        sio_hw->gpio_oe_clr = DATA_MASK;                    /* bus libere */
        if (h == hF) *cF = *sp++;                           /* octet suivant de la liste */
        else if (h == hS) sp = g_vsp;
        else if (h == hY) { *cS = 0; g_vreq = 1; }
        else if (h == hR) { *cS = 0; g_vrst = 1; }
        else if (h == hM) g_st[g_sk & (SB_Q - 1u)] = timer_hw->timerawl;   /* musique : l'heure */
        else if (h == hL) {                                 /* musique : l'entree suivante */
            const uint32_t k = ++g_sk;
            if ((int32_t)(g_sqn - k) > 0) { *cM = SB.qm[k & (SB_Q - 1u)]; *cL = SB.qr[k & (SB_Q - 1u)]; }
            else { *cM = q0m; *cL = q0r; }
        }
        VID_AFTER_ACCESS();
    }
#else
    const uint32_t aF = VZ.fifo, aS = VZ.stat, aY = VZ.sync, aR = VZ.rst;
    cell_t *const cF = img + aF, *const cS = img + aS;
    const uint32_t aM = VZ.smask ? VZ.smask : 0xFFFFFFFFu, aL = VZ.srel ? VZ.srel : 0xFFFFFFFFu;
    cell_t *const cM = img + (VZ.smask & 0x7FFFu), *const cL = img + (VZ.srel & 0x7FFFu);
    const cell_t q0m = ENC(0), q0r = ENC(SB_CAL_REL);
    for (;;) {
        uint32_t p, a;
        while ((p = sio_hw->gpio_in) & CS_MASK) ;          /* repos */
        do {                                                /* l'acces */
            a = p & ADDR_MASK;
            sio_hw->gpio_out = (uint32_t)img[a] << DATA_SHIFT;
            sio_hw->gpio_oe_set = DATA_MASK;
        } while (!((p = sio_hw->gpio_in) & CS_MASK));
        sio_hw->gpio_oe_clr = DATA_MASK;                    /* bus libere */
        if (a == aF) *cF = *sp++;                           /* octet suivant de la liste */
        else if (a == aS) sp = g_vsp;
        else if (a == aY) { *cS = 0; g_vreq = 1; }
        else if (a == aR) { *cS = 0; g_vrst = 1; }
        else if (a == aM) g_st[g_sk & (SB_Q - 1u)] = timer_hw->timerawl;   /* musique : l'heure */
        else if (a == aL) {                                 /* musique : l'entree suivante */
            const uint32_t k = ++g_sk;
            if ((int32_t)(g_sqn - k) > 0) { *cM = SB.qm[k & (SB_Q - 1u)]; *cL = SB.qr[k & (SB_Q - 1u)]; }
            else { *cM = q0m; *cL = q0r; }
        }
        VID_AFTER_ACCESS();
    }
#endif
}

static void core1_video(void) {
    save_and_disable_interrupts();
    pins_interp_init();
    serve_video();
}

/* musique : heure 32 bits (lue par le coeur 1) -> 64 bits, d'apres l'heure actuelle */
static inline int64_t sb_t64(uint32_t t, uint64_t now64) {
    return (int64_t)now64 - (int32_t)((uint32_t)now64 - t);
}
static void sb_restart(void) {
    sb_reset(&SB, g_sk);
    const uint32_t k = g_sk;
    rom[ADDR_TO_IDX(VZ.smask)] = SB.qm[k & (SB_Q - 1u)];
    rom[ADDR_TO_IDX(VZ.srel)] = SB.qr[k & (SB_Q - 1u)];
    __dmb();
    g_sqn = SB.qn;
}
/* les lectures datees, puis la file jusqu'a SB_DEPTH entrees devant la console */
static inline void sb_step(void) {
    const uint64_t n64 = time_us_64();
    const uint32_t k = g_sk;
    if ((int32_t)(k - SB.seen) > (int32_t)(SB_Q - 8u)) SB.seen = k;   /* trop de retard : on oublie */
    while (SB.seen != k) { sb_read(&SB, SB.seen, sb_t64(g_st[SB.seen & (SB_Q - 1u)], n64)); SB.seen++; }
    sb_fill(&SB, k, (int64_t)n64, sb_t64(V.t0, n64));
    __dmb();
    g_sqn = SB.qn;
}

/* publie la liste de la derniere image complete (V.s.P) */
static void vid_publish(uint32_t now) {
    int tx = 0;
    if (V.shown == 0 || (int32_t)(now - V.sec_mark) >= 1000000) {
        if (V.shown) { V.ips = V.shown - V.shown_sec; V.shown_sec = V.shown; V.sec_mark += 1000000u;
                       if ((int32_t)(now - V.sec_mark) >= 1000000) V.sec_mark = now; }
        uint32_t k = V.k ? V.k - 1 : 0;
        vt_make(&VZ, V.text, V.h->title, (uint32_t)((uint64_t)k * 1000u / V.h->fps_milli), V.ips);
        tx = 1;
    }
    vb_build(&VB, V.s.P, tx ? V.text : 0);
    rom[ADDR_TO_IDX(VZ.fifo)] = VB.buf[0];
    g_vsp = VB.buf + 1;
    __dmb();
    rom[ADDR_TO_IDX(VZ.stat)] = ENC(1);
    V.have_new = VB.partial;         /* liste incomplete : la suite des que l'EXL100 la demande */
    if (!VB.partial) V.shown++;
}

/* un pas du travail du coeur 0 : redemarrage, liste demandee, ou une ligne a decoder */
static void __noinline vid_step(void) {
    const uint32_t now = timer_hw->timerawl;
    if (!g_vok) { g_vrst = 0; g_vreq = 0; return; }
    if (g_son) sb_step();
    if (g_vrst) {
        g_vrst = 0; g_vreq = 0;
        vb_reset(&VB);
        vp_restart(&V, now);
        if (g_son) sb_restart();                        /* le lecteur relance la minuterie */
        V.shown = V.shown_sec = 0; V.ips = 0; V.sec_mark = now; V.t_end = 0;
        g_vrun = 1;
        return;
    }
    if (!g_vrun) return;
    if (g_vreq && V.have_new) { g_vreq = 0; vid_publish(now); return; }
    if (V.line == 0) {
        if (V.k >= V.h->frames) {                       /* fin : derniere image tenue, puis on reboucle */
            if (!V.have_new && (int32_t)(now - V.t_end) >= (int32_t)VID_HOLD_US) vp_restart(&V, now);
            return;
        }
#ifdef EXL_VIDEO_TEST
        if (V.have_new) return;                         /* essai : une image par demande, sans horloge */
#else
        if ((int32_t)(now - vp_due(&V, V.k)) < 0) return;
#endif
    }
    vs_decode_line(&V.s, (int)V.line);
    if (++V.line == VH) {
        vs_end(&V.s);
        V.line = 0; V.k++; V.have_new = 1;
        if (V.k >= V.h->frames) V.t_end = now;
    }
}

/* musique : presente dans la flash, et le lecteur sait la jouer (zone version 3) -> V_SON = 1 */
static void vid_sound_init(const vhdr_t *h, int ok) {
    const bhdr_t *b = ok && VZ.son ? sb_find(h) : 0;
    if (b && !sb_sum_ok(b)) b = 0;
    if (!b) return;
    SB.h = b; SB.ev = sb_ev(b);
    sb_restart();
    g_son = 1;
    rom[ADDR_TO_IDX(VZ.son)] = ENC(1);
}

/* mode video : ne revient pas */
static void __noinline video_main(const vzone_t *z, uint32_t sys_khz) {
    VZ = *z;
    const vhdr_t *h = (const vhdr_t *)(XIP_BASE + VID_FLASH_OFFSET);
    /* memoire de travail : la reserve des banques (inutilisee dans ce mode). Couleur :
     * 3 images de 28 800 octets (precedente, en cours, affichee) + 2 cartes de 9 600,
     * le reste pour la liste. */
    const uint32_t np = vh_ok(h) ? vh_planes(h) : 1u;
    uint8_t *ar = (uint8_t *)img_pool, *const end = ar + sizeof img_pool;
    V.s.P = ar; ar += VFRAME * np;  V.s.C = ar; ar += VFRAME * np;
    VB.sent = ar; ar += VFRAME * np; VB.np = np;
    V.s.chg = ar; ar += VFRAME; V.s.chp = ar; ar += VFRAME;
    ar = (uint8_t *)(((uintptr_t)ar + 3u) & ~(uintptr_t)3u);
    VB.buf = (cell_t *)ar;
    VB.cap = (uint32_t)((size_t)(end - ar) / sizeof(cell_t));
    if (VB.cap > 32768u) VB.cap = 32768u;
    VB.z = &VZ;
    vb_reset(&VB);
    V.h = h; V.data = (const uint8_t *)h + sizeof *h;
    g_vsp = VB.buf + 1;
    rom[ADDR_TO_IDX(VZ.fifo)] = ENC(VZ.c_end);
    rom[ADDR_TO_IDX(VZ.stat)] = ENC(0);
    rom[ADDR_TO_IDX(VZ.pres)] = ENC(0);
    set_sys_clock_khz(sys_khz, true);
    int ok = vh_ok(h);
#ifdef EXL_SINGLE_CORE_TEST
    ok = ok && vh_sum_ok(h);
    rom[ADDR_TO_IDX(VZ.pres)] = ENC(ok ? VID_PRESENT : VID_NOVIDEO);
    if (ok) vp_restart(&V, timer_hw->timerawl);
    g_vok = ok;
    vid_sound_init(h, ok);
    save_and_disable_interrupts();
    pins_interp_init();
    serve_video();
#else
    multicore_launch_core1(core1_video);          /* le service d'abord : la console lit le pied */
    ok = ok && vh_sum_ok(h);                      /* ~1 Mo de flash : quelques dizaines de ms */
    if (ok) vp_restart(&V, timer_hw->timerawl);
    g_vok = ok;
    vid_sound_init(h, ok);
    rom[ADDR_TO_IDX(VZ.pres)] = ENC(ok ? VID_PRESENT : VID_NOVIDEO);
    if (!ok) for (;;) __wfe();
    for (;;) vid_step();
#endif
}
#endif
#endif
