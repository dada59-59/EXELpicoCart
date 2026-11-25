; MK_PATCH -- Capitaine Menkar (Exelvision) : manette DB9 d'EXLCART en plus du joystick infrarouge
;
; Capitaine Menkar lit ses manettes uniquement par le canal infrarouge du
; TMS7041 : le moniteur range le code recu dans R3 (VALUE0, joystick 1 /
; clavier) ou R4 (VALUE1, joystick 2) et appelle BRJOY0 ($4E27) ou BRJOY1
; ($4E2D). Ces routines cherchent le code dans la table $4E42 : relache $04
; (arret), haut $80, droite $81, bas $82, gauche $83, feu ($20, $5F, 'G',
; 'N'), diagonales ('O' -> haut, 'H' -> droite, '<' -> bas, 'T' -> gauche),
; '1', '3', '*'.
;
; Le patch remplace la routine minuterie du jeu ($4D15) par mk_tick, qui lit
; l'octet manette de la zone d'echange (0 sur une vraie EPROM), le traduit en
; code du joystick infrarouge et, s'il a change, fait exactement ce que fait le
; moniteur pour le joystick 1 : code dans R3, appel de la routine BRJOY0. Puis
; il saute a $4D15.
;
; La ROM d'origine (16 Ko, $4000-$7FFF) est pleine : le patch et la zone sont
; places SOUS elle, en $3F00 et $3FE0, dans une image de 32 Ko (le firmware sert
; tout l'espace $0000-$7FFF ; la zone de Wizord est en $3FE0 de la meme facon).
;
; Modification dans la ROM d'origine (make_menkar_db9.py) :
;   $4D09  movd %$4D15,B  ->  movd %mk_tick,B   (adresse posee dans BRTIME)
; RAM : $C7F0 (le jeu utilise $C000-$C1F6 et $C7FA-$C7FF).

#include "EXL100.equ"

JLAST   .equ    $C7F0           ; dernier code envoye
MK_TIME .equ    $4D15           ; routine minuterie d'origine de Capitaine Menkar

        .org    $3F00
mk_tick:
        lda     @exl_joy
        and     %$1F,A
        mov     A,B
        lda     @mk_tab(B)      ; code infrarouge correspondant
        cmpa    @JLAST
        jeq     @mk_done        ; pas de changement : rien a faire
        sta     @JLAST
        mov     A,R3            ; comme le moniteur : code dans VALUE0...
        lda     @$C001
        mov     A,B
        lda     @$C000
        jeq     @mk_done        ; (pas de routine en ce moment)
        call    *R1             ; ... et appel de la routine BRJOY0
mk_done:
        br      @MK_TIME

; index = b0 haut, b1 bas, b2 gauche, b3 droite, b4 feu ; haut+bas ou
; gauche+droite ensemble s'annulent
mk_tab:
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
