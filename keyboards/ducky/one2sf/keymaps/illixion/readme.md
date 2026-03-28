# illixion's Ducky One 2 SF keymap

Custom ANSI keymap with:
- CMD+Esc sends ` (grave) for macOS app switching, Esc otherwise
- Mouse keys with tuned acceleration
- All RGB matrix effects enabled
- Full RGB control on layer 2

## Build

    qmk compile -kb ducky/one2sf/1967st -km illixion

## Flash

    nu-isp-cli flash ducky_one2sf_1967st_illixion.bin
