/*
 * exlflux.h -- mode DISQUE d'EXLCART (mode ROM unique) : le Pico ne calcule rien,
 * il sert deux fichiers ranges dans sa flash, octet par octet, comme le registre de
 * donnees d'un controleur de disquette. C'est l'EXL100 qui fait tout (badflux.asm :
 * listes d'image a leur heure, horloge, musique 1 bit dans son interruption).
 *
 * La ROM (le lecteur) reserve une zone "EXLD" :
 *   +0 D_DAT0  fichier 0 : apres CHAQUE lecture, l'octet suivant
 *   +1 D_DAT1  fichier 1 : idem
 *   +2 D_PRES  $A5 : fichiers presents ; $5A : pas de fichiers valides ; 0 pendant un retour
 *   +3 D_RST   lecture = retour au debut des deux fichiers (D_PRES passe a 0, puis $A5)
 *   +4 "EXLD"  +8 version (1)
 * Apres la fin d'un fichier, le Pico sert $FF.
 *
 * Flash : a 2 Mo (la place de la bibliotheque), un en-tete fhdr_t, puis les fichiers.
 * Avec une bibliotheque, le lecteur peut aussi etre un de ses jeux : le film est alors
 * range apres les jeux (en-tete EXLB version 2, champ flux_off), voir exlfluxlib.h.
 * Le coeur 1 sert le bus (les memes instructions pendant l'acces que la ROM unique) et,
 * apres l'acces, presente l'octet suivant pris dans un anneau en RAM ; le coeur 0 ne
 * fait que recopier la flash dans les anneaux, en avance.
 */
#ifndef EXLFLUX_H
#define EXLFLUX_H
#include <stdint.h>
#include "exlpins.h"

#define FLX_FLASH_OFFSET  0x00200000u
#define FLX_MAGIC         0x464C5845u            /* "EXLF" */
#define FLX_RING          4096u                  /* par fichier (puissance de 2) */
#define FLX_AHEAD         3800u                  /* octets prepares en avance */
#define FLX_PRESENT       0xA5u
#define FLX_NONE          0x5Au

typedef struct {
    uint32_t magic;
    uint16_t version, nfiles;                     /* 1 ; 2 */
    struct { uint32_t off, len, sum; } f[2];      /* depuis le debut de l'en-tete */
    uint8_t  title[28];
    uint32_t hsum;                                /* somme des 60 octets precedents */
} fhdr_t;                                         /* 64 octets */

typedef struct { uint32_t d0, d1, pres, rst; } fzone_t;

/* zone EXLD dans la ROM (octets normaux) */
static inline int fz_find(const uint8_t *r, uint32_t limit, fzone_t *z) {
    for (uint32_t a = 0; a + 9 <= limit; a++) {
        const uint8_t *p = r + a;
        if (p[4] != 'E' || p[5] != 'X' || p[6] != 'L' || p[7] != 'D' || p[0] | p[1] | p[2] | p[3] || p[8] != 1) continue;
        z->d0 = a; z->d1 = a + 1; z->pres = a + 2; z->rst = a + 3;
        return 1;
    }
    return 0;
}

static inline int fh_ok(const fhdr_t *h, uint32_t space) {
    if (h->magic != FLX_MAGIC || h->version != 1 || h->nfiles != 2) return 0;
    const uint8_t *b = (const uint8_t *)h;
    uint32_t s = 0;
    for (uint32_t i = 0; i < 60; i++) s += b[i];
    if (s != h->hsum) return 0;
    for (int i = 0; i < 2; i++)
        if (h->f[i].off < sizeof *h || h->f[i].len > space || h->f[i].off > space - h->f[i].len) return 0;
    return 1;
}

/* ================================================================ materiel (Pico) */
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "hardware/structs/sio.h"
#include "hardware/sync.h"

extern cell_t rom[ROM_SIZE];
static fzone_t FZ;
static const uint8_t *g_fd[2];                    /* debut des fichiers en flash */
static uint32_t g_fl[2];                          /* longueurs */
static cell_t g_ring[2][FLX_RING];
static volatile uint32_t g_rd[2];                 /* octets lus par la console (coeur 1) */
static uint32_t g_wr[2];                          /* octets prepares (coeur 0) */
static volatile uint32_t g_frst;                  /* retour demande (coeur 1) */
static int g_fok;
static cell_t *g_fimg;                            /* image servie (rom, ou le jeu de la bibliotheque) */

#ifdef EXL_SINGLE_CORE_TEST
static void flx_step(void);
#define FLX_AFTER_ACCESS()  flx_step()
#else
#define FLX_AFTER_ACCESS()  ((void)0)
#endif

