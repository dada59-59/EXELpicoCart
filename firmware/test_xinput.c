/* test sur PC des manettes XInput (exlxinput.h) : reconnaissance dans le descripteur de
 * configuration, decodage des rapports, paquets de mise en marche et accuses, et les
 * trames envoyees par l'emetteur (avec exlpad.h et exlir.h).
 *   gcc -O2 -Wall -o test_xinput test_xinput.c && ./test_xinput */
#include <stdio.h>
#include <string.h>
#include "exlir.h"
#include "exlpad.h"
#include "exlxinput.h"
kbd_state_t g_kbd;
pad_state_t g_pad;
static int fails = 0;
#define OK(c, ...) do { int ok_ = (c); printf("%s ", ok_ ? "OK   " : "ECHEC"); printf(__VA_ARGS__); printf("\n"); if (!ok_) fails++; } while (0)

/* Xbox 360 filaire (045E:028E) : 4 interfaces (manette, casque, ?, securite) */
static const uint8_t C_360[] = {
    0x09,0x02,0x99,0x00,0x04,0x01,0x00,0xA0,0xFA,
    0x09,0x04,0x00,0x00,0x02,0xFF,0x5D,0x01,0x00,
    0x11,0x21,0x00,0x01,0x01,0x25,0x81,0x14,0x00,0x00,0x00,0x00,0x13,0x01,0x08,0x00,0x00,
    0x07,0x05,0x81,0x03,0x20,0x00,0x04,
    0x07,0x05,0x01,0x03,0x20,0x00,0x08,
    0x09,0x04,0x01,0x00,0x04,0xFF,0x5D,0x03,0x00,
    0x1B,0x21,0x00,0x01,0x01,0x01,0x82,0x40,0x01,0x02,0x20,0x16,0x83,0x00,0x00,0x00,0x00,0x00,0x00,0x16,0x03,0x00,0x00,0x00,0x00,0x00,0x00,
    0x07,0x05,0x82,0x03,0x20,0x00,0x02,
    0x07,0x05,0x02,0x03,0x20,0x00,0x04,
    0x07,0x05,0x83,0x03,0x20,0x00,0x40,
    0x07,0x05,0x03,0x03,0x20,0x00,0x10,
    0x09,0x04,0x02,0x00,0x01,0xFF,0x5D,0x02,0x00,
    0x09,0x21,0x00,0x01,0x01,0x22,0x84,0x07,0x00,
    0x07,0x05,0x84,0x03,0x20,0x00,0x10,
    0x09,0x04,0x03,0x00,0x00,0xFF,0xFD,0x13,0x04,
    0x06,0x41,0x00,0x01,0x01,0x03 };
/* Xbox One S (045E:02EA) : manette, audio (2 reglages), et une troisieme interface */
static const uint8_t C_ONE[] = {
    0x09,0x02,0x77,0x00,0x03,0x01,0x00,0xA0,0xFA,
    0x09,0x04,0x00,0x00,0x02,0xFF,0x47,0xD0,0x00,
    0x07,0x05,0x02,0x03,0x40,0x00,0x04,
    0x07,0x05,0x82,0x03,0x40,0x00,0x04,
    0x09,0x04,0x01,0x00,0x00,0xFF,0x47,0xD0,0x00,
    0x09,0x04,0x01,0x01,0x02,0xFF,0x47,0xD0,0x00,
    0x07,0x05,0x03,0x01,0xE4,0x00,0x01,
    0x07,0x05,0x83,0x01,0x40,0x00,0x01,
    0x09,0x04,0x02,0x00,0x00,0xFF,0x47,0xD0,0x00,
    0x09,0x04,0x02,0x01,0x02,0xFF,0x47,0xD0,0x00,
    0x07,0x05,0x04,0x02,0x40,0x00,0x00,
    0x07,0x05,0x84,0x02,0x40,0x00,0x00 };
