// Test driver appended to a temporary copy of the desktop presentation file.
// It uses the real controls, constraints, page transitions, window and sheets.
// No AppDelegate or UsageMonitor is created, and no device command is invoked.
import Cocoa
import QuartzCore

final class ReviewWindow: NSWindow {
    override func constrainFrameRect(_ frameRect: NSRect, to screen: NSScreen?) -> NSRect { frameRect }
}

final class LayoutCaptureDelegate: NSObject, NSApplicationDelegate {
    func applicationDidFinishLaunching(_ notification: Notification) {
        DispatchQueue.main.async {
            do { try LayoutReview().run(); exit(0) }
            catch { fputs("Layout capture failed: \(error)\n", stderr); exit(1) }
        }
    }
}

private struct CaptureError: Error { let reason: String; init(_ reason: String) { self.reason = reason } }

private final class LayoutReview {
    private let env = ProcessInfo.processInfo.environment
    private var controller: SettingsWindowController!
    private var results: [String: Any] = [:]
    private var output: URL { URL(fileURLWithPath: env["ECHODESK_VISUAL_OUTPUT"]!, isDirectory: true) }
    private var requestedSize: NSSize {
        let parts = env["ECHODESK_VISUAL_SIZE"]!.split(separator: "x").map { Int($0)! }
        return NSSize(width: parts[0], height: parts[1])
    }

    func run() throws {
        UserDefaults.standard.set(false, forKey: "EchoDeskFirstRunOnboardingCompletedV1")
        UserDefaults.standard.set(0, forKey: "EchoDeskGuidedSetupStepV2")
        let appearance = NSAppearance(named: env["ECHODESK_VISUAL_THEME"] == "dark" ? .darkAqua : .aqua)!
        NSApp.appearance = appearance
        controller = SettingsWindowController()
        guard let window = controller.window else { throw CaptureError("No desktop window") }
        window.appearance = appearance
        window.makeKeyAndOrderFront(nil)
        window.setFrame(NSRect(origin: .zero, size: requestedSize), display: true)
        guard window.frame.size == requestedSize else {
            throw CaptureError("Requested \(requestedSize); got \(window.frame)")
        }
        guard controller.echoDeskFirstRunOverlay != nil else { throw CaptureError("No onboarding window") }
        try shot("first-launch")
        controller.echoDeskFirstRunContinue()
        try shot("first-device")
        controller.echoDeskFirstRunNotNow()

        // Representative fixture values are applied to existing UI bindings,
        // never to the live transport. Device operations cannot run here.
        controller.echoDeskConnectionLabel?.stringValue = "CONNECTED"
        controller.echoDeskConnectionDot?.state = .ok
        controller.echoDeskSidebarConnectionDot?.state = .ok
        controller.echoDeskSidebarConnectionLabel?.stringValue = "CONNECTED"
        controller.echoDeskSidebarDeviceName?.stringValue = "EchoDesk Mini"
        controller.echoDeskFooterStatus?.stringValue = "/dev/cu.usbserial-fixture"
        controller.echoDeskConnectionDetail?.stringValue = "EchoDesk is connected through USB."
        controller.echoDeskFirmwareLabel?.stringValue = "EchoDesk OS 2.33.0"
        controller.echoDeskHardwareLabel?.stringValue = "EchoDesk Mini"
        controller.echoDeskActiveAppLabel?.stringValue = "Bedside Mode"
        controller.echoDeskCommandStatus?.stringValue = "Connected and ready. Commands are confirmed by EchoDesk."
        controller.echoDeskDeviceControlStatus?.stringValue = "Connected to EchoDesk Mini through USB."
        controller.echoDeskComponentStatus?.stringValue = "ACTIVE"
        controller.echoDeskComponentDetail?.stringValue = "Codex usage is being sent to EchoDesk."
        controller.echoDeskComponentUpdated?.stringValue = "Last successful update: a minute ago"
        controller.echoDeskAppButtons.values.flatMap { $0 }.forEach { $0.isEnabled = true }
        try page(.overview, "overview")
        try scrolled("overview-lower")
        try page(.deviceControl, "device-control")
        try scrolled("device-maintenance")
        // Always cancel sheets. No destructive action is performed.
        controller.echoDeskRestart()
        try sheet("confirm-restart")
        controller.echoDeskFactoryReset()
        try sheet("confirm-reset")

        try page(.customisation, "customisation")
        for key in controller.echoDeskCustomisationSections.keys.sorted() {
            let button = NSButton()
            button.identifier = NSUserInterfaceItemIdentifier(key)
            controller.echoDeskOpenCustomisationSection(button)
            try shot("settings-" + key)
            controller.echoDeskCloseCustomisationSection()
        }
        try page(.bedside, "bedside")
        controller.echoDeskBedsideConfig.alarms = [
            EchoDeskBedsideAlarm(id: 1, enabled: true, minuteOfDay: 420, weekdays: 62, label: "Weekday alarm")
        ]
        _ = controller.perform(NSSelectorFromString("echoDeskBedsideShowAlarms"))
        try shot("bedside-alarms")
        _ = controller.perform(NSSelectorFromString("echoDeskBedsideShowDisplay"))
        try shot("bedside-display")
        try scrolled("bedside-display-lower")
        controller.selectEchoDeskPage(.setup)
        let steps: [(EchoDeskSetupStep, String)] = [(.connect, "connect"), (.usbDevice, "usb"),
            (.wifi, "wifi"), (.choice, "install-choice"), (.working, "install-progress"),
            (.quickSettings, "quick-settings"), (.welcome, "ready")]
        for (step, name) in steps {
            controller.setEchoDeskSetupStep(step)
            if step == .usbDevice {
                controller.echoDeskSetupUSBPopup.removeAllItems()
                controller.echoDeskSetupUSBPopup.addItem(withTitle: "EchoDesk Mini")
                controller.echoDeskSetupDeviceName.stringValue = "EchoDesk Mini"
                controller.echoDeskSetupDeviceFirmware.stringValue = "EchoDesk OS 2.33.0"
                controller.echoDeskSetupDeviceSerial.stringValue = "/dev/cu.usbserial-fixture"
            }
            try shot("setup-" + name)
        }
        controller.selectEchoDeskPage(.recovery)
        controller.echoDeskRecoveryDevicePopup.addItem(withTitle: "EchoDesk Mini — /dev/cu.usbserial-fixture")
        controller.echoDeskRecoveryDevicePopup.selectItem(at: controller.echoDeskRecoveryDevicePopup.numberOfItems - 1)
        controller.echoDeskRecoveryInstalledVersion.stringValue = "EchoDesk OS 2.33.0"
        controller.echoDeskRecoveryAvailableVersion.stringValue = "EchoDesk OS 2.33.0"
        controller.echoDeskRecoveryUpdateStatus.stringValue = "EchoDesk OS is up to date."
        try shot("recovery")
        try scrolled("recovery-maintenance")
        controller.echoDeskRecoveryCompletion.isHidden = false
        try scrolled("recovery-complete")
        controller.presentEchoDeskClockModeChooser()
        try dialog("clock-chooser")
        for mode in ["stopwatch", "countdown", "focus"] {
            controller.presentEchoDeskClockConfiguration(mode: mode)
            try dialog("clock-" + mode)
        }
        controller.closeEchoDeskClockWindow()
        let flash = FlashDialogController(info: "EchoDesk Mini · /dev/cu.usbserial-fixture",
            defaultVariant: kDisplayVariantST7789, preflightItems: ["Device identified", "Firmware verified"],
            warning: nil, canStart: true, s3Available: false)
        if let flashWindow = flash.window {
            flashWindow.appearance = appearance
            flashWindow.makeKeyAndOrderFront(nil)
            try shot("firmware-confirmation", window: flashWindow)
            flashWindow.orderOut(nil)
        }
        let data = try JSONSerialization.data(withJSONObject: results, options: [.prettyPrinted, .sortedKeys])
        try data.write(to: output.appendingPathComponent("geometry.json"))
    }

