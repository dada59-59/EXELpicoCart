; ============================================================================
; JOYEXL -- lecture des manettes EXL100 par le canal du TMS7041, au format
; "octet normalise" (meme methode que Wizord ; codes releves sur EXL100 avec la
; vraie manette et le programme jstest, et ceux des tables des jeux).
;
; Le 7020 ne voit les manettes (et le clavier) que sous forme d'UN code courant :
;   VALUE0 (R3) : clavier + manette 0     vecteur BRJOY0 = $C000
;   VALUE1 (R4) : manette 1               vecteur BRJOY1 = $C002
; $04 = relachement. Le moniteur appelle BRJOY0/1 a chaque nouveau code
; (A et B sauvegardes par le moniteur, la routine finit par RETS).
;
; Octet normalise (1 = appuye) :
;   bit0 haut  bit1 bas  bit2 gauche  bit3 droite  bit4 feu
;
; Particularite du 7041 : "feu + direction" n'a pas de code propre pour chaque
; direction (_ = feu + haut/bas/gauche/droite, N = feu + haut-gauche ou
; bas-droite, G = feu + haut-droite ou bas-gauche). Pour ces trois codes on
; garde la direction precedente et on ajoute le feu.
;
; Utilisation :
;       call    @joy_init       ; branche BRJOY0 / BRJOY1, etats a zero
;       ...
;       lda     @joy_state0     ; manette 0 (et fleches + espace du clavier)
;       lda     @joy_state1     ; manette 1
;
; Combinaison avec la manette DB9 d'EXLCART (octet $00 sur une vraie EPROM) :
;       lda     @joy_state0
;       mov     A,B
;       lda     @$3FE0
;       or      B,A
; ============================================================================

; VALUE0 (R3), VALUE1 (R4), BRJOY0 ($C000), BRJOY1 ($C002) viennent de
; 7020.equ / EXL100.equ. S'ils manquent, decommenter :
;VALUE0         .equ    R3
;VALUE1         .equ    R4
;BRJOY0         .equ    $C000
;BRJOY1         .equ    $C002
; A inclure dans le programme : #include "joyexl.asm" (pas de .org ici)

; 3 octets de RAM (a adapter au jeu)
joy_state0      .equ    $C5F0
joy_state1      .equ    $C5F1
joy_ch          .equ    $C5F2
JOY_N           .equ    12              ; nombre de codes de la table
JOY_KEEP        .equ    $30             ; "feu + direction precedente"

; --- branchement des vecteurs ------------------------------------------------
joy_init:
        clr     A
        sta     @joy_state0
        sta     @joy_state1
        movd    %joy_isr0,B             ; A = poids fort, B = poids faible
        sta     @BRJOY0
        mov     B,A
        sta     @BRJOY0+1
        movd    %joy_isr1,B
        sta     @BRJOY1
        mov     B,A
        sta     @BRJOY1+1
        rets

; --- routines appelees par le moniteur ----------------------------------------
joy_isr0:
        clr     A
        sta     @joy_ch
        mov     VALUE0,A
        br      @joy_code
joy_isr1:
        mov     %1,A
        sta     @joy_ch
        mov     VALUE1,A
                                        ; (suite dans joy_code)

; --- joy_code : A = code recu, met a jour joy_state0 ou joy_state1 ------------
joy_code:
        cmp     %$04,A                  ; relachement : plus rien d'appuye
        jne     @jc_look
        clr     A
        br      @jc_store
jc_look:
        mov     %JOY_N,B
jc_loop:
        cmpa    @joy_codes-1(B)
        jeq     @jc_found
        djnz    B,@jc_loop
        rets                            ; autre touche du clavier : etat inchange
jc_found:
        lda     @joy_bits-1(B)
        cmp     %JOY_KEEP,A
        jne     @jc_store
        push    A                       ; feu + direction precedente
        lda     @joy_ch
        mov     A,B
        pop     A
        lda     @joy_state0(B)
        and     %$0F,A
        or      %$10,A
jc_store:
        push    A
        lda     @joy_ch
        mov     A,B
        pop     A
        sta     @joy_state0(B)
        rets

; codes du 7041, releves sur EXL100 avec la vraie manette (programme jstest) ;
; les memes que les tables de Tennis, Virus, Guppy et Capitaine Menkar.
; (La documentation Exelvision donne $4F = haut-gauche et $45 = haut-droite :
; c'est faux, la manette envoie $54 et $4F.)
joy_codes:
        .byte   $80, $82, $83, $81      ; haut, bas, gauche, droite
        .byte   $54, $4F, $3C, $48      ; haut-gauche, haut-droite, bas-gauche, bas-droite
        .byte   $20                     ; feu (aussi : espace du clavier)
        .byte   $5F, $4E, $47           ; feu + direction (ambigus)
joy_bits:
        .byte   $01, $02, $04, $08
        .byte   $05, $09, $06, $0A
        .byte   $10
        .byte   JOY_KEEP, JOY_KEEP, JOY_KEEP
