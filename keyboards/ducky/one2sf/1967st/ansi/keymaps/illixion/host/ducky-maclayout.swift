// ducky-maclayout — assert the Mac keyboard layout on a Ducky One 2 SF over raw HID.
//
// QMK's USB OS detection mis-fingerprints this Mac as Linux (macOS serves cached
// string descriptors, so it skips the 0x02 string-length probes the OS_MACOS
// heuristic needs). Instead of trusting the keyboard's guess, the Mac asserts the
// layout via the firmware's HOSTCMD_SET_LAYOUT (0xA1) raw-HID command, which also
// sets layout_locked so the bogus async detection result can't flip it back.
//
// Resident daemon: IOHIDManager fires a device-matching callback the moment the
// raw HID collection (usage page 0xFF60 / usage 0x61) is enumerated and openable
// — on plug-in, at login if already attached, and on the re-enumeration after a
// firmware flash. No polling, no per-event process spawn.
//
// Build:
//   swiftc -O ducky-maclayout.swift -o ducky-maclayout \
//       -framework Foundation -framework IOKit
//
// Test (one-shot against an already-attached board):
//   ./ducky-maclayout --oneshot
//
// Run resident (what launchd does):
//   ./ducky-maclayout

import Foundation
import IOKit
import IOKit.hid

let VENDOR_ID = 0x445B
let PRODUCT_ID = 0x07AF
let USAGE_PAGE = 0xFF60   // QMK raw HID
let USAGE = 0x61
let REPORT_LEN = 32       // RAW_EPSIZE

let HOSTCMD_SET_LAYOUT: UInt8 = 0xA1
let LAYOUT_MAC: UInt8 = 0x01
let LAYOUT_WIN: UInt8 = 0x00

let asWindows = CommandLine.arguments.contains("--win")
let oneShot = CommandLine.arguments.contains("--oneshot")

func log(_ s: String) {
    let ts = ISO8601DateFormatter().string(from: Date())
    if let d = "\(ts) \(s)\n".data(using: .utf8) {
        FileHandle.standardOutput.write(d)
    }
}

@discardableResult
func assertLayout(_ device: IOHIDDevice) -> Bool {
    var report = [UInt8](repeating: 0, count: REPORT_LEN)
    report[0] = HOSTCMD_SET_LAYOUT
    report[1] = asWindows ? LAYOUT_WIN : LAYOUT_MAC
    // Report ID 0; IOHIDDeviceSetReport takes the payload WITHOUT a leading id byte.
    let res = IOHIDDeviceSetReport(device, kIOHIDReportTypeOutput, 0, report, report.count)
    if res == kIOReturnSuccess {
        log("asserted \(asWindows ? "win" : "mac") layout")
        return true
    }
    log(String(format: "SetReport failed: 0x%08X", res))
    return false
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
    assertLayout(device)
    if oneShot { exit(0) }
}
IOHIDManagerRegisterDeviceMatchingCallback(manager, matchCallback, nil)
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

log("watching for Ducky One 2 SF (VID 0x445B / PID 0x07AF)…")
CFRunLoopRun()
