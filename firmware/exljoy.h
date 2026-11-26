/*
 * exljoy.h -- peripheriques branches sur EXLCART (mode ROM unique, avec ou sans
 * banques) : manette DB9 "Atari" et souris USB. Le jeu lit leur etat DANS LA ROM,
 * dans une "zone d'echange" qu'il reserve lui-meme, n'importe ou dans son code :
 *
 *   +0   manette 0 : bit0 haut, bit1 bas, bit2 gauche, bit3 droite, bit4 feu (1 = appuye)
 *   +1   manette 1 (non cablee : $00)
 *   +2   $A5 quand EXLCART gere la zone
 *   +3   $00
 *   +4   signature : "EXLJ" (manette seule, zone de 8 octets)
 *                 ou "EXLP" (manette + souris, zone de 16 octets) :
 *   +8   souris : compteur X (8 bits, tourne en boucle ; vers la droite = +)
 *   +9   souris : compteur Y (8 bits, tourne en boucle ; vers le bas = +)
 *   +10  souris : boutons (bit0 gauche, bit1 droit, bit2 milieu ; 1 = appuye)
 *   +11  souris : $A5 quand une souris est branchee
 *   +12..+15  reserves ($00)
 *
 * Dans la ROM, la zone est assemblee a 00 00 00 00 "EXLJ" (ou "EXLP" suivi de
 * 8 x 00) : sur une vraie EPROM, le jeu lit donc "rien d'appuye, pas de souris".
 * Au demarrage, le firmware cherche ce motif dans la ROM (dans $0000-$3FFF s'il y
 * a des banques, puisque la fenetre change d'une banque a l'autre). Sans motif,
 * il sert la ROM telle quelle : un jeu du commerce non modifie n'est jamais touche.
 *
 * Le jeu calcule le deplacement de la souris par difference avec la valeur lue
 * la fois precedente (sur 8 bits : -128..+127). Le Pico divise les deplacements
 * bruts par la "sensibilite" (reglage EXLPICO) en gardant le reste : rien ne se
 * perd et rien n'est compte deux fois, sans aucun acquittement de la console.
 *
 * Fonctionnement : le service du bus passe sur le coeur 1 (meme boucle, en RAM,
 * interruptions coupees, comme en bibliotheque). En mode bibliotheque, le menu et
 * le jeu charge ont chacun leur zone (cherchee au chargement), mises a jour ensemble. Le coeur 0 lit la manette toutes
 * les millisecondes (etat retenu apres 3 ms stables) et fait tourner la pile USB
 * hote (TinyUSB) pour la souris. Chaque octet est reecrit d'un seul coup dans
 * chaque image servie : la console lit toujours une valeur entiere.
 *
 * Manette : directions sur GP26-GP29 (les deux cartes), feu sur GP24 (carte
 * lineaire) ou GP17 (carte support EPROM). Contacts vers la masse, tirage interne
 * a 3,3 V : la manette ne recoit jamais de 5 V (broche 7 de la DB9 NON reliee).
 * Souris : port USB du Pico, adaptateur OTG ; elle est alimentee par VBUS (5 V de
 * la cartouche). Aucune broche n'est utilisee.
 */
#ifndef EXLJOY_H
#define EXLJOY_H
#include "exlpins.h"
#include "exlpad.h"                              /* manette USB, ajoutee a la DB9 */

#define IO_PRESENT       0xA5u
#define JOY_STABLE_MS    3u
#define MOUSE_DIV_DEF    16u                      /* sensibilite par defaut */
#define BANK_MAX_IO      16u                      /* images au plus (= BANK_MAX) */

#define JOY_PIN_UP       26                       /* DB9 broche 1 */
#define JOY_PIN_DOWN     27                       /* DB9 broche 2 */
#define JOY_PIN_LEFT     28                       /* DB9 broche 3 */
#define JOY_PIN_RIGHT    29                       /* DB9 broche 4 */
#ifdef EXL_PINOUT_EPROM
#define JOY_PIN_FIRE     17                       /* DB9 broche 6 (GP17 libre sur cette carte) */
#else
#define JOY_PIN_FIRE     24                       /* DB9 broche 6 (GP24 = aussi le bouton USR) */
#endif
static const uint8_t JOY_PINS[5] = { JOY_PIN_UP, JOY_PIN_DOWN, JOY_PIN_LEFT, JOY_PIN_RIGHT, JOY_PIN_FIRE };

/* zone d'echange : adresse de son premier octet, ou -1 ; *mouse = 1 pour "EXLP" */
static inline int32_t io_find(const uint8_t *rom8, uint32_t limit, int *mouse) {
    static const uint8_t z4[4] = { 0, 0, 0, 0 };
    for (uint32_t a = 0; a + 8 <= limit; a++) {
        const uint8_t *p = rom8 + a;
        if (p[4] != 'E' || p[5] != 'X' || p[6] != 'L' || memcmp(p, z4, 4) != 0) continue;
        if (p[7] == 'J') { *mouse = 0; return (int32_t)a; }
        if (p[7] == 'P' && a + 16 <= limit && memcmp(p + 8, z4, 4) == 0 && memcmp(p + 12, z4, 4) == 0) {
            *mouse = 1; return (int32_t)a;
        }
    }
    return -1;
}

/* octet manette d'apres l'etat des broches (contacts a la masse = appuye) */
static inline uint8_t joy_from_pins(uint32_t gpio) {
    uint8_t v = 0;
    for (int k = 0; k < 5; k++) if (!(gpio >> JOY_PINS[k] & 1u)) v |= (uint8_t)(1u << k);
    return v;
}

