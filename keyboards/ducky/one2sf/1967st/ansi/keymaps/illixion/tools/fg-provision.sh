#!/bin/bash
# Generate the keyboard<->Mac secure-stream key and store it in the macOS login Keychain
# (service com.illixion.focusguard.keyboard), the only place it is kept on the Mac.
# FocusGuard reads it from there; tools/fg-flash.sh reads it at build time to compile
# it into the firmware, then deletes every build file. No copy stays on disk.
#
# The key is 256 random bits from the system CSPRNG. It is never printed, never passed
# on a command line (argv is visible to every local process), and the Keychain write
# goes through stdin. Only FocusGuard may read the item without asking; anything else,
# including fg-flash.sh, triggers a Keychain prompt you have to approve.
#
#   tools/fg-provision.sh            first time (refuses to overwrite an existing key)
#   tools/fg-provision.sh --rotate   replace the key (reflash required; old firmware stops working)
set -euo pipefail
umask 077

SERVICE="com.illixion.focusguard.keyboard"
ACCOUNT="${FG_KEYBOARD_ACCOUNT:-ducky-one2sf-1967st}"
APP="${FG_APP_PATH:-$HOME/Applications/FocusGuard.app}"

if security find-generic-password -s "$SERVICE" -a "$ACCOUNT" >/dev/null 2>&1 && [ "${1:-}" != "--rotate" ]; then
    echo "A key already exists in the Keychain ($SERVICE / $ACCOUNT). Use --rotate to replace it." >&2
    exit 1
fi
[ -d "$APP" ] || { echo "FocusGuard not found at $APP (set FG_APP_PATH). It must exist to be given access." >&2; exit 1; }

key="$(openssl rand -hex 32)"
[ "${#key}" -eq 64 ] || { echo "FAIL: could not generate a key" >&2; exit 1; }

if security -i >/dev/null 2>&1 <<SEC
add-generic-password -U -a "$ACCOUNT" -s "$SERVICE" -l "FocusGuard keyboard key" -T "$APP" -w "$key"
SEC
then
    unset key
    echo "OK: key stored in the login Keychain ($SERVICE / $ACCOUNT)."
    echo "Now build and flash it: tools/fg-flash.sh"
else
    unset key
    echo "FAIL: Keychain write failed." >&2
    exit 1
fi
