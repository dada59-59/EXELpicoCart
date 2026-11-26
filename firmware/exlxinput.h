/*
 * exlxinput.h -- manettes Xbox 360, Xbox One, Xbox Series et compatibles "XInput", sur
 * le port USB du Pico, en hote. Ce ne sont pas des manettes HID : elles ont une
 * interface "du fabricant" (classe 0xFF) et un format de rapport fixe, que voici.
 *
 * Reconnaissance (seule, d'apres l'interface ; aucun numero de modele n'est requis) :
 *   classe 0xFF, sous-classe 0x5D, protocole 0x01 : Xbox 360 filaire et compatibles
 *       (la plupart des manettes "XInput" : 8BitDo en mode X, PowerA, Logitech F310...)
 *   classe 0xFF, sous-classe 0x5D, protocole 0x81 : recepteur sans fil Xbox 360
 *       (la premiere manette jumelee)
 *   classe 0xFF, sous-classe 0x47, protocole 0xD0 : Xbox One, Xbox Series et
 *       compatibles (protocole "GIP" ; il faut les mettre en marche, voir xi_init)
 *
 * Correspondance, la meme que pour une manette HID (exlpad.h) :
 *   croix ou stick gauche -> directions ; A B X Y -> feu ;
 *   LB RB LT RT -> touches 1 2 3 4 ; Back / View -> ESPACE ; Start / Menu -> ENTREE ;
 *   clic du stick gauche, du stick droit, bouton Xbox -> touches 5 6 7.
 * (Formats et paquets de mise en marche : ceux du pilote xpad de Linux.)
 */
#ifndef EXLXINPUT_H
#define EXLXINPUT_H
#include <stdint.h>
#include <string.h>

enum { XI_NONE = 0, XI_360 = 1, XI_360W = 2, XI_ONE = 3 };

static inline int xi_kind(uint8_t cls, uint8_t sub, uint8_t proto) {
    if (cls != 0xFF) return XI_NONE;
    if (sub == 0x5D && proto == 0x01) return XI_360;
    if (sub == 0x5D && proto == 0x81) return XI_360W;
    if (sub == 0x47 && proto == 0xD0) return XI_ONE;
    return XI_NONE;
}

/* ce que l'on garde du descripteur de configuration */
typedef struct {
    uint8_t kind, itf;
    uint16_t in_off, out_off;                     /* descripteurs des deux points d'acces, 0 : absent */
    uint8_t ep_in, ep_out;
    uint16_t in_size, out_size;
} xi_found_t;

/* le descripteur de configuration -> la premiere interface XInput et ses points
 * d'acces "interruption" ; renvoie son type (XI_NONE : pas de manette XInput) */
static inline int xi_find(const uint8_t *d, uint32_t len, xi_found_t *f) {
    memset(f, 0, sizeof *f);
    int cur = 0;                                  /* dans l'interface retenue */
    for (uint32_t i = 0; i + 2 <= len;) {
        const uint8_t bl = d[i], ty = d[i + 1];
        if (bl < 2 || i + bl > len) break;
        if (ty == 4 && bl >= 9) {                 /* interface */
            if (f->kind) { if (f->ep_in) break; }      /* la suivante : on a ce qu'il faut */
            const int k = d[i + 3] == 0 ? xi_kind(d[i + 5], d[i + 6], d[i + 7]) : XI_NONE;
            cur = 0;
            if (k && !f->kind) { f->kind = (uint8_t)k; f->itf = d[i + 2]; cur = 1; }
        } else if (ty == 5 && bl >= 7 && cur && (d[i + 3] & 3u) == 3u) {   /* point d'acces "interruption" */
            const uint8_t a = d[i + 2];
            const uint16_t sz = (uint16_t)(d[i + 4] | (d[i + 5] << 8)) & 0x7FFu;
            if ((a & 0x80u) && !f->ep_in) { f->ep_in = a; f->in_off = (uint16_t)i; f->in_size = sz; }
            else if (!(a & 0x80u) && !f->ep_out) { f->ep_out = a; f->out_off = (uint16_t)i; f->out_size = sz; }
        }
        i += bl;
    }
    if (f->kind && !f->ep_in) f->kind = XI_NONE;
    return f->kind;
}

