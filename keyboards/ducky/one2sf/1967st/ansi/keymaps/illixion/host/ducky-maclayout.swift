// ducky-maclayout — host-authoritative helpers for a Ducky One 2 SF over raw HID.
//
// Two jobs, both because macOS knows things the keyboard can't reliably detect:
//
//  1. Layout: QMK's USB OS detection mis-fingerprints this Mac as Linux (macOS
//     serves cached string descriptors, so it skips the 0x02 string-length
//     probes the OS_MACOS heuristic needs). The Mac asserts the layout via the
//     firmware's HOSTCMD_SET_LAYOUT (0xA1) command, which also sets
//     layout_locked so the bogus async detection can't flip it back.
//
//  2. Privacy: macOS engages "secure input" when a password field is focused.
//     We watch that state and send HOSTCMD_SET_PRIVACY (0xA2) so the keyboard
//     blacks out all LEDs while it's active — reactive/heatmap RGB can't reveal
//     which keys you press (e.g. typing a password in public). There is no
//     public notification for secure-input changes, so this one is polled; it's
//     a single cheap C call (IsSecureEventInputEnabled) on a 0.5s timer.
//
// Resident daemon: IOHIDManager fires a device-matching callback the moment the
// raw HID collection (usage page 0xFF60 / usage 0x61) is enumerated and openable
// — on plug-in, at login if already attached, and on the re-enumeration after a
// firmware flash. No polling for device presence; native startup.
//
// Build:
//   swiftc -O ducky-maclayout.swift -o ducky-maclayout \
//       -framework Foundation -framework IOKit -framework Carbon
//
// Test (one-shot against an already-attached board):
//   ./ducky-maclayout --oneshot

import Foundation
import IOKit
import IOKit.hid
import Carbon   // IsSecureEventInputEnabled()

let VENDOR_ID = 0x445B
let PRODUCT_ID = 0x07AF
let USAGE_PAGE = 0xFF60   // QMK raw HID
let USAGE = 0x61
let REPORT_LEN = 32       // RAW_EPSIZE

let HOSTCMD_SET_LAYOUT: UInt8 = 0xA1
let HOSTCMD_SET_PRIVACY: UInt8 = 0xA2
let LAYOUT_MAC: UInt8 = 0x01
let LAYOUT_WIN: UInt8 = 0x00

let SECURE_INPUT_POLL_SEC = 0.5

let asWindows = CommandLine.arguments.contains("--win")
let oneShot = CommandLine.arguments.contains("--oneshot")
// Per-event lines (layout asserts, secure-input transitions) are gated behind
// debug: by default the log holds only a startup line and errors, so it never
// records a timeline of when password fields were focused. --oneshot implies
// verbose so the manual test tool still prints its result.
let verbose = CommandLine.arguments.contains("--verbose")
    || oneShot
    || ProcessInfo.processInfo.environment["DUCKY_DEBUG"] != nil

// The currently-attached keyboard's raw HID device, if any.
var currentDevice: IOHIDDevice?
// Last secure-input state we pushed to the keyboard.
var privacyActive = false

func log(_ s: String) {
    let ts = ISO8601DateFormatter().string(from: Date())
    if let d = "\(ts) \(s)\n".data(using: .utf8) {
        FileHandle.standardOutput.write(d)
    }
}

// Debug-only logging. Used for anything that could reveal behavioral data
// (layout asserts on each attach, secure-input transitions). Silent unless
// --verbose / --oneshot / DUCKY_DEBUG.
func vlog(_ s: String) {
    if verbose { log(s) }
}

@discardableResult
func sendCommand(_ device: IOHIDDevice, _ cmd: UInt8, _ value: UInt8, _ what: String) -> Bool {
    var report = [UInt8](repeating: 0, count: REPORT_LEN)
    report[0] = cmd
    report[1] = value
    // Report ID 0; IOHIDDeviceSetReport takes the payload WITHOUT a leading id byte.
    let res = IOHIDDeviceSetReport(device, kIOHIDReportTypeOutput, 0, report, report.count)
    if res == kIOReturnSuccess {
        vlog("\(what): ok")
        return true
    }
    log(String(format: "%@: SetReport failed 0x%08X", what, res))
    return false
}

// Assert everything the keyboard needs from the host for the current state.
func syncDevice(_ device: IOHIDDevice) {
    sendCommand(device, HOSTCMD_SET_LAYOUT, asWindows ? LAYOUT_WIN : LAYOUT_MAC,
                "assert \(asWindows ? "win" : "mac") layout")
    // Re-apply privacy so a replug during secure input stays blacked out.
    if privacyActive {
        sendCommand(device, HOSTCMD_SET_PRIVACY, 1, "privacy blackout (resync)")
    }
}

let manager = IOHIDManagerCreate(kCFAllocatorDefault, IOOptionBits(kIOHIDOptionsTypeNone))

// Match only the vendor raw collection (not the keyboard collection — keeps us
// off the protected keyboard usage page, so no Input Monitoring prompt).
let matching: [String: Any] = [
    kIOHIDVendorIDKey: VENDOR_ID,
    kIOHIDProductIDKey: PRODUCT_ID,
    kIOHIDPrimaryUsagePageKey: USAGE_PAGE,
    kIOHIDPrimaryUsageKey: USAGE,
]
IOHIDManagerSetDeviceMatching(manager, matching as CFDictionary)

let matchCallback: IOHIDDeviceCallback = { _, _, _, device in
    currentDevice = device
    syncDevice(device)
    if oneShot { exit(0) }
}
let removalCallback: IOHIDDeviceCallback = { _, _, _, device in
    if currentDevice == device { currentDevice = nil }
    vlog("keyboard detached")
}
IOHIDManagerRegisterDeviceMatchingCallback(manager, matchCallback, nil)
IOHIDManagerRegisterDeviceRemovalCallback(manager, removalCallback, nil)
IOHIDManagerScheduleWithRunLoop(manager, CFRunLoopGetCurrent(), CFRunLoopMode.defaultMode.rawValue)

let openRes = IOHIDManagerOpen(manager, IOOptionBits(kIOHIDOptionsTypeNone))
if openRes != kIOReturnSuccess {
    log(String(format: "IOHIDManagerOpen failed: 0x%08X", openRes))
    exit(3)
}

if oneShot {
    // Give the matching callback a moment to fire for an already-attached board,
    // then bail if nothing matched.
    log("one-shot: waiting for device…")
    let deadline = Date().addingTimeInterval(3)
    while Date() < deadline {
        if CFRunLoopRunInMode(.defaultMode, 0.1, true) == .stopped { break }
    }
    log("one-shot: device not found")
    exit(2)
}

// Poll macOS secure-input state; push changes to the keyboard. No public
// notification exists for this, so a light timer is the only option.
let secureInputTimer = Timer(timeInterval: SECURE_INPUT_POLL_SEC, repeats: true) { _ in
    let secure = IsSecureEventInputEnabled()
    if secure != privacyActive {
        privacyActive = secure
        vlog("secure input \(secure ? "ON" : "off")")
        if let d = currentDevice {
            sendCommand(d, HOSTCMD_SET_PRIVACY, secure ? 1 : 0,
                        "privacy \(secure ? "blackout" : "normal")")
        }
    }
}
RunLoop.current.add(secureInputTimer, forMode: .default)

log("watching for Ducky One 2 SF (VID 0x445B / PID 0x07AF) + macOS secure input…")
CFRunLoopRun()
