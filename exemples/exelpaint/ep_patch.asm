; EP_PATCH -- ExelPaint : souris USB d'EXLCART en plus de l'ExelMouse
;
; ExelPaint lit la souris par un vecteur en RAM : le programme appelle
; $C322 (boucle principale, ecran de configuration), ou $107F a ecrit
; "br @$11A7" (EXL100, ExelMouse sur P49/P50) ou "br @$1126" (EXELTEL,
; souris sur $0133). Ces deux pilotes rendent la meme chose :
;   B = deplacement X signe (+ = vers la gauche), A = deplacement Y signe
;   (+ = vers le haut), R14 = boutons ($20 = principal, $10 = second),
; puis branchent en $1243, le traitement commun (curseur, clics).
;
; Le patch remplace l'adresse du pilote par musb_e / musb_t :
;  - souris USB presente (octet $A5 dans la zone d'echange) : pilote USB
;    ci-dessous, au meme rythme (bit 7 de R68, mis par l'interruption timer)
;    et au meme format ;
;  - sinon : le pilote d'origine, inchange (sur une vraie EPROM, la zone
;    reste a $00 : ExelPaint fonctionne exactement comme avant).
; Le pilote USB ne regarde pas le bit 6 de R68 ("pas de souris", decide a
; l'ecran de configuration) : la souris USB marche meme sans interface ni
; ExelMouse, et peut etre branchee a tout moment.
;
; Modifications dans la ROM d'origine (make_exelpaint_usb.py) :
;   $107F  lda @$FFFE        -> call @musb_init   (resynchronise les compteurs)
;   $1086  movd %$11A7,B     -> movd %musb_e,B
;   $108C  movd %$1126,B     -> movd %musb_t,B
; RAM : $C7E0-$C7E1 (ExelPaint n'utilise rien au-dela de $C326).

#include "EXL100.equ"

MUSB_MAX   .equ    15           ; pas maximal par lecture (pixels) : < 16,
                                ; sinon le repliement vertical de $157A deraille
MUSB_DROP  .equ    64           ; retard au-dela duquel l'exces est oublie
musb_lx    .equ    $C7E0        ; derniers compteurs pris en compte
musb_ly    .equ    $C7E1
DRV_EXL    .equ    $11A7        ; pilote ExelMouse EXL100 d'origine
DRV_TEL    .equ    $1126        ; pilote EXELTEL d'origine
MOUSE_DONE .equ    $1243        ; traitement commun des pilotes

        .org    $7E00

; --- appele a la place de "lda @$FFFE" au debut de $107F
musb_init:
        call    @musb_sync
        lda     @$FFFE
        rets

; --- vecteur $C322 sur EXL100
musb_e:
        push    A
        lda     @exl_mpres
        cmp     %$A5,A
        jne     @me_orig
        pop     A
        br      @musb_go
me_orig:
        call    @musb_sync      ; pas de souris USB : on suit ses compteurs,
        pop     A               ; pour qu'elle ne saute pas quand on la branche
        br      @DRV_EXL

; --- vecteur $C322 sur EXELTEL
musb_t:
        push    A
        lda     @exl_mpres
        cmp     %$A5,A
        jne     @mt_orig
        pop     A
        br      @musb_go
mt_orig:
        call    @musb_sync
        pop     A
        br      @DRV_TEL

musb_sync:
        lda     @exl_mx
        sta     @musb_lx
        lda     @exl_my
        sta     @musb_ly
        rets

; --- pilote souris USB, meme sortie que $11A7
musb_go:
        btjz    %$80,R68,@mg_ret ; pas encore l'heure (interruption timer)
        and     %$7E,R68         ; comme le pilote d'origine
        lda     @exl_mx          ; X : ancien - nouveau (+ = gauche)
        mov     A,R13
        lda     @musb_lx
        call    @musb_lim
        push    A
        mov     R13,A
        sta     @musb_lx
        lda     @exl_my          ; Y : ancien - nouveau (+ = haut)
        mov     A,R13
        lda     @musb_ly
        call    @musb_lim
        push    A
        mov     R13,A
        sta     @musb_ly
        lda     @exl_mbtn        ; boutons USB -> bits ExelPaint
        and     %$07,A
        mov     A,B
        lda     @musb_btab(B)
        mov     A,R14
        pop     A                ; A = pas Y
        pop     B                ; B = pas X
        mov     A,R13
        or      B,A
        or      R14,A
        jne     @mg_move
        and     %$4F,R68         ; rien : comme le pilote d'origine
mg_ret:
        rets
mg_move:
        mov     B,R50
        mov     R13,A
        mov     A,R51
        br      @MOUSE_DONE

; A = ancien compteur, R13 = nouveau -> A = pas signe borne a +-MUSB_MAX,
; R13 = compteur a retenir (le reste sera rendu aux lectures suivantes)
musb_lim:
        mov     A,R14
        sub     R13,A            ; A = ancien - nouveau
        jn      @ml_neg
        cmp     %MUSB_MAX+1,A
        jl      @ml_ok           ; 0..MAX : tout est pris
        cmp     %MUSB_DROP,A
        jhs     @ml_pcap         ; trop de retard : on oublie l'exces
        mov     R14,A
        sub     %MUSB_MAX,A
        mov     A,R13
ml_pcap:
        mov     %MUSB_MAX,A
ml_ok:
        rets
ml_neg:
        cmp     %$100-MUSB_MAX,A
        jhs     @ml_ok           ; -MAX..-1 : tout est pris
        cmp     %$101-MUSB_DROP,A
        jl      @ml_ncap
        mov     R14,A
        add     %MUSB_MAX,A
        mov     A,R13
ml_ncap:
        mov     %$100-MUSB_MAX,A
        rets

; bit0 gauche, bit1 droit, bit2 milieu -> $20 principal (gauche, milieu),
; $10 second (droit)
musb_btab:
        .byte   $00, $20, $10, $30, $20, $20, $30, $30

; --- zone d'echange EXLCART (cherchee par le firmware, $00 sur EPROM)
        .org    $7F00
exl_io:
        .byte   $00, $00, $00, $00
        .text   "EXLP"
exl_mx    .equ    exl_io+8
exl_my    .equ    exl_io+9
exl_mbtn  .equ    exl_io+10
exl_mpres .equ    exl_io+11
        .byte   $00, $00, $00, $00, $00, $00, $00, $00

        .end