/* l'etat d'une manette XInput entre deux rapports */
typedef struct {
    uint8_t kind, v, guide, seq, present;
    uint16_t keys;
} xi_t;

static inline int16_t xi_s16(const uint8_t *p) { return (int16_t)(p[0] | (p[1] << 8)); }
static inline uint8_t xi_stick(int16_t x, int16_t y) {   /* y : vers le haut positif */
    uint8_t v = 0;
    if (x <= -16384) v |= 0x04;
    if (x >= 16384) v |= 0x08;
    if (y >= 16384) v |= 0x01;
    if (y <= -16384) v |= 0x02;
    return v;
}
/* bits de "keys" : 0 LB, 1 RB, 2 LT, 3 RT, 4 Back, 5 Start, 6 clic gauche, 7 clic droit,
 * 8 bouton Xbox (memes places que les boutons 5..13 d'une manette HID) */
static inline void xi_360_state(xi_t *x, const uint8_t *b) {   /* b : rapport 360 (b[0] = 0, b[1] = 0x14) */
    const uint8_t b2 = b[2], b3 = b[3];
    uint8_t v = (uint8_t)(b2 & 0x0Fu);            /* croix : haut bas gauche droite = format DB9 */
    if (b3 & 0xF0u) v |= 0x10;                    /* A B X Y */
    v |= xi_stick(xi_s16(b + 6), xi_s16(b + 8));
    uint16_t k = 0;
    if (b3 & 0x01u) k |= 1u << 0;
    if (b3 & 0x02u) k |= 1u << 1;
    if (b[4] >= 128) k |= 1u << 2;
    if (b[5] >= 128) k |= 1u << 3;
    if (b2 & 0x20u) k |= 1u << 4;
    if (b2 & 0x10u) k |= 1u << 5;
    if (b2 & 0x40u) k |= 1u << 6;
    if (b2 & 0x80u) k |= 1u << 7;
    if (b3 & 0x04u) k |= 1u << 8;
    x->v = v; x->keys = k;
}
static inline void xi_one_state(xi_t *x, const uint8_t *b) {   /* b : paquet GIP 0x20 */
    const uint8_t b4 = b[4], b5 = b[5];
    uint8_t v = (uint8_t)(b5 & 0x0Fu);            /* croix */
    if (b4 & 0xF0u) v |= 0x10;                    /* A B X Y */
    v |= xi_stick(xi_s16(b + 10), xi_s16(b + 12));
    uint16_t k = 0;
    if (b5 & 0x10u) k |= 1u << 0;
    if (b5 & 0x20u) k |= 1u << 1;
    if ((b[6] | (b[7] << 8)) >= 512) k |= 1u << 2;     /* gachettes : 0..1023 */
    if ((b[8] | (b[9] << 8)) >= 512) k |= 1u << 3;
    if (b4 & 0x08u) k |= 1u << 4;                 /* View */
    if (b4 & 0x04u) k |= 1u << 5;                 /* Menu */
    if (b5 & 0x40u) k |= 1u << 6;
    if (b5 & 0x80u) k |= 1u << 7;
    if (x->guide) k |= 1u << 8;
    x->v = v; x->keys = k;
}

/* un paquet recu -> 1 si l'etat (x->v, x->keys) est a reprendre ; *out / *outn : un paquet
 * a renvoyer a la manette (accuse de reception GIP, ou voyant du sans fil), 0 sinon */
