; GP_PATCH -- Guppy (Exelvision) : manette DB9 d'EXLCART en plus du joystick infrarouge
;
; Guppy lit ses manettes uniquement par le canal infrarouge du TMS7041 : le
; moniteur range le code recu dans R3 (VALUE0, joystick 1 / clavier) ou R4
; (VALUE1, joystick 2) et appelle BRJOY0 ($637F) ou BRJOY1 ($6383). Ces deux
; routines cherchent le code dans la table $6393 : haut $80, droite $81,
; bas $82, gauche $83, diagonales ($4F, $48, $3C, $54 : haut-droite -> haut,
; bas-droite -> droite, bas-gauche -> bas, haut-gauche -> gauche), '1' '2' '3'
; (niveau) et '*'. Tout autre code (relache $04, feu $20) est ignore : la
; direction choisie reste en vigueur.
;
; Le patch remplace la routine minuterie de Guppy ($62E0) par gp_tick, qui
; lit l'octet manette de la zone d'echange (0 sur une vraie EPROM), le traduit
; en code du joystick infrarouge et, s'il a change, fait exactement ce que fait
; le moniteur pour le joystick 1 : code dans R3, appel de la routine BRJOY0.
; Puis il saute a $62E0. La manette DB9 se comporte donc comme le joystick
; infrarouge 1, sans rien changer aux routines de Guppy.
;
; Modification dans la ROM d'origine (make_guppy_db9.py) :
;   $6023  movd %$62E0,B  ->  movd %gp_tick,B   (adresse posee dans BRTIME)
; RAM : $C7F0 (Guppy s'arrete a $C7CF : tampon de parole $C714-$C7CF).

#include "EXL100.equ"

JLAST   .equ    $C7F0           ; dernier code envoye
GP_TIME .equ    $62E0           ; routine minuterie d'origine de Guppy

        .org    $7F00
gp_tick:
        lda     @exl_joy
        and     %$1F,A
        mov     A,B
        lda     @gp_tab(B)      ; code infrarouge correspondant
        cmpa    @JLAST
        jeq     @gp_done        ; pas de changement : rien a faire
        sta     @JLAST
        mov     A,R3            ; comme le moniteur : code dans VALUE0...
        lda     @$C001
        mov     A,B
        lda     @$C000
        jeq     @gp_done        ; (pas de routine en ce moment)
        call    *R1             ; ... et appel de la routine BRJOY0
gp_done:
        br      @GP_TIME

; index = b0 haut, b1 bas, b2 gauche, b3 droite, b4 feu ; haut+bas ou
; gauche+droite ensemble s'annulent
gp_tab:
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
