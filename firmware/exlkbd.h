/*
 * exlkbd.h -- un clavier USB (port USB du Pico, en hote) envoye a la console par
 * l'emetteur infrarouge, comme le clavier Exelvision (meme MC14497, memes trames que
 * la manette : exlir.h).
 *
 * Le clavier EXL100 n'envoie qu'UN code a la fois (6 bits) ; les modificateurs sont
 * des touches a part, qui agissent sur la touche SUIVANTE (le TMS7041 les retient) :
 *   Shift (code 32) : la touche suivante en minuscule / son second caractere ;
 *   Ctl (8), Fct (15) : idem pour Ctl et Fct ; Caps (9) bascule les minuscules.
 * Une touche avec modificateur part donc en sequence : le modificateur, la fin de
 * transmission (62, la touche relachee), puis la touche, repetee tant qu'elle est tenue,
 * et 62 au relachement.
 *
 * Correspondance (clavier PC AZERTY francais) :
 *   - lettres : la meme lettre ; sans Maj, elle arrive en MAJUSCULE (le defaut de
 *     l'EXL100), avec Maj en minuscule (le Shift de l'EXL100) ; Verr. Maj = Caps ;
 *   - chiffres et signes : le CARACTERE tape sur le PC (Maj et AltGr compris) est
 *     reproduit si le clavier EXL100 l'a, avec ou sans son Shift : "1" -> 1, "&" ->
 *     Shift 1, "e accent aigu" -> Shift 2, "@" (AltGr a) -> Shift %, etc. ;
 *   - Ctrl -> Ctl, Alt (gauche) -> Fct, devant la touche ; F1 a F10 -> Fct puis 1 a 0 ;
 *   - Entree, Retour arriere / Suppr (del), Tab, Echap, espace, fleches, Debut (Home).
 * Les caracteres que le clavier EXL100 n'a pas ({ } | ` ² ° £ ...) sont ignores.
 *
 * Plusieurs touches tenues (comme le clavier mou infrarouge, en mieux) :
 *   - la derniere touche enfoncee compte ; quand on la relache, la touche encore
 *     tenue la plus recente reprend (gauche, puis espace : espace ; espace relache :
 *     gauche a nouveau) ;
 *   - les fleches et l'espace forment une MANETTE : deux fleches donnent la
 *     diagonale, une fleche et l'espace le tir combine, avec les codes de la vraie
 *     manette (kb_joy_code) : haut + gauche = 36 ($54, "T"), pas le $45 du clavier
 *     d'origine (croisement de contacts de sa matrice), pour que les jeux faits pour
 *     la manette (Wizord...) marchent aussi au clavier ; deux fleches opposees : la
 *     derniere enfoncee l'emporte ;
 *   - avec Ctrl ou Alt, une fleche ou l'espace redevient une touche ordinaire.
 *
 * Codes du clavier (documentation technique Exelvision, "mode non decode") :
 *   0 Z  1 haut  2 droite  3 bas  4 gauche  5 E  6 espace  7 -/_  8 Ctl  9 Caps
 *   10 X  11 / :  12 Esc  13 R  14 ? ,  15 Fct  16 Tab  17 Home  18 V  19 + =
 *   20 1  21 6  22 8  23 0  24 A  25 Del  26 C  27 . ;  28 2  29 3  30 9  31 # )
 *   32 Shift  33 O  34 H  35 < [  36 T  37 M  38 N  39 G  40 S  41 U  42 K  43 > ]
 *   44 Y  46 B  47 D  48 W  49 P  50 J  51 ~ ^  52 4  53 7  54 * \  55 F  56 Q
 *   57 I  58 L  59 Entree  60 5  61 % @  62 fin de transmission
 * Seconds caracteres (avec Shift), d'apres la ROM du TMS7041 : 1 &, 2 e aigu, 3 ",
 * 4 ', 5 (, 6 $, 7 e grave, 8 !, 9 c cedille, 0 a grave, et les paires ci-dessus.
 */
#ifndef EXLKBD_H
#define EXLKBD_H
#include <stdint.h>

#define KB_SHIFT   32u
#define KB_CTL     8u
#define KB_FCT     15u
#define KB_CAPS    9u

/* une touche tenue du clavier USB, telle que traduite a l'appui */
typedef struct { uint8_t u, code, npre, pre[3], grp; } kb_held_t;   /* grp : bit de "manette" (0 : touche ordinaire) */

/* la touche du moment, partagee entre les rappels USB et l'emetteur (meme coeur).
 * Ecrite aussi par la manette USB (exlpad.h) : gen, code, npre, pre, down. */
