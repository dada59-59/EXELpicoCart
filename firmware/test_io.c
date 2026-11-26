/* test_io.c -- tests sur PC de la zone d'echange (exljoy.h) : recherche du motif,
 * compteur de souris (division vers -infini, rien de perdu, rien de compte deux fois).
 *   gcc -O2 -Wall -o test_io test_io.c && ./test_io
 *   gcc -O2 -Wall -DEXL_PINOUT_EPROM -o test_io_eprom test_io.c && ./test_io_eprom */
#include <stdio.h>
#include <stdlib.h>
#include "exljoy.h"
static int fails;
#define OK(c, m) do { int ok_ = (c); printf("%s %s\n", ok_ ? "OK   " : "ECHEC", m); if (!ok_) fails++; } while (0)
int main(void) {
    static uint8_t r[0x8000];
    static cell_t img[ROM_SIZE];
    int m = -1;
    pins_init();
    img_load(img, (const uint8_t *)"\x5A", 0x3FE0, 1);
    OK(cell_dec(img[ADDR_TO_IDX(0x3FE0)]) == 0x5A, "une case ecrite en $3FE0 se relit (rangement de la carte)");
    memset(r, 0xFF, sizeof r);
    OK(io_find(r, 0x8000, &m) < 0, "ROM sans zone : rien trouve");
    memcpy(r + 0x3FE0, "\0\0\0\0EXLJ", 8);
    OK(io_find(r, 0x8000, &m) == 0x3FE0 && m == 0, "EXLJ en $3FE4 (Wizord) : zone $3FE0, manette seule");
    memset(r, 0xFF, sizeof r);
    memcpy(r + 0x5123, "\0\0\0\0EXLP\0\0\0\0\0\0\0\0", 16);
    OK(io_find(r, 0x8000, &m) == 0x5123 && m == 1, "EXLP en $5127 : zone $5123, manette + souris");
    OK(io_find(r, 0x4000, &m) < 0, "avec des banques, seule la zone fixe ($0000-$3FFF) est cherchee");
    r[0x5123 + 13] = 7;
    OK(io_find(r, 0x8000, &m) < 0, "EXLP sans ses 8 octets a zero : refuse (pas assez de place reservee)");
    memset(r, 0xFF, sizeof r);
    memcpy(r + 0x100, "\1\0\0\0EXLJ", 8);
    OK(io_find(r, 0x8000, &m) < 0, "EXLJ sans 4 zeros devant : refuse");
    /* compteur : on simule des rapports aleatoires et un jeu qui lit de temps en temps */
    srand(1);
    int32_t total = 0, game = 0; uint8_t last = mouse_counter(0, 16); int bad = 0;
    for (int i = 0; i < 200000; i++) {
        total += (rand() % 255) - 127;                      /* un rapport USB : -127..+127 */
        if (rand() % 4 == 0) {                              /* le jeu lit */
            uint8_t c = mouse_counter(total, 16);
            game += (int8_t)(uint8_t)(c - last); last = c;
            int32_t q = total >= 0 ? total / 16 : -((-total + 15) / 16);
            if (game != q) bad++;
        }
    }
    OK(bad == 0, "deplacements cumules par le jeu = total / sensibilite, exact sur 200 000 rapports");
    OK(mouse_counter(-1, 16) == 0xFF && mouse_counter(-16, 16) == 0xFF && mouse_counter(-17, 16) == 0xFE
       && mouse_counter(15, 16) == 0 && mouse_counter(16, 16) == 1, "division vers -infini autour de zero");
    OK(mouse_counter(4096, 16) == 0 && mouse_counter(4096 + 16, 16) == 1, "le compteur tourne sur 8 bits");
    printf(fails ? "\n%d ECHEC(S)\n" : "\ntout est bon\n", fails);
    return fails != 0;
}
