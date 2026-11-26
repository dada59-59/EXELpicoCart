/*
 * exlir.h -- la manette DB9 de la cartouche, envoyee a la console PAR INFRAROUGE,
 * comme la vraie manette Exelvision : n'importe quel jeu du commerce (non modifie)
 * la voit donc comme la manette d'origine.
 *
 * Le clavier et la manette 0 de l'EXL100 emettent avec un MC14497 : porteuse
 * 455 kHz / 16 = 28,4 kHz, et pour chaque code (6 bits) une trame de 20 "cases" de
 * 16 periodes de porteuse (~565 us) :
 *   case 0      : porteuse (reglage de gain du recepteur)
 *   cases 1-5   : silence
 *   cases 6-7   : bit de depart "1"
 *   cases 8-19  : les 6 bits du code, poids faible d'abord ; en biphase :
 *                 1 = porteuse puis silence, 0 = silence puis porteuse.
 * Tant que la touche est tenue, la trame est repetee (90 ms apres la fin de la
 * precedente) ; au relachement, le code 62 (fin de transmission) est envoye.
 * (Memes durees que le modele de MAME et le montage PS/2 -> IR d'aotta, qui
 * marche avec une vraie console ; la table des codes est celle de la documentation
 * technique Exelvision, "mode non decode".)
 *
 * La manette 0 d'origine envoie des codes du clavier : haut 1, droite 2, bas 3,
 * gauche 4 ; diagonales 33 (haut-droite), 34 (bas-droite), 35 (bas-gauche),
 * 36 (haut-gauche) ; feu seul ou avec gauche/droite 6, feu + haut ou bas 7,
 * feu + haut-gauche ou bas-droite 38, feu + haut-droite ou bas-gauche 39.
 *
 * Materiel : une diode infrarouge 940 nm, commandee par un transistor NPN depuis
 * GP25 (sortie a 1 = diode allumee), placee devant la fenetre du recepteur de la
 * console. Ou, a quelques cm du recepteur, la diode en direct : GP25 -> 150 ohms ->
 * diode -> masse (GP25 est regle a 12 mA, le maximum du RP2040). La porteuse et les trames sont fabriquees par une machine PIO
 * (exlir.pio) : le processeur ne fait que pousser un mot de 32 bits par trame.
 *
 * La manette est COUPEE quand l'image servie reserve une zone d'echange
 * ("EXLJ"/"EXLP", exljoy.h) : le jeu lit alors la manette directement dans la
 * ROM, et un jeu ne doit pas la recevoir deux fois.
 *
 * Une manette USB (exlpad.h) s'ajoute a la DB9 ; ses boutons en plus envoient
 * des touches, comme le clavier.
 * Un clavier USB (exlkbd.h) passe par le meme emetteur : il a la priorite sur la
 * manette tant qu'une touche est tenue ou qu'une sequence (modificateur, fin,
 * touche) est en cours. Une sequence avec prefixe qui suit une touche encore tenue
 * commence par la fin de transmission (le prefixe ne s'applique qu'a une nouvelle
 * touche). Le clavier n'est jamais coupe par une zone d'echange
 * (elle ne porte pas de clavier).
 */
#ifndef EXLIR_H
#define EXLIR_H
#include <stdint.h>
#include <string.h>
#include "exljoy.h"
#include "exlkbd.h"
#include "exlpad.h"

#define IR_PIN          25                        /* sortie vers la diode (LED du Purple Pico) */
#define IR_PIO_HZ_X2    2047500u                  /* 2 x 28 437,5 x 36 : horloge PIO, x 2 */
#define IR_CELLS        20u
#define IR_FRAME_US     11400u                    /* 20 x 562,6 us = 11,25 ms, avec un peu de marge */
#define IR_REPEAT_US    90000u                    /* silence entre deux trames, touche tenue */
#define IR_GAP_US       8000u                     /* silence avant un autre code (ou la fin) : 8 ms, loin des 2,8 ms de l'AGC */
#define IR_EOT          62u                       /* "fin de transmission" : touche relachee */

/* le mot pousse a la machine PIO : bit k = case k (1 = porteuse) */
static inline uint32_t ir_cells(uint8_t code) {
    uint32_t w = 1u;                              /* case 0 : porteuse ; 1-5 : silence */
    w |= 1u << 6;                                 /* bit de depart : 1 */
    for (uint32_t b = 0, k = 8; b < 6; b++, k += 2)
        w |= (code >> b & 1u) ? 1u << k : 1u << (k + 1);
    return w;
}

/* code de la manette 0 d'origine pour un etat DB9 : kb_joy_code (exlkbd.h) */
static inline int ir_joy_code(uint8_t v) { return kb_joy_code(v); }

/* l'ordonnanceur des trames (sans materiel : essai sur PC possible) */
typedef struct {
    joy_filter_t f;
    int held;                                     /* code envoye en dernier, -1 : rien / fin */
    uint32_t t_free;                              /* fin (estimee) de la derniere trame */
    uint32_t t_next;                              /* prochain releve de la manette (1 ms) */
    uint8_t q[32], qh, qt;                        /* clavier : codes a envoyer une fois, dans l'ordre */
    int kb;                                       /* clavier : touche tenue, -1 : aucune */
    uint32_t kgen; uint8_t kdown;                 /* dernier etat vu de g_kbd */
} ir_sched_t;

