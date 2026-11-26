/*
 * exllib.h -- mode bibliotheque (menu + jeux), commun a EXLCART et EXLSONDE.
 *
 * Flash, a partir de 2 Mo :
 *   0x200000 : en-tete (signature EXLB, nombre de jeux, emplacements), puis a
 *              +256 le bloc de 3840 octets recopie dans le menu en $7000-$7EFF ;
 *   0x201000 : le menu (loader), 32 Ko ;  0x209000 + n*0x8000 : le jeu n ;
 *   version 2 : flux_off = emplacement d'un film "disque" (conteneur EXLF, voir exlflux.h),
 *              rangé après les jeux, pour un lecteur disque de la bibliotheque (0 : aucun).
 * Le coeur 1 sert (serve_multi), le coeur 0 charge le jeu demande (lib_poll).
 * Une entree peut aussi etre une IMAGE EXELMEMOIRE (16 Ko au debut de son
 * emplacement ; EXLPICO marque son nom, bit 7 du 1er caractere) : a la demande
 * du menu ($6E00+n), lib_poll la copie dans la fenetre $2800-$67FF du menu.
 * Sauvegarde de l'Exelmemoire (menu v5.1) : le menu envoie ses 16 Ko par des
 * lectures (exlmulti.h), le coeur 0 les range dans la flash (save_poll, exlsave.h) ;
 * tampon : img_pool[1] s'il existe (libre en mode bibliotheque), sinon un tampon a part.
 * A n'inclure qu'une fois par firmware (definit g_req, g_ready ; game = img_pool[0]).
 */
#ifndef EXLLIB_H
#define EXLLIB_H
#include <string.h>
#include "exlserve.h"
#include "hardware/structs/timer.h"
#include "hardware/sync.h"                         /* __dmb */
#include "exlbanks.h"                             /* img_pool[] : reserve d'images en RAM */
#if EXL_POOL_IMAGES >= 2
#define SAVE_BUF ((uint8_t *)img_pool[1])         /* sauvegarde : libre en mode bibliotheque */
#else
static uint8_t save_buf[0x8000] __attribute__((aligned(4)));
#define SAVE_BUF save_buf
#endif
#include "exlmulti.h"

#define LIB_FLASH_OFFSET 0x00200000u
#define LIB_MAGIC        0x424C5845u              /* "EXLB" */
#define LIB_INJECT_ADDR  0x7000u
#define LIB_INJECT_SIZE  3840u
#define LIB_MAX_GAMES    190u
typedef struct { uint32_t magic; uint16_t version, count;
                 uint32_t menu_off, game_off, stride, flux_off; } lib_hdr_t;

extern cell_t rom[ROM_SIZE];                      /* le menu, en mode bibliotheque */
static cell_t *const game = img_pool[0];          /* le jeu charge (reserve partagee) */
volatile int32_t  g_req = -1;
volatile uint32_t g_ready = 0;
volatile uint32_t g_mstate = ST_MENU;
volatile int32_t  g_creq = -1;
volatile uint32_t g_boots = 0, g_hot = 0;
volatile uint32_t g_cram_ir = 0;                  /* coeur 0 : image Exelmemoire en cours (infrarouge) */
volatile uint32_t g_flux_game = 0;                /* le jeu charge est un lecteur disque, film pret */
volatile uint32_t g_sv_done = 0;                  /* sauvegarde de l'Exelmemoire recue (coeur 1 -> 0) */
volatile uint32_t g_k7_game = 0;                  /* le jeu charge est un BASIC avec sa cassette (exlk7.h) */
volatile uint32_t g_warm = WARM_NONE;             /* reprise a chaud du jeu charge (lib_warm) */
uint32_t g_sv_n = 0;
#include "exlsave.h"
static void (*g_lib_loaded)(int32_t r);           /* coeur 0 : appele apres le chargement d'un jeu */
static void (*g_lib_flux_serve)(void);            /* coeur 1 : service du jeu disque (revient au menu) */
static int  (*g_lib_k7_load)(const lib_hdr_t *H, int32_t r);  /* coeur 0 : entree K7 ? la charge (1) */
static void (*g_lib_k7_serve)(void);              /* coeur 1 : service du BASIC + cassette (revient au menu) */
static void (*g_lib_k7_step)(void);               /* coeur 0 : la bande (blocs, anneau) */
#ifndef LIB_AFTER_ACCESS
#define LIB_AFTER_ACCESS()  ((void)0)             /* essai en emulateur : le travail du coeur 0 */
#endif
static multi_t M0;

