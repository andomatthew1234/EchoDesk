import Cocoa
import ScreenCaptureKit
import QuartzCore

final class ReviewWindow: NSWindow {
    override func constrainFrameRect(_ rect: NSRect, to screen: NSScreen?) -> NSRect { rect }
}

final class LayoutCaptureDelegate: NSObject, NSApplicationDelegate {
    func applicationDidFinishLaunching(_ notification: Notification) {
        Task { @MainActor in
            guard #available(macOS 26, *) else { exit(2) }
            do { try await CompositorProbe().run(); exit(0) }
            catch { fputs("Probe failed: \(error)\n", stderr); exit(1) }
        }
    }
}

@available(macOS 26, *)
@MainActor
private final class CompositorProbe {
    let folder = URL(fileURLWithPath: ProcessInfo.processInfo.environment["ECHODESK_VISUAL_OUTPUT"]!, isDirectory: true)
    private func failed(_ reason: String) -> NSError {
        NSError(domain: "EchoDeskProbe", code: 1, userInfo: [NSLocalizedDescriptionKey: reason])
    }
    // Apple limits this query to our own process even without screen capture permission.
    // Never request access, enumerate other windows or capture an entire display.
    private func ownContent() async throws -> SCShareableContent {
        try await withCheckedThrowingContinuation { continuation in
            SCShareableContent.getCurrentProcessShareableContent { content, error in
                if let error { continuation.resume(throwing: error) }
                else if let content { continuation.resume(returning: content) }
                else { continuation.resume(throwing: NSError(domain: "EchoDeskProbe", code: 2)) }
            }
        }
    }
    private func capture(_ window: NSWindow, as name: String) async throws {
        window.displayIfNeeded()
        CATransaction.flush()
        try await Task.sleep(nanoseconds: 400_000_000)
        let own = try await ownContent()
        guard let target = own.windows.first(where: {
            $0.windowID == CGWindowID(window.windowNumber) && $0.owningApplication?.processID == getpid()
        }) else { throw failed("Our own requested window was not supplied to ScreenCaptureKit.") }
        let filter = SCContentFilter(desktopIndependentWindow: target)
        let config = SCStreamConfiguration()
        config.width = Int(window.frame.width); config.height = Int(window.frame.height)
        config.includeChildWindows = true
        config.ignoreShadowsSingleWindow = true
        config.showsCursor = false
        let image = try await SCScreenshotManager.captureImage(contentFilter: filter, configuration: config)
        guard let png = NSBitmapImageRep(cgImage: image).representation(using: .png, properties: [:])
            else { throw failed("Unable to encode screenshot.") }
        try png.write(to: folder.appendingPathComponent(name + ".png"))
    }
    func run() async throws {
        UserDefaults.standard.set(true, forKey: "EchoDeskFirstRunOnboardingCompletedV1")
        let theme = ProcessInfo.processInfo.environment["ECHODESK_VISUAL_THEME"]
        let appearance = NSAppearance(named: theme == "dark" ? .darkAqua : .aqua)!
        NSApp.appearance = appearance
        let controller = SettingsWindowController()
        guard let window = controller.window else { throw failed("No desktop window") }
        NSApp.appearance = appearance; window.appearance = appearance
        window.setFrame(NSRect(x: 10, y: 10, width: 900, height: 650), display: true)
        NSApp.activate(ignoringOtherApps: true); window.makeKeyAndOrderFront(nil)
        controller.selectEchoDeskPage(.overview)
        var notes: [String] = []
        func record(_ win: NSWindow, _ name: String) async {
            do { try await capture(win, as: name); notes.append("\(name): OK") }
            catch {
                let e = error as NSError
                notes.append("\(name): \(e.domain) \(e.code): \(e.localizedDescription)")
            }
        }
        await record(window, "overview-composited")
        for name in ["restart", "reset"] {
            if name == "restart" { controller.echoDeskRestart() } else { controller.echoDeskFactoryReset() }
            try await Task.sleep(nanoseconds: 400_000_000)
            guard let sheet = window.attachedSheet else { throw failed("No attached \(name) sheet") }
            await record(window, name + "-parent-composited")
            await record(sheet, name + "-sheet-composited")
            window.endSheet(sheet, returnCode: .cancel)
            try await Task.sleep(nanoseconds: 300_000_000)
        }
        let report = notes.joined(separator: "\n") + "\n"
        print(report); try report.write(to: folder.appendingPathComponent("probe.txt"), atomically: true, encoding: .utf8)
    }
}
