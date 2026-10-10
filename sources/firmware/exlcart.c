/*
 * EXLCART -- emulation d'une cartouche ROM 32 Ko pour Exelvision EXL100 / EXELTEL
 * sur Raspberry Pi Pico (RP2040). Deux modes, choisis au demarrage selon la flash :
 *   - BIBLIOTHEQUE (en-tete "EXLB" a 2 Mo) : un menu (loader exlmenu) et jusqu'a
 *     190 jeux ; le coeur 1 sert le bus, le coeur 0 charge le jeu choisi
 *     (exllib.h, exlmulti.h) ;
 *   - ROM UNIQUE (sinon) : la ROM rangee a 1 Mo, servie par le coeur 0 (exlserve.h).
 *
 * Le port cartouche ne fournit que A0-A14, D0-D7 et /ROM_CS (les broches /CE et /OE
 * de l'EPROM y sont reliees ensemble). Le Pico se comporte donc comme l'EPROM :
 * tant que /ROM_CS est bas, il presente rom[adresse] sur D0-D7 ; sinon il relache
 * le bus (haute impedance).
 *
 * Brochage : voir exlpins.h. Deux cartes, choisies a la compilation :
 *   - lineaire (cible exlcart) : A0-A14 -> GP0-GP14, D0-D7 -> GP15-GP22 ;
 *   - support EPROM (cible exlcart_eprom) : Pico soude sur l'empreinte 27C256,
 *     adresses et donnees permutees, A10 sur GP24 ; meme vitesse de service.
 *   /ROM_CS  -> GP23 dans les deux cas (broche non-ADC, branchee en direct ;
 *               elle n'est sortie que sur le "Purple Pico" type YD-RP2040, ou elle
 *               commande aussi la LED RGB WS2812 : celle-ci peut changer de couleur
 *               au hasard pendant le jeu, sans consequence)
 *
 * Toutes les entrees recoivent du 5 V de la console : le RP2040 n'est pas
 * officiellement tolerant au 5 V, mais ses broches non-ADC le supportent en
 * pratique (choix fait aussi par A8PicoCart). Aucune broche ADC (GP26-29) n'est
 * utilisee.
 *
 * Le service du bus (et ses reglages "facon EPROM") est dans exlserve.h.
 *
 * Les ROM ne sont pas compilees dans le firmware : elles sont lues dans la flash du
 * Pico (fichiers .uf2 produits par exlpico.html) et copiees en RAM. Changer de jeu
 * ou de bibliotheque ne demande donc jamais de recompiler.
 */
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/structs/sio.h"
#include "hardware/sync.h"

#include "exlserve.h"
#include "exllib.h"
#include "pico/multicore.h"

#define SYS_CLOCK_KHZ    200000u                  /* 200 MHz, valide pour le RP2040 */

cell_t rom[ROM_SIZE] __attribute__((aligned(4)));      /* ROM unique, ou le menu */

int main(void) {
    /* 1) la ROM : flash -> RAM (rangee selon le brochage), avant tout le reste */
    pins_init();
    img_load(rom, (const uint8_t *)(XIP_BASE + ROM_FLASH_OFFSET), 0, ROM_SIZE);

    /* 2) broches : toutes en entree, sans resistance de tirage */
    pins_gpio_init();

    /* (plus de clignotement ici : il retardait le service de 0,6 s quand la ROM
     *  unique est absente -- normal en mode bibliotheque -- et la console, qui lit
     *  le pied ~0,3 s apres l'allumage, ne trouvait alors pas la cartouche) */

    /* 4) bibliotheque presente : menu + jeux, servis par le coeur 1 ; le coeur 0
     *    charge le jeu demande depuis la flash */
    const lib_hdr_t *H = lib_find();
    if (H) {
        lib_prepare(H);
        set_sys_clock_khz(SYS_CLOCK_KHZ, true);
#ifdef EXL_SINGLE_CORE_TEST
        save_and_disable_interrupts();     /* essai en emulateur (1 seul coeur) */
        pins_interp_init();
        serve_multi();
#endif
        multicore_launch_core1(core1_multi);
        for (;;) lib_poll(H);
    }

    /* 5) ROM a banques (en-tete EXLK) : une image par banque, bascule en $3FF0+n */
    if (bank_prepare() >= 2) {
        set_sys_clock_khz(SYS_CLOCK_KHZ, true);
        save_and_disable_interrupts();
        pins_interp_init();
        serve_banks();
    }

    /* 6) ROM unique : reglages (bloc EXLC apres la ROM), 200 MHz, puis la boucle */
    exl_cfg_t cfg = exl_read_cfg();
    set_sys_clock_khz(SYS_CLOCK_KHZ, true);
    save_and_disable_interrupts();
    pins_interp_init();                    /* interpolateur du coeur 0 (support EPROM) */
    exl_serve(cfg);
}
