OS_DETECTION_ENABLE = yes
RAW_ENABLE = yes
KEYBOARD_SHARED_EP = yes
OPT_DEFS += -DNUC123_USB_WORKAROUND=1

# Per-key eager debounce: report press immediately (no added latency),
# then ignore further changes on that key for DEBOUNCE ms. Suppresses
# switch chatter (e.g. double-firing space) without hurting game inputs.
DEBOUNCE_TYPE = sym_eager_pk

# FocusGuard protocol: LED overrides + encrypted secure stream (see fg/ and docs/PROTOCOL.md)
SRC += fg/fg_cmd.c fg/fg_leds.c fg/fg_stream.c fg/fg_secure.c fg/monocypher.c
VPATH += $(KEYMAP_PATH)/fg
# Device key: tools/fg-flash.sh passes a private temp dir holding fg_device_key.h.
ifneq ($(strip $(FG_KEY_DIR)),)
    EXTRAINCDIRS += $(FG_KEY_DIR)
endif

