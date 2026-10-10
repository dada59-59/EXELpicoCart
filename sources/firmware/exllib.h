/*
 * exllib.h -- mode bibliotheque (menu + jeux), commun a EXLCART et EXLSONDE.
 *
 * Flash, a partir de 2 Mo :
 *   0x200000 : en-tete (signature EXLB, nombre de jeux, emplacements), puis a
 *              +256 le bloc de 3840 octets recopie dans le menu en $7000-$7EFF ;
 *   0x201000 : le menu (loader), 32 Ko ;  0x209000 + n*0x8000 : le jeu n.
 * Le coeur 1 sert (serve_multi), le coeur 0 charge le jeu demande (lib_poll).
 * A n'inclure qu'une fois par firmware (definit g_req, g_ready ; game = img_pool[0]).
 */
#ifndef EXLLIB_H
#define EXLLIB_H
#include <string.h>
#include "exlserve.h"
#include "hardware/structs/timer.h"
#include "exlmulti.h"
#include "exlbanks.h"                             /* img_pool[] : reserve d'images en RAM */

#define LIB_FLASH_OFFSET 0x00200000u
#define LIB_MAGIC        0x424C5845u              /* "EXLB" */
#define LIB_INJECT_ADDR  0x7000u
#define LIB_INJECT_SIZE  3840u
#define LIB_MAX_GAMES    190u
typedef struct { uint32_t magic; uint16_t version, count;
                 uint32_t menu_off, game_off, stride; } lib_hdr_t;

extern cell_t rom[ROM_SIZE];                      /* le menu, en mode bibliotheque */
static cell_t *const game = img_pool[0];          /* le jeu charge (reserve partagee) */
volatile int32_t  g_req = -1;
volatile uint32_t g_ready = 0;
static multi_t M0;

/* en-tete valide dans la flash, ou NULL */
static inline const lib_hdr_t *lib_find(void) {
    const lib_hdr_t *H = (const lib_hdr_t *)(XIP_BASE + LIB_FLASH_OFFSET);
    return (H->magic == LIB_MAGIC && H->count >= 1 && H->count <= LIB_MAX_GAMES) ? H : 0;
}
/* menu en RAM, liste inseree, etat initial */
static inline void lib_prepare(const lib_hdr_t *H) {
    img_load(rom, (const uint8_t *)(XIP_BASE + H->menu_off), 0, ROM_SIZE);
    img_load(rom, (const uint8_t *)H + 256, LIB_INJECT_ADDR, LIB_INJECT_SIZE);
    M0.menu = rom; M0.game = game; M0.img = rom;
    M0.state = ST_MENU; M0.count = H->count; M0.n_game = 0;
    M0.menu_foot = rom[FOOT_IDX]; M0.game_foot = ENC(0xFF);
}
/* service (coeur 1), en RAM, interruptions coupees. Pendant l'acces : exactement
 * les instructions de la ROM unique ; la gestion d'etat a lieu apres (exlmulti.h). */
static void __noinline __not_in_flash_func(serve_multi)(void) {
    multi_t m = M0;                       /* copie locale : etat en registres */
    m.t_end = timer_hw->timerawl;
#ifdef EXL_PINOUT_EPROM
    interp_hw_t *ip = interp0_hw;                       /* configure par core1_multi */
    for (;;) {
        const uint32_t b2 = (uint32_t)m.img >> 1;
        ip->base[2] = b2;                                   /* image servie */
        uint32_t p, h;
        while ((p = sio_hw->gpio_in) & CS_MASK) ;          /* repos : 3 instructions */
        do {                                                /* l'acces */
            ip->accum[0] = p;
            h = ip->peek[2];
            sio_hw->gpio_out = SERVE_CELL(h) << DATA_SHIFT;
            sio_hw->gpio_oe_set = DATA_MASK;
        } while (!((p = sio_hw->gpio_in) & CS_MASK));
        sio_hw->gpio_oe_clr = DATA_MASK;                    /* bus libere */
        multi_after(&m, IDX_TO_ADDR(h - b2), timer_hw->timerawl);   /* puis l'etat */
    }
#else
    for (;;) {
        const cell_t *img = m.img;
        uint32_t p, a;
        while ((p = sio_hw->gpio_in) & CS_MASK) ;          /* repos : 3 instructions */
        do {                                                /* l'acces */
            a = p & ADDR_MASK;
            sio_hw->gpio_out = (uint32_t)img[a] << DATA_SHIFT;
            sio_hw->gpio_oe_set = DATA_MASK;
        } while (!((p = sio_hw->gpio_in) & CS_MASK));
        sio_hw->gpio_oe_clr = DATA_MASK;                    /* bus libere */
        multi_after(&m, a, timer_hw->timerawl);             /* puis l'etat */
    }
#endif
}
static void core1_multi(void) {
    save_and_disable_interrupts();
    pins_interp_init();                                     /* interpolateur du coeur 1 */
    serve_multi();
}
/* coeur 0 : charge le jeu demande, s'il y en a un */
static inline void lib_poll(const lib_hdr_t *H) {
    int32_t r = g_req;
    if (r >= 0) {
        img_load(game, (const uint8_t *)(XIP_BASE + H->game_off + (uint32_t)r * H->stride), 0, ROM_SIZE);
        rom[FOOT_IDX] = game[FOOT_IDX];       /* au redemarrage, la console lira la signature du jeu */
        g_ready = 1;
        g_req = -1;
    }
}
#endif
