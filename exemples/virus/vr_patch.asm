; VR_PATCH -- Virus (Exelvision) : manette DB9 d'EXLCART en plus du joystick infrarouge
;
; Virus lit ses manettes uniquement par le canal infrarouge du TMS7041 : le
; moniteur range le code recu dans R3 (VALUE0, joystick 1 / clavier) ou R4
; (VALUE1, joystick 2) et appelle la routine dont l'adresse est en BRJOY0
; ($C000) ou BRJOY1 ($C002) : $40D3 (choix de l'annee, feu = commencer) ou
; $424E (partie : table $42FF, directions, feu, '#', '*', relache $04).
;
; Virus a deux routines minuterie : $4DB3 (la partie) et $5323 (posee en
; $5060 pendant une sequence, puis $4DB3 est remise en $5FF6). Le patch les
; remplace par vr_tick_a / vr_tick_b, qui lisent l'octet manette de la zone
; d'echange (0 sur une vraie EPROM), le traduisent en code du joystick
; infrarouge et, s'il a change, font exactement ce que fait le moniteur pour
; le joystick 1 : code dans R3, appel de la routine BRJOY0 en cours. Puis elles
; sautent a la routine d'origine. La manette DB9 se comporte donc comme le
; joystick infrarouge 1, sans rien changer aux routines de Virus.
;
; Codes envoyes (table $42FF de Virus) : feu (seul ou avec une direction) $20,
;   haut $80, droite $81, bas $82, gauche $83, haut+droite $4F, bas+droite $48,
;   bas+gauche $3C, haut+gauche $54, rien $04 (relache).
;
; Modifications dans la ROM d'origine (make_virus_db9.py) :
;   $403F  movd %$4DB3,B  ->  movd %vr_tick_a,B
;   $5FF6  movd %$4DB3,B  ->  movd %vr_tick_a,B
;   $5060  movd %$5323,B  ->  movd %vr_tick_b,B
; RAM : $C7F0 (Virus n'utilise rien au-dela de $C600).

#include "EXL100.equ"

JLAST   .equ    $C7F0           ; dernier code envoye
VR_TIMA .equ    $4DB3           ; routine minuterie de la partie
VR_TIMB .equ    $5323           ; routine minuterie de la sequence ($5059)

        .org    $7F00
; (la meme routine deux fois, pour ne pas ajouter de niveau de pile)
vr_tick_a:
        lda     @exl_joy
        and     %$1F,A
        mov     A,B
        lda     @vr_tab(B)      ; code infrarouge correspondant
        cmpa    @JLAST
        jeq     @va_done        ; pas de changement : rien a faire
        sta     @JLAST
        mov     A,R3            ; comme le moniteur : code dans VALUE0...
        lda     @$C001
        mov     A,B
        lda     @$C000
        jeq     @va_done        ; (pas de routine en ce moment)
        call    *R1             ; ... et appel de la routine BRJOY0
va_done:
        br      @VR_TIMA

vr_tick_b:
        lda     @exl_joy
        and     %$1F,A
        mov     A,B
        lda     @vr_tab(B)
        cmpa    @JLAST
        jeq     @vb_done
        sta     @JLAST
        mov     A,R3
        lda     @$C001
        mov     A,B
        lda     @$C000
        jeq     @vb_done
        call    *R1
vb_done:
        br      @VR_TIMB

; index = b0 haut, b1 bas, b2 gauche, b3 droite, b4 feu ; haut+bas ou
; gauche+droite ensemble s'annulent
vr_tab:
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
