/*
 * exlmouse.c -- souris, clavier (protocole "boot" HID) et manette USB (HID generique)
 * sur le port USB du Pico, en hote (TinyUSB).
 *   - souris : les rappels cumulent les deplacements dans g_mouse ; io_step
 *     (exljoy.h) les recopie dans la zone d'echange de la ROM ;
 *   - clavier : chaque rapport met a jour g_kbd (exlkbd.h) ; l'emetteur infrarouge
 *     (exlir.h) envoie les touches a la console comme le clavier Exelvision ;
 *   - manette : son descripteur de rapport est lu au branchement (exlpad.h), puis
 *     chaque rapport met a jour g_pad (directions et feu, ajoutes a la DB9) et g_kbd
 *     (ses autres boutons : des touches).
 *   - manette Xbox 360 / One / Series ou compatible XInput (exlxinput.h) : ce n'est
 *     pas un HID, TinyUSB ne la prend pas en charge ; au branchement de tout appareil
 *     (tuh_mount_cb), on lit son descripteur de configuration, et s'il a une interface
 *     XInput, on ouvre nous-memes ses points d'acces, on la met en marche et on lit
 *     ses rapports (API "points d'acces" de TinyUSB, presente des la version 0.15).
 * Souris, clavier et manette peuvent etre branches ensemble (petit hub, ou clavier
 * "combo" avec pave tactile) ; une seule manette (HID ou XInput) a la fois.
 * Compile seulement avec EXL_WITH_MOUSE.
 */
#include "tusb.h"
#include "exljoy.h"
#include "exlkbd.h"
#include "exlpad.h"
#include "exlxinput.h"

mouse_state_t g_mouse;
static uint8_t m_addr, m_inst, m_on;
static uint8_t k_addr, k_inst, k_on;
static uint8_t p_addr, p_inst, p_on;
static pad_layout_t p_L;

/* manette XInput */
static uint8_t x_cfg[512];                        /* descripteur de configuration */
static uint8_t x_in[64] __attribute__((aligned(4)));
static uint8_t x_addr, x_on, x_ask;               /* x_ask : appareil examine (0 : aucun) */
static uint16_t x_pend;                           /* appareils a examiner (bit = adresse) */
static uint8_t x_busy;                            /* lecture du descripteur en cours */
static uint8_t x_need_in;                         /* la lecture des rapports est a relancer */
static xi_found_t x_f;
static xi_t x_st;
#define X_QN 8
static uint8_t x_q[X_QN][16], x_ql[X_QN], x_qh, x_qt, x_out_busy;   /* paquets a envoyer */

static void x_out_cb(tuh_xfer_t *xf);
static void x_out_next(void) {
    if (!x_on || !x_f.ep_out || x_out_busy || x_qh == x_qt) return;
    uint8_t *b = x_q[x_qh % X_QN];
    tuh_xfer_t xf = { .daddr = x_addr, .ep_addr = x_f.ep_out, .buflen = x_ql[x_qh % X_QN], .buffer = b,
                      .complete_cb = x_out_cb, .user_data = 0 };
    if (tuh_edpt_xfer(&xf)) x_out_busy = 1;
}
static void x_out_cb(tuh_xfer_t *xf) {
    (void)xf;
    x_out_busy = 0; x_qh++;
    x_out_next();
}
static void x_send(const uint8_t *d, uint8_t n) {
    if ((uint8_t)(x_qt - x_qh) >= X_QN || n > 16) return;
    uint8_t *b = x_q[x_qt % X_QN];
    memcpy(b, d, n);
    if (x_f.kind == XI_ONE && n >= 3 && d[0] != 0x01) b[2] = x_st.seq++;   /* numero de sequence GIP (pas pour un accuse) */
    x_ql[x_qt % X_QN] = n; x_qt++;
    x_out_next();
}
static void x_in_cb(tuh_xfer_t *xf);
static void x_in_start(void) {
    tuh_xfer_t xf = { .daddr = x_addr, .ep_addr = x_f.ep_in, .buflen = x_f.in_size > sizeof x_in ? sizeof x_in : x_f.in_size,
                      .buffer = x_in, .complete_cb = x_in_cb, .user_data = 0 };
    x_need_in = !tuh_edpt_xfer(&xf);
}
static void x_in_cb(tuh_xfer_t *xf) {
    if (!x_on || xf->daddr != x_addr) return;
    if (xf->result == XFER_RESULT_SUCCESS) {
        uint8_t out[16], on;
        if (xi_input(&x_st, x_in, xf->actual_len, out, &on)) pad_apply(&g_pad, &g_kbd, x_st.v, x_st.keys);
        if (on) x_send(out, on);
    }
    x_in_start();
}
/* le descripteur de configuration est arrive : une interface XInput ? */
static void x_cfg_cb(tuh_xfer_t *xf) {
    x_busy = 0;
    const uint8_t a = (uint8_t)xf->daddr;
    if (a != x_ask) return;                       /* debranche entre-temps */
    x_ask = 0;
    if (xf->result != XFER_RESULT_SUCCESS || x_on || p_on) return;
    uint32_t n = xf->actual_len;
    if (n >= 4) { const uint32_t tot = (uint32_t)(x_cfg[2] | (x_cfg[3] << 8)); if (tot < n) n = tot; }
    if (!xi_find(x_cfg, n, &x_f)) return;
    if (!tuh_edpt_open(a, (tusb_desc_endpoint_t const *)(x_cfg + x_f.in_off))) return;
    if (x_f.ep_out && !tuh_edpt_open(a, (tusb_desc_endpoint_t const *)(x_cfg + x_f.out_off))) x_f.ep_out = 0;
    memset(&x_st, 0, sizeof x_st);
    x_st.kind = x_f.kind;
    x_addr = a; x_on = 1; x_qh = x_qt = 0; x_out_busy = 0;
    pad_reset(&g_pad, &g_kbd); g_pad.on = 1;
    uint16_t vid = 0, pid = 0;
    tuh_vid_pid_get(a, &vid, &pid);
    xi_pkt_t pk[6];
    const int np = xi_init(x_f.kind, vid, pid, pk, 6);
    for (int i = 0; i < np; i++) x_send(pk[i].d, pk[i].n);
    x_in_start();
}
/* un appareil a la fois (le canal de commande est partage) ; appele a chaque tour */
static void x_examine(void) {
    if (x_busy || !x_pend) return;
    if (x_on || p_on) { x_pend = 0; return; }     /* deja une manette */
    uint8_t a = 0;
    while (!(x_pend >> a & 1u)) a++;
    x_ask = a;
    if (tuh_descriptor_get_configuration(a, 0, x_cfg, sizeof x_cfg, x_cfg_cb, 0)) {
        x_busy = 1; x_pend &= (uint16_t)~(1u << a);
    }                                             /* sinon : canal occupe, au tour suivant */
}
/* tout appareil branche (apres sa configuration) : peut-etre une manette XInput */
void tuh_mount_cb(uint8_t daddr) {
    if (daddr < 16) x_pend |= (uint16_t)(1u << daddr);
    x_examine();
}
void tuh_umount_cb(uint8_t daddr) {
    if (daddr < 16) x_pend &= (uint16_t)~(1u << daddr);
    if (x_ask == daddr) { x_ask = 0; x_busy = 0; }
    if (x_on && daddr == x_addr) {
        x_on = 0; x_need_in = 0; x_qh = x_qt = 0; x_out_busy = 0;
        pad_reset(&g_pad, &g_kbd); g_pad.on = 0;
    }
}

