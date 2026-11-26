/* tusb_config.h -- TinyUSB sur le port USB natif (EXLCART) :
 *   - en HOTE (le cas normal, cartouche dans la console) : souris, clavier, manettes ;
 *   - en PERIPHERIQUE (Pico seul sur un PC, mode bibliotheque) : port serie CDC,
 *     pour relire les sauvegardes de l'Exelmemoire dans EXLPICO (exlusbdev.c).
 * Un seul des deux est demarre : tusb_init() demarre l'hote (RHPORT0_MODE),
 * tud_init(0) le peripherique. */
#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#define CFG_TUSB_RHPORT0_MODE     OPT_MODE_HOST
#define CFG_TUSB_DEBUG            0
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN        __attribute__ ((aligned(4)))

#define CFG_TUH_ENABLED           1
#define CFG_TUH_ENUMERATION_BUFSIZE 1024          /* descripteurs de rapport des manettes (DualShock 4 : 507 octets) */
#define CFG_TUH_HUB               1               /* une souris derriere un petit hub */
#define CFG_TUH_DEVICE_MAX        4
#define CFG_TUH_HID               4               /* souris "combo" : plusieurs interfaces */
#define CFG_TUH_HID_EPIN_BUFSIZE  64
#define CFG_TUH_HID_EPOUT_BUFSIZE 64
#define CFG_TUH_API_EDPT_XFER     1               /* manettes XInput : points d'acces ouverts par exlmouse.c */

#define CFG_TUD_ENABLED           1               /* peripherique : sur un PC seulement */
#define CFG_TUD_ENDPOINT0_SIZE    64
#define CFG_TUD_CDC               1
#define CFG_TUD_CDC_RX_BUFSIZE    64
#define CFG_TUD_CDC_TX_BUFSIZE    512
#define CFG_TUD_CDC_EP_BUFSIZE    64

#endif
