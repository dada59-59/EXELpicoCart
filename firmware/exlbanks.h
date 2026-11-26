/*
 * exlbanks.h -- ROM a banques de 16 Ko (mode ROM unique), et reserve d'images
 * en RAM partagee avec la bibliotheque. Commun a EXLCART et EXLSONDE.
 *
 * Vue de la console (meme convention que l'image EPROM d'EXLBANK) :
 *   $0000-$3FFF  zone FIXE (le programme)
 *   $3FF0-$3FFF  SIGNAL : une lecture en $3FF0+n selectionne la banque n
 *   $4000-$7FFF  FENETRE : la banque courante ; chaque banque porte le pied
 *
 * Flash (ecrite par EXLPICO) :
 *   0x100000  la ROM principale : zone fixe + banque 0 (une ROM 32 Ko normale ;
 *             un firmware sans banques la sert donc telle quelle)
 *   0x10F000  en-tete EXLK : signature, version, nombre de banques (2..16)
 *   0x110000 + (k-1) * 0x4000 : la banque k (k >= 1), 16 Ko
 *
 * En RAM, chaque banque est une IMAGE COMPLETE (zone fixe recopiee + sa
 * fenetre) : changer de banque revient a changer le pointeur de l'image servie,
 * exactement comme la bascule menu -> jeu de la bibliotheque. Le chemin critique
 * du service ne change pas. La bascule se fait APRES l'acces au signal : la
 * lecture suivante de la fenetre voit deja la nouvelle banque.
 *
 * VERSION 2 de l'en-tete (26/11/2026) : DEUX IMAGES COMPLETES DE 32 Ko, comme
 * la cartouche EXELTEL pour EXL100 (64 Ko, une bascule 74LS sur le bus d'adresses) :
 *   0x100000  la 1re moitie (banque 0), une ROM 32 Ko normale
 *   0x10F000  en-tete EXLK, version 2, 2 banques
 *   0x110000  la 2e moitie (banque 1), 32 Ko, servie telle quelle (son pied compris)
 * Une lecture en $7FF0 passe en banque 1, en $7FF8 en banque 0 (routines que le
 * programme recopie en RAM : LDA @>7FF0 / LDA @>7FF8 ; RETS). Adresses EXACTES :
 * le pied n'agit pas, car le programme et le moniteur sautent en $7FFD, qui doit
 * rester dans la banque courante (8C 02 F3 = BR @>02F3 en banque 0, 8C 59 C7 =
 * BR @>59C7 en banque 1). Bascule apres l'acces, comme pour les banques de 16 Ko.
 * Au demarrage : banque 0.
 *
 * Prix : une image par banque (32 Ko en lineaire, 64 Ko en support EPROM). La
 * reserve img_pool[] sert aussi de tampon "jeu" a la bibliotheque (les deux
 * modes s'excluent). Nombre de banques utilisables = 1 + EXL_POOL_IMAGES.
 */
#ifndef EXLBANKS_H
#define EXLBANKS_H
#include <string.h>
#include "exlserve.h"

#ifndef EXL_POOL_IMAGES
#ifdef EXL_PINOUT_EPROM
#define EXL_POOL_IMAGES  2                        /* 3 banques (64 Ko par image) */
#else
#define EXL_POOL_IMAGES  5                        /* 6 banques (32 Ko par image) */
#endif
#endif

#define BANK_HDR_OFFSET   0x0010F000u
#define BANK_DATA_OFFSET  0x00110000u
#define BANK_SIZE         0x4000u
#define BANK_MAGIC        0x4B4C5845u             /* "EXLK" */
#define BANK_MAX          16u
#define BANK_SEL_ADDR     0x3FF0u                 /* $3FF0 + n */
#define BANK_WIN_ADDR     0x4000u
#define BANK_V_FULL       2u                      /* en-tete v2 : deux images de 32 Ko, bascule par lecture de $7FF0 / $7FF8 */
#define FULL_SEL_ADDR     0x7FF0u
typedef struct { uint32_t magic; uint16_t version, count; } bank_hdr_t;

extern cell_t rom[ROM_SIZE];
static cell_t img_pool[EXL_POOL_IMAGES][ROM_SIZE] __attribute__((aligned(4)));
static const cell_t *bank_img[BANK_MAX];
static uint32_t g_nbanks = 0;                     /* 0 : pas de banques */
static uint32_t g_nbanks_flash = 0;               /* annonces par l'en-tete */
static uint32_t g_bank_full = 0;                  /* 1 : deux images de 32 Ko (en-tete v2) */