/* coeur 0, apres le chargement d'un jeu : REPRISE A CHAUD de l'EXELTEL.
 * Au reset, la ROM de l'EXELTEL compare $C006-$C008 a $C7FD-$C7FF ; s'ils sont egaux,
 * elle repart a l'adresse rangee en $C007-$C008 SANS relire le pied de la cartouche :
 * le Pico ne voyait pas le reset (EXEL BASIC PLUS : on restait dans le BASIC). Un jeu
 * qui pose cette adresse W le fait par "movd %W,B / sta @$C007 / sta @$C7FE" : on la
 * cherche ; W ne sert qu'a cette reprise. On y met "br @$7FFD" dans le jeu, et dans le
 * menu (zone libre sous $1000) les octets $7F $FD apres W : apres un reset, la console lit
 * W (le $8C du jeu), le Pico passe au menu (multi_after, serve_k7), $7F $FD viennent du
 * menu, puis le BR du pied du menu : le menu demarre. Sur l'EXL100 (pas de reprise a
 * chaud), rien ne change. */
static void lib_warm(const lib_hdr_t *H, const uint8_t *src) {
    static const uint8_t pat[10] = { 0x88, 0, 0, 0x01, 0x8B, 0xC0, 0x07, 0x8B, 0xC7, 0xFE };
    const uint8_t *menu = (const uint8_t *)(XIP_BASE + H->menu_off);
    g_warm = WARM_NONE;
    for (uint32_t i = 0x0200; i + sizeof pat <= 0x7FFCu; i++) {
        if (src[i] != 0x88 || memcmp(src + i + 3, pat + 3, sizeof pat - 3)) continue;
        const uint32_t w = (uint32_t)src[i + 1] << 8 | src[i + 2];
        if (w < 0x0200u || w + 3u > 0x1000u) return;                  /* le menu commence en $1000 */
        for (uint32_t k = 1; k < 3; k++) if (menu[w + k] != 0xFF && menu[w + k] != 0x00) return;
        game[ADDR_TO_IDX(w)] = ENC(0x8C);                               /* jeu : br @$7FFD */
        game[ADDR_TO_IDX(w + 1u)] = ENC(0x7F); game[ADDR_TO_IDX(w + 2u)] = ENC(0xFD);
        rom[ADDR_TO_IDX(w + 1u)] = ENC(0x7F); rom[ADDR_TO_IDX(w + 2u)] = ENC(0xFD);   /* menu : la suite */
        g_warm = w;
        return;
    }
}
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
 * les instructions de la ROM unique ; la gestion d'etat a lieu apres (exlmulti.h).
 * (menu v5.1) L'attente et l'acces sont ecrits en assembleur, instruction pour
 * instruction comme le compilateur les produisait en v4.1 (verifie sur la machine) :
 * le code qu'on ajoute apres l'acces ne peut plus deplacer un registre et ajouter
 * une instruction entre la descente de /ROM_CS et l'octet presente. */
_Static_assert(CS_PIN == 23, "serve_multi : /ROM_CS sur GP23");
/* l'attente et l'acces, en assembleur (serve_multi et serve_k7, exlk7.h) :
 *   EPROM : ip = interp0 (PEEK2 = image/2 + case), h = PEEK2 du dernier acces ;
 *   lineaire : img = l'image, a = l'adresse du dernier acces ;  now = l'heure (TIMERAWL) */
