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
 */
#ifndef EXLMULTI_H
#define EXLMULTI_H
#include <stdint.h>

#define ST_MENU     0u
#define ST_LAUNCH   1u
#define ST_GAME     2u
#define FOOT_ADDR   0x7FFCu
#define SEL_BASE    0x6F00u          /* $6F00 + n : choix du jeu n */
#define GO_ADDR     0x6FFFu          /* "bascule maintenant" (jeu choisi et charge) */
#define RESET_US    2000u            /* pause sans acces qui signale un reset */
#define ARM_N       1000u            /* acces du jeu avant d'armer le retour au menu */

typedef struct {
    uint8_t *menu, *game;            /* les deux images de 32 Ko (en RAM) */
    const uint8_t *img;              /* image servie */
    uint32_t state, count, t_end, n_game;
    uint8_t menu_foot, game_foot;    /* octets de signature d'origine */
} multi_t;

/* partages entre les deux coeurs */
extern volatile int32_t  g_req;      /* jeu demande (coeur 1 -> coeur 0), -1 : aucun */
extern volatile uint32_t g_ready;    /* jeu charge, signature posee dans le pied du menu */

#ifndef MULTI_INLINE
#define MULTI_INLINE static inline __attribute__((always_inline))
#endif

/* bascule sur le jeu charge : l'acces suivant le lira */
MULTI_INLINE void multi_to_game(multi_t *m) {
    m->menu[FOOT_ADDR] = m->menu_foot;
    m->game_foot = m->game[FOOT_ADDR];
    m->img = m->game; m->state = ST_GAME; m->n_game = 0;
}
/* apres un acces a l'adresse a, au temps now (us) */
MULTI_INLINE void multi_after(multi_t *m, uint32_t a, uint32_t now) {
    uint32_t pause = now - m->t_end;
    m->t_end = now;
    if (a == FOOT_ADDR) {
        if (m->state == ST_LAUNCH && g_ready) {              /* redemarrage : le jeu */
            multi_to_game(m);
        } else if (m->state == ST_GAME && m->n_game >= ARM_N && pause > RESET_US) {
            m->game[FOOT_ADDR] = m->game_foot;               /* reset : le menu */
            m->img = m->menu; m->state = ST_MENU;
        }
        return;
    }
    if (m->state == ST_GAME) {
        if (m->n_game < ARM_N && ++m->n_game == ARM_N) m->game[FOOT_ADDR] = m->menu_foot;
        return;
    }
    if (m->state == ST_LAUNCH) {
        if (a == GO_ADDR && g_ready) multi_to_game(m);       /* le loader saute au jeu */
        return;
    }
    if (m->state == ST_MENU && (a & 0x7F00u) == SEL_BASE && (a & 0xFFu) < m->count) {
        g_ready = 0;
        g_req = (int32_t)(a & 0xFFu);
        m->state = ST_LAUNCH;
    }
}
#endif
