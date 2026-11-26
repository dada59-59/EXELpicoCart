/*
 * EXLCART -- emulation d'une cartouche ROM 32 Ko pour Exelvision EXL100 / EXELTEL
 * sur Raspberry Pi Pico (RP2040). Deux modes, choisis au demarrage selon la flash :
 *   - BIBLIOTHEQUE (en-tete "EXLB" a 2 Mo) : un menu (loader exlmenu) et jusqu'a
 *     190 jeux ; le coeur 1 sert le bus, le coeur 0 charge le jeu choisi
 *     (exllib.h, exlmulti.h) ;
 *   - ROM UNIQUE (sinon) : la ROM rangee a 1 Mo, servie par le coeur 0 (exlserve.h) ;
 *     avec des banques de 16 Ko si un en-tete EXLK est present (exlbanks.h) ;
 *     si la ROM reserve une zone d'echange ("EXLJ" / "EXLP", voir exljoy.h), le
 *     service passe sur le coeur 1 et le coeur 0 gere la manette DB9 et la souris USB ;
 *     si elle reserve une zone video ("EXLV", voir exlvideo.h), le coeur 0 decode
 *     la video rangee a 8 Mo dans la flash et le coeur 1 la sert, liste par liste ;
 *     si elle reserve une zone disque ("EXLD", voir exlflux.h), le Pico sert deux
 *     fichiers octet par octet et c'est l'EXL100 qui fait tout ; ce lecteur peut aussi
 *     etre un jeu de la bibliotheque, son film range apres les jeux (exlfluxlib.h).
 *   - INFRAROUGE (exlir.h) : la manette DB9 est aussi envoyee a la console par une
 *     diode infrarouge sur GP25, comme la manette Exelvision d'origine, pour les jeux
 *     du commerce ; coupe quand l'image servie a sa propre zone d'echange, et dans
 *     les modes video et disque. Un clavier USB (exlkbd.h) passe par la meme diode,
 *     comme le clavier Exelvision, dans tous les modes sauf video et disque.
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
 * pratique (choix fait aussi par A8PicoCart). Les broches ADC (GP26-29) ne recoivent
 * jamais de 5 V : elles ne servent qu'a la manette DB9, en entrees 3,3 V avec tirage.
 * La souris USB utilise le port USB du Pico, en hote (exlmouse.c, TinyUSB).
 * Mode bibliotheque, Pico SEUL sur un PC (aucun demarrage de la console en 3 s) :
 * le port USB devient un port serie (exlusbdev.c) ; EXLPICO y relit les sauvegardes
 * de l'Exelmemoire faites depuis le menu (exlsave.h).
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
#include "hardware/watchdog.h"

#include "exlserve.h"
#ifdef EXL_LIBFLUX_TEST
static void lib_test_step(void);                  /* essai : le travail du coeur 0 apres chaque acces */
#define LIB_AFTER_ACCESS()  lib_test_step()
#define K7_AFTER_ACCESS()   lib_test_step()
#endif
#include "exllib.h"
#include "exljoy.h"
#include "pico/multicore.h"
#include "exlvideo.h"
#include "exlflux.h"
#include "exlfluxlib.h"
#include "exlk7.h"
#include "exlir.h"
#include "exlxinput.h"

#define SYS_CLOCK_KHZ    200000u                  /* 200 MHz, valide pour le RP2040 */

cell_t rom[ROM_SIZE] __attribute__((aligned(4)));      /* ROM unique, ou le menu */
kbd_state_t g_kbd;                                     /* clavier USB -> infrarouge (exlkbd.h) */
pad_state_t g_pad = { .held = -1 };                    /* manette USB : DB9 et touches (exlpad.h) */

/* mode manette : le coeur 1 sert la ROM (avec ou sans banques), le coeur 0 lit la DB9 */
static exl_cfg_t g_cfg;
/* mode ROM sans zone d'echange : le coeur 0 sert (comme toujours), le coeur 1 ne fait
 * que la manette infrarouge (sans interruption, hors du chemin du service) */
static void core1_ir(void) {
    ir_t ir;
    ir_start(&ir);
#ifdef EXL_WITH_MOUSE
    mouse_usb_init();                      /* clavier USB (et souris, inutilisee ici) */
#endif
    for (;;) {
        ir_poll(&ir, 1);
#ifdef EXL_WITH_MOUSE
        mouse_usb_task();
#endif
    }
}
/* ROM avec zone d'echange : DB9 et souris dans la zone, clavier USB par infrarouge
 * (la manette, elle, n'y passe pas : le jeu la lit dans sa zone) */
