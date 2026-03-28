# illixion's Ducky One 2 SF keymap

Custom ANSI keymap with:
- CMD+Esc sends ` (grave) for macOS app switching, Esc otherwise
- Mouse keys with tuned acceleration
- All RGB matrix effects enabled
- Full RGB control on layer 2 (Right Fn + Z to toggle, X to cycle modes, ArrowUp/Down to adjust brightness, ArrowLeft/Right to adjust speed)

## Build

    cargo install nu-isp-cli
    qmk compile -kb ducky/one2sf/1967st -km illixion

## Flash

Hold D+L while plugging in the keyboard to enter bootloader mode, then:

    nu-isp-cli flash ducky_one2sf_1967st_illixion.bin