/* ecrit un octet dans les images servies (une case = un seul acces memoire) */
static inline void io_store(cell_t *const *imgs, uint32_t n, uint32_t addr, uint8_t v) {
    const uint32_t i = ADDR_TO_IDX(addr);
    const cell_t c = ENC(v);
    for (uint32_t k = 0; k < n; k++) ((volatile cell_t *)imgs[k])[i] = c;
}

/* filtre anti-rebond : renvoie l'etat valide (change apres JOY_STABLE_MS mesures egales) */
typedef struct { uint8_t cur, cand; uint8_t n; } joy_filter_t;
static inline uint8_t joy_filter(joy_filter_t *f, uint8_t raw) {
    if (raw == f->cur) { f->n = 0; return f->cur; }
    if (raw != f->cand) { f->cand = raw; f->n = 1; }
    else if (++f->n >= JOY_STABLE_MS) { f->cur = raw; f->n = 0; }
    return f->cur;
}

/* compteur de souris : total brut / sensibilite, arrondi vers le bas (le reste
 * est garde dans le total) ; les 8 bits de poids faible tournent en boucle */
static inline uint8_t mouse_counter(int32_t total, uint32_t div) {
    int32_t d = (int32_t)div, q = total / d;
    if ((total % d) != 0 && total < 0) q--;      /* division vers -infini */
    return (uint8_t)q;
}

/* etat de la souris, rempli par les rappels USB (exlmouse.h), lu par io_loop */
typedef struct { volatile int32_t x, y; volatile uint8_t btn, present; } mouse_state_t;
extern mouse_state_t g_mouse;                     /* defini dans exlmouse.c */

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "hardware/gpio.h"
#include "hardware/timer.h"
static inline void joy_gpio_init(void) {
    for (int k = 0; k < 5; k++) {
        gpio_init(JOY_PINS[k]); gpio_set_dir(JOY_PINS[k], GPIO_IN); gpio_pull_up(JOY_PINS[k]);
    }
}
#ifdef EXL_WITH_MOUSE
void mouse_usb_init(void);
void mouse_usb_task(void);
#endif
/* une image servie et sa zone d'echange (base = adresse du premier octet) */
typedef struct { cell_t *img; uint32_t base; uint8_t mouse, on; } io_target_t;
/* etat courant des peripheriques, tel qu'il doit apparaitre dans les zones */
typedef struct { joy_filter_t f; uint8_t v[12]; uint32_t t_next, div; int usb; } io_t;

static inline void io_put(io_target_t *t, uint32_t n, uint32_t off, uint8_t v) {
    const cell_t c = ENC(v);
    for (uint32_t k = 0; k < n; k++)
        if (t[k].on && (off < 8 || t[k].mouse))
            ((volatile cell_t *)t[k].img)[ADDR_TO_IDX(t[k].base + off)] = c;
}
/* ecrit tout l'etat courant dans une zone (nouvelle image : jeu qu'on vient de charger) */
static inline void io_fill(io_t *s, io_target_t *t) {
    for (uint32_t off = 0; off < 12; off++) if (off < 4 || off >= 8) io_put(t, 1, off, s->v[off]);
}
static inline void io_init(io_t *s, uint32_t div, int usb) {
    memset(s, 0, sizeof *s);
    s->v[2] = IO_PRESENT;
    s->div = div ? div : MOUSE_DIV_DEF;
    s->t_next = timer_hw->timerawl;
#ifdef EXL_WITH_MOUSE
    s->usb = usb;
    if (usb) mouse_usb_init();
#else
    (void)usb;
#endif
}
/* un tour : manette toutes les 1 ms, souris a chaque tour ; seuls les octets
 * qui changent sont reecrits */
static inline void io_step(io_t *s, io_target_t *t, uint32_t n) {
    if ((int32_t)(timer_hw->timerawl - s->t_next) >= 0) {
        s->t_next += 1000;
        uint8_t v = joy_filter(&s->f, joy_from_pins(sio_hw->gpio_in) | g_pad.joy);   /* DB9 + manette USB */
        if (v != s->v[0]) { s->v[0] = v; io_put(t, n, 0, v); }
    }
#ifdef EXL_WITH_MOUSE
    if (s->usb) {
        mouse_usb_task();
        uint8_t m[4] = { mouse_counter(g_mouse.x, s->div), mouse_counter(g_mouse.y, s->div),
                         (uint8_t)(g_mouse.btn & 7u), (uint8_t)(g_mouse.present ? IO_PRESENT : 0) };
        for (uint32_t k = 0; k < 4; k++)
            if (m[k] != s->v[8 + k]) { s->v[8 + k] = m[k]; io_put(t, n, 8 + k, m[k]); }
    }
#endif
}
/* mode ROM unique (coeur 0) : la meme zone dans chaque image servie ; ne revient pas */
static void __noinline io_loop(cell_t *const *imgs, uint32_t n, uint32_t base, int mouse, uint32_t div) {
    io_target_t t[BANK_MAX_IO];
    if (n > BANK_MAX_IO) n = BANK_MAX_IO;
    io_t s;
    io_init(&s, div, mouse);
    for (uint32_t k = 0; k < n; k++) { t[k].img = imgs[k]; t[k].base = base; t[k].mouse = (uint8_t)mouse; t[k].on = 1; io_fill(&s, &t[k]); }
    for (;;) io_step(&s, t, n);
}
#endif
#endif
