/*
 * EXLRAM -- ESSAI de cartouche RAM : la ROM est servie comme d'habitude (coeur 1,
 * exlserve.h) et les ECRITURES dans la zone cartouche sont reconnues par un
 * automate PIO (exlram.pio), grace au doigt A1 (NROMCS) relie par un fil volant a
 * GP26 ; le coeur 0 les range dans l'image servie.
 *
 * Fenetre inscriptible : $4000-$7BFF (le programme et le pied de ROM ne peuvent
 * pas etre abimes par une ecriture fantome). Bloc d'etat, lu par la ROM d'essai
 * (testram.asm) a travers le bus, en $7F80 :
 *   $7F80 "RAM1" ; $7F84 ecritures acceptees ; $7F86 ecritures hors fenetre ;
 *   $7F88 instantanes annules (A1 deja remonte, ou B1 a 0) ; $7F8A derniere adresse
 *   acceptee ; $7F8C derniere donnee ; $7F8D entrees du journal ; $7F90 journal des
 *   32 premieres ecritures hors fenetre (adresse haute, basse, donnee).
 *   Compteurs sur 16 bits, poids fort en premier.
 * Rien n'est garde a l'extinction (essai) ; pas d'USB, pas de bibliotheque.
 */
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/structs/sio.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "exlram.pio.h"
#include "exlserve.h"

#define SYS_CLOCK_KHZ   200000u
#define A1_PIN          26
#define A1_MASK         (1u << A1_PIN)
#define WIN_LO          0x4000u
#define WIN_HI          0x7BFFu
#define ST_BASE         0x7F80u

cell_t rom[ROM_SIZE] __attribute__((aligned(4)));

static inline void put8(uint32_t a, uint8_t v) { rom[ADDR_TO_IDX(a)] = ENC(v); }
static inline void put16(uint32_t a, uint32_t v) { put8(a, (uint8_t)(v >> 8)); put8(a + 1, (uint8_t)v); }

static void core1_entry(void) {
    save_and_disable_interrupts();
    pins_interp_init();
    exl_serve(exl_read_cfg());
}

static void __noinline __not_in_flash_func(ecritures)(PIO pio, uint sm) {
    uint32_t acc = 0, hors = 0, ann = 0, nlog = 0;
    for (;;) {
        if (pio_sm_is_rx_fifo_empty(pio, sm)) continue;
        uint32_t v = pio->rxf[sm];
        uint32_t P = (v << 26) | (v >> 6);            /* in_base = 26 */
        if ((P & A1_MASK) || !(P & CS_MASK)) { put16(ST_BASE + 8, ++ann); continue; }
        uint32_t idx = idx_of_pins(P);
        uint32_t a = IDX_TO_ADDR(idx);
        cell_t c = (cell_t)((P & DATA_MASK) >> DATA_SHIFT);  /* la case : deja dans l'ordre des broches */
        if (a >= WIN_LO && a <= WIN_HI) {
            rom[idx] = c;
            put16(ST_BASE + 4, ++acc);
            put16(ST_BASE + 10, a);
            rom[ADDR_TO_IDX(ST_BASE + 12)] = c;
        } else {
            put16(ST_BASE + 6, ++hors);
            if (nlog < 32) {
                put8(ST_BASE + 16 + 3 * nlog, (uint8_t)(a >> 8));
                put8(ST_BASE + 17 + 3 * nlog, (uint8_t)a);
                rom[ADDR_TO_IDX(ST_BASE + 18 + 3 * nlog)] = c;
                put8(ST_BASE + 13, (uint8_t)++nlog);
            }
        }
    }
}

int main(void) {
    pins_init();
    img_load(rom, (const uint8_t *)(XIP_BASE + ROM_FLASH_OFFSET), 0, ROM_SIZE);
    put8(ST_BASE, 'R'); put8(ST_BASE + 1, 'A'); put8(ST_BASE + 2, 'M'); put8(ST_BASE + 3, '1');
    for (uint32_t a = ST_BASE + 4; a < ST_BASE + 16 + 96; a++) put8(a, 0);
    pins_gpio_init();
    gpio_init(A1_PIN); gpio_set_dir(A1_PIN, GPIO_IN); gpio_disable_pulls(A1_PIN);
    set_sys_clock_khz(SYS_CLOCK_KHZ, true);
    multicore_launch_core1(core1_entry);

    PIO pio = pio0; uint sm = 0;
    uint off = pio_add_program(pio, &ramw_program);
    pio_sm_config c = ramw_program_get_default_config(off);
    sm_config_set_in_pins(&c, A1_PIN);
    sm_config_set_jmp_pin(&c, CS_PIN);
    sm_config_set_in_shift(&c, false, true, 32);      /* autopush a 32 bits */
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    sm_config_set_clkdiv(&c, 1.0f);
    pio_sm_init(pio, sm, off, &c);
    pio_sm_set_enabled(pio, sm, true);
    ecritures(pio, sm);
}
