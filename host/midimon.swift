// Minimal Core MIDI monitor: connects to all sources and prints every message.
// Build: swiftc -O host/midimon.swift -o /tmp/midimon
// Run:   /tmp/midimon   (Ctrl-C to stop)
import CoreMIDI
import Foundation

// Monotonic seconds since launch, prefixed on every line so a raw-MIDI capture can
// be lined up against a plugin trace (make trace) when diagnosing jog touch.
let t0 = DispatchTime.now().uptimeNanoseconds
func stamp() -> String {
    let s = Double(DispatchTime.now().uptimeNanoseconds - t0) / 1_000_000_000.0
    return String(format: "%8.4f", s)
}

var client = MIDIClientRef()
MIDIClientCreate("scmon" as CFString, nil, nil, &client)

var inPort = MIDIPortRef()
MIDIInputPortCreateWithBlock(client, "scin" as CFString, &inPort) { (pktList, _) in
    // Correct multi-packet iteration (the naive var+MIDIPacketNext loop reads
    // stack garbage when a list carries many packets).
    for packet in pktList.unsafeSequence() {
        let len = min(Int(packet.pointee.length), 256)  // data field is 256 bytes; don't over-read coalesced packets
        withUnsafeBytes(of: packet.pointee.data) { raw in
            var s = ""
            for i in 0..<len { s += String(format: "%02X ", raw[i]) }
            print("\(stamp()) MIDI: \(s)")
        }
    }
    fflush(stdout)
}

let count = MIDIGetNumberOfSources()
print("CoreMIDI sources: \(count)")
for i in 0..<count {
    let src = MIDIGetSource(i)
    var nameRef: Unmanaged<CFString>?
    MIDIObjectGetStringProperty(src, kMIDIPropertyDisplayName, &nameRef)
    let name = nameRef?.takeRetainedValue() as String? ?? "(unknown)"
    print("  [\(i)] \(name)")
    MIDIPortConnectSource(inPort, src, nil)
}
print("listening — move ONE control at a time...")
fflush(stdout)
CFRunLoopRun()
