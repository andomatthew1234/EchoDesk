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
        // The saved System preference can clear the test app appearance when
        // constructing the settings page. Apply the requested test appearance
        // to both the app (menus/toolbar) and window after that initialization.
        NSApp.appearance = appearance
        window.appearance = appearance
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        window.setFrame(NSRect(origin: .zero, size: requestedSize), display: true)
        guard window.frame.size == requestedSize else {
            throw CaptureError("Requested \(requestedSize); got \(window.frame)")
        }
        guard controller.echoDeskFirstRunOverlay != nil else { throw CaptureError("No onboarding window") }
        try shot("first-launch")
        controller.echoDeskFirstRunContinue()
        try shot("first-device")
        controller.echoDeskFirstRunNotNow()
        try page(.overview, "overview-disconnected")

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
        controller.echoDeskOverviewBrightness?.isEnabled = true
        controller.echoDeskDeviceBrightness?.isEnabled = true
        controller.echoDeskOverviewTheme?.isEnabled = true
        #if VISUAL_CONNECTED_PREVIEW
        controller.echoDeskOverviewPreview?.primaryText = "Bedside Mode"
        controller.echoDeskOverviewPreview?.secondaryText = "Open on your device"
        controller.echoDeskMaintenanceButtons.forEach { $0.isEnabled = true }
        #endif
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
        for key in Set(controller.echoDeskCustomisationSections.keys).union(["bedside"]).sorted() {
            controller.selectEchoDeskPage(.customisation)
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
                controller.echoDeskSetupDeviceModel.stringValue = "Model: EchoDesk Mini"
                #if VISUAL_USB_START
                controller.echoDeskSetupStartButton?.isEnabled = true
                #endif
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
        controller.echoDeskRecoveryStatus.stringValue = "Installation complete."
        try shot("recovery-complete")
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
        if requestedSize.width == 1440 { try exerciseResizing(window) }
    }

    /// Exercise the same window in both directions instead of testing only
    /// independent launches. Use the real sidebar action and native menu.
    private func exerciseResizing(_ window: NSWindow) throws {
        controller.selectEchoDeskPage(.customisation)
        let key = "clock"
        let category = NSButton()
        category.identifier = NSUserInterfaceItemIdentifier(key)
        controller.echoDeskOpenCustomisationSection(category)
        var passes: [[String: Any]] = []
        let sequence: [(Int, Int)] = [(900,650), (1100,750), (1280,800), (1440,900)]
        for (w,h) in sequence {
            let size = NSSize(width:w, height:h)
            window.setFrame(NSRect(origin:.zero, size:size), display:true)
            for hide in [false, true, false] {
                if controller.echoDeskSidebarView?.isHidden != hide { controller.echoDeskToggleSidebar() }
                settle()
                guard let view = controller.echoDeskCustomisationSections[key],
                      !view.isHidden, controller.echoDeskSelectedPage == .customisation,
                      let scroll = controller.echoDeskPageViews[EchoDeskDesktopPage.customisation.rawValue] as? NSScrollView else {
                    throw CaptureError("Resizing lost the selected Clock category")
                }
                let expected = CGFloat(w) - (hide ? 0 : (controller.echoDeskSidebarView?.bounds.width ?? 0))
                passes.append(["target":w,"settledWidth":window.frame.width,"sidebarHidden":hide,
                               "pageViewport":scroll.bounds.width])
                guard window.frame.size == size, abs(scroll.bounds.width - expected) < 1,
                      controller.echoDeskSidebarView?.isHidden == hide else {
                    throw CaptureError("Window or sidebar didn't settle at requested size: \(passes.last!)")
                }
            }
        }
        try JSONSerialization.data(withJSONObject: passes, options: [.prettyPrinted])
            .write(to: output.appendingPathComponent("live-resize.json"))
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
        var record: [String: Any] = ["requestedParentWidth": requestedSize.width, "requestedParentHeight": requestedSize.height,
                                     "windowWidth": window.frame.width, "windowHeight": window.frame.height,
                                     "pixelsWide": rep.pixelsWide, "pixelsHigh": rep.pixelsHigh, "items": frames]
        if override == nil && controller.echoDeskFirstRunOverlay == nil,
           let scroll = controller.echoDeskPageViews[controller.echoDeskSelectedPage.rawValue] as? NSScrollView,
           let document = scroll.documentView, let page = document.subviews.first {
            func position(_ view: NSView?) -> [String: CGFloat] {
                guard let view else { return [:] }
                let rect = view.convert(view.bounds, to: root)
                return ["x": rect.minX, "y": rect.minY, "width": rect.width, "height": rect.height]
            }
            record["pageLayout"] = ["sidebar": position(controller.echoDeskSidebarView),
                                    "stage": position(scroll.superview), "scroll": position(scroll),
                                    "clip": position(scroll.contentView), "document": position(document),
                                    "page": position(page)]
            let sidebar = controller.echoDeskSidebarView?.convert(controller.echoDeskSidebarView!.bounds, to: root)
            let pageRect = page.convert(page.bounds, to: root)
            if let sidebar, pageRect.minX - sidebar.maxX > max(56, window.frame.width * 0.08) {
                print("LARGE LEADING GAP: \(name), sidebar ends at \(sidebar.maxX); page starts at \(pageRect.minX)")
            }
        }
        results[name] = record
        if override == nil && requestedSize != window.frame.size {
            print("WINDOW GREW: \(name), requested \(requestedSize), settled \(window.frame.size)")
        }
        print("Captured \(name) (\(rep.pixelsWide)×\(rep.pixelsHigh))")
    }
}
