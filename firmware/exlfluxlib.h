/*
 * exlfluxlib.h -- un lecteur "disque" (zone EXLD, badflux) parmi les jeux de la
 * bibliotheque. Le film (conteneur EXLF) est range dans la flash apres les jeux ;
 * l'en-tete de la bibliotheque (version 2) donne son emplacement (flux_off).
 *
 *   - coeur 0, au chargement d'un jeu (lib_poll -> lib_flux_loaded) : si le jeu porte
 *     une zone EXLD et que le film est la, prepare les anneaux et le debut des deux
 *     fichiers dans l'image du jeu, puis g_flux_game = 1 ; sans film, D_PRES = $5A
 *     (le lecteur affiche "pas de film") et le jeu est servi normalement ;
 *   - coeur 1 : serve_multi revient au moment ou il bascule sur ce jeu ; serve_flux_lib
 *     sert alors le jeu comme le mode disque (memes instructions pendant l'acces, octet
 *     suivant apres l'acces), plus la regle du retour au menu de la bibliotheque : apres
 *     ARM_N acces, la signature du menu dans le pied du jeu ; une lecture du pied apres
 *     plus de RESET_US sans acces (reset) -> le menu, et serve_multi reprend ;
 *   - coeur 0, dans sa boucle : flx_step (recopie de la flash devant la console).
 * A inclure apres exllib.h et exlflux.h.
 */
#ifndef EXLFLUXLIB_H
#define EXLFLUXLIB_H
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE

static const lib_hdr_t *g_flib;                   /* la bibliotheque (pour lib_flux_loaded) */

/* coeur 0, juste apres le chargement du jeu r (avant g_ready) */
static void lib_flux_loaded(int32_t r) {
    g_flux_game = 0; g_fok = 0;
    const lib_hdr_t *H = g_flib;
    fzone_t z;
    if (!fz_find((const uint8_t *)(XIP_BASE + H->game_off + (uint32_t)r * H->stride), ROM_SIZE, &z)) return;
    FZ = z; g_fimg = game;
    const uint32_t off = (H->version >= 2) ? H->flux_off : 0u;
    const fhdr_t *h = (const fhdr_t *)(XIP_BASE + off);
    if (off < 0x00200000u || off >= 0x01000000u || (off & 3u) || !fh_ok(h, 0x01000000u - off)) {
        game[ADDR_TO_IDX(FZ.pres)] = ENC(FLX_NONE);          /* lecteur sans film : il le dit */
        return;
    }
    for (int k = 0; k < 2; k++) { g_fd[k] = (const uint8_t *)h + h->f[k].off; g_fl[k] = h->f[k].len; }
    g_rd[0] = g_rd[1] = 0; g_frst = 0;
    flx_rewind();
    g_fok = 1;
    __dmb();
    g_flux_game = 1;
}

/* coeur 1 : le jeu disque, jusqu'au reset (retour : M0 pret pour serve_multi) */
static void __noinline __not_in_flash_func(serve_flux_lib)(void) {
    multi_t m = M0;                                       /* etat ST_GAME, image = le jeu */
    cell_t *const img = m.game;
    cell_t *const r0 = g_ring[0], *const r1 = g_ring[1];
    uint32_t i0 = g_rd[0], i1 = g_rd[1];
    uint32_t t_end = timer_hw->timerawl, n = m.n_game;
#ifdef EXL_PINOUT_EPROM
    interp_hw_t *ip = interp0_hw;
    const uint32_t b2 = (uint32_t)img >> 1;
    ip->base[2] = b2;
    const uint32_t h0 = b2 + ADDR_TO_IDX(FZ.d0), h1 = b2 + ADDR_TO_IDX(FZ.d1), hR = b2 + ADDR_TO_IDX(FZ.rst),
                   hF = b2 + FOOT_IDX;
    cell_t *const c0 = img + ADDR_TO_IDX(FZ.d0), *const c1 = img + ADDR_TO_IDX(FZ.d1), *const cP = img + ADDR_TO_IDX(FZ.pres);
    for (;;) {
        uint32_t p, h;
        while ((p = sio_hw->gpio_in) & CS_MASK) ;          /* repos */
        do {                                                /* l'acces */
            ip->accum[0] = p;
            h = ip->peek[2];
            sio_hw->gpio_out = SERVE_CELL(h) << DATA_SHIFT;
            sio_hw->gpio_oe_set = DATA_MASK;
        } while (!((p = sio_hw->gpio_in) & CS_MASK));
        sio_hw->gpio_oe_clr = DATA_MASK;                    /* bus libere */
        const uint32_t now = timer_hw->timerawl, pause = now - t_end;
        t_end = now;
        if (h == h0) { *c0 = r0[++i0 & (FLX_RING - 1u)]; g_rd[0] = i0; }
        else if (h == h1) { *c1 = r1[++i1 & (FLX_RING - 1u)]; g_rd[1] = i1; }
        else if (h == hR) { *cP = ENC(0); i0 = i1 = 0; g_rd[0] = g_rd[1] = 0; g_frst = 1; }
        else if (h == hF) { if (n >= ARM_N && pause > RESET_US) break; }
        else if (n < ARM_N && ++n == ARM_N) img[FOOT_IDX] = m.menu_foot;
        FLX_AFTER_ACCESS();
    }
#else
    const uint32_t a0 = FZ.d0, a1 = FZ.d1, aR = FZ.rst;
    cell_t *const c0 = img + a0, *const c1 = img + a1, *const cP = img + FZ.pres;
    for (;;) {
        uint32_t p, a;
        while ((p = sio_hw->gpio_in) & CS_MASK) ;          /* repos */
        do {                                                /* l'acces */
            a = p & ADDR_MASK;
            sio_hw->gpio_out = (uint32_t)img[a] << DATA_SHIFT;
            sio_hw->gpio_oe_set = DATA_MASK;
        } while (!((p = sio_hw->gpio_in) & CS_MASK));
        sio_hw->gpio_oe_clr = DATA_MASK;                    /* bus libere */
        const uint32_t now = timer_hw->timerawl, pause = now - t_end;
        t_end = now;
        if (a == a0) { *c0 = r0[++i0 & (FLX_RING - 1u)]; g_rd[0] = i0; }
        else if (a == a1) { *c1 = r1[++i1 & (FLX_RING - 1u)]; g_rd[1] = i1; }
        else if (a == aR) { *cP = ENC(0); i0 = i1 = 0; g_rd[0] = g_rd[1] = 0; g_frst = 1; }
        else if (a == FOOT_ADDR) { if (n >= ARM_N && pause > RESET_US) break; }
        else if (n < ARM_N && ++n == ARM_N) img[FOOT_IDX] = m.menu_foot;
        FLX_AFTER_ACCESS();
    }
#endif
    /* reset : le menu (comme multi_after) ; la console a deja lu la signature du menu */
    img[FOOT_IDX] = m.game_foot;
    m.img = m.menu; m.state = ST_MENU; m.n_game = n; m.t_end = t_end;
    g_flux_game = 0;
    g_mstate = ST_MENU;
    M0 = m;
}

/* branchement : a appeler une fois, avant le lancement du coeur 1 */
static void lib_flux_init(const lib_hdr_t *H) {
    g_flib = H;
    g_lib_loaded = lib_flux_loaded;
    g_lib_flux_serve = serve_flux_lib;
}
#endif
#endif
