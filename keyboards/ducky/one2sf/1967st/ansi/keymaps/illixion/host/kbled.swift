// kbled — pin a colour to any key on the Ducky One 2 SF (illixion firmware).
//
//   kbled set <keys> <color> [--ttl SECONDS]   pin keys to a colour (e.g. status tracking)
//   kbled reset [<keys> | --all]               release keys back to the animation
//   kbled get <key>                            show a key's override
//   kbled list                                 every LED: index, matrix position, key name
//   kbled status                               firmware protocol status
//
// <keys>   comma separated names (esc,f1,a,lshift,...), "all", "led:12" or "matrix:0,0"
// <color>  red|green|blue|yellow|orange|purple|pink|cyan|white|off, RRGGBB, #RRGGBB or r,g,b
//
// Overrides are drawn after every animation and any OpenRGB frame, live in RAM only
// (a replug clears them) and need no authentication: they are cosmetic. The secure
// keystroke stream is a different, authenticated part of the protocol (not here).
//
// Run `kbled help` for the full reference.
//
// Build:  swiftc -O kbled.swift -o kbled -framework Foundation -framework IOKit
//
// Env:    KBLED_VID / KBLED_PID override the USB ids (default 0x445B / 0x07AF).

import Foundation
import IOKit
import IOKit.hid

let reportLen = 32
let vendorID = Int(ProcessInfo.processInfo.environment["KBLED_VID"] ?? "") ?? 0x445B
let productID = Int(ProcessInfo.processInfo.environment["KBLED_PID"] ?? "") ?? 0x07AF

let cmdStatus: UInt8 = 0xB0, cmdLedSet: UInt8 = 0xB1, cmdLedReset: UInt8 = 0xB2, cmdLedGet: UInt8 = 0xB3
let qmkdGetDevice: UInt8 = 0x06, qmkdGetLedMap: UInt8 = 0x07
let statusText = ["ok", "authentication failed", "replay", "unavailable", "bad arguments", "not active"]

func die(_ message: String) -> Never {
    FileHandle.standardError.write("kbled: \(message)\n".data(using: .utf8)!)
    exit(1)
}

// MARK: - Device

final class Link {
    let device: IOHIDDevice
    private let manager: IOHIDManager
    private var inbox = [[UInt8]]()
    private let buffer = UnsafeMutablePointer<UInt8>.allocate(capacity: reportLen)

    init() {
        manager = IOHIDManagerCreate(kCFAllocatorDefault, IOOptionBits(kIOHIDOptionsTypeNone))
        let match: [String: Any] = [kIOHIDVendorIDKey: vendorID, kIOHIDProductIDKey: productID,
                                    kIOHIDDeviceUsagePageKey: 0xFF60, kIOHIDDeviceUsageKey: 0x61]
        IOHIDManagerSetDeviceMatching(manager, match as CFDictionary)
        IOHIDManagerScheduleWithRunLoop(manager, CFRunLoopGetCurrent(), CFRunLoopMode.defaultMode.rawValue)
        guard IOHIDManagerOpen(manager, IOOptionBits(kIOHIDOptionsTypeNone)) == kIOReturnSuccess else {
            die("cannot open the HID manager")
        }
        guard let set = IOHIDManagerCopyDevices(manager) as? Set<IOHIDDevice>, let dev = set.first else {
            die(String(format: "keyboard %04x:%04x not found (is it plugged in?)", vendorID, productID))
        }
        device = dev
        IOHIDDeviceRegisterInputReportCallback(device, buffer, reportLen, { ctx, _, _, _, _, report, len in
            let me = Unmanaged<Link>.fromOpaque(ctx!).takeUnretainedValue()
            me.inbox.append(Array(UnsafeBufferPointer(start: report, count: len)))
        }, Unmanaged.passUnretained(self).toOpaque())
    }

    /// Send one 32-byte packet and wait for the response whose first byte matches.
    func request(_ bytes: [UInt8], timeout: TimeInterval = 1.0) -> [UInt8] {
        var report = bytes + [UInt8](repeating: 0, count: reportLen - bytes.count)
        inbox.removeAll()
        let r = IOHIDDeviceSetReport(device, kIOHIDReportTypeOutput, 0, &report, reportLen)
        guard r == kIOReturnSuccess else { die(String(format: "write failed (0x%08X)", r)) }
        let deadline = Date().addingTimeInterval(timeout)
        while Date() < deadline {
            RunLoop.current.run(mode: .default, before: Date().addingTimeInterval(0.01))
            if let i = inbox.firstIndex(where: { $0.first == bytes[0] }) { return inbox[i] }
            if bytes[0] >= 0xB0, bytes[0] <= 0xBF, inbox.contains(where: { $0.first == 0xFF }) {
                die("this firmware does not speak the FocusGuard protocol; flash the current build")
            }
        }
        die("no response (firmware without the FocusGuard protocol? flash the current build)")
    }
}

