/* test sur PC de la bibliotheque : etat gere APRES chaque acces, signature du pied posee d'avance.
 *   gcc -O2 -Wall -o test_multi test_multi.c && ./test_multi                       (lineaire)
 *   gcc -O2 -Wall -DEXL_PINOUT_EPROM -o test_multi_eprom test_multi.c && ./test_multi_eprom
 * Les images en RAM sont rangees selon le brochage : on y lit et ecrit a travers
 * ADDR_TO_IDX / ENC / cell_dec, comme le firmware. */
#include <stdio.h>
#include <string.h>
#include "exlmulti.h"
volatile int32_t g_req = -1; volatile uint32_t g_ready = 0;
static cell_t menu[0x8000], game[0x8000];
static uint8_t menu_src[0x8000], games[3][0x8000];
static multi_t m; static uint32_t t = 0;
static int fails = 0;
#define OK(c,msg) do{ int ok_=(c); printf("%s %s\n",ok_?"OK   ":"ECHEC",msg); if(!ok_) fails++; }while(0)
/* un acces comme la boucle : l'octet de l'image courante, puis multi_after ; 1 us par acces */
static uint32_t rd(uint32_t a){ uint32_t d=cell_dec(m.img[ADDR_TO_IDX(a)]); t+=1; multi_after(&m,a,t); return d; }
static void pause_us(uint32_t us){ t+=us; }
static void coeur0(void){ if(g_req>=0){ img_load(game,games[g_req],0,0x8000); menu[FOOT_IDX]=game[FOOT_IDX]; g_ready=1; g_req=-1; } }
static void jouer(uint32_t n){ for(uint32_t i=0;i<n;i++) rd(0x1000+(i&0xFF)); }
int main(void){
    printf("brochage : %s\n", PINOUT_NAME); pins_init();
    memset(menu_src,0x11,sizeof menu_src); menu_src[0x7FFC]=0x55; menu_src[0x7FFD]=0x8C; menu_src[0x7FFE]=0x10; menu_src[0x7FFF]=0;
    img_load(menu,menu_src,0,0x8000);
    for(int g=0;g<3;g++){ memset(games[g],0x20+g,0x8000); games[g][0x7FFC]=(g==2)?0xAA:0x55; games[g][0x7FFD]=0x8C; games[g][0x7FFE]=0x10; games[g][0x7FFF]=0; }
    m.menu=menu; m.game=game; m.img=menu; m.state=ST_MENU; m.count=3; m.n_game=0; m.menu_foot=ENC(0x55); m.game_foot=ENC(0xFF); m.t_end=0;
    OK(rd(0x7FFC)==0x55 && rd(0x1000)==0x11 && m.state==ST_MENU, "allumage : pied du menu, puis le menu");
    rd(0x6F05); OK(m.state==ST_MENU && g_req==-1, "lecture $6F05 avec 3 jeux : ignoree");
    rd(0x6F02); OK(m.state==ST_LAUNCH && g_req==2 && g_ready==0, "lecture $6F02 : jeu 2 (signature AA) demande");
    OK(rd(0x1000)==0x11 && rd(0x7FFC)==0x55 && m.state==ST_LAUNCH, "jeu pas encore charge : menu servi, pied du menu, on reste en attente");
    coeur0();
    OK(cell_dec(menu[ADDR_TO_IDX(0x7FFC)])==0xAA && rd(0x1000)==0x11, "jeu charge : signature AA posee dans le pied du menu, le menu reste servi");
    pause_us(100000);
    OK(rd(0x7FFC)==0xAA, "redemarrage : la console lit AA (signature du jeu) en $7FFC");
    OK(m.state==ST_GAME && rd(0x7FFD)==0x8C && rd(0x1000)==0x22, "puis le jeu est servi ($7FFD, puis le code)");
    OK(cell_dec(menu[ADDR_TO_IDX(0x7FFC)])==0x55, "le pied du menu a retrouve sa signature");
    rd(0x7FFC); OK(m.state==ST_GAME, "pied relu tout de suite par le jeu : on reste sur le jeu (non arme)");
    jouer(ARM_N+10);
    OK(cell_dec(game[ADDR_TO_IDX(0x7FFC)])==0x55, "apres ARM_N acces : signature du menu posee dans le pied du jeu");
    jouer(500); rd(0x7FFC); OK(m.state==ST_GAME, "pied lu sans pause pendant la partie : on reste sur le jeu");
    pause_us(RESET_US+500);
    OK(rd(0x7FFC)==0x55 && m.state==ST_MENU, "reset pendant la partie : la console lit 55 (menu), retour au menu");
    OK(rd(0x7FFD)==0x8C && rd(0x1000)==0x11 && cell_dec(game[ADDR_TO_IDX(0x7FFC)])==0xAA, "le menu est servi, le pied du jeu est restaure");
    rd(0x6F00); coeur0(); OK(m.state==ST_LAUNCH && cell_dec(menu[ADDR_TO_IDX(0x7FFC)])==0x55, "nouveau choix : jeu 0 (signature 55) charge");
    pause_us(50000); rd(0x7FFC); OK(m.state==ST_GAME && rd(0x1000)==0x20, "redemarrage : jeu 0 servi");
    jouer(100); pause_us(RESET_US+500); rd(0x7FFC);
    OK(m.state==ST_GAME, "reset tres tot (avant ARM_N acces) : on reste sur le jeu (signature du jeu lue)");
    /* lancement direct par le loader : lecture $6FFF puis BR @$7FFD */
    pause_us(RESET_US+500); rd(0x7FFC); m.state=ST_MENU; m.img=menu; menu[FOOT_IDX]=ENC(0x55);
    rd(0x6FFF); OK(m.state==ST_MENU, "$6FFF dans le menu, sans jeu choisi : ignore");
    rd(0x6F02); OK(m.state==ST_LAUNCH, "choix du jeu 2");
    rd(0x6FFF); OK(m.state==ST_LAUNCH && m.img==menu, "$6FFF avant la fin du chargement : on reste sur le menu (le BR @$7FFD relance le menu)");
    coeur0(); rd(0x1000);
    OK(rd(0x6FFF)==cell_dec(menu[ADDR_TO_IDX(0x6FFF)]) && m.state==ST_GAME, "$6FFF jeu charge : l'octet lu vient encore du menu, puis bascule");
    OK(rd(0x7FFD)==0x8C && rd(0x7FFE)==0x10 && rd(0x7FFF)==0x00 && rd(0x1000)==0x22, "BR @$7FFD execute celui du JEU, puis son code");
    OK(cell_dec(menu[ADDR_TO_IDX(0x7FFC)])==0x55, "pied du menu restaure");
    jouer(ARM_N+10); pause_us(RESET_US+500);
    OK(rd(0x7FFC)==0x55 && m.state==ST_MENU, "puis un reset ramene au menu");
    m.state=ST_MENU; m.img=menu; pause_us(RESET_US+500);
    OK(rd(0x7FFC)==0x55 && m.state==ST_MENU, "reset pendant le menu : toujours le menu");
    printf(fails?"\n%d ECHEC(S)\n":"\ntout est bon\n",fails);
    return fails!=0;
}
