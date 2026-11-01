import Cocoa
import CoreAudio
import Darwin
import Foundation
import IOKit.ps

/// Native macOS telemetry for EchoDesk. Collectors never open the serial port;
/// they publish bounded v1 envelopes back to UsageMonitor, the single owner of
/// the device transport.
final class EchoDeskTelemetry {
    typealias Publish = (_ kind: String, _ payload: [String: Any]) -> Void

    private let publish: Publish
    private let queue = DispatchQueue(label: "com.echodesk.telemetry", qos: .utility)
    private var performanceTimer: DispatchSourceTimer?
    private var musicTimer: DispatchSourceTimer?
    private var performance = MacPerformanceCollector()
    private var pcDashboard = MacPcDashboardCollector()
    private var music = MacNowPlayingCollector()

    init(publish: @escaping Publish) {
        self.publish = publish
    }

    func start() {
        guard performanceTimer == nil, musicTimer == nil else { return }

        let performanceTimer = DispatchSource.makeTimerSource(queue: queue)
        performanceTimer.schedule(deadline: .now(), repeating: 1.0, leeway: .milliseconds(100))
        performanceTimer.setEventHandler { [weak self] in
            guard let self, let sample = self.performance.sample() else { return }
            self.publish("performance", sample)
            if let dashboard = self.pcDashboard.sample() {
                self.publish("pc_dashboard_sample", dashboard)
            }
        }
        self.performanceTimer = performanceTimer
        performanceTimer.resume()

        let musicTimer = DispatchSource.makeTimerSource(queue: queue)
        musicTimer.schedule(deadline: .now(), repeating: 1.0, leeway: .milliseconds(150))
        musicTimer.setEventHandler { [weak self] in
            guard let self else { return }
            self.publish("music", self.music.sample())
        }
        self.musicTimer = musicTimer
        musicTimer.resume()
    }

    func stop() {
        performanceTimer?.cancel()
        performanceTimer = nil
        musicTimer?.cancel()
        musicTimer = nil
    }

    func handleMusicControl(_ message: [String: Any]) {
        queue.async { [weak self] in self?.music.handleControl(message) }
    }
}

private struct MacPcDashboardCollector {
    func sample() -> [String: Any]? {
        var result: [String: Any] = [
            "type": "pc_dashboard_sample",
            "version": 1,
            "timestamp": max(1, Int(Date().timeIntervalSince1970)),
        ]
        if let battery = battery() {
            result["batteryPct"] = battery.percent
            result["charging"] = battery.charging
        }
        if let microphone = microphoneInUse() { result["microphoneInUse"] = microphone }
        if let storage = storage() {
            result["storageUsedBytes"] = storage.used
            result["storageTotalBytes"] = storage.total
        }
        return result.count > 3 ? result : nil
    }

    private func battery() -> (percent: Double, charging: Bool)? {
        guard let info = IOPSCopyPowerSourcesInfo()?.takeRetainedValue(),
              let list = IOPSCopyPowerSourcesList(info)?.takeRetainedValue() as? [CFTypeRef] else { return nil }
        for source in list {
            guard let description = IOPSGetPowerSourceDescription(info, source)?.takeUnretainedValue() as? [String: Any],
                  let current = description[kIOPSCurrentCapacityKey] as? Double,
                  let maximum = description[kIOPSMaxCapacityKey] as? Double,
                  maximum > 0 else { continue }
            let charging = description[kIOPSIsChargingKey] as? Bool ?? false
            return (min(100, max(0, current * 100 / maximum)), charging)
        }
        return nil
    }

    private func storage() -> (used: UInt64, total: UInt64)? {
        guard let values = try? URL(fileURLWithPath: "/").resourceValues(forKeys: [.volumeTotalCapacityKey, .volumeAvailableCapacityForImportantUsageKey]),
              let totalValue = values.volumeTotalCapacity, totalValue > 0,
              let availableValue = values.volumeAvailableCapacityForImportantUsage else { return nil }
        let total = UInt64(totalValue)
        let available = min(total, UInt64(max(0, availableValue)))
        return (total - available, total)
    }

    private func microphoneInUse() -> Bool? {
        var address = AudioObjectPropertyAddress(
            mSelector: kAudioHardwarePropertyDefaultInputDevice,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain)
        var device = AudioDeviceID(0)
        var size = UInt32(MemoryLayout<AudioDeviceID>.size)
        guard AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &address, 0, nil, &size, &device) == noErr,
              device != kAudioObjectUnknown else { return nil }
        address = AudioObjectPropertyAddress(
            mSelector: kAudioDevicePropertyDeviceIsRunningSomewhere,
            mScope: kAudioDevicePropertyScopeInput,
            mElement: kAudioObjectPropertyElementMain)
        var running: UInt32 = 0
        size = UInt32(MemoryLayout<UInt32>.size)
        guard AudioObjectGetPropertyData(device, &address, 0, nil, &size, &running) == noErr else { return nil }
        return running != 0
    }
}

private struct MacPerformanceCollector {
    private var previousCPUTicks: [UInt64]?
    private var previousNetwork: (received: UInt64, sent: UInt64, date: Date)?

