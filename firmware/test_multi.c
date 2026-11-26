/* test sur PC de la bibliotheque : etat gere APRES chaque acces, signature du pied posee d'avance.
 *   gcc -O2 -Wall -o test_multi test_multi.c && ./test_multi                       (lineaire)
 *   gcc -O2 -Wall -DEXL_PINOUT_EPROM -o test_multi_eprom test_multi.c && ./test_multi_eprom
 * Les images en RAM sont rangees selon le brochage : on y lit et ecrit a travers
 * ADDR_TO_IDX / ENC / cell_dec, comme le firmware. */
#include <stdio.h>
#include <string.h>
#include "exlmulti.h"
volatile int32_t g_req = -1; volatile uint32_t g_ready = 0; volatile uint32_t g_mstate = ST_MENU; volatile int32_t g_creq = -1; volatile uint32_t g_boots = 0, g_hot = 0, g_cram_ir = 0;
volatile uint32_t g_sv_done = 0; volatile uint32_t g_warm = WARM_NONE; uint32_t g_sv_n = 0; uint8_t g_sv_mem[SAVE_BUF_SIZE];
static cram_st_t CR;
static cell_t menu[0x8000], game[0x8000];
static uint8_t menu_src[0x8000], games[3][0x8000];
static multi_t m; static uint32_t t = 0;
static int fails = 0;
#define OK(c,msg) do{ int ok_=(c); printf("%s %s\n",ok_?"OK   ":"ECHEC",msg); if(!ok_) fails++; }while(0)
/* un acces comme la boucle : l'octet de l'image courante, puis multi_after ; 1 us par acces */
static uint32_t rd(uint32_t a){ uint32_t d=cell_dec(m.img[ADDR_TO_IDX(a)]); t+=1; multi_after(&m,a,t); return d; }
static void pause_us(uint32_t us){ t+=us; }
static void coeur0(void){ if(g_req>=0){ img_load(game,games[g_req],0,0x8000); menu[FOOT_IDX]=game[FOOT_IDX]; g_ready=1; g_req=-1; } }
/* coeur 0, image Exelmemoire : les 16 premiers Ko de l'entree -> fenetre du menu, puis le signal */
static void coeur0_cram(void){ if(g_creq>=0){ int32_t c=g_creq; g_creq=-1; img_load(menu,games[c],CRAM_WIN_A,CRAM_SIZE); menu[CRAM_STAT_IDX]=ENC(CRAM_READY); } }
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
    OK(g_mstate==ST_GAME, "le coeur 0 voit que le jeu est servi (infrarouge selon le jeu)");
    OK(cell_dec(menu[ADDR_TO_IDX(0x7FFC)])==0x55, "pied du menu restaure");
    jouer(ARM_N+10); pause_us(RESET_US+500);
    OK(rd(0x7FFC)==0x55 && m.state==ST_MENU, "puis un reset ramene au menu");
    OK(g_mstate==ST_MENU, "le coeur 0 voit le retour au menu");
    m.state=ST_MENU; m.img=menu; pause_us(RESET_US+500);
    OK(rd(0x7FFC)==0x55 && m.state==ST_MENU, "reset pendant le menu : toujours le menu");
    /* Exelmemoire (menu v4) : $6E00+n -> image n dans la fenetre $2800-$67FF du menu, $A5 en $6DFF */
    for(int g=0;g<3;g++) for(int i=0;i<0x4000;i++) games[g][i]=(uint8_t)(i*7+g*31+(i>>8));
    menu[CRAM_STAT_IDX]=ENC(CRAM_READY);
    rd(0x6E03); OK(g_creq==-1 && m.state==ST_MENU && rd(0x6DFF)==CRAM_READY, "lecture $6E03 avec 3 entrees : ignoree");
    rd(0x6E01); OK(g_creq==1 && m.state==ST_MENU && g_req==-1, "lecture $6E01 : image Exelmemoire 1 demandee, on reste dans le menu");
    OK(rd(0x6DFF)==0x00, "le signal $6DFF retombe a 0 tout de suite (ancien $A5 efface)");
    OK(rd(0x1000)==0x11, "pendant ce temps, le menu est servi");
    coeur0_cram();
    { int same=1; for(uint32_t i=0;i<0x4000;i++) if(rd(0x2800+i)!=games[1][i]) { same=0; break; }
      OK(same && rd(0x6DFF)==CRAM_READY && m.state==ST_MENU, "coeur 0 : les 16 Ko de l'entree 1 lus en $2800-$67FF, puis $A5 en $6DFF"); }
    OK(rd(0x67FF+1)==0x11 && rd(0x27FF)==0x11 && rd(0x1000)==0x11, "le reste du menu est intact (avant $2800, apres $67FF)");
    rd(0x6E02); coeur0_cram(); OK(rd(0x2800)==games[2][0] && rd(0x67FF)==games[2][0x3FFF] && rd(0x6DFF)==CRAM_READY, "une autre image ensuite (entree 2)");
    rd(0x6F00); coeur0(); OK(m.state==ST_LAUNCH, "un jeu se choisit toujours ensuite ($6F00)");
    rd(0x6E01); OK(g_creq==-1, "pendant un lancement, $6E00+n est ignore");
    pause_us(50000); rd(0x7FFC); jouer(ARM_N+10); rd(0x6E01); OK(m.state==ST_GAME && g_creq==-1, "pendant une partie aussi");
    /* v4.1 : image demarree ($6DFE), puis sans cartouche ($6DFD) ; le coeur 1 note, le coeur 0 (cram_step) agit */
    #define C0(us) cram_step(menu, m.menu_foot, t + (us), &CR)
    pause_us(RESET_US+500); rd(0x7FFC); OK(m.state==ST_MENU, "reset : le menu");
    rd(0x6DFE); OK(m.state==ST_MENU && g_hot==0x6DFE && m.img==menu, "$6DFE : note pour le coeur 0, le menu reste servi");
    C0(0); OK(g_cram_ir==CRAM_IR_RUN && g_hot==0, "coeur 0 : image demarree -> infrarouge");
    rd(0x6DFF); rd(0x1000); C0(0); OK(g_hot==0 && g_cram_ir==CRAM_IR_RUN, "$6DFF (signal de copie) n'est pas une commande");
    rd(0x7FFC); C0(0); OK(g_cram_ir==CRAM_IR_RUN, "pied lu sans pause : toujours l'image");
    pause_us(RESET_US+500); OK(rd(0x7FFC)==0x55, "reset : la console lit le pied du menu");
    C0(0); OK(g_cram_ir==0 && m.state==ST_MENU, "coeur 0 : demarrage vu, fin de l'infrarouge (menu, zone EXLP)");
    rd(0x6DFD); C0(0); OK(g_cram_ir==CRAM_IR_NOCART && rd(0x7FFC)==0xFF, "$6DFD : sans cartouche, pied a $FF");
    OK(rd(0x1000)==0x11 && m.state==ST_MENU, "le menu continue de tourner (attente du reset)");
    rd(0x6DFC); C0(0); OK(g_cram_ir==0 && rd(0x7FFC)==0x55, "$6DFC avant le reset : annule, pied du menu revenu");
    rd(0x6DFD); C0(0); pause_us(RESET_US+500);
    OK(rd(0x7FFC)==0xFF, "reset : la console lit $FF (pas de cartouche)");
    C0(0); pause_us(10000); OK(rd(0x7FFC)==0xFF, "relu pendant le demarrage : toujours $FF");
    C0(500000); OK(rd(0x7FFC)==0xFF && g_cram_ir==CRAM_IR_NOCART, "0,5 s apres : toujours $FF, infrarouge");
    rd(0x6DFC); C0(600000); OK(g_cram_ir==CRAM_IR_NOCART, "apres ce demarrage, $6DFC ne change plus rien");
    C0(1200000); OK(cell_dec(menu[FOOT_IDX])==0x55 && g_cram_ir==CRAM_IR_NOCART, "1 s apres : le pied du menu revient, infrarouge toujours");
    C0(1300000); OK(g_cram_ir==CRAM_IR_NOCART, "sans nouveau reset : on reste ainsi");
    pause_us(RESET_US+500); OK(rd(0x7FFC)==0x55 && m.state==ST_MENU, "reset suivant : le pied du menu, le menu");
    C0(0); OK(g_cram_ir==0, "coeur 0 : fin de l'infrarouge");
    rd(0x6F02); coeur0(); OK(m.state==ST_LAUNCH, "puis un jeu se choisit comme avant");
    /* menu v5.1 : sauvegarde de l'Exelmemoire, envoyee par des lectures $6C00+v */
    rd(0x1000); rd(0x6FFF); jouer(ARM_N+10); pause_us(RESET_US+500); rd(0x7FFC);
    OK(m.state==ST_MENU, "(retour au menu)");
    menu[SAVE_STAT_IDX]=ENC(0x77);
    rd(0x6BF0); OK(g_sv_n==0 && cell_dec(menu[SAVE_STAT_IDX])==0 && g_sv_done==0, "$6BF0 : debut, compteur a zero, reponse $6BF2 = 0");
    { uint32_t sum=0; static uint8_t ex[0x4000];
      for(uint32_t i=0;i<0x4000;i++){ ex[i]=(uint8_t)(i*7+(i>>8)); sum+=ex[i]; }
      for(uint32_t i=0;i<0x4000;i++){ rd(0x6C00+ex[0x3FFF-i]); if((i&511)==0) rd(0x1000+(i&0xFF)); }   /* $BFFF -> $8000, code du menu entre */
      rd(0x6C00+((sum>>8)&0xFF)); rd(0x6C00+(sum&0xFF));
      OK(g_sv_done==0 && g_sv_n==SAVE_LEN, "16 386 lectures $6C00+v rangees (les lectures du code n'y sont pas)");
      int bon=1; for(uint32_t i=0;i<0x4000;i++) if(g_sv_mem[i]!=ex[0x3FFF-i]) bon=0;
      OK(bon && g_sv_mem[0x4000]==((sum>>8)&0xFF) && g_sv_mem[0x4001]==(sum&0xFF), "tampon : les octets dans l'ordre d'envoi, puis la somme");
      rd(0x6BF1); OK(g_sv_done==SAVE_LEN+1, "$6BF1 : fin, le coeur 0 est prevenu (octets + 1)");
      OK(m.state==ST_MENU && rd(0x7FFC)==0x55 && rd(0x1000)==0x11, "toujours le menu, servi normalement"); }
    g_sv_done=0; rd(0x6BF0); for(int i=0;i<40000;i++) rd(0x6C00+(i&0xFF)); rd(0x6BF1);
    OK(g_sv_done==40001, "trop d'octets : compte (le coeur 0 refusera), tampon jamais deborde");
    rd(0x6F00); jouer(5); OK(m.state==ST_LAUNCH, "(un jeu choisi)"); g_sv_done=0; g_sv_n=7;
    rd(0x6BF0); rd(0x6C12); rd(0x6BF1); OK(g_sv_n==7 && g_sv_done==0, "hors de l'etat MENU : $6BF0/$6C00+v/$6BF1 ignores");
    /* EXELTEL : reprise a chaud (lib_warm) -> le menu, seulement apres une pause et une fois arme */
    g_sv_done=0; coeur0(); rd(0x6FFF); jouer(ARM_N+10); pause_us(RESET_US+500); rd(0x7FFC);
    OK(m.state==ST_MENU, "(retour au menu)");
    g_warm=0x031D; rd(0x6F00); coeur0(); rd(0x6FFF);
    OK(m.state==ST_GAME && m.warm==0x031D, "lancement : l'adresse de reprise a chaud est reprise (g_warm)");
    jouer(20); pause_us(RESET_US+500); rd(0x031D);
    OK(m.state==ST_GAME, "pas encore arme : W apres une pause ne fait rien");
    jouer(ARM_N+10); rd(0x031D);
    OK(m.state==ST_GAME, "arme, mais W sans pause : rien");
    pause_us(RESET_US+500); rd(0x031D);
    OK(m.state==ST_MENU && m.img==m.menu && cell_dec(game[FOOT_IDX])==0x55, "arme, pause puis W : reset en reprise a chaud -> le menu (pied du jeu remis)");
    g_warm=WARM_NONE;
    printf(fails?"\n%d ECHEC(S)\n":"\ntout est bon\n",fails);
    return fails!=0;
}