// MARK: - Key names

struct LedInfo { let index: Int; let row: Int; let col: Int; let keycode: Int?   // nil when not a basic key
}

var nameToKeycode: [String: Int] = {
    var m = [String: Int]()
    for (i, c) in "abcdefghijklmnopqrstuvwxyz".enumerated() { m[String(c)] = 0x04 + i }
    for (i, c) in "1234567890".enumerated() { m[String(c)] = 0x1E + i }
    let named: [(String, Int)] = [
        ("enter", 0x28), ("return", 0x28), ("bksp", 0x2A), ("backspace", 0x2A), ("tab", 0x2B), ("space", 0x2C),
        ("minus", 0x2D), ("equal", 0x2E), ("lbracket", 0x2F), ("rbracket", 0x30), ("backslash", 0x31),
        ("semicolon", 0x33), ("quote", 0x34), ("grave", 0x35), ("comma", 0x36), ("dot", 0x37), ("slash", 0x38),
        ("caps", 0x39), ("prtsc", 0x46), ("scrlk", 0x47), ("pause", 0x48), ("ins", 0x49), ("home", 0x4A),
        ("pgup", 0x4B), ("del", 0x4C), ("delete", 0x4C), ("end", 0x4D), ("pgdn", 0x4E),
        ("right", 0x4F), ("left", 0x50), ("down", 0x51), ("up", 0x52),
        ("lctrl", 0xE0), ("lshift", 0xE1), ("lalt", 0xE2), ("lgui", 0xE3),
        ("rctrl", 0xE4), ("rshift", 0xE5), ("ralt", 0xE6), ("rgui", 0xE7),
    ]
    for (n, k) in named { m[n] = k }
    for i in 1...12 { m["f\(i)"] = 0x39 + i }
    return m
}()

func keyName(_ kc: Int) -> String {
    nameToKeycode.filter { $0.value == kc }.keys.min { $0.count < $1.count } ?? String(format: "0x%02x", kc)
}

func loadLedMap(_ link: Link) -> [LedInfo] {
    let dev = link.request([qmkdGetDevice])
    let count = Int(dev[1])
    var leds = [LedInfo]()
    var start = 0
    while start < count {
        let r = link.request([qmkdGetLedMap, UInt8(start)])
        let n = Int(r[2])
        guard n > 0 else { break }
        for k in 0..<n {
            let o = 3 + k * 7
            let row = Int(r[o + 3]), col = Int(r[o + 4]), lo = Int(r[o + 5]), hi = Int(r[o + 6])
            leds.append(LedInfo(index: start + k, row: row, col: col, keycode: (row != 0xFF && hi == 0) ? lo : nil))
        }
        start += n
    }
    return leds
}

func resolveKeys(_ spec: [String], _ leds: [LedInfo]) -> [Int] {
    var out = [Int]()
    for token in spec.flatMap({ $0.split(separator: ",", omittingEmptySubsequences: true).map(String.init) }) {
        let t = token.lowercased()
        if t == "all" { out += leds.map { $0.index }; continue }
        if t.hasPrefix("led:"), let n = Int(t.dropFirst(4)), n < leds.count { out.append(n); continue }
        if t.hasPrefix("matrix:") {
            let rc = t.dropFirst(7).split(separator: ".").compactMap { Int($0) }
            // matrix:R.C  (a comma would clash with the list separator)
            if rc.count == 2, let l = leds.first(where: { $0.row == rc[0] && $0.col == rc[1] }) { out.append(l.index); continue }
        }
        if t == "esc" || t == "escape", let l = leds.first(where: { $0.row == 0 && $0.col == 0 }) { out.append(l.index); continue }
        if let kc = nameToKeycode[t], let l = leds.first(where: { $0.keycode == kc }) { out.append(l.index); continue }
        die("unknown key '\(token)' (try `kbled list`)")
    }
    return out
}

// MARK: - Colours

