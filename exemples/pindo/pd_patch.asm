; PD_PATCH -- PINDO : manette DB9 d'EXLCART en plus du joystick infrarouge
;
; PINDO ne s'execute pas en ROM : un chargeur recopie son code en RAM
; ($C350-$C8FF, depuis $63E9), puis le jeu tourne par "recouvrements" : a
; chaque tour, des morceaux de code sont recopies de la VRAM vers $C442-$C6A9.
; Il ne pose aucune routine en BRJOY0/BRJOY1 : il LIT R3 (VALUE0, joystick 1 /
; clavier), ou le moniteur range le dernier code recu ($04 = rien). Le
; recouvrement de jeu ($C47D) garde la derniere direction : haut $80,
; droite $81, bas $82, gauche $83 (les autres codes sont ignores) ; les menus
; attendent '1', '2', '3'.
;
; La seule routine minuterie est $C43E (en RAM, posee une fois au demarrage par
; le chargeur, en $C67C). Le patch fait pointer BRTIME sur pd_tick : a chaque
; interruption, pd_tick lit l'octet manette de la zone d'echange (0 sur une
; vraie EPROM) et, s'il a change, range le code du joystick infrarouge
; correspondant dans R3, exactement comme le moniteur. Puis il saute a $C43E.
;
; La ROM d'origine (16 Ko) n'a pas de place libre : le patch et la zone sont
; places SOUS elle, en $3F00 et $3FE0, dans une image de 32 Ko.
;
; Modification dans la ROM d'origine (make_pindo_db9.py) :
;   $6716-$6717 : l'adresse $C43E du "movd %$C43E,B" que le chargeur recopie
;                 en $C67C devient pd_tick
; RAM : $C340 (le jeu ecrit seulement $C004-$C005 et $C350-$C800).

#include "EXL100.equ"

JLAST   .equ    $C340           ; dernier code range dans R3
PD_TIME .equ    $C43E           ; routine minuterie de PINDO (en RAM)

        .org    $3F00
pd_tick:
        lda     @exl_joy
        and     %$1F,A
        mov     A,B
        lda     @pd_tab(B)      ; code infrarouge correspondant
        cmpa    @JLAST
        jeq     @pd_done        ; pas de changement : R3 n'est pas touche
        sta     @JLAST
        mov     A,R3            ; comme le moniteur : code dans VALUE0
pd_done:
        br      @PD_TIME

; index = b0 haut, b1 bas, b2 gauche, b3 droite, b4 feu ; haut+bas ou
; gauche+droite ensemble s'annulent
pd_tab:
        .byte   $04, $80, $82, $04, $83, $54, $3C, $83
        .byte   $81, $4F, $48, $81, $04, $80, $82, $04
        .byte   $20, $20, $20, $20, $20, $20, $20, $20
        .byte   $20, $20, $20, $20, $20, $20, $20, $20

; --- zone d'echange EXLCART (cherchee par le firmware, $00 sur EPROM)
        .org    $3FE0
exl_joy:
        .byte   $00, $00, $00, $00
        .text   "EXLJ"

        .end
