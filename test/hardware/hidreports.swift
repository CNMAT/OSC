// hidreports.swift: print every raw HID input report a device sends, by
// report ID, as macOS delivers it to IOHIDDeviceRegisterInputReportCallback.
// Chrome's WebHID hides reports from Mouse/Keyboard collections; this does not.
//
//   swiftc -O -o /tmp/hidreports test/hardware/hidreports.swift
//   /tmp/hidreports 0x0955 0x7210 60        # VID PID seconds
//
// Prints one line per report: seconds since start, report ID, length, hex.
// Ends with a count per report ID. Exit 2 if the device is not found, 3 if it
// cannot be opened (e.g. macOS Input Monitoring permission denied).
import Foundation
import IOKit.hid

let args = CommandLine.arguments
guard args.count >= 3, let vid = Int(args[1].replacingOccurrences(of: "0x", with: ""), radix: 16),
      let pid = Int(args[2].replacingOccurrences(of: "0x", with: ""), radix: 16) else {
    FileHandle.standardError.write("usage: hidreports VID PID [seconds]\n".data(using: .utf8)!)
    exit(64)
}
let seconds = args.count >= 4 ? (Double(args[3]) ?? 30) : 30

let manager = IOHIDManagerCreate(kCFAllocatorDefault, IOOptionBits(kIOHIDOptionsTypeNone))
IOHIDManagerSetDeviceMatching(manager, [kIOHIDVendorIDKey: vid, kIOHIDProductIDKey: pid] as CFDictionary)
IOHIDManagerScheduleWithRunLoop(manager, CFRunLoopGetCurrent(), CFRunLoopMode.defaultMode.rawValue)
let openResult = IOHIDManagerOpen(manager, IOOptionBits(kIOHIDOptionsTypeNone))

guard let devices = IOHIDManagerCopyDevices(manager) as? Set<IOHIDDevice>, let device = devices.first else {
    print(String(format: "no device %04x:%04x (manager open 0x%08x)", vid, pid, openResult))
    exit(2)
}
let product = IOHIDDeviceGetProperty(device, kIOHIDProductKey as CFString) as? String ?? "?"
let maxIn = IOHIDDeviceGetProperty(device, kIOHIDMaxInputReportSizeKey as CFString) as? Int ?? 64
let devOpen = IOHIDDeviceOpen(device, IOOptionBits(kIOHIDOptionsTypeNone))
print(String(format: "device %04x:%04x \"%@\" maxInput %d, manager open 0x%08x, device open 0x%08x", vid, pid, product, maxIn, openResult, devOpen))
if devOpen != kIOReturnSuccess { exit(3) }

let t0 = Date()
var counts: [UInt32: Int] = [:]
let buffer = UnsafeMutablePointer<UInt8>.allocate(capacity: maxIn)
let callback: IOHIDReportCallback = { _, _, _, _, reportID, report, length in
    counts[reportID, default: 0] += 1
    let hex = (0..<length).map { String(format: "%02x", report[$0]) }.joined(separator: " ")
    print(String(format: "%8.3f id %3u len %2d: ", Date().timeIntervalSince(t0), reportID, length) + hex)
    fflush(stdout)
}
IOHIDDeviceRegisterInputReportCallback(device, buffer, maxIn, callback, nil)
print("listening \(seconds) s"); fflush(stdout)
CFRunLoopRunInMode(CFRunLoopMode.defaultMode, seconds, false)
print("counts by report ID: " + counts.sorted { $0.key < $1.key }.map { "\($0.key): \($0.value)" }.joined(separator: ", "))