    private func page(_ page: EchoDeskDesktopPage, _ name: String) throws {
        controller.selectEchoDeskPage(page); try shot(name)
    }
    private func scrolled(_ name: String) throws {
        let page = controller.echoDeskPageViews[controller.echoDeskSelectedPage.rawValue]
        guard let scroll = page as? NSScrollView, let document = scroll.documentView else {
            throw CaptureError("Missing native scroll document: " + name)
        }
        settle()
        let bottom = max(0, document.bounds.height - scroll.contentView.bounds.height)
        scroll.contentView.scroll(to: NSPoint(x: 0, y: bottom))
        scroll.reflectScrolledClipView(scroll.contentView)
        try shot(name)
        scroll.contentView.scroll(to: .zero)
        scroll.reflectScrolledClipView(scroll.contentView)
    }
    private func sheet(_ name: String) throws {
        settle()
        guard let parent = controller.window, let sheet = parent.attachedSheet else {
            throw CaptureError("Missing native sheet: " + name)
        }
        try shot(name, window: sheet)
        parent.endSheet(sheet, returnCode: .cancel)
        settle()
    }
    private func dialog(_ name: String) throws {
        guard let window = controller.echoDeskClockWindow else { throw CaptureError("Missing Clock dialog") }
        try shot(name, window: window)
    }
    private func settle() {
        // Advance native transitions and deferred layout passes, like a visible app.
        let end = Date().addingTimeInterval(0.21)
        while Date() < end {
            if let event = NSApp.nextEvent(matching: .any, until: Date().addingTimeInterval(0.012),
                                           inMode: .default, dequeue: true) { NSApp.sendEvent(event) }
            RunLoop.main.run(until: Date().addingTimeInterval(0.002))
        }
    }
    private func shot(_ name: String, window override: NSWindow? = nil) throws {
        let window = override ?? controller.window!
        settle()
        guard let root = window.contentView?.superview ?? window.contentView else { throw CaptureError(name) }
        root.layoutSubtreeIfNeeded()
        window.displayIfNeeded()
        CATransaction.flush()
        guard let rep = root.bitmapImageRepForCachingDisplay(in: root.bounds) else { throw CaptureError(name) }
        root.cacheDisplay(in: root.bounds, to: rep)
        guard let png = rep.representation(using: .png, properties: [:]) else { throw CaptureError(name) }
        try png.write(to: output.appendingPathComponent(name + ".png"))

        var frames: [[String: Any]] = []
        func inspect(_ view: NSView) {
            guard !view.isHidden, view.alphaValue > 0.01 else { return }
            let r = view.convert(view.bounds, to: root)
            if let text = view as? NSTextField, !text.stringValue.isEmpty {
                frames.append(["text": text.stringValue, "x": r.minX, "y": r.minY,
                               "width": r.width, "height": r.height])
            } else if let button = view as? NSButton, !button.title.isEmpty {
                frames.append(["button": button.title, "x": r.minX, "y": r.minY,
                               "width": r.width, "height": r.height])
            }
            view.subviews.forEach(inspect)
        }
        inspect(root)
        results[name] = ["windowWidth": window.frame.width, "windowHeight": window.frame.height,
                         "pixelsWide": rep.pixelsWide, "pixelsHigh": rep.pixelsHigh, "items": frames]
        print("Captured \(name) (\(rep.pixelsWide)×\(rep.pixelsHigh))")
    }
}
