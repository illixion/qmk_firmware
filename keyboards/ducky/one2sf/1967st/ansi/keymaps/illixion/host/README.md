# Mac layout assert (host-authoritative OS layout)

QMK's USB OS detection mis-fingerprints this Mac as **Linux** — macOS serves
cached string descriptors, so it never sends the `0x02` string-length probes
the `OS_MACOS` heuristic needs, and the detector falls through to the all-`0xFF`
Linux branch. The keyboard then swaps Alt/GUI into the Windows layout.

Fix: don't trust the keyboard's guess. The Mac *asserts* the layout over raw
HID using the firmware's `HOSTCMD_SET_LAYOUT` (`0xA1`) command, which also sets
`layout_locked` so the bogus async OS-detection result can't flip it back.

## Implementation: native Swift daemon (`ducky-maclayout.swift`)

A small resident daemon built on `IOHIDManager`. It registers a device-matching
callback for the keyboard's vendor raw-HID collection (usage page `0xFF60` /
usage `0x61`) and asserts the layout **the instant that collection is enumerated
and openable** — on plug-in, at login if already attached, and on the
re-enumeration after a firmware flash. No polling for device presence, no
per-event process spawn, native startup (~0 ms), and no Python/`hid`/venv
dependency.

Matching only the vendor usage page keeps it off the protected keyboard usage,
so macOS does not require an Input Monitoring grant. **Confirmed:** the daemon
runs under launchd and asserts on every attach with no TCC / Input Monitoring
prompt.

### Privacy blackout on macOS secure input

macOS engages **secure input** (`EnableSecureEventInput`) when a password field
is focused — that's the OS-level mode that stops apps from observing keystrokes.
The daemon watches that state and sends the firmware's `HOSTCMD_SET_PRIVACY`
(`0xA2`) command, which forces **all LEDs off** (overriding both local effects
*and* host/OpenRGB frames) while it's active, then restores normal RGB when it
clears. This stops reactive/heatmap effects from revealing which keys you press
while typing a password — handy in public.

There is no public notification for secure-input changes, so this is the one
thing that must be polled: a single cheap `IsSecureEventInputEnabled()` call on
a 0.5 s timer (`SECURE_INPUT_POLL_SEC`). A replug while secure input is active
re-applies the blackout. **Confirmed** end-to-end (state change → `0xA2` write).

> A previous Python version (`mac_layout_assert.py` + launchd
> `com.apple.iokit.matching`) also works but was slower: per-event interpreter
> startup plus a poll-retry loop, because launchd's USB match fires before the
> raw HID interface is ready. The Swift daemon supersedes it; the Python files
> can be deleted.

## Build
```sh
swiftc -O ducky-maclayout.swift -o ducky-maclayout -framework Foundation -framework IOKit -framework Carbon
```
Source is committed; the binary is not — build it locally (it's arch-specific).
For a portable binary add `-target arm64-apple-macos12` / build a universal
binary with `lipo` if you want to ship one.

## Install
```sh
# build first (above), then:
cp com.illixion.ducky-maclayout.plist ~/Library/LaunchAgents/
launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/com.illixion.ducky-maclayout.plist
```
The plist points `ProgramArguments` at the built binary in this repo dir; update
the path if you move the repo or install the binary elsewhere.

## Test
```sh
# one-shot against an already-attached board:
./ducky-maclayout --oneshot          # -> "asserted mac layout"

# confirm the agent is loaded and running:
launchctl print gui/$(id -u)/com.illixion.ducky-maclayout | grep -E 'state|pid'
cat ~/Library/Logs/ducky-maclayout.log

# real test: unplug/replug -> a fresh "asserted mac layout" line appears instantly
```

`./ducky-maclayout --win` asserts the Windows layout instead (the default is Mac).

## Reload after rebuilding or editing the plist
```sh
launchctl bootout gui/$(id -u)/com.illixion.ducky-maclayout 2>/dev/null
cp com.illixion.ducky-maclayout.plist ~/Library/LaunchAgents/
launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/com.illixion.ducky-maclayout.plist
```
(Rebuilding the binary in place is picked up on the next KeepAlive restart, but a
bootout/bootstrap is the clean way.)

## Uninstall
```sh
launchctl bootout gui/$(id -u)/com.illixion.ducky-maclayout
rm ~/Library/LaunchAgents/com.illixion.ducky-maclayout.plist
```

## Notes
- The keyboard keeps state in RAM across macOS sleep/wake (USB stays powered),
  so no wake hook is needed — only attach events matter.
- Manual override still works: the `MACWIN_TOGG` key flips the layout by hand at
  any time (it also sets `layout_locked`).
