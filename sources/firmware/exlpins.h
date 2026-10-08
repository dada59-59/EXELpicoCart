/*
 * exlpins.h -- brochage de la carte et rangement des images en RAM.
 *
 * Deux cartes, choisies a la compilation :
 *
 *  LINEAIRE (par defaut) -- la carte d'origine :
 *      A0-A14 = GP0-GP14, D0-D7 = GP15-GP22, /ROM_CS = GP23.
 *      Image = 32 Ko d'octets ; index = broches GP0-GP14 telles quelles.
 *      C'est exactement le code valide sur EXL100 et EXELTEL.
 *
 *  SUPPORT EPROM (EXL_PINOUT_EPROM) -- le Pico soude sur l'empreinte d'une 27C256 :
 *      A0 GP13   A1 GP0    A2 GP12   A3 GP11   A4 GP10   A5 GP9    A6 GP8    A7 GP7
 *      A8 GP1    A9 GP2    A10 GP24  A11 GP3   A12 GP6   A13 GP4   A14 GP5
 *      D0 GP14   D1 GP15   D2 GP16   D3 GP18   D4 GP19   D5 GP20   D6 GP21   D7 GP22
 *      /ROM_CS GP23 ; GP17 non connectee.
 *    Meme vitesse de service, car l'image est rangee "dans l'ordre des broches" :
 *      - l'INDEX d'une case est obtenu par l'interpolateur materiel du coeur qui
 *        sert : voie 0 = GP0-GP13, voie 1 (entree croisee) = GP24 place en bit 14,
 *        plus BASE2 = adresse de l'image / 2 ; une ecriture et une lecture, au lieu
 *        des deux decalages du code lineaire ;
 *      - chaque CASE (16 bits) contient deja les bits a sortir sur GP14-GP22 (GP17
 *        a 0) : un decalage, comme avant.
 *    Prix : 64 Ko par image au lieu de 32 Ko. Toute la permutation est faite une
 *    seule fois, a la copie flash -> RAM (img_load).
 *
 * Les ROM, les fichiers .uf2 et la bibliotheque ne changent pas : la flash contient
 * toujours les images "normales" ; seule leur copie en RAM est rangee autrement.
 *
 * Ce fichier se compile aussi sur PC (tests) : seule pins_interp_init() touche au
 * materiel.
 */
#ifndef EXLPINS_H
#define EXLPINS_H
#include <stdint.h>
#include <string.h>

#define CS_PIN           23                       /* /ROM_CS, actif bas (les deux cartes) */
#define CS_MASK          (1u << CS_PIN)
#define ROM_SIZE         0x8000u

#ifdef EXL_PINOUT_EPROM
/* ------------------------------------------------------------- support EPROM */
#define PINOUT_NAME      "support EPROM"
static const uint8_t PIN_A[15] = { 13, 0, 12, 11, 10, 9, 8, 7, 1, 2, 24, 3, 6, 4, 5 };  /* A0..A14 */
static const uint8_t PIN_D[8]  = { 14, 15, 16, 18, 19, 20, 21, 22 };                  /* D0..D7  */
#define DATA_SHIFT       14                       /* case << 14 -> GP14..GP22 */
#define DATA_MASK        0x007DC000u              /* GP14-16, GP18-22 */
typedef uint16_t cell_t;
/* index = ((P >> LANE0_SHIFT) & bits LSB..MSB) + ((P >> LANE1_SHIFT) & bits LSB..MSB) */
#define LANE0_SHIFT      0
#define LANE0_LSB        0
#define LANE0_MSB        13                       /* GP0..GP13 */
#define LANE1_SHIFT      10
#define LANE1_LSB        14
#define LANE1_MSB        14                       /* GP24 -> bit 14 */
#else
/* ------------------------------------------------------------------ lineaire */
#define PINOUT_NAME      "lineaire"
static const uint8_t PIN_A[15] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14 };
static const uint8_t PIN_D[8]  = { 15, 16, 17, 18, 19, 20, 21, 22 };
#define DATA_SHIFT       15
#define DATA_MASK        (0xFFu << DATA_SHIFT)
#define ADDR_MASK        0x00007FFFu              /* GP0..GP14 = A0..A14 */
typedef uint8_t cell_t;
#define LANE0_SHIFT      0
#define LANE0_LSB        0
#define LANE0_MSB        14
#define LANE1_SHIFT      31                       /* rien : GP31 n'existe pas (lu a 0) */
#define LANE1_LSB        0
#define LANE1_MSB        0
#endif

#define LANE_MASK(lsb, msb)  ((uint32_t)((2ull << (msb)) - 1u) & ~((1u << (lsb)) - 1u))

/* motif des broches pour une adresse (A0..A14 -> GPx) */
static inline uint32_t pins_of_addr(uint32_t r) {
    uint32_t P = 0;
    for (int k = 0; k < 15; k++) if (r >> k & 1u) P |= 1u << PIN_A[k];
    return P;
}
/* index d'une case pour un motif de broches : ce que calcule l'interpolateur */
static inline uint32_t idx_of_pins(uint32_t P) {
    return ((P >> LANE0_SHIFT) & LANE_MASK(LANE0_LSB, LANE0_MSB))
         + ((P >> LANE1_SHIFT) & LANE_MASK(LANE1_LSB, LANE1_MSB));
}