    mutating func sample() -> [String: Any]? {
        let cpu = sampleCPU()
        let memory = sampleMemory()
        let network = sampleNetwork()
        guard cpu != nil || memory != nil || network != nil else { return nil }

        return [
            "type": "performance",
            "version": 1,
            "timestamp": max(1, Int(Date().timeIntervalSince1970)),
            "cpuPct": jsonValue(cpu),
            "gpuPct": NSNull(),
            "ramPct": jsonValue(memory?.percent),
            "ramUsedBytes": jsonValue(memory?.used),
            "ramTotalBytes": jsonValue(memory?.total),
            "network": [
                "downBps": jsonValue(network?.down),
                "upBps": jsonValue(network?.up),
            ],
            // macOS has no public API for system-wide CPU/GPU temperatures.
            "temperature": [
                "cpuC": NSNull(),
                "gpuC": NSNull(),
                "motherboardC": NSNull(),
            ],
        ]
    }

    private func jsonValue<T>(_ value: T?) -> Any {
        if let value { return value }
        return NSNull()
    }

    private mutating func sampleCPU() -> Double? {
        var info = host_cpu_load_info()
        var count = mach_msg_type_number_t(
            MemoryLayout<host_cpu_load_info_data_t>.stride / MemoryLayout<integer_t>.stride)
        let result = withUnsafeMutablePointer(to: &info) { pointer in
            pointer.withMemoryRebound(to: integer_t.self, capacity: Int(count)) {
                host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, $0, &count)
            }
        }
        guard result == KERN_SUCCESS else { return nil }
        let ticks = [
            UInt64(info.cpu_ticks.0), UInt64(info.cpu_ticks.1),
            UInt64(info.cpu_ticks.2), UInt64(info.cpu_ticks.3),
        ]
        defer { previousCPUTicks = ticks }
        guard let previous = previousCPUTicks else { return nil }
        let deltas = zip(ticks, previous).map { current, old in current >= old ? current - old : 0 }
        let total = deltas.reduce(0, +)
        guard total > 0 else { return nil }
        let idle = deltas[Int(CPU_STATE_IDLE)]
        return min(100, max(0, Double(total - idle) * 100 / Double(total)))
    }

    private func sampleMemory() -> (used: UInt64, total: UInt64, percent: Double)? {
        var stats = vm_statistics64()
        var count = mach_msg_type_number_t(HOST_VM_INFO64_COUNT)
        let result = withUnsafeMutablePointer(to: &stats) { pointer in
            pointer.withMemoryRebound(to: integer_t.self, capacity: Int(count)) {
                host_statistics64(mach_host_self(), HOST_VM_INFO64, $0, &count)
            }
        }
        guard result == KERN_SUCCESS else { return nil }
        let total = ProcessInfo.processInfo.physicalMemory
        guard total > 0 else { return nil }
        let availablePages = UInt64(stats.free_count) + UInt64(stats.speculative_count)
        let available = min(total, availablePages * UInt64(vm_kernel_page_size))
        let used = total - available
        return (used, total, Double(used) * 100 / Double(total))
    }

    private mutating func sampleNetwork() -> (down: UInt64, up: UInt64)? {
        var head: UnsafeMutablePointer<ifaddrs>?
        guard getifaddrs(&head) == 0, let first = head else { return nil }
        defer { freeifaddrs(head) }

        var received: UInt64 = 0
        var sent: UInt64 = 0
        var cursor: UnsafeMutablePointer<ifaddrs>? = first
        while let interface = cursor {
            let flags = Int32(interface.pointee.ifa_flags)
            if flags & IFF_UP != 0, flags & IFF_LOOPBACK == 0,
               let data = interface.pointee.ifa_data?.assumingMemoryBound(to: if_data.self) {
                received &+= UInt64(data.pointee.ifi_ibytes)
                sent &+= UInt64(data.pointee.ifi_obytes)
            }
            cursor = interface.pointee.ifa_next
        }

        let now = Date()
        defer { previousNetwork = (received, sent, now) }
        guard let previous = previousNetwork else { return nil }
        let elapsed = now.timeIntervalSince(previous.date)
        guard elapsed > 0, received >= previous.received, sent >= previous.sent else { return nil }
        return (
            UInt64(Double(received - previous.received) / elapsed),
            UInt64(Double(sent - previous.sent) / elapsed))
    }
}

private struct MacNowPlayingCollector {
    private struct Snapshot {
        let state: String
        let title: String
        let artist: String
        let album: String
        let application: String
        let positionMs: Int
        let durationMs: Int
        let bundleIdentifier: String

        var rank: Int { state == "playing" ? 2 : (state == "paused" ? 1 : 0) }
        var signature: String { "\(bundleIdentifier)\u{1f}\(title)\u{1f}\(artist)\u{1f}\(album)" }
    }

    private var generation = 1
    private var lastSignature = ""
    private var activeBundleIdentifier: String?