/* banques : en-tete valide -> images en RAM ; renvoie le nombre servi (0 ou >= 2) */
static uint32_t bank_prepare(void) {
    const bank_hdr_t *K = (const bank_hdr_t *)(XIP_BASE + BANK_HDR_OFFSET);
    if (K->magic != BANK_MAGIC || K->count < 2 || K->count > BANK_MAX) return 0;
    g_nbanks_flash = K->count;
    if (K->version == BANK_V_FULL) {                          /* deux images de 32 Ko */
        if (K->count != 2) return 0;
        cell_t *d = img_pool[0];
        img_load(d, (const uint8_t *)(XIP_BASE + BANK_DATA_OFFSET), 0, ROM_SIZE);   /* telle quelle */
        bank_img[0] = rom;
        bank_img[1] = d;
        g_bank_full = 1;
        g_nbanks = 2;
        return 2;
    }
    uint32_t n = K->count;
    if (n > 1u + EXL_POOL_IMAGES) n = 1u + EXL_POOL_IMAGES;   /* RAM : le reste est ignore */
    bank_img[0] = rom;
    for (uint32_t k = 1; k < n; k++) {
        cell_t *d = img_pool[k - 1];
        memcpy(d, rom, sizeof(cell_t) * ROM_SIZE);            /* zone fixe (et tout le reste) */
        img_load(d, (const uint8_t *)(XIP_BASE + BANK_DATA_OFFSET + (k - 1) * BANK_SIZE),
                 BANK_WIN_ADDR, BANK_SIZE);                   /* la fenetre de la banque k */
        for (uint32_t i = 0; i < 4; i++)                      /* le pied, toujours celui de la ROM */
            d[ADDR_TO_IDX(0x7FFCu + i)] = rom[ADDR_TO_IDX(0x7FFCu + i)];
        bank_img[k] = d;
    }
    g_nbanks = n;
    return n;
}

/* deux images de 32 Ko (en-tete v2) : meme chemin critique ; apres l'acces,
 * l'adresse $7FF0 choisit l'image 1, $7FF8 l'image 0 (masque 0x7FF7 : ces deux-la) */
static void __noinline __not_in_flash_func(serve_full)(void) {
    const cell_t *const i0 = bank_img[0], *const i1 = bank_img[1];
    const cell_t *img = i0;
#ifdef EXL_PINOUT_EPROM
    interp_hw_t *ip = interp0_hw;                 /* configure par pins_interp_init */
    for (;;) {
        const uint32_t b2 = (uint32_t)img >> 1;
        ip->base[2] = b2;
        uint32_t p, h;
        while ((p = sio_hw->gpio_in) & CS_MASK) ;
        do {
            ip->accum[0] = p;
            h = ip->peek[2];
            sio_hw->gpio_out = SERVE_CELL(h) << DATA_SHIFT;
            sio_hw->gpio_oe_set = DATA_MASK;
        } while (!((p = sio_hw->gpio_in) & CS_MASK));
        sio_hw->gpio_oe_clr = DATA_MASK;
        uint32_t a = IDX_TO_ADDR(h - b2);
        if ((a & 0x7FF7u) == FULL_SEL_ADDR) img = (a & 8u) ? i0 : i1;
    }
#else
    for (;;) {
        uint32_t p, a;
        while ((p = sio_hw->gpio_in) & CS_MASK) ;
        do {
            a = p & ADDR_MASK;
            sio_hw->gpio_out = (uint32_t)img[a] << DATA_SHIFT;
            sio_hw->gpio_oe_set = DATA_MASK;
        } while (!((p = sio_hw->gpio_in) & CS_MASK));
        sio_hw->gpio_oe_clr = DATA_MASK;
        if ((a & 0x7FF7u) == FULL_SEL_ADDR) img = (a & 8u) ? i0 : i1;
    }
#endif
}

/* service avec banques : chemin critique identique a la ROM unique ; la bascule
 * a lieu apres l'acces, si l'adresse lue est $3FF0+n avec n < nombre de banques */
static void __noinline __not_in_flash_func(serve_banks)(void) {
    if (g_bank_full) serve_full();
    const cell_t *img = bank_img[0];
    const uint32_t nb = g_nbanks;
#ifdef EXL_PINOUT_EPROM
    interp_hw_t *ip = interp0_hw;                 /* configure par pins_interp_init */
    for (;;) {
        const uint32_t b2 = (uint32_t)img >> 1;
        ip->base[2] = b2;
        uint32_t p, h;
        while ((p = sio_hw->gpio_in) & CS_MASK) ;
        do {
            ip->accum[0] = p;
            h = ip->peek[2];
            sio_hw->gpio_out = SERVE_CELL(h) << DATA_SHIFT;
            sio_hw->gpio_oe_set = DATA_MASK;
        } while (!((p = sio_hw->gpio_in) & CS_MASK));
        sio_hw->gpio_oe_clr = DATA_MASK;
        uint32_t a = IDX_TO_ADDR(h - b2);
        if ((a & 0x7FF0u) == BANK_SEL_ADDR && (a & 15u) < nb) img = bank_img[a & 15u];
    }
#else
    for (;;) {
        uint32_t p, a;
        while ((p = sio_hw->gpio_in) & CS_MASK) ;
        do {
            a = p & ADDR_MASK;
            sio_hw->gpio_out = (uint32_t)img[a] << DATA_SHIFT;
            sio_hw->gpio_oe_set = DATA_MASK;
        } while (!((p = sio_hw->gpio_in) & CS_MASK));
        sio_hw->gpio_oe_clr = DATA_MASK;
        if ((a & 0x7FF0u) == BANK_SEL_ADDR && (a & 15u) < nb) img = bank_img[a & 15u];
    }
#endif
}
#endif
