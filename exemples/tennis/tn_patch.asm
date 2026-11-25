; TN_PATCH -- Tennis (Exelvision) : manette DB9 d'EXLCART en plus du joystick infrarouge
;
; Tennis lit ses manettes uniquement par le canal infrarouge du TMS7041 : le
; moniteur range le code recu dans R3 (VALUE0, joystick 1 / clavier) ou R4
; (VALUE1, joystick 2) et appelle la routine dont l'adresse est en BRJOY0
; ($C000) ou BRJOY1 ($C002). Tennis change cette routine selon la phase :
; menus ($0B2B, $0B96, $0BD5, $0C0C), service ($0C22 / $0C79), echange
; ($0D00 / $0D14).
;
; Le patch remplace la routine minuterie de Tennis ($0E07) par tn_tick, qui
; lit l'octet manette de la zone d'echange (0 sur une vraie EPROM), le traduit
; en code du joystick infrarouge et, s'il a change, fait exactement ce que fait
; le moniteur pour le joystick 1 : code dans R3, appel de la routine BRJOY0 en
; cours. Puis il saute a $0E07. La manette DB9 se comporte donc comme le
; joystick infrarouge 1, dans toutes les phases du jeu, sans rien changer aux
; routines de Tennis.
;
; Codes envoyes (ceux qu'attend Tennis, voir sa table $0D33-$0D95) :
;   feu (seul ou avec une direction) $20 (ESPACE), haut $80, droite $81,
;   bas $82, gauche $83, haut+droite $4F, bas+droite $48, bas+gauche $3C,
;   haut+gauche $54, rien $04 (relache).
;
; Modification dans la ROM d'origine (make_tennis_db9.py) :
;   $026D  movd %$0E07,B  ->  movd %tn_tick,B   (adresse posee dans BRTIME)
; RAM : $C7F0 (Tennis n'utilise rien au-dela de $C719).

#include "EXL100.equ"

JLAST   .equ    $C7F0           ; dernier code envoye
TN_TIME .equ    $0E07           ; routine minuterie d'origine de Tennis

        .org    $7F60
tn_tick:
        lda     @exl_joy
        and     %$1F,A
        mov     A,B
        lda     @tn_tab(B)      ; code infrarouge correspondant
        cmpa    @JLAST
        jeq     @tn_done        ; pas de changement : rien a faire
        sta     @JLAST
        mov     A,R3            ; comme le moniteur : code dans VALUE0...
        lda     @$C001
        mov     A,B
        lda     @$C000
        jeq     @tn_done        ; (pas de routine en ce moment)
        call    *R1             ; ... et appel de la routine BRJOY0
tn_done:
        br      @TN_TIME

; index = b0 haut, b1 bas, b2 gauche, b3 droite, b4 feu ; haut+bas ou
; gauche+droite ensemble s'annulent
tn_tab:
        .byte   $04, $80, $82, $04, $83, $54, $3C, $83
        .byte   $81, $4F, $48, $81, $04, $80, $82, $04
        .byte   $20, $20, $20, $20, $20, $20, $20, $20
        .byte   $20, $20, $20, $20, $20, $20, $20, $20

; --- zone d'echange EXLCART (cherchee par le firmware, $00 sur EPROM)
        .org    $7FE0
exl_joy:
        .byte   $00, $00, $00, $00
        .text   "EXLJ"

        .end