void mouse_usb_init(void) {
    tusb_init();                                  /* port USB natif, en hote */
}

void mouse_usb_task(void) {
    tuh_task();
    x_examine();
    if (x_on && x_need_in) x_in_start();
}

/* une interface HID est branchee : une souris et un clavier (protocole boot), une
 * manette (sans protocole : son descripteur de rapport dit si c'en est une) */
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len) {
    const uint8_t pr = tuh_hid_interface_protocol(dev_addr, instance);
    if (pr == HID_ITF_PROTOCOL_MOUSE && !m_on) {
        m_addr = dev_addr; m_inst = instance; m_on = 1;
        g_mouse.present = 1;
        tuh_hid_receive_report(dev_addr, instance);
    } else if (pr == HID_ITF_PROTOCOL_KEYBOARD && !k_on) {
        k_addr = dev_addr; k_inst = instance; k_on = 1;
        tuh_hid_receive_report(dev_addr, instance);
    } else if (pr == HID_ITF_PROTOCOL_NONE && !p_on && !x_on && desc_report && pad_parse(desc_report, desc_len, &p_L)) {
        p_addr = dev_addr; p_inst = instance; p_on = 1;
        pad_reset(&g_pad, &g_kbd); g_pad.on = 1;
        tuh_hid_receive_report(dev_addr, instance);
    }
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    if (m_on && dev_addr == m_addr && instance == m_inst) {
        m_on = 0; g_mouse.present = 0; g_mouse.btn = 0;
    }
    if (k_on && dev_addr == k_addr && instance == k_inst) {
        static const uint8_t none[8] = { 0 };
        k_on = 0; kb_report(&g_kbd, none);       /* plus de touche tenue */
    }
    if (p_on && dev_addr == p_addr && instance == p_inst) {
        p_on = 0; pad_reset(&g_pad, &g_kbd); g_pad.on = 0;
    }
}

/* souris : boutons, dX, dY (signes), [molette] ; clavier : modificateurs, 0, 6 touches ;
 * manette : selon son descripteur */
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *r, uint16_t len) {
    if (m_on && dev_addr == m_addr && instance == m_inst && len >= 3) {
        g_mouse.btn = r[0] & 7u;
        g_mouse.x += (int8_t)r[1];
        g_mouse.y += (int8_t)r[2];
    } else if (k_on && dev_addr == k_addr && instance == k_inst && len >= 8) {
        kb_report(&g_kbd, r);
    } else if (p_on && dev_addr == p_addr && instance == p_inst) {
        pad_report(&g_pad, &g_kbd, &p_L, r, len);
    }
    tuh_hid_receive_report(dev_addr, instance);
}