/* recepteur sans fil Xbox 360 (045E:0719), debut : manette 1, casque 1, manette 2 */
static const uint8_t C_360W[] = {
    0x09,0x02,0xD9,0x00,0x08,0x01,0x00,0xA0,0xFA,
    0x09,0x04,0x00,0x00,0x02,0xFF,0x5D,0x81,0x00,
    0x14,0x22,0x00,0x01,0x13,0x81,0x1D,0x00,0x17,0x01,0x02,0x08,0x13,0x01,0x0C,0x00,0x0C,0x01,0x02,0x08,
    0x07,0x05,0x81,0x03,0x20,0x00,0x01,
    0x07,0x05,0x01,0x03,0x20,0x00,0x08,
    0x09,0x04,0x01,0x00,0x04,0xFF,0x5D,0x82,0x00,
    0x07,0x05,0x82,0x03,0x20,0x00,0x02,
    0x09,0x04,0x02,0x00,0x02,0xFF,0x5D,0x81,0x00,
    0x07,0x05,0x83,0x03,0x20,0x00,0x01 };
/* manette HID ordinaire (une interface HID) : pas XInput */
static const uint8_t C_HID[] = {
    0x09,0x02,0x22,0x00,0x01,0x01,0x00,0x80,0x32,
    0x09,0x04,0x00,0x00,0x01,0x03,0x00,0x00,0x00,
    0x09,0x21,0x10,0x01,0x00,0x01,0x22,0x65,0x00,
    0x07,0x05,0x81,0x03,0x08,0x00,0x0A };

static void st(const char *what, xi_t *x, const uint8_t *r, uint32_t n, int wret, uint8_t wv, uint16_t wk) {
    uint8_t out[16], on;
    const int ret = xi_input(x, r, n, out, &on);
    const int ok = ret == wret && (!ret || (x->v == wv && x->keys == wk));
    OK(ok, "%-40s -> %s %02X touches %03X%s", what, ret ? "pris" : "ignore", x->v, x->keys, ok ? "" : "  (attendu autre chose)");
}

