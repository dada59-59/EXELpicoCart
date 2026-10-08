/*
 * test_pins.c -- verification sur PC du brochage et du rangement des images.
 *   gcc -O2 -Wall -o test_pins test_pins.c && ./test_pins                      (lineaire)
 *   gcc -O2 -Wall -DEXL_PINOUT_EPROM -o test_pins_eprom test_pins.c && ./test_pins_eprom
 * Pour chaque adresse : le motif de broches vu par le Pico (avec du bruit sur toutes
 * les broches qui ne sont pas des adresses), le calcul de l'interpolateur (voies 0 et 1
 * + BASE2), la case lue, la valeur ecrite sur les broches de donnees -> l'octet relu
 * par la console doit etre celui de la ROM.
 */
#include <stdio.h>
#include <stdlib.h>
#include "exlpins.h"
static cell_t img[ROM_SIZE];
static uint8_t romsrc[ROM_SIZE];
static int fails = 0;
#define OK(c,msg) do{ int ok_=(c); printf("%s %s\n",ok_?"OK   ":"ECHEC",msg); if(!ok_) fails++; }while(0)
static uint32_t rnd(void){ static uint32_t x=12345; x^=x<<13; x^=x>>17; x^=x<<5; return x; }
int main(void){
    printf("brochage : %s\n", PINOUT_NAME);
    pins_init();
    /* 1) broches : chaque signal une seule fois, rien sur les broches interdites */
    uint32_t used = 0; int dup = 0;
    for (int k=0;k<15;k++){ if (used>>PIN_A[k]&1) dup=1; used |= 1u<<PIN_A[k]; }
    for (int j=0;j<8;j++){ if (used>>PIN_D[j]&1) dup=1; used |= 1u<<PIN_D[j]; }
    if (used & CS_MASK) dup = 1;
    used |= CS_MASK;
    OK(!dup, "24 signaux sur 24 broches differentes");
    OK(!(used & (0xFu<<25)), "aucune broche GP25-GP28 (LED, ADC)");
    uint32_t dm = 0; for (int j=0;j<8;j++) dm |= 1u<<PIN_D[j];
    OK(dm == DATA_MASK, "DATA_MASK = les 8 broches de donnees");
    /* 2) index : bijection adresse <-> case */
    static uint8_t seen[ROM_SIZE]; int bij = 1, inv = 1;
    for (uint32_t r=0;r<ROM_SIZE;r++){
        uint32_t i = ADDR_TO_IDX(r);
        if (i >= ROM_SIZE || seen[i]++) bij = 0;
        if (IDX_TO_ADDR(i) != r) inv = 0;
        if (i != idx_of_pins(pins_of_addr(r))) bij = 0;
    }
    OK(bij, "les 32768 adresses tombent sur 32768 cases differentes (0..32767)");
    OK(inv, "retour case -> adresse exact pour les 32768 adresses");
    /* 3) le service, avec du bruit sur toutes les broches qui ne sont pas des adresses */
    for (uint32_t r=0;r<ROM_SIZE;r++) romsrc[r] = (uint8_t)rnd();
    img_load(img, romsrc, 0, ROM_SIZE);
    uint32_t amask = 0; for (int k=0;k<15;k++) amask |= 1u<<PIN_A[k];
    int good = 1, clean = 1;
    for (int pass=0; pass<4; pass++)
    for (uint32_t r=0;r<ROM_SIZE;r++){
        uint32_t P = pins_of_addr(r) | (rnd() & ~amask);            /* bruit : donnees, CS, GP17, GP25+ */
#ifdef EXL_PINOUT_EPROM
        uint32_t base2 = 0x20010000u >> 1;                          /* image a une adresse paire */
        uint32_t h = base2 + idx_of_pins(P);                        /* interpolateur : BASE2 + voie0 + voie1 */
        cell_t c = img[(h + h - 0x20010000u) / 2];                  /* ldrh rX, [rh, rh] */
#else
        cell_t c = img[P & ADDR_MASK];                              /* rom[p & ADDR_MASK] */
#endif
        uint32_t out = (uint32_t)c << DATA_SHIFT;                   /* gpio_out */
        if (out & ~DATA_MASK) clean = 0;
        uint8_t b = 0; for (int j=0;j<8;j++) if (out>>PIN_D[j]&1) b |= 1u<<j;   /* lu par la console */
        if (b != romsrc[r]) good = 0;
    }
    OK(good, "4 x 32768 acces avec bruit sur les autres broches : la console lit toujours le bon octet");
    OK(clean, "rien n'est jamais ecrit hors des 8 broches de donnees");
    /* 4) liste inseree en $7000 et sommes */
    uint8_t inj[3840]; for (int i=0;i<3840;i++) inj[i]=(uint8_t)(i*7+1);
    img_load(img, inj, 0x7000, 3840);
    int injok = 1; for (int i=0;i<3840;i++) if (cell_dec(img[ADDR_TO_IDX(0x7000+i)]) != inj[i]) injok = 0;
    for (int i=0;i<3840;i++) romsrc[0x7000+i] = inj[i];
    int restok = 1; for (uint32_t r=0;r<ROM_SIZE;r++) if (cell_dec(img[ADDR_TO_IDX(r)]) != romsrc[r]) restok = 0;
    OK(injok && restok, "liste inseree en $7000-$7EFF, reste de l'image intact");
    uint32_t s = 0; for (uint32_t r=0;r<ROM_SIZE;r++) s += romsrc[r];
    OK(img_sum(img) == s, "somme de l'image (decodee) = somme de la ROM");
    printf(fails ? "\n%d ECHEC(S)\n" : "\ntout est bon\n", fails);
    return fails != 0;
}
