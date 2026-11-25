/*
 * exlk7.h -- cassettes K7 dans la bibliotheque : un programme BASIC (fichier .k7 des
 * emulateurs) se lance depuis le menu ; le Pico sert le BASIC (EXELBASIC ou EXEL BASIC
 * PLUS, une autre entree de la bibliotheque) et joue la cassette : LOAD "1" charge en
 * une fraction de seconde, LOAD "1.NOM" cherche le bon bloc, comme une vraie bande.
 *
 * La lecture cassette du BASIC passe par le TRAP 14 (ROM interne du TMS7020) : on ne
 * peut pas le corriger. Le BASIC l'appelle a deux endroits pour LOAD (bloc RAM, puis
 * bloc VRAM), par "dint / trap 14 / eint" (3 octets). Au chargement :
 *   - image servie = le BASIC, ou ces deux sequences deviennent "call @R" (3 octets) ;
 *   - RECOUVREMENT (img_pool[1]) = la meme image, plus la routine k7load en R
 *     (~130 octets, assemblee par EXLPICO a l'adresse choisie pour ce BASIC) ;
 *   - coeur 1 (serve_k7) : quand la console lit le dernier octet d'un de ces CALL, il
 *     sert le recouvrement ; quand elle lit le RETS final de la routine, le BASIC
 *     revient. Rien n'est retire du BASIC : la vraie cassette marche toujours en SAVE.
 *     Dans la routine, trois cases :
 *       K7_NEXT  lecture = bloc suivant (K7_STAT passe a $FF) ;
 *       K7_STAT  $FF : le Pico cherche ; 0 : bloc pret ; $20 : fin de bande ;
 *       K7_DATA  apres chaque lecture, l'octet suivant du bloc (anneau en RAM) ;
 *   - coeur 0 (k7_step) : bloc suivant (apres le dernier : le premier ; un tour entier
 *     sans lire de donnees : $20, "fin de bande"), et recopie de la flash dans l'anneau.
 *
 * Entree K7 dans la flash (son emplacement de 32 Ko), ecrite par EXLPICO :
 *   +0   "EXK7", version 1, numero de l'entree du BASIC
 *   +8   s1, s2 (les deux sequences), org, rlen (la routine)
 *   +16  exit, next, stat, data (adresses dans la routine)
 *   +24  nombre de blocs, +28 longueur de la bande
 *   +32  debut de chaque bloc dans la bande (64 au plus)
 *   +160 la routine (rlen octets, 192 au plus)
 *   +512 la bande : pour chaque bloc, ce qui suit l'octet $70 (nom, offset, fin,
 *        debut, donnees, somme x2).
 * A inclure apres exllib.h et exlflux.h (l'anneau du mode disque sert aussi ici).
 */
#ifndef EXLK7_H
#define EXLK7_H
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE && EXL_POOL_IMAGES >= 2

#define K7_MAGIC   0x374B5845u                    /* "EXK7" */
#define K7_TAPE    512u
#define K7_MAXBLK  64u
#define K7_MAXR    192u
#define K7_AHEAD   3800u                          /* octets prepares en avance (anneau de FLX_RING) */
typedef struct {
    uint32_t magic; uint16_t version, basic;
    uint16_t s1, s2, org, rlen;
    uint16_t exit, next, stat, data;
    uint16_t nblk, rsv; uint32_t tlen;
    uint16_t boff[K7_MAXBLK];
    uint8_t  code[K7_MAXR];
} k7_hdr_t;
_Static_assert(sizeof(k7_hdr_t) == 352, "k7_hdr_t");

typedef struct {
    uint32_t t1, t2, ax, an, ad;                  /* adresses vues par le coeur 1 */
    cell_t *cD, *cS;                              /* cases K7_DATA, K7_STAT du recouvrement */
    const uint8_t *tape; const uint16_t *boff;
    uint32_t nblk, tlen;
    int32_t  cur; uint32_t scan;                  /* coeur 0 : bloc en cours, blocs vus sans lecture */
    const uint8_t *src; uint32_t len, wr;         /* le bloc en cours, octets prepares */
} k7_t;
static k7_t K7;
static cell_t *const k7_ovl = img_pool[1];
#define K7_RING g_ring[0]
static volatile uint32_t g_k7rd;                  /* octets lus dans le bloc (coeur 1) */
static volatile uint32_t g_k7next;                /* bloc suivant demande (coeur 1 -> 0) */
static volatile uint32_t g_k7done;                /* octets lus dans le bloc precedent (au K7_NEXT) */
static volatile uint32_t g_k7_blocks;             /* blocs servis (essais) */