int main(void) {
    xi_found_t f;
    puts("-- reconnaissance (descripteur de configuration)");
    OK(xi_find(C_360, sizeof C_360, &f) == XI_360 && f.itf == 0 && f.ep_in == 0x81 && f.ep_out == 0x01 && f.in_size == 32,
       "Xbox 360 filaire : interface 0, IN 81, OUT 01");
    OK(xi_find(C_ONE, sizeof C_ONE, &f) == XI_ONE && f.itf == 0 && f.ep_in == 0x82 && f.ep_out == 0x02 && f.in_size == 64,
       "Xbox One : interface 0, IN 82, OUT 02 (pas les points d'acces audio)");
    OK(C_ONE[f.in_off + 1] == 5 && C_ONE[f.in_off + 2] == 0x82 && C_ONE[f.out_off + 2] == 0x02, "Xbox One : descripteurs des points d'acces retrouves");
    OK(xi_find(C_360W, sizeof C_360W, &f) == XI_360W && f.itf == 0 && f.ep_in == 0x81 && f.ep_out == 0x01,
       "recepteur sans fil 360 : manette 1 (interface 0)");
    OK(xi_find(C_HID, sizeof C_HID, &f) == XI_NONE, "manette HID : pas XInput");
    OK(xi_find(C_ONE, 30, &f) == XI_NONE, "descripteur tronque (sans IN) : rien");

    puts("-- Xbox 360 filaire : rapports de 20 octets");
    { xi_t x = { .kind = XI_360 }; uint8_t r[20] = { 0x00, 0x14 };
      st("repos", &x, r, 20, 1, 0x00, 0);
      r[2] = 0x01; st("croix haut", &x, r, 20, 1, 0x01, 0);
      r[2] = 0x0A; st("croix bas-droite", &x, r, 20, 1, 0x0A, 0);
      r[2] = 0; r[3] = 0x10; st("A : feu", &x, r, 20, 1, 0x10, 0);
      r[3] = 0x80; st("Y : feu", &x, r, 20, 1, 0x10, 0);
      r[3] = 0; r[6] = 0x00; r[7] = 0x80; st("stick gauche a gauche (X = -32768)", &x, r, 20, 1, 0x04, 0);
      r[6] = 0; r[7] = 0; r[8] = 0xFF; r[9] = 0x7F; st("stick gauche en haut (Y = +32767)", &x, r, 20, 1, 0x01, 0);
      r[8] = 0x00; r[9] = 0x30; st("stick un peu en haut (Y = 12288) : rien", &x, r, 20, 1, 0x00, 0);
      r[8] = 0; r[9] = 0; r[10] = 0; r[11] = 0x80; st("stick droit : ignore", &x, r, 20, 1, 0x00, 0);
      r[11] = 0; r[3] = 0x01; st("LB", &x, r, 20, 1, 0x00, 0x001);
      r[3] = 0; r[5] = 200; st("RT enfoncee", &x, r, 20, 1, 0x00, 0x008);
      r[5] = 0; r[2] = 0x10; st("Start", &x, r, 20, 1, 0x00, 0x020);
      r[2] = 0x20; st("Back", &x, r, 20, 1, 0x00, 0x010);
      r[2] = 0; r[3] = 0x04; st("bouton Xbox", &x, r, 20, 1, 0x00, 0x100);
      const uint8_t led[3] = { 0x01, 0x03, 0x06 }; st("etat du voyant : ignore", &x, led, 3, 0, 0, 0); }

    puts("-- Xbox One / Series : paquets GIP");
    { xi_t x = { .kind = XI_ONE }; uint8_t r[18] = { 0x20, 0x00, 0x01, 0x0E }, out[16], on;
      st("repos", &x, r, 18, 1, 0x00, 0);
      r[5] = 0x04; st("croix gauche", &x, r, 18, 1, 0x04, 0);
      r[5] = 0x05; st("croix haut-gauche", &x, r, 18, 1, 0x05, 0);
      r[5] = 0; r[4] = 0x20; st("B : feu", &x, r, 18, 1, 0x10, 0);
      r[4] = 0x04; st("Menu", &x, r, 18, 1, 0x00, 0x020);
      r[4] = 0x08; st("View", &x, r, 18, 1, 0x00, 0x010);
      r[4] = 0; r[5] = 0x20; st("RB", &x, r, 18, 1, 0x00, 0x002);
      r[5] = 0; r[6] = 0xFF; r[7] = 0x03; st("LT a fond (1023)", &x, r, 18, 1, 0x00, 0x004);
      r[6] = 0; r[7] = 0; r[10] = 0xFF; r[11] = 0x7F; st("stick a droite", &x, r, 18, 1, 0x08, 0);
      r[10] = 0; r[11] = 0; r[12] = 0x00; r[13] = 0x80; st("stick en bas (Y = -32768)", &x, r, 18, 1, 0x02, 0);
      r[12] = 0; r[13] = 0;
      const uint8_t g1[6] = { 0x07, 0x30, 0x05, 0x02, 0x01, 0x5B };
      int ret = xi_input(&x, g1, 6, out, &on);
      OK(ret && x.keys == 0x100 && on == 13 && out[0] == 0x01 && out[2] == 0x05 && out[5] == 0x07,
         "bouton Xbox (paquet 07, accuse demande) -> touche 7, accuse au numero 5");
      st("rapport suivant, bouton Xbox tenu", &x, r, 18, 1, 0x00, 0x100);
      const uint8_t g0[6] = { 0x07, 0x20, 0x06, 0x02, 0x00, 0x5B };
      ret = xi_input(&x, g0, 6, out, &on);
      OK(ret && x.keys == 0 && on == 0, "bouton Xbox relache (sans accuse)");
      const uint8_t hb[6] = { 0x03, 0x20, 0x07, 0x04, 0x80, 0x00 };
      st("battement de coeur (03) : ignore", &x, hb, 6, 0, 0, 0); }

    puts("-- recepteur sans fil 360");
    { xi_t x = { .kind = XI_360W }; uint8_t out[16], on;
      const uint8_t conn[2] = { 0x08, 0x80 };
      int ret = xi_input(&x, conn, 2, out, &on);
      OK(!ret && x.present && on == 12 && out[2] == 0x08 && out[3] == 0x46, "manette jumelee : voyant 1 envoye");
      uint8_t r[29] = { 0x00, 0x01, 0x00, 0xF0, 0x00, 0x13 };
      r[6] = 0x02; st("croix bas", &x, r, 29, 1, 0x02, 0);
      r[6] = 0; r[7] = 0x40; st("X : feu", &x, r, 29, 1, 0x10, 0);
      const uint8_t gone[2] = { 0x08, 0x00 };
      st("manette perdue : tout relache", &x, gone, 2, 1, 0x00, 0);
      ret = xi_input(&x, conn, 2, out, &on);
      OK(on == 12, "rejumelee : voyant renvoye"); }

    puts("-- paquets de mise en marche");
    { xi_pkt_t p[8]; int n;
      n = xi_init(XI_360, 0x045E, 0x028E, p, 8);
      OK(n == 1 && p[0].n == 3 && p[0].d[0] == 0x01, "360 filaire : le voyant 1");
      n = xi_init(XI_ONE, 0x045E, 0x02D1, p, 8);
      OK(n == 1 && p[0].d[0] == 0x05 && p[0].d[4] == 0x00, "Xbox One d'origine : mise en marche");
      n = xi_init(XI_ONE, 0x045E, 0x0B12, p, 8);
      OK(n == 2 && p[1].d[3] == 0x0F, "Xbox Series : mise en marche + init One S");
      n = xi_init(XI_ONE, 0x045E, 0x0B00, p, 8);
      OK(n == 3 && p[2].d[0] == 0x4D, "Elite 2 : + paquet d'entrees en plus");
      n = xi_init(XI_ONE, 0x0E6F, 0x02A4, p, 8);
      OK(n == 3 && p[1].d[0] == 0x0A && p[2].d[0] == 0x06, "PDP : + voyant, + authentification");
      n = xi_init(XI_ONE, 0x24C6, 0x541A, p, 8);
      OK(n == 3 && p[1].d[10] == 0xFF && p[2].d[10] == 0x00, "PowerA : + vibration marche / arret");
      n = xi_init(XI_360W, 0x045E, 0x0719, p, 8);
      OK(n == 0, "recepteur sans fil : rien (voyant au jumelage)"); }

    puts("-- emetteur : Xbox One, croix, A, Menu (codes des trames)");
    {
        memset(&g_kbd, 0, sizeof g_kbd); memset(&g_pad, 0, sizeof g_pad); pad_reset(&g_pad, &g_kbd);
        xi_t x = { .kind = XI_ONE };
        ir_sched_t s; ir_sched_init(&s, 0);
        uint8_t r[18] = { 0x20, 0x00, 0x01, 0x0E }, out[16], on;
        int code[64], n = 0;
        for (uint32_t t = 0; t < 900; t++) {
            int ch = 1;
            if (t == 10) r[5] = 0x08;             /* droite */
            else if (t == 60) r[5] = 0x00;
            else if (t == 200) r[4] = 0x10;       /* A */
            else if (t == 240) r[4] = 0x00;
            else if (t == 400) r[4] = 0x04;       /* Menu */
            else if (t == 430) r[4] = 0x00;
            else ch = 0;
            if (ch && xi_input(&x, r, 18, out, &on)) pad_apply(&g_pad, &g_kbd, x.v, x.keys);
            ir_kbd(&s, &g_kbd);
            const int c = ir_sched(&s, g_pad.joy, 1, t * 1000u);
            if (c >= 0 && n < 64) code[n++] = c;
        }
        char got[256] = "";
        for (int i = 0; i < n; i++) sprintf(got + strlen(got), "%s%d", i ? "," : "", code[i]);
        const char *want = "2,62,6,62,59,62";
        OK(strcmp(got, want) == 0, "droite, A, Menu : %s%s%s", got, strcmp(got, want) ? "  attendu " : "", strcmp(got, want) ? want : "");
    }
    printf(fails ? "\n%d ECHEC(S)\n" : "\ntout est bon\n", fails);
    return fails != 0;
}