let namedColors: [String: (UInt8, UInt8, UInt8)] = [
    "red": (255, 0, 0), "green": (0, 255, 0), "blue": (0, 0, 255), "yellow": (255, 200, 0),
    "orange": (255, 90, 0), "purple": (150, 0, 255), "pink": (255, 40, 120), "cyan": (0, 220, 255),
    "white": (255, 255, 255), "off": (0, 0, 0), "black": (0, 0, 0),
]

func parseColor(_ s: String) -> (UInt8, UInt8, UInt8) {
    let t = s.lowercased()
    if let c = namedColors[t] { return c }
    let hex = t.hasPrefix("#") ? String(t.dropFirst()) : t
    if hex.count == 6, let v = UInt32(hex, radix: 16) { return (UInt8(v >> 16 & 255), UInt8(v >> 8 & 255), UInt8(v & 255)) }
    let parts = t.split(separator: ",").compactMap { UInt8($0) }
    if parts.count == 3 { return (parts[0], parts[1], parts[2]) }
    die("bad colour '\(s)'")
}

// MARK: - Commands

// MARK: - Help

let overviewHelp = """
kbled — pin a colour to any key on the Ducky One 2 SF (illixion firmware)

USAGE
  kbled <command> [arguments]

COMMANDS
  set <keys> <color> [--ttl SECONDS]   Pin one or more keys to a colour
  reset [<keys> | --all]               Release keys back to the animation (no args = all)
  get <key>                            Show a key's override and time left
  list                                 Every LED: index, matrix position, key name
  status                               Firmware protocol version, overrides, secure-relay state
  help [command]                       This text, or help for one command

KEYS    comma-separated, no spaces:  esc,a,lshift   (several arguments also work)
  names     a-z  0-9  esc  tab  caps  space  enter  bksp  del  ins  home  end  pgup  pgdn
            up  down  left  right  minus  equal  lbracket  rbracket  backslash  semicolon
            quote  grave  comma  dot  slash  f1-f12 (only if your layout has them)
            lctrl lshift lalt lgui   rctrl rshift ralt rgui
  all       every LED (keys and underglow)
  led:N     by LED index, e.g. led:12          (see `kbled list`)
  matrix:R.C  by matrix position, e.g. matrix:0.0
  Anything else is rejected with the name that failed; `kbled list` shows what exists.

COLORS  red green blue yellow orange purple pink cyan white off
        RRGGBB   #RRGGBB   r,g,b  (0-255 each, e.g. 255,90,0)
        "off" turns the key's LED black — it is still an override, use `reset` to hand it back.

EXAMPLES
  kbled set esc red                    # until you reset it
  kbled set a,s,d,f green --ttl 30     # clears itself after 30 s
  kbled set all off                    # blank the board (overrides every animation)
  kbled set matrix:0.0 '#ff8800'
  kbled reset esc                      # one key back to the animation
  kbled reset                          # everything back
  kbled get esc
  kbled list | grep -i shift

NOTES
  * Overrides draw over every animation and any OpenRGB frame, live in the keyboard's RAM
    only, and disappear on replug. Caps Lock / passthrough whites and the red Esc shown while
    the secure keystroke relay is open are drawn on top of them.
  * The RGB matrix must be on (Fn2+Z); with it off there is nothing to draw on.
  * No authentication: these commands are cosmetic. The keystroke relay is a separate,
    authenticated part of the protocol (docs/PROTOCOL.md) and is not controlled from here.

ENVIRONMENT
  KBLED_VID, KBLED_PID   USB ids of the keyboard (default 0x445B / 0x07AF), e.g. KBLED_PID=0x07AF

EXIT STATUS
  0 success   1 failed (no keyboard, old firmware, bad key/colour, firmware refused)   2 usage error
"""

let commandHelp: [String: String] = [
    "set": """
    kbled set <keys> <color> [--ttl SECONDS]

    Pin the given keys to a colour. Draws over every animation until reset (or until the
    optional TTL runs out; 1-65535 seconds). Setting an already-pinned key just changes it.

      kbled set esc red
      kbled set w,a,s,d cyan --ttl 120
      kbled set all off
      kbled set led:3 255,90,0

    See `kbled help` for key names and colour formats.
    """,
    "reset": """
    kbled reset [<keys> | --all]

    Hand keys back to the animation. With no arguments (or --all) every override is released.

      kbled reset            # everything
      kbled reset esc,a
    """,
    "get": """
    kbled get <key>

    Print the colour pinned to a key and how long it has left, or "no override".

      kbled get esc
    """,
    "list": """
    kbled list

    One line per LED: index, matrix row,col and the base-layer key name. Use it to find the
    name or index for `set`/`reset`. Keys the name table can't describe show as (non-basic
    key); address those with led:N or matrix:R.C.
    """,
    "status": """
    kbled status

    Protocol version, LED count, how many keys are pinned, and the secure-relay state
    (whether the firmware has a key and whether the stream is open right now).
    """,
]