#ifdef EXL_PINOUT_EPROM
/* tables (remplies par pins_init) : adresse <-> index, octet -> case */
static uint16_t pin_idx_lo[256], pin_idx_hi[128];
static uint16_t pin_adr_lo[256], pin_adr_hi[128];
static cell_t   pin_enc[256];
#define ADDR_TO_IDX(r)   ((uint32_t)(pin_idx_lo[(r) & 0xFFu] | pin_idx_hi[((r) >> 8) & 0x7Fu]))
#define IDX_TO_ADDR(i)   ((uint32_t)(pin_adr_lo[(i) & 0xFFu] | pin_adr_hi[((i) >> 8) & 0x7Fu]))
#define ENC(b)           (pin_enc[(b) & 0xFFu])

static void pins_init(void) {
    uint32_t bitidx[15];                          /* index d'une adresse a un seul bit */
    for (int k = 0; k < 15; k++) bitidx[k] = idx_of_pins(1u << PIN_A[k]);
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t a = 0, b = 0;
        for (int k = 0; k < 15; k++) {
            if (i & bitidx[k]) a |= 1u << k;             /* bits 0-7 de l'index */
            if ((i << 8) & bitidx[k]) b |= 1u << k;      /* bits 8-15 de l'index */
        }
        pin_adr_lo[i] = (uint16_t)a;
        if (i < 128) pin_adr_hi[i] = (uint16_t)b;
        pin_idx_lo[i] = (uint16_t)idx_of_pins(pins_of_addr(i));
        if (i < 128) pin_idx_hi[i] = (uint16_t)idx_of_pins(pins_of_addr(i << 8));
        uint32_t c = 0;
        for (int j = 0; j < 8; j++) if (i >> j & 1u) c |= 1u << (PIN_D[j] - DATA_SHIFT);
        pin_enc[i] = (cell_t)c;
    }
}
/* case -> octet (sommes de controle) */
static inline uint8_t cell_dec(cell_t c) {
    uint8_t b = 0;
    for (int j = 0; j < 8; j++) if (c >> (PIN_D[j] - DATA_SHIFT) & 1u) b |= (uint8_t)(1u << j);
    return b;
}
/* copie src (octets, len) vers l'image dst, a partir de l'adresse start */
static void img_load(cell_t *dst, const uint8_t *src, uint32_t start, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) dst[ADDR_TO_IDX(start + i)] = ENC(src[i]);
}
#else
static inline void pins_init(void) { }
#define ADDR_TO_IDX(r)   ((uint32_t)(r))
#define IDX_TO_ADDR(i)   ((uint32_t)(i))
#define ENC(b)           ((cell_t)(b))
static inline uint8_t cell_dec(cell_t c) { return c; }
static inline void img_load(cell_t *dst, const uint8_t *src, uint32_t start, uint32_t len) {
    memcpy(dst + start, src, len);
}
#endif

/* somme des octets d'une image (decodee) */
static inline uint32_t img_sum(const cell_t *img) {
    uint32_t s = 0;
    for (uint32_t i = 0; i < ROM_SIZE; i++) s += cell_dec(img[i]);
    return s;
}

/* broches : toutes en entree sans tirage ; donnees en sortie rapide (OE pilote ensuite) */
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "hardware/gpio.h"
static inline void pins_gpio_init(void) {
    for (int k = 0; k < 15; k++) { gpio_init(PIN_A[k]); gpio_set_dir(PIN_A[k], GPIO_IN); gpio_disable_pulls(PIN_A[k]); }
    for (int j = 0; j < 8; j++) {
        gpio_init(PIN_D[j]); gpio_set_dir(PIN_D[j], GPIO_IN); gpio_disable_pulls(PIN_D[j]);
        gpio_set_drive_strength(PIN_D[j], GPIO_DRIVE_STRENGTH_8MA);
        gpio_set_slew_rate(PIN_D[j], GPIO_SLEW_RATE_FAST);
    }
    gpio_init(CS_PIN); gpio_set_dir(CS_PIN, GPIO_IN); gpio_disable_pulls(CS_PIN);
}
#ifdef EXL_PINOUT_EPROM
#include "hardware/interp.h"
/* a appeler SUR LE COEUR QUI SERT (chaque coeur a ses propres interpolateurs) */
static inline void pins_interp_init(void) {
    interp_config c0 = interp_default_config();
    interp_config_set_shift(&c0, LANE0_SHIFT);
    interp_config_set_mask(&c0, LANE0_LSB, LANE0_MSB);
    interp_set_config(interp0, 0, &c0);
    interp_config c1 = interp_default_config();
    interp_config_set_cross_input(&c1, true);     /* voie 1 : lit ACCUM0 */
    interp_config_set_shift(&c1, LANE1_SHIFT);
    interp_config_set_mask(&c1, LANE1_LSB, LANE1_MSB);
    interp_set_config(interp0, 1, &c1);
    interp0->base[0] = 0; interp0->base[1] = 0; interp0->base[2] = 0;
}
#else
static inline void pins_interp_init(void) { }
#endif
#endif
#endif