static inline int xi_input(xi_t *x, const uint8_t *r, uint32_t n, uint8_t *out, uint8_t *outn) {
    *outn = 0;
    switch (x->kind) {
    case XI_360:
        if (n >= 14 && r[0] == 0x00 && r[1] >= 0x0E) { xi_360_state(x, r); return 1; }
        return 0;
    case XI_360W:
        if (n >= 2 && (r[0] & 0x08u)) {           /* jumelage / perte de la manette */
            const uint8_t p = (r[1] & 0x80u) != 0;
            if (p && !x->present) {               /* voyant 1 allume */
                static const uint8_t led[12] = { 0x00, 0x00, 0x08, 0x46 };
                memcpy(out, led, sizeof led); *outn = sizeof led;
            }
            x->present = p;
            if (!p) { x->v = 0; x->keys = 0; return 1; }
            return 0;
        }
        if (n >= 18 && r[1] == 0x01) { x->present = 1; xi_360_state(x, r + 4); return 1; }
        return 0;
    case XI_ONE:
        if (n >= 18 && r[0] == 0x20) { xi_one_state(x, r); return 1; }
        if (n >= 5 && r[0] == 0x07) {             /* bouton Xbox, a part */
            if (r[1] == 0x30) {                   /* accuse de reception demande */
                static const uint8_t ack[13] = { 0x01, 0x20, 0x00, 0x09, 0x00, 0x07, 0x20, 0x02 };
                memcpy(out, ack, sizeof ack); out[2] = r[2]; *outn = sizeof ack;
            }
            x->guide = r[4] & 1u;
            x->keys = (uint16_t)((x->keys & ~(1u << 8)) | (x->guide ? 1u << 8 : 0));
            return 1;
        }
        return 0;
    }
    return 0;
}

/* les paquets de mise en marche, dans l'ordre (le numero de sequence, octet 2 des
 * paquets GIP, est mis a l'envoi) ; renvoie leur nombre */
typedef struct { const uint8_t *d; uint8_t n; } xi_pkt_t;
static inline int xi_init(int kind, uint16_t vid, uint16_t pid, xi_pkt_t *p, int max) {
    int n = 0;
#define XI_ADD(a) do { if (n < max) { p[n].d = a; p[n].n = (uint8_t)sizeof a; n++; } } while (0)
    if (kind == XI_360) {
        static const uint8_t led1[3] = { 0x01, 0x03, 0x06 };          /* voyant 1 allume */
        XI_ADD(led1);
    } else if (kind == XI_ONE) {
        static const uint8_t power_on[5] = { 0x05, 0x20, 0x00, 0x01, 0x00 };
        static const uint8_t s_init[5] = { 0x05, 0x20, 0x00, 0x0F, 0x06 };
        static const uint8_t extra_input[6] = { 0x4D, 0x10, 0x00, 0x02, 0x07, 0x00 };
        static const uint8_t pdp_led[7] = { 0x0A, 0x20, 0x00, 0x03, 0x00, 0x01, 0x14 };
        static const uint8_t pdp_auth[6] = { 0x06, 0x20, 0x00, 0x02, 0x01, 0x00 };
        static const uint8_t rumble_on[13] = { 0x09, 0x00, 0x00, 0x09, 0x00, 0x0F, 0x00, 0x00, 0x1D, 0x1D, 0xFF, 0x00, 0x00 };
        static const uint8_t rumble_off[13] = { 0x09, 0x00, 0x00, 0x09, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
        XI_ADD(power_on);                                                /* toutes */
        if (vid == 0x045E && (pid == 0x02EA || pid == 0x0B00 || pid == 0x0B12)) XI_ADD(s_init);   /* One S, Elite 2, Series */
        if (vid == 0x045E && pid == 0x0B00) XI_ADD(extra_input);       /* Elite 2 */
        if (vid == 0x0E6F) { XI_ADD(pdp_led); XI_ADD(pdp_auth); }      /* PDP */
        if (vid == 0x24C6) { XI_ADD(rumble_on); XI_ADD(rumble_off); }  /* PowerA */
    }
#undef XI_ADD
    return n;
}
#endif
