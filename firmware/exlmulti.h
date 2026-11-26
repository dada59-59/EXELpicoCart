/*
 * exlmulti.h -- service "bibliotheque" : un menu (loader) et des jeux.
 *
 * Le service doit etre AUSSI RAPIDE que celui de la ROM unique : mesure sur
 * EXL100, une boucle qui faisait ses controles avant de presenter l'octet ne le
 * sortait qu'a ~200-260 ns, et la console ne reconnaissait plus le pied.
 * Donc, dans la boucle de service :
 *   - au repos : lire les broches, tester /ROM_CS, recommencer (rien d'autre) ;
 *   - pendant l'acces : exactement les instructions de la ROM unique ;
 *   - APRES l'acces (/ROM_CS remonte, >= 400 ns avant le suivant) : multi_after,
 *     qui gere l'etat, la pause etant mesuree au minuteur materiel (us).
 * Aucune decision au moment de lire le pied : l'octet de signature de la
 * PROCHAINE image est ecrit d'avance dans le pied de l'image courante
 *   - jeu choisi : le coeur 0, une fois le jeu charge, ecrit sa signature dans
 *     le pied du menu ; au redemarrage, la console lit donc celle du jeu ;
 *   - pendant la partie (apres ARM_N acces) : la signature du menu est ecrite
 *     dans le pied du jeu, pour qu'un reset relise celle du menu.
 * Etats :
 *   MENU   : le menu ; une lecture en $6F00+n (n < nombre de jeux) demande le jeu n -> LAUNCH
 *   LAUNCH : si le jeu est charge, bascule -> GAME
 *            - a une lecture en GO_ADDR ($6FFF) : le loader saute ensuite lui-meme au
 *              jeu (BR @$7FFD, l'instruction de demarrage du pied), depuis la RAM ;
 *            - ou a la lecture du pied (redemarrage par reset ou TRAP 0)
 *   GAME   : une lecture du pied apres une pause de plus de RESET_US (reset) -> MENU
 * Exelmemoire (menu v4) : dans l'etat MENU, une lecture en $6E00+n (n < nombre
 * d'entrees) demande l'image Exelmemoire n (16 Ko) : CRAM_STAT ($6DFF) passe a 0
 * tout de suite, le coeur 0 copie l'image dans la fenetre $2800-$67FF DU MENU,
 * puis ecrit CRAM_READY ($A5) en CRAM_STAT. On reste dans l'etat MENU : le
 * loader recopie lui-meme la fenetre dans l'Exelmemoire ($8000-$BFFF).
 * Ensuite (menu v4.1), dans l'etat MENU, le coeur 1 ne fait que NOTER deux choses
 * (g_hot : une lecture $6DFC-$6DFE ; g_boots : un pied lu apres une pause) ; c'est
 * le coeur 0 qui agit (cram_poll, exllib.h), pour ne pas alourdir la boucle :
 *   - $6DFE : le loader demarre l'image (BR @$8001). Le menu reste servi ; la
 *     manette est relayee par infrarouge jusqu'au prochain demarrage.
 *   - $6DFD : sans cartouche. Le pied du menu passe a $FF ; au reset suivant, la
 *     console demarre sans cartouche (Exelmemoire, menu de la console) ; ~1 s
 *     apres, le pied du menu revient : le reset d'apres ramene au menu. Manette
 *     relayee par infrarouge pendant ce temps. $6DFC : annulation (avant le reset).
 * Sauvegarde de l'Exelmemoire (menu v5.1, voir exlsave.h) : dans l'etat MENU, une
 * lecture en $6BF0 remet le compteur a zero ; chaque lecture en $6C00+v range
 * l'octet v (le bus d'ADRESSES porte la donnee) ; $6BF1 : fin, le coeur 0 range
 * la sauvegarde dans la flash et repond en $6BF2 / $6BF3.
 */
#ifndef EXLMULTI_H
#define EXLMULTI_H
#include <stdint.h>
#include "exlpins.h"                 /* cell_t, ADDR_TO_IDX : rangement des images en RAM */