#ifdef EXL_PINOUT_EPROM
_Static_assert(DATA_SHIFT == 14, "serve : D sur GP14-GP22");
#define SERVE_ACCESS(sio_, cs_, dm_, ip_, p_, h_, now_) pico_default_asm_volatile( \
            "1: ldr  %[p], [%[sio], #4]   \n"  /* repos : 3 instructions */ \
            "   tst  %[p], %[cs]          \n" \
            "   bne  1b                   \n" \
            "2: str  %[p], [%[ip], #0]    \n"  /* l'acces : ACCUM0 = broches */ \
            "   ldr  %[h], [%[ip], #40]   \n"  /* PEEK2 = image/2 + case */ \
            "   ldrh %[d], [%[h], %[h]]   \n"  /* la case (16 bits) */ \
            "   lsls %[d], %[d], #14      \n" \
            "   str  %[d], [%[sio], #16]  \n"  /* GPIO_OUT */ \
            "   str  %[dm], [%[sio], #36] \n"  /* GPIO_OE_SET */ \
            "   ldr  %[p], [%[sio], #4]   \n" \
            "   tst  %[p], %[cs]          \n" \
            "   beq  2b                   \n" \
            "   ldr  %[d], 9f             \n"  /* (comme en v4.1) */ \
            "   str  %[dm], [%[sio], #40] \n"  /* GPIO_OE_CLR : bus libere */ \
            "   ldr  %[d], [%[d], #40]    \n"  /* TIMERAWL : l'heure de l'acces */ \
            "   b    8f                   \n" \
            "   .balign 4                 \n" \
            "9: .word %c[tb]              \n" \
            "8:                           \n" \
            : [p] "=&l" (p_), [h] "=&l" (h_), [d] "=&l" (now_) \
            : [sio] "l" (sio_), [cs] "l" (cs_), [dm] "l" (dm_), [ip] "l" (ip_), [tb] "i" (TIMER_BASE) \
            : "cc", "memory")
#else
_Static_assert(DATA_SHIFT == 15 && ADDR_MASK == 0x7FFFu, "serve : A sur GP0-GP14, D sur GP15-GP22");
#define SERVE_ACCESS(sio_, cs_, dm_, img_, p_, a_, now_) pico_default_asm_volatile( \
            "1: ldr  %[p], [%[sio], #4]   \n"  /* repos : 3 instructions */ \
            "   tst  %[p], %[cs]          \n" \
            "   bne  1b                   \n" \
            "2: lsls %[a], %[p], #17      \n"  /* l'acces : a = broches & $7FFF */ \
            "   lsrs %[a], %[a], #17      \n" \
            "   ldrb %[d], [%[img], %[a]] \n" \
            "   lsls %[d], %[d], #15      \n" \
            "   str  %[d], [%[sio], #16]  \n"  /* GPIO_OUT */ \
            "   str  %[dm], [%[sio], #36] \n"  /* GPIO_OE_SET */ \
            "   movs %[d], %[p]           \n"  /* (comme en v4.1) */ \
            "   ldr  %[p], [%[sio], #4]   \n" \
            "   tst  %[p], %[cs]          \n" \
            "   beq  2b                   \n" \
            "   ldr  %[d], 9f             \n"  /* (comme en v4.1) */ \
            "   str  %[dm], [%[sio], #40] \n"  /* GPIO_OE_CLR : bus libere */ \
            "   ldr  %[d], [%[d], #40]    \n"  /* TIMERAWL : l'heure de l'acces */ \
            "   b    8f                   \n" \
            "   .balign 4                 \n" \
            "9: .word %c[tb]              \n" \
            "8:                           \n" \
            : [p] "=&l" (p_), [a] "=&l" (a_), [d] "=&l" (now_) \
            : [sio] "l" (sio_), [cs] "l" (cs_), [dm] "l" (dm_), [img] "l" (img_), [tb] "i" (TIMER_BASE) \
            : "cc", "memory")