static inline uint32_t k7_blen(uint32_t k) {
    return (k + 1 < K7.nblk ? K7.boff[k + 1] : K7.tlen) - K7.boff[k];
}
/* coeur 0, au chargement de l'entree r : 1 si c'est une K7 (le BASIC est alors dans game) */
static int k7_load(const lib_hdr_t *H, int32_t r) {
    const uint8_t *slot = (const uint8_t *)(XIP_BASE + H->game_off + (uint32_t)r * H->stride);
    const k7_hdr_t *k = (const k7_hdr_t *)slot;
    if (k->magic != K7_MAGIC) return 0;
    g_k7_game = 0;
    int ok = k->version == 1 && k->basic < H->count && k->rlen && k->rlen <= K7_MAXR
          && k->org >= 0x0200u && k->org + k->rlen <= 0x7FFCu
          && k->nblk >= 1 && k->nblk <= K7_MAXBLK && k->tlen <= H->stride - K7_TAPE;
    for (uint32_t i = 0; ok && i < k->nblk; i++)
        ok = k->boff[i] < k->tlen && (i == 0 ? k->boff[0] == 0 : k->boff[i] > k->boff[i - 1]);
    const uint16_t in[4] = { k->exit, k->next, k->stat, k->data };
    for (int i = 0; ok && i < 4; i++) ok = in[i] >= k->org && in[i] < k->org + k->rlen;
    const uint8_t *bas = slot + ((int32_t)k->basic - r) * (int32_t)H->stride;
    const uint16_t s[2] = { k->s1, k->s2 };
    for (int i = 0; ok && i < 2; i++)                         /* le BASIC attendu : dint / trap 14 / eint */
        ok = s[i] >= 0x0200u && (s[i] + 3u <= k->org || s[i] >= k->org + k->rlen)
          && bas[s[i]] == 0x06 && bas[s[i] + 1] == 0xF1 && bas[s[i] + 2] == 0x05;
    if (((const k7_hdr_t *)bas)->magic == K7_MAGIC) ok = 0;
    img_load(game, bas, 0, ROM_SIZE);                         /* (entree fausse : le BASIC seul) */
    lib_warm(H, bas);                                         /* reprise a chaud de l'EXELTEL -> le menu */
    if (!ok) return 1;
    for (int i = 0; i < 2; i++) {
        game[ADDR_TO_IDX(s[i])] = ENC(0x8E);                  /* call @org */
        game[ADDR_TO_IDX(s[i] + 1u)] = ENC(k->org >> 8);
        game[ADDR_TO_IDX(s[i] + 2u)] = ENC(k->org & 0xFFu);
    }
    memcpy(k7_ovl, game, sizeof(cell_t) * ROM_SIZE);          /* recouvrement = le BASIC corrige + la routine */
    for (uint32_t i = 0; i < k->rlen; i++) k7_ovl[ADDR_TO_IDX(k->org + i)] = ENC(k->code[i]);
    K7.t1 = s[0] + 2u; K7.t2 = s[1] + 2u;
    K7.ax = k->exit; K7.an = k->next; K7.ad = k->data;
    K7.cD = k7_ovl + ADDR_TO_IDX(k->data); K7.cS = k7_ovl + ADDR_TO_IDX(k->stat);
    K7.tape = slot + K7_TAPE; K7.boff = k->boff; K7.nblk = k->nblk; K7.tlen = k->tlen;
    K7.cur = -1; K7.scan = 0; K7.src = K7.tape; K7.len = 0; K7.wr = 0;
    g_k7rd = 0; g_k7next = 0; g_k7done = 0; g_k7_blocks = 0;
    *K7.cS = ENC(0xFF);
    __dmb();
    g_k7_game = 1;
    return 1;
}
/* coeur 0, dans sa boucle : bloc suivant, puis l'anneau en avance */
static void k7_step(void) {
    if (g_k7next) {
        g_k7next = 0;
        if (K7.cur >= 0 && g_k7done + 1u >= K7.len) K7.scan = 0; /* le bloc a ete lu en entier */
        if (++K7.scan > K7.nblk) {                              /* un tour sans rien lire : fin */
            K7.scan = 0; K7.cur = -1; K7.len = 0;
            *K7.cS = ENC(0x20);
            return;
        }
        K7.cur = (K7.cur + 1) % (int32_t)K7.nblk;
        K7.src = K7.tape + K7.boff[K7.cur]; K7.len = k7_blen((uint32_t)K7.cur); K7.wr = 0;
        for (; K7.wr < K7_AHEAD && K7.wr < FLX_RING; K7.wr++)
            K7_RING[K7.wr & (FLX_RING - 1u)] = ENC(K7.wr < K7.len ? K7.src[K7.wr] : 0xFFu);
        *K7.cD = K7_RING[0];
        g_k7_blocks++;
        __dmb();                                                /* l'octet d'abord, le signal ensuite */
        *K7.cS = ENC(0);
        return;
    }
    if (K7.cur < 0) return;
    const uint32_t rd = g_k7rd;
    for (int n = 0; n < 64 && K7.wr - rd < K7_AHEAD; n++, K7.wr++)
        K7_RING[K7.wr & (FLX_RING - 1u)] = ENC(K7.wr < K7.len ? K7.src[K7.wr] : 0xFFu);
}
#ifndef K7_AFTER_ACCESS
#define K7_AFTER_ACCESS()  ((void)0)              /* essai en emulateur : le travail du coeur 0 */
#endif
/* coeur 1 : le BASIC et sa cassette, jusqu'au reset (retour : M0 pret pour serve_multi)
 * RETOUR AU BASIC (11/11/2026, releve sur l'EXELTEL) : le TMS7000 lit l'octet qui suit une
 * instruction d'un octet. Pendant le EINT final, il lit donc deja l'adresse du RETS : en
 * rebasculant sur le BASIC a cet acces, le vrai RETS etait lu dans le BASIC (son code de
 * SAVE), qui s'executait a la place -> registres non restaures, ecritures parasites au debut
 * du programme, "Err 18" (ou un chargement reussi par chance). On ne rebascule donc qu'a
 * l'acces a l'instruction qui suit le CALL, apres avoir vu le RETS : cet octet est le meme
 * dans les deux images, et une interruption prise entre EINT et RETS ne gene pas. */
