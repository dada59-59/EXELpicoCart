/*
 * exlusbdev.c -- EXLCART en PERIPHERIQUE USB (port serie CDC), quand le Pico est
 * branche seul sur un PC en mode bibliotheque : EXLPICO y relit les sauvegardes de
 * l'Exelmemoire (exlsave.h). Le reste du temps, le port USB est en HOTE (exlmouse.c).
 */
#include "tusb.h"
#include "hardware/timer.h"

#define USB_VID 0x2E8A                        /* comme le port serie du SDK du Pico */
#define USB_PID 0x000A

static const tusb_desc_device_t dev_desc = {
    .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE, .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC, .bDeviceSubClass = MISC_SUBCLASS_COMMON, .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE, .idVendor = USB_VID, .idProduct = USB_PID, .bcdDevice = 0x0100,
    .iManufacturer = 1, .iProduct = 2, .iSerialNumber = 3, .bNumConfigurations = 1 };
uint8_t const *tud_descriptor_device_cb(void) { return (uint8_t const *)&dev_desc; }

enum { ITF_CDC = 0, ITF_CDC_DATA, ITF_TOTAL };
#define CFG_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)
static const uint8_t cfg_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_TOTAL, 0, CFG_LEN, 0, 100),
    TUD_CDC_DESCRIPTOR(ITF_CDC, 4, 0x81, 8, 0x02, 0x82, 64) };
uint8_t const *tud_descriptor_configuration_cb(uint8_t index) { (void)index; return cfg_desc; }

static const char *const str_desc[] = { "", "EXLCART", "EXLCART (Exelmemoire)", "EXLCART1", "EXLCART serie" };
static uint16_t str_buf[32];
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    uint32_t n;
    if (index == 0) { str_buf[1] = 0x0409; n = 1; }
    else {
        if (index >= sizeof str_desc / sizeof str_desc[0]) return 0;
        const char *s = str_desc[index];
        for (n = 0; s[n] && n < 31; n++) str_buf[1 + n] = (uint8_t)s[n];
    }
    str_buf[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * n + 2));
    return str_buf;
}

void usbdev_init(void) { tud_init(0); }
void usbdev_task(void) { tud_task(); }
int usbdev_getc(void) {
    tud_task();
    if (!tud_mounted() || !tud_cdc_available()) return -1;
    return tud_cdc_read_char();
}
/* envoie tout ; abandonne si le PC ne lit plus rien pendant 2 s */
void usbdev_write(const char *s, uint32_t n) {
    uint64_t t = time_us_64();
    while (n) {
        tud_task();
        if (!tud_mounted() || time_us_64() - t > 2000000u) return;
        uint32_t k = tud_cdc_write_available();
        if (!k) { tud_cdc_write_flush(); continue; }
        if (k > n) k = n;
        tud_cdc_write(s, k); s += k; n -= k;
        t = time_us_64();
    }
    tud_cdc_write_flush();
}
