OS_DETECTION_ENABLE = yes
RAW_ENABLE = yes
KEYBOARD_SHARED_EP = yes
OPT_DEFS += -DNUC123_USB_WORKAROUND=1

# Per-key eager debounce: report press immediately (no added latency),
# then ignore further changes on that key for DEBOUNCE ms. Suppresses
# switch chatter (e.g. double-firing space) without hurting game inputs.
DEBOUNCE_TYPE = sym_eager_pk
