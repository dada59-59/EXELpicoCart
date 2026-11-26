/* test sur PC du clavier USB -> codes du clavier EXL100 (exlkbd.h), et des sequences
 * envoyees par l'emetteur (ordonnanceur d'exlir.h, temps simule a la milliseconde).
 *   gcc -O2 -Wall -o test_kbd test_kbd.c && ./test_kbd */
#include <stdio.h>
#include <string.h>
#include "exlir.h"
kbd_state_t g_kbd;
static int fails = 0;
#define OK(c, ...) do { int ok_ = (c); printf("%s ", ok_ ? "OK   " : "ECHEC"); printf(__VA_ARGS__); printf("\n"); if (!ok_) fails++; } while (0)

/* une touche : code attendu et prefixes attendus (0 a 3) */
static void key(const char *what, uint8_t u, uint8_t mods, int want, const char *wpre) {
    uint8_t pre[3], n = 0;
    const int c = kb_map(u, mods, pre, &n);
    char got[16] = "";
    for (int i = 0; i < n; i++) sprintf(got + strlen(got), "%s%d", i ? "," : "", pre[i]);
    OK(c == want && strcmp(got, wpre) == 0, "%-24s -> %3d [%s]%s", what, c, got,
       c == want && strcmp(got, wpre) == 0 ? "" : "  (attendu autre chose)");
}
/* l'ordonnanceur, minute par minute : rapports de clavier a des instants donnes */
typedef struct { uint32_t ms; uint8_t r[8]; } ev_t;
static int run(const ev_t *ev, int nev, uint32_t until, int *out, int max) {
    ir_sched_t s; ir_sched_init(&s, 0);
    memset(&g_kbd, 0, sizeof g_kbd);
    int n = 0, e = 0;
    for (uint32_t t = 0; t < until; t++) {
        while (e < nev && ev[e].ms == t) kb_report(&g_kbd, ev[e++].r);
        ir_kbd(&s, &g_kbd);
        const int c = ir_sched(&s, 0, 1, t * 1000u);
        if (c >= 0 && n < max) out[n++] = c;
    }
    return n;
}
static void seq(const char *what, const ev_t *ev, int nev, uint32_t until, const char *want) {
    int out[64]; const int n = run(ev, nev, until, out, 64);
    char got[256] = "";
    for (int i = 0; i < n; i++) sprintf(got + strlen(got), "%s%d", i ? "," : "", out[i]);
    OK(strcmp(got, want) == 0, "%-38s : %s%s%s", what, got, strcmp(got, want) ? "  attendu " : "", strcmp(got, want) ? want : "");
}