typedef struct {
    volatile uint32_t gen;                        /* +1 a chaque nouvelle touche (ou sequence) */
    volatile uint8_t  code, npre, down;           /* code EXL, prefixes, touche tenue */
    volatile uint8_t  pre[3];                     /* Ctl / Fct / Shift a envoyer avant */
    uint8_t usage;                                /* (garde pour la manette : 0) */
    uint8_t prev[6];                              /* touches du rapport precedent */
    /* le clavier lui-meme */
    uint8_t nh;                                   /* touches tenues, dans l'ordre d'appui */
    kb_held_t h[6];
    uint8_t con, ccode, cnpre, cpre[3];           /* ce que le clavier envoie (con = 0 : rien) */
    uint32_t mygen;                               /* gen de la derniere touche du clavier */
} kbd_state_t;
extern kbd_state_t g_kbd;

/* code de la manette 0 d'origine pour un etat au format DB9 (bit0 haut, bit1 bas,
 * bit2 gauche, bit3 droite, bit4 feu), ou -1 : rien d'appuye. Codes = positions de la
 * matrice du clavier : colonne = direction (1 haut, 2 droite, 3 bas, 4 gauche) ou tir
 * (6 : feu seul ou avec gauche / droite, 7 : feu avec haut / bas) ; +32 (ligne M2) :
 * diagonale. D'ou les caracteres recus : O H < T pour les diagonales, N G pour le tir
 * en diagonale. */
static inline int kb_joy_code(uint8_t v) {
    int u = v & 1, d = v >> 1 & 1, l = v >> 2 & 1, r = v >> 3 & 1;
    const int f = v >> 4 & 1;
    if (u && d) u = d = 0;                        /* contacts opposes : sans effet */
    if (l && r) l = r = 0;
    if (f) {
        if ((u && l) || (d && r)) return 38;
        if ((u && r) || (d && l)) return 39;
        return (u || d) ? 7 : 6;
    }
    if (u && r) return 33;
    if (d && r) return 34;
    if (d && l) return 35;
    if (u && l) return 36;
    if (u) return 1;
    if (r) return 2;
    if (d) return 3;
    if (l) return 4;
    return -1;
}