static void __noinline __not_in_flash_func(serve_k7)(void) {
    multi_t m = M0;                                       /* etat ST_GAME, image = le BASIC */
    cell_t *const base = m.game, *const ovl = k7_ovl;
    cell_t *const cD = K7.cD, *const cS = K7.cS;
    const uint32_t t1 = K7.t1, t2 = K7.t2, ax = K7.ax, an = K7.an, ad = K7.ad, warm = m.warm;
    cell_t *const ring = K7_RING;
    uint32_t i = 0, n = m.n_game, t_end = timer_hw->timerawl, in_ovl = 0, leaving = 0;
    const uint32_t r1 = t1 + 1u, r2 = t2 + 1u;            /* l'instruction qui suit chaque CALL */
    ovl[FOOT_IDX] = m.menu_foot;                          /* (reset pendant la routine : le menu) */
    io_rw_32 *const sio = (io_rw_32 *)sio_hw;
    const uint32_t cs = CS_MASK, dm = DATA_MASK;
#ifdef EXL_PINOUT_EPROM
    interp_hw_t *ip = interp0_hw;
    uint32_t b2 = (uint32_t)base >> 1;
    ip->base[2] = b2;
#else
    const cell_t *img = base;
#endif
    for (;;) {
        uint32_t p, a, now;
#ifdef EXL_PINOUT_EPROM
        uint32_t h;
        SERVE_ACCESS(sio, cs, dm, ip, p, h, now);
        a = IDX_TO_ADDR(h - b2);
#else
        SERVE_ACCESS(sio, cs, dm, img, p, a, now);
#endif
        const uint32_t pause = now - t_end;
        t_end = now;
        if (!in_ovl) {
            if (a == t1 || a == t2) {                     /* dernier octet du CALL : la routine */
                in_ovl = 1; leaving = 0;
#ifdef EXL_PINOUT_EPROM
                b2 = (uint32_t)ovl >> 1; ip->base[2] = b2;
#else
                img = ovl;
#endif
            } else if (a == FOOT_ADDR) { if (n >= ARM_N && pause > RESET_US) break; }
            else if (n < ARM_N) { if (++n == ARM_N) base[FOOT_IDX] = m.menu_foot; }
            else if (a == warm && pause > RESET_US) break;       /* reprise a chaud de l'EXELTEL */
        } else {
            if (a == ad) { *cD = ring[++i & (FLX_RING - 1u)]; g_k7rd = i; }
            else if (a == an) { *cS = ENC(0xFF); g_k7done = i; i = 0; g_k7rd = 0; g_k7next = 1; }
            else if (a == ax) leaving = 1;                /* RETS vu (voir plus haut : lu deux fois) */
            else if (leaving && (a == r1 || a == r2)) {   /* retour apres le CALL : le BASIC */
                in_ovl = 0;
#ifdef EXL_PINOUT_EPROM
                b2 = (uint32_t)base >> 1; ip->base[2] = b2;
#else
                img = base;
#endif
            } else if (a == FOOT_ADDR && pause > RESET_US) break;
        }
        K7_AFTER_ACCESS();
    }
    /* reset : le menu (comme multi_after) ; la console a deja lu la signature du menu */
    base[FOOT_IDX] = m.game_foot;
    m.img = m.menu; m.state = ST_MENU; m.n_game = n; m.t_end = t_end;
    g_k7_game = 0;
    g_mstate = ST_MENU;
    M0 = m;
}
/* branchement : a appeler une fois, avant le lancement du coeur 1 */
static void lib_k7_init(void) {
    g_lib_k7_load = k7_load;
    g_lib_k7_serve = serve_k7;
    g_lib_k7_step = k7_step;
}
#else
static inline void lib_k7_init(void) {}
#endif
#endif
