/*
 * exlserve.h -- service du bus de la cartouche, commun a EXLCART et EXLSONDE.
 *
 * Deux reglages, lus dans la flash juste apres la ROM (bloc "EXLC" ecrit par
 * EXLPICO), pour imiter une EPROM plus fidelement que la reponse immediate :
 *   delai   : la donnee presentee est celle de l'adresse lue 'delai' tours plus tot
 *             (une EPROM met 150-200 ns a suivre un changement d'adresse) ;
 *   maintien: le bus reste pilote 'maintien' tours apres la remontee de /ROM_CS
 *             (une EPROM continue quelques dizaines de ns).
 * delai = maintien = 0 : reponse immediate (boucle rapide d'origine).
 * La boucle reglable a un temps de tour CONSTANT (pas de branche dependant des
 * donnees), pour que 'n tours' corresponde a une duree fixe.
 */
#ifndef EXLSERVE_H
#define EXLSERVE_H
#include "hardware/structs/sio.h"
#include "exlpins.h"                              /* brochage, rangement des images */

#define ROM_FLASH_OFFSET 0x00100000u              /* ROM : 1 Mo dans la flash      */
#define CFG_FLASH_OFFSET (ROM_FLASH_OFFSET + ROM_SIZE)  /* reglages : juste apres */
#define CFG_MAGIC        0x434C5845u              /* "EXLC"                        */

typedef struct { uint32_t magic; uint8_t delai, maintien, r1, r2; } exl_cfg_t;

extern cell_t rom[ROM_SIZE];

#ifdef EXL_PINOUT_EPROM
/* support EPROM : l'interpolateur (configure par pins_interp_init sur ce coeur)
 * donne h = image/2 + index ; la case est en 2*h (ldrh rX, [rh, rh]) */
static inline __attribute__((always_inline)) uint32_t serve_cell(uint32_t h) {
    uint32_t c;
    __asm volatile ("ldrh %0, [%1, %1]" : "=l" (c) : "l" (h));   /* une seule instruction */
    return c;
}
#define SERVE_CELL(h)    serve_cell(h)
#endif

/* reponse immediate : donnee de l'adresse courante, bus relache des /ROM_CS = 1 */
static void __noinline __not_in_flash_func(serve_fast)(void) {
#ifdef EXL_PINOUT_EPROM
    interp_hw_t *ip = interp0_hw;
    ip->base[2] = (uint32_t)rom >> 1;
    for (;;) {
        uint32_t p = sio_hw->gpio_in;
        if (!(p & CS_MASK)) {
            ip->accum[0] = p;
            uint32_t h = ip->peek[2];
            sio_hw->gpio_out = SERVE_CELL(h) << DATA_SHIFT;
            sio_hw->gpio_oe_set = DATA_MASK;
        } else {
            sio_hw->gpio_oe_clr = DATA_MASK;
        }
    }
#else
    for (;;) {
        uint32_t p = sio_hw->gpio_in;
        if (!(p & CS_MASK)) {
            sio_hw->gpio_out = (uint32_t)rom[p & ADDR_MASK] << DATA_SHIFT;
            sio_hw->gpio_oe_set = DATA_MASK;
        } else {
            sio_hw->gpio_oe_clr = DATA_MASK;
        }
    }
#endif
}

/* reponse "facon EPROM" : temps de tour constant */
static void __noinline __not_in_flash_func(serve_tuned)(uint32_t delai, uint32_t maintien) {
    uint32_t hist[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    uint32_t i = 0, garde = 0;
#ifdef EXL_PINOUT_EPROM
    interp_hw_t *ip = interp0_hw;
    ip->base[2] = (uint32_t)rom >> 1;
    for (uint32_t k = 0; k < 8; k++) hist[k] = (uint32_t)rom >> 1;
#endif
    for (;;) {
        uint32_t p = sio_hw->gpio_in;
#ifdef EXL_PINOUT_EPROM
        ip->accum[0] = p;
        hist[i & 7] = ip->peek[2];
        uint32_t h = hist[(i - delai) & 7];
        sio_hw->gpio_out = SERVE_CELL(h) << DATA_SHIFT;
#else
        hist[i & 7] = p & ADDR_MASK;
        uint32_t a = hist[(i - delai) & 7];
        sio_hw->gpio_out = (uint32_t)rom[a] << DATA_SHIFT;
#endif
        uint32_t bas = ((p >> CS_PIN) & 1u) ^ 1u;          /* 1 si /ROM_CS = 0 */
        garde = bas ? maintien + 1u : (garde ? garde - 1u : 0u);
        /* piloter si /ROM_CS = 0, ou pendant le maintien qui suit sa remontee */
        uint32_t oe = garde ? DATA_MASK : 0u;
        sio_hw->gpio_oe_set = oe;
        sio_hw->gpio_oe_clr = oe ^ DATA_MASK;
        i++;
    }
}

static inline exl_cfg_t exl_read_cfg(void) {
    exl_cfg_t c = *(const exl_cfg_t *)(XIP_BASE + CFG_FLASH_OFFSET);
    if (c.magic != CFG_MAGIC || c.delai > 7 || c.maintien > 7) { c.delai = 0; c.maintien = 0; }
    return c;
}

static void __noinline exl_serve(exl_cfg_t c) {
    if (c.delai == 0 && c.maintien == 0) serve_fast();
    else serve_tuned(c.delai, c.maintien);
}
#endif