#define ST_MENU     0u
#define ST_LAUNCH   1u
#define ST_GAME     2u
#define FOOT_ADDR   0x7FFCu
#define SEL_BASE    0x6F00u          /* $6F00 + n : choix du jeu n */
#define GO_ADDR     0x6FFFu          /* "bascule maintenant" (jeu choisi et charge) */
#define FOOT_IDX    ADDR_TO_IDX(FOOT_ADDR)   /* case du pied dans une image en RAM */
#define RESET_US    2000u            /* pause sans acces qui signale un reset */
#define ARM_N       1000u            /* acces du jeu avant d'armer le retour au menu */
#define CRAM_BASE_A 0x6E00u          /* $6E00 + n : image Exelmemoire n -> fenetre */
#define CRAM_STAT_A 0x6DFFu          /* CRAM_READY quand la fenetre est remplie */
#define CRAM_WIN_A  0x2800u          /* fenetre du menu : $2800-$67FF */
#define CRAM_SIZE   0x4000u          /* une image Exelmemoire : $8000-$BFFF */
#define CRAM_READY  0xA5u
#define CRAM_STAT_IDX ADDR_TO_IDX(CRAM_STAT_A)
#define CRAM_GO_A   0x6DFEu          /* l'image Exelmemoire demarre */
#define NOCART_A    0x6DFDu          /* cacher la cartouche jusqu'au prochain reset */
#define NOCART_BACK 0x6DFCu          /* ... annule */
#define NOCART_US   1000000u         /* apres le demarrage sans cartouche : pied du menu remis */
#define CRAM_IR_RUN 1u               /* g_cram_ir : image demarree */
#define CRAM_IR_NOCART 2u            /*             sans cartouche */
#define SAVE_GO_A   0x6BF0u          /* sauvegarde : debut ($6BF1 : fin) */
#define SAVE_STAT_A 0x6BF2u          /* reponse du Pico (SAVE_OK...) */
#define SAVE_SLOT_A 0x6BF3u          /* emplacement utilise (1-8) */
#define SAVE_WIN_A  0x6C00u          /* $6C00 + v : un octet de l'Exelmemoire */
#define SAVE_LEN    (CRAM_SIZE + 2u) /* 16 Ko, puis la somme sur 16 bits */
#define SAVE_STAT_IDX ADDR_TO_IDX(SAVE_STAT_A)
#define SAVE_SLOT_IDX ADDR_TO_IDX(SAVE_SLOT_A)

typedef struct {
    cell_t *menu, *game;             /* les deux images (en RAM, rangees selon le brochage) */
    const cell_t *img;               /* image servie */
    uint32_t state, count, t_end, n_game;
    cell_t menu_foot, game_foot;     /* cases de signature d'origine */
    uint32_t warm;                   /* reprise a chaud du jeu (EXELTEL), ou WARM_NONE */
} multi_t;
#define WARM_NONE 0xFFFFFFFFu

/* partages entre les deux coeurs */
extern volatile int32_t  g_req;      /* jeu demande (coeur 1 -> coeur 0), -1 : aucun */
extern volatile uint32_t g_ready;    /* jeu charge, signature posee dans le pied du menu */
extern volatile uint32_t g_mstate;   /* image servie : ST_GAME ou non (coeur 1 -> coeur 0, infrarouge) */
extern volatile int32_t  g_creq;     /* image Exelmemoire demandee (coeur 1 -> coeur 0), -1 : aucune */
extern volatile uint32_t g_boots;    /* pied lu apres une pause, dans le menu : demarrages (coeur 1 -> 0) */
extern volatile uint32_t g_hot;      /* derniere lecture $6DFC-$6DFE dans le menu (coeur 1 -> 0), 0 : lue */
extern volatile uint32_t g_cram_ir;  /* coeur 0 : image Exelmemoire en cours -> infrarouge (CRAM_IR_...) */
extern volatile uint32_t g_sv_done;  /* sauvegarde recue : octets + 1 (coeur 1 -> 0), 0 : rien */
extern volatile uint32_t g_warm;     /* reprise a chaud du jeu charge (coeur 0, voir lib_warm) */
extern uint32_t g_sv_n;              /* sauvegarde : octets recus (coeur 1 seul ; hors de multi_t, et un
                                      * tampon a adresse fixe : la boucle de service garde ses registres) */
#ifndef SAVE_BUF                     /* tampon de SAVE_BUF_SIZE octets (exllib.h : img_pool[1]) */
extern uint8_t g_sv_mem[];
#define SAVE_BUF g_sv_mem
#endif
#define SAVE_BUF_SIZE 0x8000u        /* index masque : jamais hors du tampon */

#ifndef MULTI_INLINE
#define MULTI_INLINE static inline __attribute__((always_inline))
#endif

/* bascule sur le jeu charge : l'acces suivant le lira */
MULTI_INLINE void multi_to_game(multi_t *m) {
    m->menu[FOOT_IDX] = m->menu_foot;
    m->game_foot = m->game[FOOT_IDX];
    m->img = m->game; m->state = ST_GAME; m->n_game = 0;
    m->warm = g_warm;
    g_mstate = ST_GAME;
}
/* apres un acces a l'adresse a (adresse REELLE, deja remise dans l'ordre), au temps now (us) ;
 * renvoie 1 quand on vient de basculer sur le jeu (le service peut alors changer : jeu "disque") */