func usageError(_ message: String) -> Never {
    FileHandle.standardError.write("kbled: \(message)\nTry `kbled help`.\n".data(using: .utf8)!)
    exit(2)
}

var args = Array(CommandLine.arguments.dropFirst())
if args.isEmpty { FileHandle.standardError.write((overviewHelp + "\n").data(using: .utf8)!); exit(2) }
if ["help", "-h", "--help"].contains(args[0]) {
    if args.count > 1, let text = commandHelp[args[1]] { print(text) }
    else if args.count > 1 { usageError("no help for '\(args[1])'") }
    else { print(overviewHelp) }
    exit(0)
}
let command = args.removeFirst()
guard commandHelp[command] != nil else { usageError("unknown command '\(command)'") }
if args.contains("-h") || args.contains("--help") { print(commandHelp[command]!); exit(0) }

let link = Link()

func checked(_ r: [UInt8], _ what: String) {
    if r[1] != 0 { die("\(what): \(r[1] < statusText.count ? statusText[Int(r[1])] : "error \(r[1])")") }
}

switch command {
case "status":
    let r = link.request([cmdStatus])
    checked(r, "status")
    var ctr: UInt64 = 0
    for i in 0..<6 { ctr |= UInt64(r[6 + i]) << (8 * UInt64(i)) }
    print("protocol v\(r[2]), \(r[4]) LEDs, \(r[5]) overridden")
    print("secure stream: \(r[3] & 1 != 0 ? "key provisioned" : "no key in this firmware build")\(r[3] & 2 != 0 ? ", ACTIVE" : "")")
    print("last accepted command counter: \(ctr)")

case "list":
    let leds = loadLedMap(link)
    print("LED  matrix  key")
    for l in leds {
        let pos = l.row == 0xFF ? "  -   " : String(format: "%2d,%-2d ", l.row, l.col)
        let name = l.keycode.map(keyName) ?? (l.row == 0 && l.col == 0 ? "esc (custom)" : (l.row == 0xFF ? "(underglow)" : "(non-basic key)"))
        print(String(format: "%3d  %@ %@", l.index, pos, name))
    }

case "set":
    var ttl = 0
    if let i = args.firstIndex(of: "--ttl"), i + 1 < args.count { ttl = Int(args[i + 1]) ?? 0; args.removeSubrange(i...(i + 1)) }
    guard args.count >= 2 else { usageError("set needs <keys> and <color>") }
    let color = parseColor(args.removeLast())
    let leds = loadLedMap(link)
    let targets = resolveKeys(args, leds)
    for chunk in stride(from: 0, to: targets.count, by: 7).map({ Array(targets[$0..<min($0 + 7, targets.count)]) }) {
        var pkt: [UInt8] = [cmdLedSet, UInt8(ttl >> 8 & 255), UInt8(ttl & 255), UInt8(chunk.count)]
        for led in chunk { pkt += [UInt8(led), color.0, color.1, color.2] }
        checked(link.request(pkt), "set")
    }

case "reset":
    if args.isEmpty || args == ["--all"] {
        checked(link.request([cmdLedReset, 0]), "reset")
    } else {
        let leds = loadLedMap(link)
        let targets = resolveKeys(args, leds)
        for chunk in stride(from: 0, to: targets.count, by: 20).map({ Array(targets[$0..<min($0 + 20, targets.count)]) }) {
            checked(link.request([cmdLedReset, UInt8(chunk.count)] + chunk.map { UInt8($0) }), "reset")
        }
    }

case "get":
    guard args.count == 1 else { usageError("get needs exactly one <key>") }
    let leds = loadLedMap(link)
    for led in resolveKeys(args, leds) {
        let r = link.request([cmdLedGet, UInt8(led)])
        checked(r, "get")
        if r[3] == 0 { print("LED \(led): no override") }
        else { print(String(format: "LED %d: #%02x%02x%02x%@", led, r[4], r[5], r[6],
                            (Int(r[7]) << 8 | Int(r[8])) > 0 ? " (expires in \(Int(r[7]) << 8 | Int(r[8]))s)" : "")) }
    }

default:
    break   // unreachable: unknown commands are rejected above
}