static void __noinline io_ir_loop(cell_t *const *imgs, uint32_t n, uint32_t base, int mouse, uint32_t div) {
    io_target_t t[BANK_MAX_IO];
    if (n > BANK_MAX_IO) n = BANK_MAX_IO;
    io_t s;
    io_init(&s, div, 1);                   /* USB toujours : le clavier */
    for (uint32_t k = 0; k < n; k++) { t[k].img = imgs[k]; t[k].base = base; t[k].mouse = (uint8_t)mouse; t[k].on = 1; io_fill(&s, &t[k]); }
    ir_t ir;
    ir_start(&ir);
    for (;;) { io_step(&s, t, n); ir_poll(&ir, 0); }
}
static void core1_rom(void) {
    save_and_disable_interrupts();
    pins_interp_init();                    /* interpolateur du coeur 1 (support EPROM) */
    if (g_nbanks >= 2) serve_banks();
    exl_serve(g_cfg);
}

#ifdef EXL_IR_TEST
volatile uint8_t g_test_kbd[8];
volatile uint32_t g_test_kbd_n;
volatile uint8_t g_test_pad_desc[512];                 /* manette : descripteur de rapport, */
volatile uint32_t g_test_pad_desc_len;                 /* (lu quand g_test_pad_n change la 1re fois) */
volatile uint8_t g_test_pad[64];                       /* puis ses rapports */
volatile uint32_t g_test_pad_len, g_test_pad_n;
volatile uint32_t g_test_xi_kind;                      /* non nul : les rapports sont XInput (exlxinput.h) */
static xi_t g_test_xi;
static pad_layout_t g_test_L;
static uint8_t g_test_buf[512];
#endif
#ifdef EXL_LIBFLUX_TEST
static const lib_hdr_t *g_test_H;
static void lib_test_step(void) { lib_poll(g_test_H); }
#endif

/* Pico seul sur un PC (mode bibliotheque) : port serie USB pour EXLPICO.
 *   'm' : les sauvegardes de l'Exelmemoire (exlsave.h) ; 'x' : les effacer ;
 *   'd' : (bouton "releve" d'EXLPICO) ce n'est pas la sonde.
 * Si la console demarre quand meme (Pico en retard sur la console : elle avait
 * demarre sans cartouche), on redemarre le Pico, qui sautera alors l'attente. */
#define PC_WAIT_US     3000000u
#define PC_SKIP_MAGIC  0x45584C43u           /* watchdog scratch[4] : "console vue" */
#ifdef EXL_WITH_MOUSE
void usbdev_init(void);
int  usbdev_getc(void);
void usbdev_write(const char *s, uint32_t n);
static void usbdev_puts(const char *s) { usbdev_write(s, (uint32_t)strlen(s)); }
static void __noinline usb_pc_mode(const lib_hdr_t *H) {
    usbdev_init();
    const uint32_t boots = g_boots;
    for (;;) {
        lib_poll(H);
        const int c = usbdev_getc();
        if (c == 'm' || c == 'M') save_dump(usbdev_write);
        else if (c == 'x' || c == 'X') { save_erase(); usbdev_puts("EXLMEM EFFACE\n"); }
        else if (c == 'd' || c == 'D') usbdev_puts("EXLCART (firmware cartouche, pas de releve)\nEXLSONDE VIDE\n");
        if (g_boots != boots) {                    /* la console lit la cartouche */
            watchdog_hw->scratch[4] = PC_SKIP_MAGIC;
            watchdog_reboot(0, 0, 0);
            for (;;) tight_loop_contents();
        }
    }
}
#endif