#endif
static void __noinline __not_in_flash_func(serve_multi)(void) {
    multi_t m = M0;                       /* copie locale : etat en registres */
    m.t_end = timer_hw->timerawl;
    io_rw_32 *const sio = (io_rw_32 *)sio_hw;
    const uint32_t cs = CS_MASK, dm = DATA_MASK;
#ifdef EXL_PINOUT_EPROM
    interp_hw_t *ip = interp0_hw;                       /* configure par core1_multi */
    for (;;) {
        const uint32_t b2 = (uint32_t)m.img >> 1;
        ip->base[2] = b2;                                   /* image servie */
        uint32_t p, h, now;
        SERVE_ACCESS(sio, cs, dm, ip, p, h, now);
        if (multi_after(&m, IDX_TO_ADDR(h - b2), now) && (g_flux_game | g_k7_game)) { M0 = m; return; }
        LIB_AFTER_ACCESS();
    }
#else
    for (;;) {
        const cell_t *img = m.img;
        uint32_t p, a, now;
        SERVE_ACCESS(sio, cs, dm, img, p, a, now);
        if (multi_after(&m, a, now) && (g_flux_game | g_k7_game)) { M0 = m; return; }   /* jeu disque, K7 */
        LIB_AFTER_ACCESS();
    }
#endif
}
static void core1_multi(void) {
    save_and_disable_interrupts();
    pins_interp_init();                                     /* interpolateur du coeur 1 */
    for (;;) {
        serve_multi();                                      /* revient au lancement d'un jeu disque ou K7 */
        if (g_k7_game) { if (g_lib_k7_serve) g_lib_k7_serve(); }   /* revient au menu (reset) */
        else if (g_lib_flux_serve) g_lib_flux_serve();
    }
}
static cram_st_t CR;                      /* coeur 0 : Exelmemoire (cram_step, exlmulti.h) */
/* coeur 0 : charge le jeu demande, s'il y en a un */
static volatile uint32_t g_loads;          /* nombre de jeux charges */
static volatile int32_t g_last_game = -1;  /* le dernier charge */
static inline void lib_poll(const lib_hdr_t *H) {
    int32_t r = g_req;
    if (r >= 0) {
        const int k7 = g_lib_k7_load && g_lib_k7_load(H, r);   /* K7 : son BASIC, corrige, et la bande */
        if (!k7) {
            const uint8_t *src = (const uint8_t *)(XIP_BASE + H->game_off + (uint32_t)r * H->stride);
            if (*(const uint32_t *)src == 0x374B5845u && *(const uint16_t *)(src + 6) < H->count)   /* K7 sans exlk7.h */
                src = (const uint8_t *)(XIP_BASE + H->game_off + (uint32_t)*(const uint16_t *)(src + 6) * H->stride);
            img_load(game, src, 0, ROM_SIZE);     /*   (sonde) : son BASIC seul */
            lib_warm(H, src);                     /* reprise a chaud de l'EXELTEL -> le menu */
        }
        rom[FOOT_IDX] = game[FOOT_IDX];       /* au redemarrage, la console lira la signature du jeu */
        g_last_game = r;
        if (g_lib_loaded && !k7) g_lib_loaded(r);   /* (jeu disque : film prepare avant le lancement) */
        g_loads++;                            /* (peripheriques : zone d'echange du nouveau jeu) */
        g_ready = 1;
        g_req = -1;
    }
    int32_t c = g_creq;
    if (c >= 0) {                             /* image Exelmemoire -> fenetre du menu */
        g_creq = -1;
        img_load(rom, (const uint8_t *)(XIP_BASE + H->game_off + (uint32_t)c * H->stride), CRAM_WIN_A, CRAM_SIZE);
        __dmb();                              /* la fenetre d'abord, le signal ensuite */
        rom[CRAM_STAT_IDX] = ENC(CRAM_READY);
    }
    cram_step(rom, M0.menu_foot, timer_hw->timerawl, &CR);
    save_poll(rom, SAVE_BUF);                 /* sauvegarde de l'Exelmemoire recue : la flash */
    if (g_k7_game && g_lib_k7_step) g_lib_k7_step();   /* la cassette : blocs, octets en avance */
}
#endif