/* lettres : position du clavier AZERTY (code d'usage HID) -> code EXL */
static inline int kb_letter(uint8_t u) {
    switch (u) {
    case 0x14: return 24; case 0x05: return 46; case 0x06: return 26; case 0x07: return 47;   /* A B C D */
    case 0x08: return 5;  case 0x09: return 55; case 0x0A: return 39; case 0x0B: return 34;   /* E F G H */
    case 0x0C: return 57; case 0x0D: return 50; case 0x0E: return 42; case 0x0F: return 58;   /* I J K L */
    case 0x33: return 37; case 0x11: return 38; case 0x12: return 33; case 0x13: return 49;   /* M N O P */
    case 0x04: return 56; case 0x15: return 13; case 0x16: return 40; case 0x17: return 36;   /* Q R S T */
    case 0x18: return 41; case 0x19: return 18; case 0x1D: return 48; case 0x1B: return 10;   /* U V W X */
    case 0x1C: return 44; case 0x1A: return 0;                                                 /* Y Z */
    }
    return -1;
}
/* caractere tape sur un PC AZERTY : [normal, Maj, AltGr] ; 0 = rien d'utile */
static inline uint8_t kb_fr_char(uint8_t u, int col) {
    static const uint8_t T[][4] = {
        {0x1E, '&',  '1',  0},   {0x1F, 0xE9, '2',  '~'}, {0x20, '"',  '3',  '#'},
        {0x21, '\'', '4',  0},   {0x22, '(',  '5',  '['}, {0x23, '-',  '6',  0},
        {0x24, 0xE8, '7',  0},   {0x25, '_',  '8',  '\\'},{0x26, 0xE7, '9',  '^'},
        {0x27, 0xE0, '0',  '@'}, {0x2D, ')',  0,    ']'}, {0x2E, '=',  '+',  0},
        {0x2F, '^',  0,    0},   {0x30, '$',  0,    0},   {0x34, 0,    '%',  0},
        {0x31, '*',  0,    0},   {0x32, '*',  0,    0},   {0x64, '<',  '>',  0},
        {0x10, ',',  '?',  0},   {0x36, ';',  '.',  0},   {0x37, ':',  '/',  0},
        {0x38, '!',  0,    0},
        /* pave numerique (Verr. Num.) */
        {0x54, '/',  '/',  0},   {0x55, '*',  '*',  0},   {0x56, '-',  '-',  0},
        {0x57, '+',  '+',  0},   {0x59, '1',  '1',  0},   {0x5A, '2',  '2',  0},
        {0x5B, '3',  '3',  0},   {0x5C, '4',  '4',  0},   {0x5D, '5',  '5',  0},
        {0x5E, '6',  '6',  0},   {0x5F, '7',  '7',  0},   {0x60, '8',  '8',  0},
        {0x61, '9',  '9',  0},   {0x62, '0',  '0',  0},   {0x63, '.',  '.',  0},
    };
    for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) if (T[i][0] == u) return T[i][1 + col];
    return 0;
}
/* caractere -> touche EXL (bits 0-5) et Shift EXL (bit 7) ; -1 : absent du clavier */
static inline int kb_exl_char(uint8_t c) {
    static const uint8_t D[10] = { 23, 20, 28, 29, 52, 60, 21, 53, 22, 30 };   /* 0..9 */
    if (c >= '0' && c <= '9') return D[c - '0'];
    static const uint8_t S[][3] = {                    /* caractere, touche, Shift */
        {'&', 20, 1}, {0xE9, 28, 1}, {'"', 29, 1}, {'\'', 52, 1}, {'(', 60, 1},
        {'$', 21, 1}, {0xE8, 53, 1}, {'!', 22, 1}, {0xE7, 30, 1}, {0xE0, 23, 1},
        {'#', 31, 0}, {')', 31, 1}, {'_', 7, 0},  {'-', 7, 1},  {'~', 51, 0},
        {'^', 51, 1}, {'<', 35, 0}, {'[', 35, 1}, {'>', 43, 0}, {']', 43, 1},
        {'%', 61, 0}, {'@', 61, 1}, {'?', 14, 0}, {',', 14, 1}, {'.', 27, 0},
        {';', 27, 1}, {'/', 11, 0}, {':', 11, 1}, {'+', 19, 0}, {'=', 19, 1},
        {'*', 54, 0}, {'\\', 54, 1}, {' ', 6, 0},
    };
    for (unsigned i = 0; i < sizeof S / sizeof S[0]; i++) if (S[i][0] == c) return S[i][1] | (S[i][2] << 7);
    return -1;
}
/* une touche USB (usage HID) et les modificateurs du rapport -> code EXL et prefixes.
 * Renvoie le code (0..61), ou -1 si la touche n'a pas d'equivalent. */