static inline void ir_sched_init(ir_sched_t *s, uint32_t now) {
    memset(s, 0, sizeof *s);
    s->held = -1; s->t_free = now - IR_REPEAT_US; s->t_next = now; s->kb = -1;
}
static inline void ir_q(ir_sched_t *s, uint8_t c) {
    if ((uint8_t)(s->qt - s->qh) < sizeof s->q) s->q[s->qt++ & (sizeof s->q - 1u)] = c;   /* plein : perdu */
}
/* le clavier, a chaque releve : une nouvelle touche part en sequence (chaque prefixe
 * Ctl / Fct / Shift suivi de la fin de transmission, puis la touche, au moins une fois
 * meme deja relachee) ; un relachement ajoute la fin de transmission. Les touches
 * tapees vite se suivent dans l'ordre, sans se perdre. */
static inline void ir_kbd(ir_sched_t *s, const kbd_state_t *k) {
    if (k->gen != s->kgen) {
        s->kgen = k->gen;
        if (k->npre && s->kdown) ir_q(s, IR_EOT);     /* une touche tenue : relachee avant le prefixe */
        for (uint8_t i = 0; i < k->npre && i < 3; i++) { ir_q(s, k->pre[i]); ir_q(s, IR_EOT); }
        ir_q(s, k->code);
        s->kdown = 1;
    }
    if (s->kdown && !k->down) { ir_q(s, IR_EOT); s->kdown = 0; }
    s->kb = k->down ? (int)k->code : -1;
}
/* un releve (toutes les 1 ms) : v = etat brut de la DB9, on = infrarouge permis.
 * Renvoie le code a envoyer maintenant, ou -1. */
static inline int ir_sched(ir_sched_t *s, uint8_t v, int on, uint32_t now) {
    const uint8_t st = joy_filter(&s->f, v);
    const int32_t idle = (int32_t)(now - s->t_free);   /* temps depuis la fin de la trame */
    if (idle < 0) return -1;                           /* trame en cours */
    if (s->qh != s->qt) {                              /* sequence du clavier en cours */
        if (idle < (int32_t)IR_GAP_US) return -1;
        const int c = s->q[s->qh++ & (sizeof s->q - 1u)];
        s->held = (c == (int)IR_EOT) ? -1 : c; s->t_free = now + IR_FRAME_US;
        return c;
    }
    const int code = s->kb >= 0 ? s->kb : on ? ir_joy_code(st) : -1;   /* le clavier d'abord */
    if (code >= 0) {
        if (idle >= (int32_t)(code == s->held ? IR_REPEAT_US : IR_GAP_US)) {
            s->held = code; s->t_free = now + IR_FRAME_US;
            return code;
        }
        return -1;
    }
    if (s->held >= 0 && idle >= (int32_t)IR_GAP_US) {  /* relache : fin de transmission */
        s->held = -1; s->t_free = now + IR_FRAME_US;
        return IR_EOT;
    }
    return -1;
}

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/structs/sio.h"
#include "hardware/structs/timer.h"
#include "exlir.pio.h"

typedef struct { ir_sched_t s; PIO pio; uint sm; int ok; } ir_t;

/* machine PIO + broches de la manette ; a appeler apres le reglage de l'horloge */
static void __noinline ir_start(ir_t *ir) {
    joy_gpio_init();
    ir->pio = pio0;
    ir->ok = 0;
    if (!pio_can_add_program(ir->pio, &exlir_program)) return;
    const int sm = pio_claim_unused_sm(ir->pio, false);
    if (sm < 0) return;
    ir->sm = (uint)sm;
    const uint off = pio_add_program(ir->pio, &exlir_program);
    pio_sm_config c = exlir_program_get_default_config(off);
    sm_config_set_set_pins(&c, IR_PIN, 1);
    sm_config_set_out_shift(&c, true, false, 32);           /* case 0 d'abord, sans auto */
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    /* diviseur = horloge systeme / 1,02375 MHz, en 16.8 */
    const uint64_t d256 = ((uint64_t)clock_get_hz(clk_sys) * 512u + IR_PIO_HZ_X2 / 2u) / IR_PIO_HZ_X2;
    sm_config_set_clkdiv_int_frac(&c, (uint16_t)(d256 >> 8), (uint8_t)(d256 & 0xFFu));
    pio_sm_set_pins_with_mask(ir->pio, ir->sm, 0u, 1u << IR_PIN);
    pio_sm_set_consecutive_pindirs(ir->pio, ir->sm, IR_PIN, 1, true);
    pio_gpio_init(ir->pio, IR_PIN);
    gpio_set_drive_strength(IR_PIN, GPIO_DRIVE_STRENGTH_12MA);   /* diode en direct possible (sans transistor) */
    pio_sm_init(ir->pio, ir->sm, off, &c);
    pio_sm_set_enabled(ir->pio, ir->sm, true);
    ir_sched_init(&ir->s, timer_hw->timerawl);
    ir->s.kgen = g_kbd.gen;
    ir->ok = 1;
}
/* a appeler souvent (boucle du coeur 0, ou boucle du coeur 1) ; on = manette permise */
static inline void ir_poll(ir_t *ir, int on) {
    const uint32_t now = timer_hw->timerawl;
    if (!ir->ok || (int32_t)(now - ir->s.t_next) < 0) return;
    ir->s.t_next += 1000u;
    if ((int32_t)(now - ir->s.t_next) > 0) ir->s.t_next = now + 1000u;   /* retard : on recale */
    ir_kbd(&ir->s, &g_kbd);
    const int c = ir_sched(&ir->s, joy_from_pins(sio_hw->gpio_in) | g_pad.joy, on, now);   /* DB9 + manette USB */
    if (c >= 0) pio_sm_put(ir->pio, ir->sm, ir_cells((uint8_t)c));
}
#endif
#endif