int main(void) {
#ifdef EXL_IR_TEST
    /* essai en emulateur : l'emetteur infrarouge seul, sur le coeur 0 ; le banc
     * ecrit des rapports de clavier dans g_test_kbd (et incremente g_test_kbd_n),
     * et de manette dans g_test_pad (apres son descripteur) */
    set_sys_clock_khz(SYS_CLOCK_KHZ, true);
    {
        ir_t ir; ir_start(&ir);
        uint32_t seen = g_test_kbd_n, pseen = g_test_pad_n;
        int pad_ok = 0;
        for (;;) {
            if (g_test_pad_n != pseen) {
                pseen = g_test_pad_n;
                if (pad_ok != 2 && g_test_xi_kind) {      /* (apres une manette HID : on passe a XInput) */
                    memset(&g_test_xi, 0, sizeof g_test_xi);
                    g_test_xi.kind = (uint8_t)g_test_xi_kind;
                    pad_ok = 2; pad_reset(&g_pad, &g_kbd); g_pad.on = 1;
                }
                if (!pad_ok) {
                    uint32_t n = g_test_pad_desc_len;
                    if (n > sizeof g_test_buf) n = sizeof g_test_buf;
                    for (uint32_t i = 0; i < n; i++) g_test_buf[i] = g_test_pad_desc[i];
                    pad_ok = pad_parse(g_test_buf, n, &g_test_L);
                    if (pad_ok) { pad_reset(&g_pad, &g_kbd); g_pad.on = 1; }
                }
                if (pad_ok) {
                    uint32_t n = g_test_pad_len;
                    if (n > 64) n = 64;
                    for (uint32_t i = 0; i < n; i++) g_test_buf[i] = g_test_pad[i];
                    if (pad_ok == 2) {
                        uint8_t out[16], on;
                        if (xi_input(&g_test_xi, g_test_buf, n, out, &on)) pad_apply(&g_pad, &g_kbd, g_test_xi.v, g_test_xi.keys);
                    } else pad_report(&g_pad, &g_kbd, &g_test_L, g_test_buf, n);
                }
            }
            if (g_test_kbd_n != seen) {
                seen = g_test_kbd_n;
                uint8_t r[8];
                for (int i = 0; i < 8; i++) r[i] = g_test_kbd[i];   /* lectures volatiles : le banc ecrit */
                kb_report(&g_kbd, r);
            }
            ir_poll(&ir, 1);
        }
    }
#endif
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
        const int skip_pc = watchdog_hw->scratch[4] == PC_SKIP_MAGIC;   /* console deja vue */
        watchdog_hw->scratch[4] = 0;
        gpio_pull_up(CS_PIN);              /* sur un PC, /ROM_CS reste a 1 (comme la sonde) */
        lib_prepare(H);
        lib_flux_init(H);                  /* jeu "disque" possible (film apres les jeux) */
        lib_k7_init();                     /* cassettes K7 (BASIC + bande) */
        set_sys_clock_khz(SYS_CLOCK_KHZ, true);
#ifdef EXL_LIBFLUX_TEST
        g_test_H = H;                      /* essai en emulateur (1 seul coeur) : bibliotheque + disque */
        save_and_disable_interrupts();
        pins_interp_init();
        for (;;) { serve_multi(); if (g_k7_game) serve_k7(); else serve_flux_lib(); }
#endif
#ifdef EXL_SINGLE_CORE_TEST
        save_and_disable_interrupts();     /* essai en emulateur (1 seul coeur) */
        pins_interp_init();
        serve_multi();
#endif
#ifndef EXL_LIBIO_TEST                         /* essai en emulateur : les peripheriques seuls */
        multicore_launch_core1(core1_multi);
#endif
#if defined(EXL_WITH_MOUSE) && (!defined(EXL_LIBIO_TEST) || defined(EXL_USBDEV_TEST))
        /* la console lit le pied ~0,3 s apres l'allumage ; rien en 3 s : sur un PC */
        if (!skip_pc) {
            const uint32_t t0 = timer_hw->timerawl;
            while (!g_boots && timer_hw->timerawl - t0 < PC_WAIT_US) lib_poll(H);
            if (!g_boots) usb_pc_mode(H);          /* ne revient pas */
        }
#else
        (void)skip_pc;
#endif
        /* peripheriques : la zone d'echange du menu, et celle du jeu charge (cherchee
         * a chaque chargement) ; un jeu sans zone n'est jamais touche */
        joy_gpio_init();
        ir_t ir;
        ir_start(&ir);
        int mm = 0;
        const int32_t mio = io_find((const uint8_t *)(XIP_BASE + H->menu_off), ROM_SIZE, &mm);
        io_target_t T[2] = { { rom, (uint32_t)(mio < 0 ? 0 : mio), (uint8_t)mm, (uint8_t)(mio >= 0) },
                             { game, 0, 0, 0 } };
        io_t s;
        io_init(&s, exl_read_cfg().r1, 1);
        if (T[0].on) io_fill(&s, &T[0]);
        uint32_t loads = g_loads;
        for (;;) {
            lib_poll(H);
            if (g_loads != loads) {
                loads = g_loads;
                int gm = 0;
                const int32_t gio = io_find((const uint8_t *)(XIP_BASE + H->game_off + (uint32_t)g_last_game * H->stride),
                                            ROM_SIZE, &gm);
                T[1].base = (uint32_t)(gio < 0 ? 0 : gio); T[1].mouse = (uint8_t)gm; T[1].on = (uint8_t)(gio >= 0);
                if (T[1].on) io_fill(&s, &T[1]);
            }
            io_step(&s, T, 2);
            if (g_fok) flx_step();         /* jeu disque : la flash devant la console */
            /* infrarouge : seulement pour une image sans zone d'echange, ou pendant
             * qu'une image Exelmemoire tourne (g_cram_ir) */
            ir_poll(&ir, g_mstate == ST_GAME ? !T[1].on : (g_cram_ir ? 1 : !T[0].on));   /* Exelmemoire : oui */
        }
    }

    /* 4b) ROM qui reserve une zone VIDEO ("EXLV", lecteur video) : le coeur 1 sert,
     *     le coeur 0 decode la video rangee a 8 Mo dans la flash (exlvideo.h) */
    {
        vzone_t vz;
        if (vz_find((const uint8_t *)(XIP_BASE + ROM_FLASH_OFFSET), ROM_SIZE, &vz))
            video_main(&vz, SYS_CLOCK_KHZ);
    }

    /* 4c) ROM qui reserve une zone DISQUE ("EXLD", lecteur badflux) : le coeur 1 sert
     *     deux fichiers ranges a 2 Mo, octet par octet ; le coeur 0 ne fait que recopier
     *     la flash devant la console (exlflux.h) */
    {
        fzone_t fz;
        if (fz_find((const uint8_t *)(XIP_BASE + ROM_FLASH_OFFSET), ROM_SIZE, &fz))
            flux_main(&fz, SYS_CLOCK_KHZ);
    }

    /* 5) ROM qui reserve une zone d'echange (signature "EXLJ" : manette DB9, ou
     *    "EXLP" : manette + souris USB) : service sur le coeur 1, peripheriques sur
     *    le coeur 0, avec ou sans banques */
    const uint32_t nb = bank_prepare();
    int mouse = 0;
    const int32_t io = io_find((const uint8_t *)(XIP_BASE + ROM_FLASH_OFFSET),
                               (nb >= 2 && !g_bank_full) ? 0x4000u : ROM_SIZE, &mouse);
    if (io >= 0) {
        joy_gpio_init();
        cell_t *imgs[BANK_MAX];
        uint32_t n = 0;
        imgs[n++] = rom;
        for (uint32_t k = 1; k < nb; k++) imgs[n++] = (cell_t *)bank_img[k];
        g_cfg = exl_read_cfg();
        set_sys_clock_khz(SYS_CLOCK_KHZ, true);
#ifdef EXL_SINGLE_CORE_TEST
        io_loop(imgs, n, (uint32_t)io, mouse, g_cfg.r1);   /* essai en emulateur : les peripheriques seuls */
#endif
        multicore_launch_core1(core1_rom);
        io_ir_loop(imgs, n, (uint32_t)io, mouse, g_cfg.r1);
    }

    /* 6) ROM a banques (en-tete EXLK) : une image par banque, bascule en $3FF0+n
     *    (en-tete v2 : deux images de 32 Ko, bascule en $7FF0 / $7FF8) */
    if (nb >= 2) {
        set_sys_clock_khz(SYS_CLOCK_KHZ, true);
#ifndef EXL_SINGLE_CORE_TEST
        multicore_launch_core1(core1_ir);
#endif
        save_and_disable_interrupts();
        pins_interp_init();
        serve_banks();
    }

    /* 7) ROM unique : reglages (bloc EXLC apres la ROM), 200 MHz, puis la boucle */
    exl_cfg_t cfg = exl_read_cfg();
    set_sys_clock_khz(SYS_CLOCK_KHZ, true);
#ifndef EXL_SINGLE_CORE_TEST
    multicore_launch_core1(core1_ir);
#endif
    save_and_disable_interrupts();
    pins_interp_init();                    /* interpolateur du coeur 0 (support EPROM) */
    exl_serve(cfg);
}