static inline int kb_map(uint8_t u, uint8_t mods, uint8_t *pre, uint8_t *npre) {
    const int shift = (mods & 0x22u) != 0, ctrl = (mods & 0x11u) != 0, alt = (mods & 0x04u) != 0,
              altgr = (mods & 0x40u) != 0;
    int code = -1, exl_shift = 0;
    uint8_t n = 0;
    switch (u) {                                  /* touches speciales */
    case 0x28: case 0x58: code = 59; break;       /* Entree */
    case 0x2A: case 0x4C: code = 25; break;       /* Retour arriere, Suppr -> Del */
    case 0x2B: code = 16; break;                  /* Tab */
    case 0x29: code = 12; break;                  /* Echap */
    case 0x2C: code = 6; break;                   /* espace */
    case 0x4F: code = 2; break;                   /* fleches */
    case 0x50: code = 4; break;
    case 0x51: code = 3; break;
    case 0x52: code = 1; break;
    case 0x4A: code = 17; break;                  /* Debut -> Home */
    case 0x39: code = KB_CAPS; break;             /* Verr. Maj -> Caps */
    default:
        if (u >= 0x3A && u <= 0x43) {             /* F1..F10 -> Fct puis 1..0 */
            static const uint8_t F[10] = { 20, 28, 29, 52, 60, 21, 53, 22, 30, 23 };
            pre[n++] = KB_FCT; *npre = n; return F[u - 0x3A];
        }
        code = kb_letter(u);
        if (code >= 0) { exl_shift = shift; break; }
        if ((ctrl || alt) && u >= 0x1E && u <= 0x27) {     /* Ctl / Fct + chiffre : la touche du chiffre */
            static const uint8_t D[10] = { 20, 28, 29, 52, 60, 21, 53, 22, 30, 23 };   /* 1..9, 0 */
            code = D[u - 0x1E];
            break;
        }
        {
            const uint8_t c = kb_fr_char(u, altgr ? 2 : shift ? 1 : 0);
            const int e = c ? kb_exl_char(c) : -1;
            if (e < 0) return -1;
            code = e & 0x3F; exl_shift = e >> 7;
        }
    }
    if (ctrl) pre[n++] = KB_CTL;
    if (alt && !altgr) pre[n++] = KB_FCT;
    if (exl_shift) pre[n++] = KB_SHIFT;
    *npre = n;
    return code;
}
/* fleches et espace : bit "manette" (format DB9), 0 pour les autres touches */
static inline uint8_t kb_grp(uint8_t u) {
    switch (u) {
    case 0x52: return 0x01;                       /* haut */
    case 0x51: return 0x02;                       /* bas */
    case 0x50: return 0x04;                       /* gauche */
    case 0x4F: return 0x08;                       /* droite */
    case 0x2C: return 0x10;                       /* espace : le feu */
    }
    return 0;
}
/* ce que le clavier doit envoyer d'apres les touches tenues ; 0 : rien */
static inline int kb_current(const kbd_state_t *k, uint8_t *code, uint8_t *pre, uint8_t *npre) {
    if (!k->nh) return 0;
    const kb_held_t *t = &k->h[k->nh - 1];        /* la plus recente */
    if (!t->grp) {
        *code = t->code; *npre = t->npre;
        for (int j = 0; j < t->npre; j++) pre[j] = t->pre[j];
        return 1;
    }
    uint8_t v = 0;                                /* la "manette" : toutes les fleches et l'espace tenus */
    for (int i = 0; i < k->nh; i++) {
        const uint8_t g = k->h[i].grp;
        if (g & 0x03) v &= (uint8_t)~0x03;        /* haut / bas : le plus recent l'emporte */
        if (g & 0x0C) v &= (uint8_t)~0x0C;        /* gauche / droite : idem */
        v |= g;
    }
    const int c = kb_joy_code(v);
    if (c < 0) return 0;
    *code = (uint8_t)c; *npre = 0;
    return 1;
}
/* un rapport "boot" du clavier (8 octets : modificateurs, 0, 6 touches) -> g_kbd.
 * Chaque touche est traduite a l'appui (avec les modificateurs du moment) ; quand ce
 * qu'il faut envoyer change, une nouvelle sequence part (gen + 1) ; plus rien de
 * tenu : la touche est relachee (sauf si la manette USB a pris la main entre-temps). */
static inline void kb_report(kbd_state_t *k, const uint8_t *r) {
    const uint8_t mods = r[0];
    const uint8_t *keys = r + 2;
    uint8_t n = 0;                                /* 1) les touches relachees sortent de la liste */
    for (int i = 0; i < k->nh; i++) {
        int still = 0;
        for (int j = 0; j < 6; j++) if (keys[j] == k->h[i].u) still = 1;
        if (still) k->h[n++] = k->h[i];
    }
    k->nh = n;
    for (int i = 0; i < 6; i++) {                 /* 2) les nouvelles entrent a la fin */
        const uint8_t u = keys[i];
        if (u < 4) continue;                      /* 0 : rien, 1-3 : erreurs (trop de touches) */
        int old = 0;
        for (int j = 0; j < k->nh; j++) if (k->h[j].u == u) old = 1;
        if (old || k->nh >= 6) continue;
        kb_held_t t = { u, 0, 0, { 0, 0, 0 }, 0 };
        t.grp = (mods & 0x55u) ? 0 : kb_grp(u);   /* avec Ctrl ou Alt : touche ordinaire */
        if (!t.grp) {
            const int c = kb_map(u, mods, t.pre, &t.npre);
            if (c < 0) continue;                  /* sans equivalent : ignoree */
            t.code = (uint8_t)c;
        }
        k->h[k->nh++] = t;
    }
    for (int i = 0; i < 6; i++) k->prev[i] = keys[i];
    uint8_t code = 0, pre[3] = { 0, 0, 0 }, np = 0;   /* 3) a envoyer : a-t-il change ? */
    const int on = kb_current(k, &code, pre, &np);
    int same = on == k->con;
    if (same && on) {
        same = code == k->ccode && np == k->cnpre;
        for (int j = 0; same && j < np; j++) same = pre[j] == k->cpre[j];
    }
    if (same) return;
    k->con = (uint8_t)on; k->ccode = code; k->cnpre = np;
    for (int j = 0; j < 3; j++) k->cpre[j] = pre[j];
    if (on) {
        k->code = code; k->npre = np;
        for (int j = 0; j < np; j++) k->pre[j] = pre[j];
        k->usage = 0; k->down = 1;
        k->gen++; k->mygen = k->gen;
    } else if (k->gen == k->mygen) {
        k->down = 0;
    }
}
#endif