int main(void) {
    const uint8_t SH = 0x02, CT = 0x01, AL = 0x04, AG = 0x40;
    puts("-- lettres (AZERTY) : majuscule par defaut, Maj -> Shift EXL (minuscule)");
    key("a", 0x14, 0, 24, "");           key("Maj a", 0x14, SH, 24, "32");
    key("q (position A du QWERTY)", 0x04, 0, 56, "");
    key("m (position ; du QWERTY)", 0x33, 0, 37, "");
    key("w", 0x1D, 0, 48, "");           key("z", 0x1A, 0, 0, "");
    puts("-- chiffres et signes : le caractere tape sur le PC");
    key("& (touche 1)", 0x1E, 0, 20, "32");   key("1 (Maj 1)", 0x1E, SH, 20, "");
    key("e aigu (touche 2)", 0x1F, 0, 28, "32"); key("~ (AltGr 2)", 0x1F, AG, 51, "");
    key("\" (touche 3)", 0x20, 0, 29, "32");  key("# (AltGr 3)", 0x20, AG, 31, "");
    key("' (touche 4)", 0x21, 0, 52, "32");   key("( (touche 5)", 0x22, 0, 60, "32");
    key("[ (AltGr 5)", 0x22, AG, 35, "32");   key("- (touche 6)", 0x23, 0, 7, "32");
    key("6 (Maj 6)", 0x23, SH, 21, "");       key("e grave (touche 7)", 0x24, 0, 53, "32");
    key("_ (touche 8)", 0x25, 0, 7, "");      key("\\ (AltGr 8)", 0x25, AG, 54, "32");
    key("c cedille (touche 9)", 0x26, 0, 30, "32"); key("^ (AltGr 9)", 0x26, AG, 51, "32");
    key("a grave (touche 0)", 0x27, 0, 23, "32");   key("@ (AltGr 0)", 0x27, AG, 61, "32");
    key("0 (Maj 0)", 0x27, SH, 23, "");
    key(")", 0x2D, 0, 31, "32");             key("] (AltGr ))", 0x2D, AG, 43, "32");
    key("=", 0x2E, 0, 19, "32");             key("+ (Maj =)", 0x2E, SH, 19, "");
    key("^ (touche morte)", 0x2F, 0, 51, "32"); key("$", 0x30, 0, 21, "32");
    key("% (Maj u grave)", 0x34, SH, 61, ""); key("*", 0x31, 0, 54, "");
    key("<", 0x64, 0, 35, "");               key("> (Maj <)", 0x64, SH, 43, "");
    key(",", 0x10, 0, 14, "32");             key("? (Maj ,)", 0x10, SH, 14, "");
    key(";", 0x36, 0, 27, "32");             key(". (Maj ;)", 0x36, SH, 27, "");
    key(":", 0x37, 0, 11, "32");             key("/ (Maj :)", 0x37, SH, 11, "");
    key("!", 0x38, 0, 22, "32");
    key("pave 7", 0x5F, 0, 53, "");          key("pave +", 0x57, 0, 19, "");
    key("pave /", 0x54, 0, 11, "");          key("pave .", 0x63, 0, 27, "");
    puts("-- touches speciales, Ctrl, Alt, F1-F10");
    key("Entree", 0x28, 0, 59, "");          key("Retour arriere", 0x2A, 0, 25, "");
    key("Tab", 0x2B, 0, 16, "");             key("Echap", 0x29, 0, 12, "");
    key("espace", 0x2C, 0, 6, "");           key("fleche haut", 0x52, 0, 1, "");
    key("fleche droite", 0x4F, 0, 2, "");    key("fleche bas", 0x51, 0, 3, "");
    key("fleche gauche", 0x50, 0, 4, "");    key("Debut", 0x4A, 0, 17, "");
    key("Verr. Maj", 0x39, 0, 9, "");
    key("Ctrl c", 0x06, CT, 26, "8");        key("Ctrl Maj c", 0x06, CT | SH, 26, "8,32");
    key("Alt a", 0x14, AL, 24, "15");        key("Ctrl 1", 0x1E, CT, 20, "8");
    key("F1", 0x3A, 0, 20, "15");            key("F10", 0x43, 0, 23, "15");
    puts("-- absents du clavier EXL100");
    key("{ (AltGr 4)", 0x21, AG, -1, "");    key("u grave", 0x34, 0, -1, "");
    key("| (AltGr 6)", 0x23, AG, -1, "");    key("Inser", 0x49, 0, -1, "");

    puts("-- sequences envoyees (codes des trames, dans l'ordre)");
    { ev_t e[] = { {10, {0,0,0x14}}, {40, {0}} };
      seq("a : tape et relache", e, 2, 200, "24,62"); }
    { ev_t e[] = { {10, {0x02,0,0x14}}, {80, {0x02}} , {90, {0}} };
      seq("Maj a", e, 3, 300, "32,62,24,62"); }
    { ev_t e[] = { {10, {0,0,0x1E}}, {12, {0}} };
      seq("& : touche relachee aussitot", e, 2, 300, "32,62,20,62"); }
    { ev_t e[] = { {10, {0,0,0x14}}, {400, {0}} };
      seq("a tenu 390 ms : repete", e, 2, 600, "24,24,24,24,62"); }
    { ev_t e[] = { {10, {0x01,0,0x06}}, {30, {0}} };
      seq("Ctrl c", e, 2, 300, "8,62,26,62"); }
    { ev_t e[] = { {10, {0,0,0x14}}, {50, {0,0,0x14,0x05}}, {90, {0,0,0x14}}, {120, {0}} };
      seq("a, b, b relache, a encore tenu : a reprend", e, 4, 400, "24,46,24,62"); }
    { ev_t e[] = { {10, {0,0,0x14}}, {11, {0}}, {13, {0,0,0x05}}, {14, {0}} };
      seq("a et b tapes en 4 ms", e, 4, 400, "24,62,46,62"); }
    { ev_t e[] = { {10, {0,0,0x14}}, {11, {0}}, {12, {0,0,0x14}}, {13, {0}} };
      seq("a a tapes en 3 ms", e, 4, 400, "24,62,24,62"); }
    { ev_t e[] = { {10, {0,0,0x0F}}, {11, {0}}, {12, {0,0,0x12}}, {13, {0}}, {14, {0,0,0x0F}}, {15, {0}}, {16, {0,0,0x0E}}, {17, {0}} };
      seq("LOLK tapes en 8 ms", e, 8, 600, "58,62,33,62,58,62,42,62"); }
    { ev_t e[] = { {10, {0x02}}, {50, {0}} };
      seq("Maj seule : rien", e, 2, 200, ""); }
    puts("-- fleches et espace : une manette (codes de la vraie manette)");
    { ev_t e[] = { {10, {0,0,0x50}}, {50, {0,0,0x50,0x2C}}, {90, {0,0,0x50}}, {120, {0}} };
      seq("gauche, + espace (feu), espace relache", e, 4, 400, "4,6,4,62"); }
    { ev_t e[] = { {10, {0,0,0x51}}, {50, {0,0,0x51,0x2C}}, {90, {0,0,0x51}}, {120, {0}} };
      seq("bas, + espace : feu + bas ($5F)", e, 4, 400, "3,7,3,62"); }
    { ev_t e[] = { {10, {0,0,0x52}}, {30, {0,0,0x52,0x50}}, {60, {0}} };
      seq("haut + gauche : diagonale 36 ($54)", e, 3, 400, "1,36,62"); }
    { ev_t e[] = { {10, {0,0,0x4F,0x51}}, {40, {0}} };
      seq("droite + bas ensemble : 34 ($48)", e, 2, 400, "34,62"); }
    { ev_t e[] = { {10, {0,0,0x50,0x51}}, {40, {0,0,0x50,0x51,0x2C}}, {80, {0}} };
      seq("bas-gauche, + espace : 39 ($47)", e, 3, 400, "35,39,62"); }
    { ev_t e[] = { {10, {0,0,0x52}}, {40, {0,0,0x52,0x51}}, {80, {0,0,0x52}}, {110, {0}} };
      seq("haut, + bas : bas l'emporte, puis haut", e, 4, 400, "1,3,1,62"); }
    { ev_t e[] = { {10, {0,0,0x50}}, {40, {0,0,0x50,0x14}}, {80, {0,0,0x50}}, {110, {0}} };
      seq("gauche, + a : a, a relache : gauche", e, 4, 400, "4,24,4,62"); }
    { ev_t e[] = { {10, {0,0,0x14}}, {40, {0,0,0x14,0x52}}, {70, {0,0,0x14,0x52,0x50}}, {100, {0,0,0x14}}, {130, {0}} };
      seq("a, + haut, + gauche, fleches relachees", e, 5, 400, "24,1,36,24,62"); }
    { ev_t e[] = { {10, {0x01,0,0x52}}, {40, {0}} };
      seq("Ctrl haut : touche ordinaire (Ctl, fin, haut)", e, 2, 400, "8,62,1,62"); }
    { ev_t e[] = { {10, {0x02,0,0x14}}, {40, {0x02,0,0x14,0x05}}, {80, {0x02,0,0x14}}, {110, {0}} };
      seq("Maj a, Maj b, b relache : a reprend avec son Shift", e, 4, 500, "32,62,24,62,32,62,46,62,32,62,24,62"); }
    { ev_t e[] = { {10, {0,0,0x2C}}, {11, {0}}, {13, {0,0,0x2C}}, {14, {0}} };
      seq("espace tape deux fois en 4 ms", e, 4, 400, "6,62,6,62"); }
    printf(fails ? "\n%d ECHEC(S)\n" : "\ntout est bon\n", fails);
    return fails != 0;
}