    mutating func sample() -> [String: Any] {
        let candidates = [queryMusic(), querySpotify()].compactMap { $0 }
        let current = candidates.sorted {
            if $0.rank != $1.rank { return $0.rank > $1.rank }
            if $0.bundleIdentifier == activeBundleIdentifier { return true }
            if $1.bundleIdentifier == activeBundleIdentifier { return false }
            return $0.application.localizedCaseInsensitiveCompare($1.application) == .orderedAscending
        }.first

        if let current {
            if current.signature != lastSignature {
                generation &+= 1
                lastSignature = current.signature
            }
            activeBundleIdentifier = current.bundleIdentifier
        } else {
            activeBundleIdentifier = nil
            lastSignature = ""
        }

        return [
            "type": "music",
            "version": 1,
            "timestamp": max(1, Int(Date().timeIntervalSince1970)),
            "generation": generation,
            "state": current?.state ?? "stopped",
            "title": current?.title ?? "",
            "artist": current?.artist ?? "",
            "album": current?.album ?? "",
            "application": current?.application ?? "",
            "positionMs": current?.positionMs ?? 0,
            "durationMs": current?.durationMs ?? 0,
            "canPlayPause": current != nil,
            "canPrevious": current != nil,
            "canNext": current != nil,
            "artId": "",
            "backgroundId": "",
        ]
    }

    mutating func handleControl(_ message: [String: Any]) {
        guard let requestedGeneration = (message["generation"] as? NSNumber)?.intValue,
              requestedGeneration == generation,
              let action = message["action"] as? String,
              ["toggle", "previous", "next"].contains(action),
              let bundleIdentifier = activeBundleIdentifier else { return }

        let application: String
        if bundleIdentifier == "com.apple.Music" { application = "Music" }
        else if bundleIdentifier == "com.spotify.client" { application = "Spotify" }
        else { return }

        let command: String
        switch action {
        case "toggle": command = "playpause"
        case "previous": command = "previous track"
        default: command = "next track"
        }
        _ = runAppleScript("tell application \"\(application)\" to \(command)")
    }

    private func queryMusic() -> Snapshot? {
        guard !NSRunningApplication.runningApplications(withBundleIdentifier: "com.apple.Music").isEmpty else {
            return nil
        }
        let script = """
        tell application "Music"
          set playbackState to (player state as text)
          if playbackState is "stopped" then return {playbackState, "", "", "", 0, 0}
          set mediaTrack to current track
          return {playbackState, (name of mediaTrack as text), (artist of mediaTrack as text), (album of mediaTrack as text), (player position as real), (duration of mediaTrack as real)}
        end tell
        """
        return decode(runAppleScript(script), application: "Music", bundleIdentifier: "com.apple.Music",
                      durationIsMilliseconds: false)
    }

    private func querySpotify() -> Snapshot? {
        guard !NSRunningApplication.runningApplications(withBundleIdentifier: "com.spotify.client").isEmpty else {
            return nil
        }
        let script = """
        tell application "Spotify"
          set playbackState to (player state as text)
          if playbackState is "stopped" then return {playbackState, "", "", "", 0, 0}
          set mediaTrack to current track
          return {playbackState, (name of mediaTrack as text), (artist of mediaTrack as text), (album of mediaTrack as text), (player position as real), (duration of mediaTrack as integer)}
        end tell
        """
        return decode(runAppleScript(script), application: "Spotify", bundleIdentifier: "com.spotify.client",
                      durationIsMilliseconds: true)
    }

    private func decode(_ descriptor: NSAppleEventDescriptor?, application: String,
                        bundleIdentifier: String, durationIsMilliseconds: Bool) -> Snapshot? {
        guard let descriptor, descriptor.numberOfItems >= 6 else { return nil }
        let rawState = descriptor.atIndex(1)?.stringValue?.lowercased() ?? "stopped"
        let state = rawState == "playing" ? "playing" : (rawState == "paused" ? "paused" : "stopped")
        let position = max(0, descriptor.atIndex(5)?.doubleValue ?? 0)
        let rawDuration = max(0, descriptor.atIndex(6)?.doubleValue ?? 0)
        return Snapshot(
            state: state,
            title: bounded(descriptor.atIndex(2)?.stringValue, maximum: 96),
            artist: bounded(descriptor.atIndex(3)?.stringValue, maximum: 96),
            album: bounded(descriptor.atIndex(4)?.stringValue, maximum: 96),
            application: application,
            positionMs: Int(min(Double(UInt32.max), position * 1000)),
            durationMs: Int(min(Double(UInt32.max), durationIsMilliseconds ? rawDuration : rawDuration * 1000)),
            bundleIdentifier: bundleIdentifier)
    }

    private func bounded(_ value: String?, maximum: Int) -> String {
        String((value ?? "").replacingOccurrences(of: "\n", with: " ").prefix(maximum))
    }

    private func runAppleScript(_ source: String) -> NSAppleEventDescriptor? {
        var error: NSDictionary?
        let result = NSAppleScript(source: source)?.executeAndReturnError(&error)
        if let error { NSLog("[Music] AppleScript unavailable: %@", error.description) }
        return result
    }
}