MULTI_INLINE int multi_after(multi_t *m, uint32_t a, uint32_t now) {
    uint32_t pause = now - m->t_end;
    m->t_end = now;
    if (a == FOOT_ADDR) {
        if (m->state == ST_LAUNCH && g_ready) {              /* redemarrage : le jeu */
            multi_to_game(m);
            return 1;
        } else if (m->state == ST_GAME && m->n_game >= ARM_N && pause > RESET_US) {
            m->game[FOOT_IDX] = m->game_foot;               /* reset : le menu */
            m->img = m->menu; m->state = ST_MENU;
            g_mstate = ST_MENU;
        } else if (pause > RESET_US) {
            g_boots++;                                      /* demarrage de la console (Exelmemoire) */
        }
        return 0;
    }
    if (m->state == ST_GAME) {
        if (m->n_game < ARM_N) { if (++m->n_game == ARM_N) m->game[FOOT_IDX] = m->menu_foot; }
        else if (a == m->warm && pause > RESET_US) {        /* reset de l'EXELTEL en reprise a chaud : le menu */
            m->game[FOOT_IDX] = m->game_foot;
            m->img = m->menu; m->state = ST_MENU;
            g_mstate = ST_MENU;
        }
        return 0;
    }
    if (m->state == ST_LAUNCH) {
        if (a == GO_ADDR && g_ready) { multi_to_game(m); return 1; }   /* le loader saute au jeu */
        return 0;
    }
    /* (ici : etat MENU) sauvegarde de l'Exelmemoire : $6C00+v range v ; $6BF0 debut, $6BF1 fin
     * (code compile verifie : la boucle de service est restee la meme) */
    if ((a & 0x7F00u) == SAVE_WIN_A) {
        const uint32_t n = g_sv_n;
        SAVE_BUF[n & (SAVE_BUF_SIZE - 1u)] = (uint8_t)a;
        g_sv_n = n + 1u;
        return 0;
    }
    if (a - SAVE_GO_A < 2u) {
        if (a == SAVE_GO_A) { g_sv_n = 0; m->menu[SAVE_STAT_IDX] = ENC(0); }
        else g_sv_done = g_sv_n + 1u;
        return 0;
    }
    if (m->state == ST_MENU && (a & 0xFFu) < m->count) {
        if ((a & 0x7F00u) == SEL_BASE) {
            g_ready = 0;
            g_req = (int32_t)(a & 0xFFu);
            m->state = ST_LAUNCH;
        } else if ((a & 0x7F00u) == CRAM_BASE_A) {      /* image Exelmemoire : on reste au menu */
            m->menu[CRAM_STAT_IDX] = ENC(0);
            g_creq = (int32_t)(a & 0xFFu);
        }
    } else if (m->state == ST_MENU && a - NOCART_BACK < 3u) {
        g_hot = a;                                       /* $6DFC-$6DFE : le coeur 0 s'en occupe */
    }
    return 0;
}
/* coeur 0 (menu v4.1) : apres le chargement d'une image Exelmemoire.
 *   $6DFE : l'image demarre -> infrarouge jusqu'au prochain demarrage de la console ;
 *   $6DFD : pied du menu a $FF ; au demarrage suivant (sans cartouche), ~1 s apres,
 *           le pied revient ; infrarouge jusqu'au demarrage d'apres ; $6DFC : annule. */
typedef struct { uint32_t boots0, t; } cram_st_t;
MULTI_INLINE void cram_step(cell_t *menu, cell_t menu_foot, uint32_t now, cram_st_t *c) {
    const uint32_t h = g_hot;
    if (h) {
        g_hot = 0;
        if (h == CRAM_GO_A) { g_cram_ir = CRAM_IR_RUN; c->boots0 = g_boots; }
        else if (h == NOCART_A) {
            menu[FOOT_IDX] = ENC(0xFF);
            g_cram_ir = CRAM_IR_NOCART; c->boots0 = g_boots; c->t = 0;
        } else if (h == NOCART_BACK && g_cram_ir == CRAM_IR_NOCART && g_boots == c->boots0) {
            menu[FOOT_IDX] = menu_foot; g_cram_ir = 0;
        }
    }
    if (!g_cram_ir) return;
    const uint32_t nb = g_boots - c->boots0;
    if (g_cram_ir == CRAM_IR_RUN) { if (nb) g_cram_ir = 0; return; }     /* reset : le menu */
    if (!nb) return;                                                     /* NOCART : pas encore de reset */
    if (menu[FOOT_IDX] != menu_foot) {                                   /* a demarre sans cartouche */
        if (!c->t) c->t = now | 1u;
        else if (now - c->t > NOCART_US) { menu[FOOT_IDX] = menu_foot; c->boots0 = g_boots; }
    } else g_cram_ir = 0;                                                /* reset suivant : le menu */
}
#endif