/* service : pendant l'acces, les instructions de la ROM unique ; apres, l'octet suivant */
static void __noinline __not_in_flash_func(serve_flux)(void) {
    cell_t *const img = rom;
    cell_t *const r0 = g_ring[0], *const r1 = g_ring[1];
    uint32_t i0 = g_rd[0], i1 = g_rd[1];
#ifdef EXL_PINOUT_EPROM
    interp_hw_t *ip = interp0_hw;
    const uint32_t b2 = (uint32_t)img >> 1;
    ip->base[2] = b2;
    const uint32_t h0 = b2 + ADDR_TO_IDX(FZ.d0), h1 = b2 + ADDR_TO_IDX(FZ.d1), hR = b2 + ADDR_TO_IDX(FZ.rst);
    cell_t *const c0 = img + ADDR_TO_IDX(FZ.d0), *const c1 = img + ADDR_TO_IDX(FZ.d1), *const cP = img + ADDR_TO_IDX(FZ.pres);
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
        if (h == h0) { *c0 = r0[++i0 & (FLX_RING - 1u)]; g_rd[0] = i0; }
        else if (h == h1) { *c1 = r1[++i1 & (FLX_RING - 1u)]; g_rd[1] = i1; }
        else if (h == hR) { *cP = ENC(0); i0 = i1 = 0; g_rd[0] = g_rd[1] = 0; g_frst = 1; }
        FLX_AFTER_ACCESS();
    }
#else
    const uint32_t a0 = FZ.d0, a1 = FZ.d1, aR = FZ.rst;
    cell_t *const c0 = img + a0, *const c1 = img + a1, *const cP = img + FZ.pres;
    for (;;) {
        uint32_t p, a;
        while ((p = sio_hw->gpio_in) & CS_MASK) ;          /* repos */
        do {                                                /* l'acces */
            a = p & ADDR_MASK;
            sio_hw->gpio_out = (uint32_t)img[a] << DATA_SHIFT;
            sio_hw->gpio_oe_set = DATA_MASK;
        } while (!((p = sio_hw->gpio_in) & CS_MASK));
        sio_hw->gpio_oe_clr = DATA_MASK;                    /* bus libere */
        if (a == a0) { *c0 = r0[++i0 & (FLX_RING - 1u)]; g_rd[0] = i0; }
        else if (a == a1) { *c1 = r1[++i1 & (FLX_RING - 1u)]; g_rd[1] = i1; }
        else if (a == aR) { *cP = ENC(0); i0 = i1 = 0; g_rd[0] = g_rd[1] = 0; g_frst = 1; }
        FLX_AFTER_ACCESS();
    }
#endif
}

static void core1_flux(void) {
    save_and_disable_interrupts();
    pins_interp_init();
    serve_flux();
}

/* recopie la flash dans les anneaux, devant la console */
static inline void flx_fill(int k) {
    const uint32_t r = g_rd[k];
    while (g_wr[k] - r < FLX_AHEAD) {
        const uint32_t n = g_wr[k];
        g_ring[k][n & (FLX_RING - 1u)] = ENC(n < g_fl[k] ? g_fd[k][n] : 0xFFu);
        g_wr[k] = n + 1;
    }
}
static void flx_rewind(void) {
    g_wr[0] = g_wr[1] = 0;
    flx_fill(0); flx_fill(1);
    g_fimg[ADDR_TO_IDX(FZ.d0)] = g_ring[0][0];
    g_fimg[ADDR_TO_IDX(FZ.d1)] = g_ring[1][0];
    __dmb();
    g_fimg[ADDR_TO_IDX(FZ.pres)] = ENC(FLX_PRESENT);
}
static void __noinline flx_step(void) {
    if (!g_fok) return;
    if (g_frst) { g_frst = 0; flx_rewind(); return; }
    flx_fill(0); flx_fill(1);
}

/* mode disque : ne revient pas */
static void __noinline flux_main(const fzone_t *z, uint32_t sys_khz) {
    FZ = *z;
    g_fimg = rom;
    const fhdr_t *h = (const fhdr_t *)(XIP_BASE + FLX_FLASH_OFFSET);
    const uint32_t space = 0x01000000u - FLX_FLASH_OFFSET;     /* flash de 16 Mo */
    rom[ADDR_TO_IDX(FZ.pres)] = ENC(0);
    set_sys_clock_khz(sys_khz, true);
    g_fok = fh_ok(h, space);
    if (g_fok) {
        for (int k = 0; k < 2; k++) { g_fd[k] = (const uint8_t *)h + h->f[k].off; g_fl[k] = h->f[k].len; }
        g_rd[0] = g_rd[1] = 0;
        flx_rewind();
    } else
        rom[ADDR_TO_IDX(FZ.pres)] = ENC(FLX_NONE);
#ifdef EXL_SINGLE_CORE_TEST
    save_and_disable_interrupts();
    pins_interp_init();
    serve_flux();
#else
    multicore_launch_core1(core1_flux);
    if (!g_fok) for (;;) __wfe();
    for (;;) flx_step();
#endif
}
#endif
#endif
