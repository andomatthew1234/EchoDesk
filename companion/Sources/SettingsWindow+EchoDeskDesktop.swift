#if ECHODESK_DESKTOP
import Cocoa

enum EchoDeskDesktopPage: Int, CaseIterable {
    case overview, deviceControl, customisation, setup, recovery
}

enum EchoDeskSetupStep: Int {
    case connect, usbDevice, wifi, choice, working, quickSettings, welcome
}

private final class EchoDeskSurfaceView: NSView {
    enum Kind { case sidebar, card, canvas }
    let kind: Kind
    init(_ kind: Kind) {
        self.kind = kind
        super.init(frame: .zero)
        wantsLayer = true
        layer?.cornerCurve = .continuous
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) is not supported") }
    override var wantsUpdateLayer: Bool { true }
    override func updateLayer() {
        switch kind {
        case .sidebar:
            layer?.backgroundColor = NSColor.windowBackgroundColor.blended(withFraction: 0.025, of: .labelColor)?.cgColor
            layer?.borderColor = NSColor.separatorColor.cgColor
            layer?.borderWidth = 0.5
        case .card:
            layer?.backgroundColor = NSColor.controlBackgroundColor.cgColor
            layer?.borderColor = NSColor.separatorColor.cgColor
            layer?.borderWidth = 1
            layer?.cornerRadius = 12
        case .canvas:
            layer?.backgroundColor = NSColor.windowBackgroundColor.cgColor
        }
    }
}

private final class EchoDeskPopupButton: NSPopUpButton {
    override init(frame buttonFrame: NSRect, pullsDown flag: Bool) {
        super.init(frame: buttonFrame, pullsDown: flag)
        bezelStyle = .rounded
        controlSize = .large
        font = .systemFont(ofSize: 13, weight: .medium)
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) is not supported") }
}

extension SettingsWindowController {
    // MARK: Desktop shell

    func buildEchoDeskDesktopUI(in root: NSView) {
        window?.setContentSize(NSSize(width: 1120, height: 760))
        window?.minSize = NSSize(width: 820, height: 600)
        window?.titleVisibility = .hidden
        window?.titlebarAppearsTransparent = true
        window?.toolbar = nil

        let sidebar = EchoDeskSurfaceView(.sidebar)
        let canvas = EchoDeskSurfaceView(.canvas)
        sidebar.translatesAutoresizingMaskIntoConstraints = false
        canvas.translatesAutoresizingMaskIntoConstraints = false
        root.addSubview(sidebar)
        root.addSubview(canvas)
        NSLayoutConstraint.activate([
            sidebar.leadingAnchor.constraint(equalTo: root.leadingAnchor),
            sidebar.topAnchor.constraint(equalTo: root.topAnchor),
            sidebar.bottomAnchor.constraint(equalTo: root.bottomAnchor),
            sidebar.widthAnchor.constraint(equalToConstant: 232),
            canvas.leadingAnchor.constraint(equalTo: sidebar.trailingAnchor),
            canvas.trailingAnchor.constraint(equalTo: root.trailingAnchor),
            canvas.topAnchor.constraint(equalTo: root.topAnchor),
            canvas.bottomAnchor.constraint(equalTo: root.bottomAnchor),
        ])

        buildEchoDeskSidebar(in: sidebar)
        buildEchoDeskContent(in: canvas)
        buildEchoDeskLegacyBackendViews(in: root)
        EchoDeskWiFiSetupManager.shared.onChange = { [weak self] in
            DispatchQueue.main.async {
                self?.refreshEchoDeskWiFiSetup()
                self?.refreshEchoDeskGuidedWiFi()
            }
        }
        selectEchoDeskPage(.overview)
    }

    private func buildEchoDeskSidebar(in sidebar: NSView) {
        let mark = NSTextField(labelWithString: "⌗")
        mark.font = .systemFont(ofSize: 28, weight: .bold)
        mark.alignment = .center
        mark.translatesAutoresizingMaskIntoConstraints = false
        mark.widthAnchor.constraint(equalToConstant: 36).isActive = true

        let brand = NSTextField(labelWithString: "EchoDesk")
        brand.font = .systemFont(ofSize: 18, weight: .semibold)
        let version = NSTextField(labelWithString: "DESKTOP \(kAppVersion)")
        version.font = .systemFont(ofSize: 10, weight: .medium)
        version.textColor = .secondaryLabelColor
        let brandText = NSStackView(views: [brand, version])
        brandText.orientation = .vertical
        brandText.alignment = .leading
        brandText.spacing = 1
        let brandRow = NSStackView(views: [mark, brandText])
        brandRow.orientation = .horizontal
        brandRow.alignment = .centerY
        brandRow.spacing = 9

        let items: [(EchoDeskDesktopPage, String, String)] = [
            (.overview, "Overview", "house"),
            (.deviceControl, "Device Control", "switch.2"),
            (.customisation, "Customisation", "gearshape"),
            (.setup, "Setup new device", "plus.circle"),
            (.recovery, "Device recovery & updates", "arrow.triangle.2.circlepath"),
        ]
        echoDeskSidebarButtons = items.map { item in
            let (page, title, symbol) = item
            let button = NSButton(title: title, target: self, action: #selector(echoDeskNavigationPressed(_:)))
            button.tag = page.rawValue
            button.bezelStyle = .recessed
            button.isBordered = false
            button.image = NSImage(systemSymbolName: symbol, accessibilityDescription: nil)
            button.imagePosition = .imageLeading
            button.alignment = .left
            button.font = .systemFont(ofSize: 13, weight: .medium)
            button.contentTintColor = .labelColor
            button.translatesAutoresizingMaskIntoConstraints = false
            button.heightAnchor.constraint(equalToConstant: 42).isActive = true
            button.setAccessibilityLabel(title)
            return button
        }
        let nav = NSStackView(views: echoDeskSidebarButtons)
        nav.orientation = .vertical
        nav.alignment = .leading
        nav.spacing = 3
        echoDeskSidebarButtons.forEach { $0.widthAnchor.constraint(equalTo: nav.widthAnchor).isActive = true }

        echoDeskSidebarConnectionDot = StatusIndicator(state: .inactive)
        echoDeskSidebarConnectionLabel = NSTextField(labelWithString: "CHECKING")
        echoDeskSidebarConnectionLabel.font = .systemFont(ofSize: 11, weight: .semibold)
        echoDeskFooterStatus = NSTextField(labelWithString: "Waiting…")
        echoDeskFooterStatus.font = .systemFont(ofSize: 11)
        echoDeskFooterStatus.textColor = .secondaryLabelColor
        echoDeskFooterStatus.lineBreakMode = .byTruncatingMiddle
        let footerTop = NSStackView(views: [echoDeskSidebarConnectionDot, echoDeskSidebarConnectionLabel])
        footerTop.orientation = .horizontal
        footerTop.alignment = .centerY
        footerTop.spacing = 7
        let footerStack = NSStackView(views: [footerTop, echoDeskFooterStatus])
        footerStack.orientation = .vertical
        footerStack.alignment = .leading
        footerStack.spacing = 5
        let footer = echoDeskCard(footerStack, padding: 12)

        for view in [brandRow, nav, footer] {
            view.translatesAutoresizingMaskIntoConstraints = false
            sidebar.addSubview(view)
        }
        NSLayoutConstraint.activate([
            brandRow.leadingAnchor.constraint(equalTo: sidebar.leadingAnchor, constant: 18),
            brandRow.trailingAnchor.constraint(lessThanOrEqualTo: sidebar.trailingAnchor, constant: -14),
            brandRow.topAnchor.constraint(equalTo: sidebar.topAnchor, constant: 24),
            nav.leadingAnchor.constraint(equalTo: sidebar.leadingAnchor, constant: 10),
            nav.trailingAnchor.constraint(equalTo: sidebar.trailingAnchor, constant: -10),
            nav.topAnchor.constraint(equalTo: brandRow.bottomAnchor, constant: 26),
            footer.leadingAnchor.constraint(equalTo: sidebar.leadingAnchor, constant: 14),
            footer.trailingAnchor.constraint(equalTo: sidebar.trailingAnchor, constant: -14),
            footer.bottomAnchor.constraint(equalTo: sidebar.bottomAnchor, constant: -18),
        ])
    }

    private func buildEchoDeskContent(in canvas: NSView) {
        banner = NotificationBanner()
        banner.isHidden = true
        banner.translatesAutoresizingMaskIntoConstraints = false
        canvas.addSubview(banner)

        let pages: [NSView] = [
            buildEchoDeskOverviewPage(),
            buildEchoDeskDeviceControlPage(),
            buildEchoDeskCustomisationPage(),
            buildEchoDeskSetupPage(),
            buildEchoDeskRecoveryPage(),
        ]
        echoDeskPageViews = pages.map(echoDeskScrollPage)
        for page in echoDeskPageViews {
            page.translatesAutoresizingMaskIntoConstraints = false
            canvas.addSubview(page)
            NSLayoutConstraint.activate([
                page.leadingAnchor.constraint(equalTo: canvas.leadingAnchor, constant: 32),
                page.trailingAnchor.constraint(equalTo: canvas.trailingAnchor, constant: -32),
                page.topAnchor.constraint(equalTo: banner.bottomAnchor, constant: 14),
                page.bottomAnchor.constraint(equalTo: canvas.bottomAnchor, constant: -18),
            ])
        }
        NSLayoutConstraint.activate([
            banner.leadingAnchor.constraint(equalTo: canvas.leadingAnchor, constant: 32),
            banner.trailingAnchor.constraint(equalTo: canvas.trailingAnchor, constant: -32),
            banner.topAnchor.constraint(equalTo: canvas.topAnchor, constant: 16),
        ])
    }

    /// Initialises every mature backend-bound control so the existing update
    /// engine remains the single source of device/firmware state. The views are
    /// deliberately not navigation surfaces in the EchoDesk product.
    private func buildEchoDeskLegacyBackendViews(in root: NSView) {
        providerSegmented = NSSegmentedControl(labels: CodexBarProvider.allCases.map(\.displayLabel),
                                               trackingMode: .selectOne, target: nil,
                                               action: nil)
        footerVersionLabel = NSTextField(labelWithString: "\(kProductName) v\(kAppVersion)")
        footerAboutButton = NSButton()
        let bootstrap = NSStackView(views: [
            providerSegmented, footerVersionLabel, footerAboutButton,
            buildOverviewPage(), buildDisplayPage(), buildPluginsPage(),
            buildConnectionPage(), buildUpdatesPage(), buildDiagnosticsPage(),
        ])
        bootstrap.orientation = .vertical
        bootstrap.isHidden = true
        bootstrap.frame = NSRect(x: -10_000, y: -10_000, width: 900, height: 1_400)
        root.addSubview(bootstrap)
    }

    private func echoDeskScrollPage(_ document: NSView) -> NSView {
        let scroll = NSScrollView()
        scroll.hasVerticalScroller = true
        scroll.autohidesScrollers = true
        scroll.drawsBackground = false
        scroll.borderType = .noBorder
        scroll.contentView = FlippedClipView()
        scroll.contentView.drawsBackground = false
        document.translatesAutoresizingMaskIntoConstraints = false
        scroll.documentView = document
        NSLayoutConstraint.activate([
            document.leadingAnchor.constraint(equalTo: scroll.contentView.leadingAnchor),
            document.trailingAnchor.constraint(equalTo: scroll.contentView.trailingAnchor),
            document.topAnchor.constraint(equalTo: scroll.contentView.topAnchor),
            document.widthAnchor.constraint(equalTo: scroll.contentView.widthAnchor),
        ])
        return scroll
    }

    @objc private func echoDeskNavigationPressed(_ sender: NSButton) {
        guard let page = EchoDeskDesktopPage(rawValue: sender.tag) else { return }
        selectEchoDeskPage(page)
    }

    func selectEchoDeskPage(_ page: EchoDeskDesktopPage) {
        echoDeskSelectedPage = page
        for (index, view) in echoDeskPageViews.enumerated() { view.isHidden = index != page.rawValue }
        for button in echoDeskSidebarButtons {
            let selected = button.tag == page.rawValue
            button.state = selected ? .on : .off
            button.contentTintColor = selected ? .controlAccentColor : .labelColor
        }
        if page == .setup { refreshEchoDeskSetupDevices() }
        if page == .recovery { refreshEchoDeskRecoveryDevices() }
    }

    // MARK: Pages

    private func buildEchoDeskOverviewPage() -> NSView {
        let header = echoDeskPageHeader("Overview", "Your EchoDesk, at a glance.")
        echoDeskConnectionLabel = NSTextField(labelWithString: "CHECKING")
        echoDeskConnectionLabel.font = .systemFont(ofSize: 12, weight: .bold)
        echoDeskConnectionDetail = echoDeskMuted("EchoDesk Desktop is looking for your device.")
        let name = NSTextField(labelWithString: "EchoDesk")
        name.font = .systemFont(ofSize: 25, weight: .semibold)
        echoDeskConnectionDot = StatusIndicator(state: .inactive)
        let stateRow = NSStackView(views: [echoDeskConnectionDot, echoDeskConnectionLabel])
        stateRow.orientation = .horizontal; stateRow.spacing = 8
        let reconnect = NSButton(title: "Reconnect", target: self, action: #selector(echoDeskReconnect))
        let refresh = echoDeskPrimaryButton("Refresh", #selector(echoDeskRefresh))
        let buttons = NSStackView(views: [reconnect, refresh]); buttons.orientation = .horizontal; buttons.spacing = 8
        let stateText = NSStackView(views: [stateRow, name, echoDeskConnectionDetail])
        stateText.orientation = .vertical; stateText.alignment = .leading; stateText.spacing = 6
        let deviceCardRow = NSStackView(views: [stateText, NSView(), buttons])
        deviceCardRow.orientation = .horizontal; deviceCardRow.alignment = .centerY
        let deviceCard = echoDeskCard(deviceCardRow)

        echoDeskFirmwareLabel = echoDeskMetricValue("Not reported")
        echoDeskHardwareLabel = echoDeskMetricValue("Not reported")
        echoDeskActiveAppLabel = echoDeskMetricValue("Not reported")
        let metrics = NSStackView(views: [
            echoDeskMetric("FIRMWARE", echoDeskFirmwareLabel),
            echoDeskMetric("HARDWARE", echoDeskHardwareLabel),
            echoDeskMetric("ACTIVE APP", echoDeskActiveAppLabel),
        ])
        metrics.orientation = .horizontal; metrics.distribution = .fillEqually; metrics.spacing = 14

        let componentTitle = NSTextField(labelWithString: "AI Usage Monitor")
        componentTitle.font = .systemFont(ofSize: 17, weight: .semibold)
        echoDeskComponentStatus = NSTextField(labelWithString: "STARTING")
        echoDeskComponentStatus.font = .systemFont(ofSize: 11, weight: .bold)
        let componentHeading = NSStackView(views: [componentTitle, echoDeskComponentStatus])
        componentHeading.orientation = .horizontal; componentHeading.spacing = 12
        echoDeskComponentDetail = echoDeskMuted("Waiting for Codex usage data.")
        echoDeskComponentUpdated = echoDeskMuted("Last successful update: No successful update yet")
        let enabled = NSButton(checkboxWithTitle: "Enabled", target: nil, action: nil)
        enabled.state = .on; enabled.isEnabled = false
        let componentText = NSStackView(views: [componentHeading, echoDeskComponentDetail, echoDeskComponentUpdated])
        componentText.orientation = .vertical; componentText.alignment = .leading; componentText.spacing = 7
        let componentRow = NSStackView(views: [componentText, NSView(), enabled])
        componentRow.orientation = .horizontal; componentRow.alignment = .top
        let component = echoDeskCard(componentRow)

        let apps = echoDeskAppGrid(columns: 3)
        let stack = echoDeskPageStack([header, deviceCard, metrics, echoDeskSectionTitle("Components"), component,
                                       echoDeskSectionTitle("Quick actions"), apps])
        return stack
    }

    private func buildEchoDeskDeviceControlPage() -> NSView {
        let header = echoDeskPageHeader("Device Control", "Open an app on the physical touchscreen.")
        let apps = echoDeskCard(echoDeskPageStack([echoDeskSectionTitle("Apps"), echoDeskAppGrid(columns: 2)]))
        let restart = echoDeskDangerRow("Restart EchoDesk",
            "The USB connection will briefly disconnect and recover automatically.",
            "Restart…", #selector(echoDeskRestart))
        let reset = echoDeskDangerRow("Factory Reset",
            "Clear setup completion and return the physical device to its Hello screen. Firmware and unrelated flash partitions are not erased.",
            "Factory Reset…", #selector(echoDeskFactoryReset))
        let danger = echoDeskCard(echoDeskPageStack([restart, echoDeskDivider(), reset]))
        return echoDeskPageStack([header, apps, danger])
    }

    private func buildEchoDeskCustomisationPage() -> NSView {
        let header = echoDeskPageHeader("Customisation",
            "Every enabled control writes to the physical EchoDesk and waits for confirmation.")
        echoDeskQuickTheme = NSSegmentedControl(labels: ["Light", "Dark"], trackingMode: .selectOne,
                                                 target: self, action: #selector(echoDeskQuickThemeChanged))
        echoDeskQuickTheme.selectedSegment = Settings.shared.themeMode == "light" ? 0 : 1
        echoDeskQuickClockFormat = EchoDeskPopupButton(frame: .zero, pullsDown: false)
        echoDeskQuickClockFormat.addItems(withTitles: ["12-hour", "24-hour"])
        echoDeskQuickClockFormat.target = self
        echoDeskQuickClockFormat.action = #selector(echoDeskClockFormatChanged)
        let quick = echoDeskCard(echoDeskPageStack([
            echoDeskSectionTitle("Quick settings"),
            echoDeskLabeledControl("Device appearance", echoDeskQuickTheme),
            echoDeskLabeledControl("Time format", echoDeskQuickClockFormat),
        ]))
        let brightness = NSSlider(value: Double(Settings.shared.lastKnownBrightness), minValue: 10, maxValue: 100,
                                  target: self, action: #selector(echoDeskSettingSliderChanged(_:)))
        brightness.isContinuous = false
        brightness.identifier = NSUserInterfaceItemIdentifier("brightness")
        echoDeskSettingControls["brightness"] = brightness
        let general = echoDeskCard(echoDeskPageStack([
            echoDeskSectionTitle("General"),
            echoDeskLabeledControl("Brightness", brightness),
            echoDeskLabeledControl("Default app on startup", echoDeskSettingPopup("defaultApp", [
                ("Home", "home"), ("Codex Usage", "codex"), ("Clock", "clock"),
                ("Performance", "performance"), ("Now Playing", "nowPlaying"), ("Settings", "settings"),
            ])),
        ]))
        let nowPlaying = echoDeskCard(echoDeskPageStack([
            echoDeskSectionTitle("Now Playing"),
            echoDeskLabeledControl("Layout", echoDeskSettingPopup("nowPlayingLayout", [
                ("Album Cover", "albumCover"), ("CarPlay", "carPlay"), ("EchoDesk", "echoDesk"),
            ])),
            echoDeskLabeledControl("Background", echoDeskSettingPopup("nowPlayingBackground", [
                ("Blurred Album Cover", "blurredAlbumCover"), ("EchoDesk", "echoDesk"), ("Audio Bars", "audioBars"),
            ])),
        ]))
        let clock = echoDeskCard(echoDeskPageStack([
            echoDeskSectionTitle("Clock"),
            echoDeskLabeledControl("Time format", echoDeskSettingPopup("clockFormat", [("12-hour", "12h"), ("24-hour", "24h")])),
            echoDeskLabeledControl("Timezone", echoDeskSettingPopup("timezone", [
                ("Melbourne", "Australia/Melbourne"), ("Sydney", "Australia/Sydney"), ("UTC", "UTC"),
            ])),
        ]))
        let codex = echoDeskCard(echoDeskPageStack([
            echoDeskSectionTitle("Codex Usage"),
            echoDeskLabeledControl("Percentage display", echoDeskSettingPopup("codexPercentageMode", [
                ("Used", "used"), ("Remaining", "remaining"),
            ])),
            echoDeskLabeledControl("Preferred usage display", echoDeskSettingPopup("codexPreferredDisplay", [
                ("Session", "session"), ("Weekly", "weekly"), ("Both", "both"),
            ])),
        ]))
        let performance = echoDeskCard(echoDeskPageStack([
            echoDeskSectionTitle("Performance"),
            echoDeskLabeledControl("Default view", echoDeskSettingPopup("performanceDefaultView", [
                ("Overview", "overview"), ("CPU", "cpu"), ("GPU", "gpu"), ("Memory", "memory"),
            ])),
        ]))
        let desktopAppearance = EchoDeskPopupButton(frame: .zero, pullsDown: false)
        desktopAppearance.addItems(withTitles: ["System", "Light", "Dark"])
        let savedDesktopAppearance = max(0, min(2,
            UserDefaults.standard.integer(forKey: "EchoDeskDesktopAppearance")))
        desktopAppearance.selectItem(at: savedDesktopAppearance)
        desktopAppearance.identifier = NSUserInterfaceItemIdentifier("desktopAppearance")
        desktopAppearance.target = self; desktopAppearance.action = #selector(echoDeskDesktopAppearanceChanged(_:))
        applyEchoDeskDesktopAppearance(savedDesktopAppearance)
        let desktop = echoDeskCard(echoDeskPageStack([
            echoDeskSectionTitle("Desktop app"), echoDeskLabeledControl("Desktop appearance", desktopAppearance),
        ]))
        return echoDeskPageStack([header, quick, general, nowPlaying, clock, codex, performance, desktop])
    }

    private func buildEchoDeskSetupPage() -> NSView {
        let header = echoDeskPageHeader("Setup new device", "A guided setup for EchoDesk Mini.")
        let stepNames = ["1  Connect", "2  Choose", "3  Install", "4  Quick settings", "5  Ready"]
        echoDeskSetupStepLabels = stepNames.map { name in
            let label = NSTextField(labelWithString: name)
            label.font = .systemFont(ofSize: 11, weight: .medium)
            label.alignment = .center
            label.textColor = .secondaryLabelColor
            return label
        }
        let stepper = NSStackView(views: echoDeskSetupStepLabels)
        stepper.orientation = .horizontal
        stepper.distribution = .fillEqually
        stepper.spacing = 6
        let stepperCard = echoDeskCard(stepper, padding: 12)
        let connect = buildEchoDeskSetupConnectStep()
        let usb = buildEchoDeskSetupUSBStep()
        let wifi = buildEchoDeskSetupWiFiStep()
        let choice = buildEchoDeskSetupChoiceStep()
        let working = buildEchoDeskSetupWorkingStep()
        let quick = buildEchoDeskSetupQuickSettingsStep()
        let welcome = buildEchoDeskSetupWelcomeStep()
        echoDeskSetupViews = [connect, usb, wifi, choice, working, quick, welcome]
        echoDeskPendingSetupPort = UserDefaults.standard.string(forKey: "EchoDeskPendingSetupPortV2")
        let restored = EchoDeskSetupStep(rawValue: UserDefaults.standard.integer(forKey: "EchoDeskGuidedSetupStepV2")) ?? .connect
        setEchoDeskSetupStep(restored == .working && echoDeskPendingSetupPort == nil ? .connect : restored)
        return echoDeskPageStack([header, stepperCard] + echoDeskSetupViews)
    }

    private func buildEchoDeskRecoveryPage() -> NSView {
        let header = echoDeskPageHeader("Device recovery & updates",
            "Update, reinstall or recover EchoDesk OS without command-line tools.")
        echoDeskRecoveryDevicePopup = EchoDeskPopupButton(frame: .zero, pullsDown: false)
        echoDeskRecoveryDevicePopup.translatesAutoresizingMaskIntoConstraints = false
        echoDeskRecoveryDevicePopup.widthAnchor.constraint(greaterThanOrEqualToConstant: 360).isActive = true
        let scan = NSButton(title: "Scan for devices", target: self, action: #selector(echoDeskRecoveryScan))
        let deviceRow = NSStackView(views: [echoDeskRecoveryDevicePopup, scan])
        deviceRow.orientation = .horizontal; deviceRow.spacing = 10
        let selector = echoDeskCard(echoDeskPageStack([
            echoDeskSectionTitle("Select device"), echoDeskMuted("Select an EchoDesk Mini USB serial device before choosing an action."), deviceRow,
        ]))

        echoDeskRecoveryActions = NSStackView(views: [
            echoDeskRecoveryAction(0, "Update EchoDesk", "Update EchoDesk OS to the latest version"),
            echoDeskRecoveryAction(1, "Reset/Install EchoDesk OS", "Install a fresh version of EchoDesk OS"),
            echoDeskRecoveryAction(2, "Repair device", "Repair and reset EchoDesk OS if on older firmware"),
            echoDeskRecoveryAction(3, "Recovery", "Fully reset EchoDesk without checks"),
        ])
        echoDeskRecoveryActions.orientation = .vertical; echoDeskRecoveryActions.spacing = 12
        echoDeskRecoveryStatus = echoDeskMuted("")
        echoDeskRecoveryProgress = NSProgressIndicator()
        echoDeskRecoveryProgress.style = .bar; echoDeskRecoveryProgress.isIndeterminate = true
        echoDeskRecoveryProgress.isHidden = true
        echoDeskRecoveryCompletion = echoDeskCard(echoDeskPageStack([
            echoDeskSectionTitle("EchoDesk OS is ready"),
            echoDeskMuted("The firmware was written, verified and the device reconnected."),
            echoDeskPrimaryButton("Setup EchoDesk Now", #selector(echoDeskSetupAfterRecovery)),
        ]))
        echoDeskRecoveryCompletion.isHidden = true
        return echoDeskPageStack([header, selector, echoDeskRecoveryActions, echoDeskRecoveryProgress,
                                  echoDeskRecoveryStatus, echoDeskRecoveryCompletion])
    }

    // MARK: Setup steps

    private func buildEchoDeskSetupConnectStep() -> NSView {
        let usb = echoDeskChoiceCard("Setup with USB", "Setup EchoDesk by plugging into your PC.",
                                     "Continue with USB", #selector(echoDeskChooseUSB))
        let wifi = echoDeskChoiceCard("Setup with WiFi",
            "Setup EchoDesk without plugging into your PC. You'll still need to use USB connection for some utilities.",
            "Setup with WiFi", #selector(echoDeskChooseWiFi))
        let choices = NSStackView(views: [usb, wifi]); choices.orientation = .horizontal
        choices.distribution = .fillEqually; choices.spacing = 16
        return echoDeskCard(echoDeskPageStack([
            echoDeskHero("How do you want to set up EchoDesk?"),
            echoDeskMuted("USB is recommended. WiFi setup starts only when you choose it."), choices,
        ]))
    }

    private func buildEchoDeskSetupUSBStep() -> NSView {
        echoDeskSetupUSBPopup = EchoDeskPopupButton(frame: .zero, pullsDown: false)
        echoDeskSetupUSBPopup.target = self
        echoDeskSetupUSBPopup.action = #selector(echoDeskSetupPortChanged)
        echoDeskSetupUSBPopup.translatesAutoresizingMaskIntoConstraints = false
        echoDeskSetupUSBPopup.widthAnchor.constraint(greaterThanOrEqualToConstant: 390).isActive = true
        echoDeskSetupDeviceName = echoDeskMetricValue("No known EchoDesk found")
        echoDeskSetupDeviceModel = echoDeskMuted("Model: —")
        echoDeskSetupDeviceFirmware = echoDeskMuted("Firmware: —")
        echoDeskSetupDeviceSerial = echoDeskMuted("USB Serial: —")
        let device = echoDeskCard(echoDeskPageStack([echoDeskSetupDeviceName, echoDeskSetupDeviceModel,
                                                     echoDeskSetupDeviceFirmware, echoDeskSetupDeviceSerial]))
        echoDeskSetupTroubleButton = NSButton(title: "Having trouble finding your device?", target: self,
                                              action: #selector(echoDeskRevealUnknownDevices))
        let controls = NSStackView(views: [
            NSButton(title: "Back", target: self, action: #selector(echoDeskSetupBack)),
            NSButton(title: "Refresh now", target: self, action: #selector(echoDeskSetupRefresh)),
            echoDeskPrimaryButton("Set up this device", #selector(echoDeskBeginUSBSetup)),
        ])
        controls.orientation = .horizontal; controls.spacing = 8
        return echoDeskCard(echoDeskPageStack([
            echoDeskHero("Select your EchoDesk."),
            echoDeskMuted("Known EchoDesk devices appear automatically. Other serial devices remain hidden until you ask for troubleshooting options."),
            echoDeskSetupUSBPopup, device, echoDeskSetupTroubleButton, controls,
        ]))
    }

    private func buildEchoDeskSetupWiFiStep() -> NSView {
        echoDeskGuidedWiFiStatus = echoDeskMuted("Join EchoDesk-XXXX, then find WiFi devices.")
        echoDeskGuidedWiFiDevicePopup = EchoDeskPopupButton(frame: .zero, pullsDown: false)
        echoDeskGuidedWiFiCode = NSTextField(); echoDeskGuidedWiFiCode.placeholderString = "8-digit setup code"
        echoDeskGuidedWiFiSSID = NSTextField(); echoDeskGuidedWiFiSSID.placeholderString = "Home WiFi name (SSID)"
        echoDeskGuidedWiFiPassword = NSSecureTextField(); echoDeskGuidedWiFiPassword.placeholderString = "Home WiFi password"
        let open = NSButton(title: "Open macOS WiFi settings", target: self, action: #selector(echoDeskOpenMacWiFiSettings))
        let find = NSButton(title: "Find WiFi devices", target: self, action: #selector(echoDeskGuidedWiFiFind))
        let row = NSStackView(views: [open, find]); row.orientation = .horizontal; row.spacing = 8
        let fields = NSStackView(views: [echoDeskGuidedWiFiCode, echoDeskGuidedWiFiSSID, echoDeskGuidedWiFiPassword])
        fields.orientation = .vertical; fields.spacing = 9
        let actions = NSStackView(views: [
            NSButton(title: "Back", target: self, action: #selector(echoDeskSetupBack)),
            echoDeskPrimaryButton("Connect EchoDesk to WiFi", #selector(echoDeskGuidedWiFiProvision)),
        ])
        actions.orientation = .horizontal; actions.spacing = 8
        let step = echoDeskCard(echoDeskPageStack([
            echoDeskHero("Connect through the temporary EchoDesk network."),
            echoDeskMuted("On EchoDesk, choose Setup with WiFi. Join EchoDesk-XXXX in macOS using the displayed code, then finish the single setup form below."),
            row, echoDeskGuidedWiFiStatus, echoDeskGuidedWiFiDevicePopup, fields, actions,
        ]))
        echoDeskGuidedWiFiContainer = step
        return step
    }

    private func buildEchoDeskSetupChoiceStep() -> NSView {
        echoDeskSetupChoiceStatus = echoDeskMuted("Your EchoDesk is connected and ready for setup.")
        let now = echoDeskChoiceCard("Get started now", "Setup EchoDesk without updating firmware.",
                                     "Get started now", #selector(echoDeskGetStartedNow))
        let update = echoDeskChoiceCard("Check for updates",
            "EchoDesk will find your current device firmware and update it if a newer version is available.",
            "Check for updates", #selector(echoDeskCheckUpdatesDuringSetup))
        let row = NSStackView(views: [now, update]); row.orientation = .horizontal
        row.distribution = .fillEqually; row.spacing = 16
        return echoDeskCard(echoDeskPageStack([echoDeskHero("Your EchoDesk is connected."), echoDeskSetupChoiceStatus, row]))
    }

    private func buildEchoDeskSetupWorkingStep() -> NSView {
        echoDeskSetupProgress = NSProgressIndicator(); echoDeskSetupProgress.style = .bar
        echoDeskSetupProgress.isIndeterminate = true; echoDeskSetupProgress.startAnimation(nil)
        let cancel = NSButton(title: "Cancel safely", target: self, action: #selector(echoDeskCancelGuidedSetup))
        return echoDeskCard(echoDeskPageStack([
            echoDeskHero("Getting EchoDesk ready."),
            echoDeskMuted("Keep the USB cable connected while EchoDesk OS is checked, installed and reconnected."),
            echoDeskSetupProgress, cancel,
        ]))
    }

    private func buildEchoDeskSetupQuickSettingsStep() -> NSView {
        echoDeskSetupQuickTheme = NSSegmentedControl(labels: ["Light", "Dark"], trackingMode: .selectOne,
                                                      target: nil, action: nil)
        echoDeskSetupQuickTheme.selectedSegment = 1
        echoDeskSetupQuickClockFormat = EchoDeskPopupButton(frame: .zero, pullsDown: false)
        echoDeskSetupQuickClockFormat.addItems(withTitles: ["12-hour", "24-hour"])
        echoDeskSetupQuickClockFormat.selectItem(at: 1)
        let controls = echoDeskPageStack([
            echoDeskLabeledControl("Appearance", echoDeskSetupQuickTheme),
            echoDeskLabeledControl("Time format", echoDeskSetupQuickClockFormat),
        ])
        return echoDeskCard(echoDeskPageStack([
            echoDeskHero("Choose your quick settings."),
            echoDeskMuted("You can change these at any time from Customisation."), controls,
            echoDeskPrimaryButton("Continue", #selector(echoDeskApplySetupQuickSettings)),
        ]))
    }

    private func buildEchoDeskSetupWelcomeStep() -> NSView {
        let confetti = NSTextField(labelWithString: "●  ◆  ●  ✦  ◆  ●  ✦  ●")
        confetti.font = .systemFont(ofSize: 24, weight: .bold)
        confetti.textColor = .controlAccentColor
        confetti.alignment = .center
        echoDeskSetupCompletionTitle = echoDeskHero("Welcome to EchoDesk!")
        echoDeskSetupCompletionTitle.alignment = .center
        echoDeskSetupCompletionDetail = echoDeskMuted("Welcome to the community. Your new EchoDesk is ready to go.")
        echoDeskSetupCompletionDetail.alignment = .center
        let button = echoDeskPrimaryButton("Go to home", #selector(echoDeskFinishSetup))
        let stack = echoDeskPageStack([confetti, echoDeskSetupCompletionTitle, echoDeskSetupCompletionDetail, button])
        stack.alignment = .centerX
        return echoDeskCard(stack)
    }

    func setEchoDeskSetupStep(_ step: EchoDeskSetupStep) {
        echoDeskSetupStep = step
        for (index, view) in echoDeskSetupViews.enumerated() { view.isHidden = index != step.rawValue }
        let progressIndex: Int
        switch step {
        case .connect: progressIndex = 0
        case .usbDevice, .wifi: progressIndex = 1
        case .choice, .working: progressIndex = 2
        case .quickSettings: progressIndex = 3
        case .welcome: progressIndex = 4
        }
        for (index, label) in echoDeskSetupStepLabels.enumerated() {
            label.textColor = index <= progressIndex ? .controlAccentColor : .secondaryLabelColor
            label.font = .systemFont(ofSize: 11, weight: index == progressIndex ? .semibold : .medium)
        }
        UserDefaults.standard.set(step.rawValue, forKey: "EchoDeskGuidedSetupStepV2")
    }

    // MARK: State refresh

    func refreshEchoDeskDesktopState(forceStateRequest: Bool) {
        guard let monitor else { return }
        let serial = monitor.serialPort
        let ready = serial.state == .connected
        let profile = ready ? DeviceRegistry.shared.currentProfile() : nil
        let observedPort = ready ? serial.connectedPort : nil
        let observedMAC = ready ? profile?.mac : nil
        if echoDeskObservedConnectionPort != observedPort || echoDeskObservedConnectionMAC != observedMAC {
            echoDeskLastState = nil
            echoDeskLastStatePort = nil
            echoDeskLastStateMAC = nil
            echoDeskLastStateRequest = .distantPast
            echoDeskObservedConnectionPort = observedPort
            echoDeskObservedConnectionMAC = observedMAC
        }
        echoDeskConnectionDot?.state = ready ? .ok : (serial.state == .probing ? .pending : .attention)
        echoDeskSidebarConnectionDot?.state = ready ? .ok : (serial.state == .probing ? .pending : .attention)
        echoDeskConnectionLabel?.stringValue = ready ? "CONNECTED" : (serial.state == .probing ? "CONNECTING" : "OFFLINE")
        echoDeskSidebarConnectionLabel?.stringValue = ready ? "CONNECTED" : (serial.state == .probing ? "CONNECTING" : "OFFLINE")
        echoDeskConnectionDetail?.stringValue = ready
            ? "EchoDesk is connected through USB."
            : "Connect EchoDesk Mini or use Setup new device."
        echoDeskFooterStatus?.stringValue = serial.connectedPort ?? "Waiting…"
        echoDeskFirmwareLabel?.stringValue = serial.deviceFirmwareVersion.map { "EchoDesk OS \($0)" } ?? "Not reported"
        echoDeskHardwareLabel?.stringValue = ready ? "EchoDesk Mini" : "Not reported"
        echoDeskActiveAppLabel?.stringValue = (echoDeskLastState?["activeApp"] as? String)?.echoDeskTitle ?? "Not reported"

        let source = monitor.codexBar
        if source.status.isOK {
            echoDeskComponentStatus?.stringValue = "ACTIVE"
            echoDeskComponentDetail?.stringValue = "Codex usage is being sent to EchoDesk."
        } else {
            echoDeskComponentStatus?.stringValue = "NEEDS ATTENTION"
            echoDeskComponentDetail?.stringValue = source.status.shortLabel
        }
        if let date = monitor.lastUpdateDate {
            echoDeskComponentUpdated?.stringValue = "Last successful update: \(RelativeDateTimeFormatter().localizedString(for: date, relativeTo: Date()))"
        } else {
            echoDeskComponentUpdated?.stringValue = "Last successful update: No successful update yet"
        }
        if echoDeskSelectedPage == .setup {
            if echoDeskSetupStep == .usbDevice { refreshEchoDeskSetupDevices() }
            continueEchoDeskSetupAfterReconnect(profile: profile)
        }
        if echoDeskSelectedPage == .recovery {
            if !echoDeskRecoveryOperationStarted { refreshEchoDeskRecoveryDevices() }
            continueEchoDeskRecoveryAfterReconnect()
            refreshEchoDeskRecoveryProgress()
        }
        if ready && (forceStateRequest || Date().timeIntervalSince(echoDeskLastStateRequest) > 5) {
            requestEchoDeskState()
        }
    }

    private func requestEchoDeskState() {
        guard !echoDeskStateRequestInFlight, let monitor, monitor.serialPort.state == .connected,
              let requestedPort = monitor.serialPort.connectedPort,
              let requestedMAC = DeviceRegistry.shared.currentProfile()?.mac else { return }
        echoDeskStateRequestInFlight = true
        echoDeskLastStateRequest = Date()
        let request = nextEchoDeskRequest()
        monitor.serialPort.performJSONCommand([
            "cmd": "echodesk_get_state", "version": 1, "requestId": request,
        ], acceptedTypes: ["echodesk_state", "echodesk_error"], timeout: 3) { [weak self] json in
            guard let self else { return }
            self.echoDeskStateRequestInFlight = false
            guard let json, json["type"] as? String == "echodesk_state",
                  self.echoDeskResponseMatches(json, request: request),
                  monitor.serialPort.state == .connected,
                  monitor.serialPort.connectedPort == requestedPort,
                  DeviceRegistry.shared.currentProfile()?.mac == requestedMAC else { return }
            self.echoDeskLastState = json
            self.echoDeskLastStatePort = requestedPort
            self.echoDeskLastStateMAC = requestedMAC
            if let theme = (json["settings"] as? [String: Any])?["theme"] as? String {
                self.echoDeskQuickTheme?.selectedSegment = theme == "light" ? 0 : 1
            }
            if let format = (json["settings"] as? [String: Any])?["clockFormat"] as? String {
                self.echoDeskQuickClockFormat?.selectItem(at: format == "12h" ? 0 : 1)
            }
            self.applyEchoDeskStateToControls(json)
            self.applyDeferredEchoDeskQuickSettingsIfNeeded()
            self.refreshEchoDeskDesktopState(forceStateRequest: false)
        }
    }

    private func nextEchoDeskRequest() -> UInt32 {
        echoDeskRequestID &+= 1
        if echoDeskRequestID == 0 { echoDeskRequestID = 1 }
        return echoDeskRequestID
    }

    private func echoDeskResponseMatches(_ json: [String: Any], request: UInt32,
                                         command: String? = nil) -> Bool {
        guard (json["requestId"] as? NSNumber)?.uint32Value == request else { return false }
        if let command, json["type"] as? String == "echodesk_ack" {
            return json["command"] as? String == command
        }
        return true
    }

    private func sendEchoDeskCommand(_ command: String, extra: [String: Any] = [:],
                                     completion: ((Bool) -> Void)? = nil) {
        guard let monitor else { completion?(false); return }
        let request = nextEchoDeskRequest()
        var payload: [String: Any] = ["cmd": command, "version": 1, "requestId": request]
        extra.forEach { payload[$0.key] = $0.value }
        monitor.serialPort.performJSONCommand(payload,
            acceptedTypes: ["echodesk_ack", "echodesk_error"], timeout: 5) { [weak self] json in
            let ok = json?["type"] as? String == "echodesk_ack"
                && self?.echoDeskResponseMatches(json ?? [:], request: request, command: command) == true
            completion?(ok)
        }
    }

    // MARK: Actions

    @objc private func echoDeskReconnect() { monitor?.serialPort.requestReconnect() }
    @objc private func echoDeskRefresh() { monitor?.codexBar.loadOnce(); requestEchoDeskState() }

    @objc private func echoDeskOpenApp(_ sender: NSButton) {
        guard let app = sender.identifier?.rawValue else { return }
        sendEchoDeskCommand("echodesk_open_app", extra: ["app": app]) { [weak self] ok in
            self?.echoDeskFooterStatus?.stringValue = ok ? "Opened \(sender.title)" : "EchoDesk did not accept the app command."
            if ok { self?.requestEchoDeskState() }
        }
    }

    @objc private func echoDeskRestart() {
        echoDeskConfirm(title: "Restart EchoDesk?", detail: "The USB connection will disconnect briefly.", action: "Restart") {
            self.sendEchoDeskCommand("echodesk_restart")
        }
    }

    @objc private func echoDeskFactoryReset() {
        echoDeskConfirm(title: "Factory Reset EchoDesk?",
            detail: "EchoDesk will return to its Hello screen. Firmware is not erased.", action: "Factory Reset") {
            self.sendEchoDeskCommand("echodesk_factory_reset") { ok in
                if ok { self.selectEchoDeskPage(.setup); self.setEchoDeskSetupStep(.connect) }
            }
        }
    }

    @objc private func echoDeskQuickThemeChanged() {
        let value = echoDeskQuickTheme.selectedSegment == 0 ? "light" : "dark"
        applyEchoDeskSetting("theme", value: value)
    }

    @objc private func echoDeskClockFormatChanged() {
        let value = echoDeskQuickClockFormat.indexOfSelectedItem == 0 ? "12h" : "24h"
        applyEchoDeskSetting("clockFormat", value: value)
    }

    @objc private func echoDeskSettingPopupChanged(_ sender: NSPopUpButton) {
        guard let key = sender.identifier?.rawValue,
              let value = sender.selectedItem?.representedObject else { return }
        applyEchoDeskSetting(key, value: value)
    }

    @objc private func echoDeskSettingSliderChanged(_ sender: NSSlider) {
        guard let key = sender.identifier?.rawValue else { return }
        applyEchoDeskSetting(key, value: Int(sender.doubleValue.rounded()))
    }

    @objc private func echoDeskDesktopAppearanceChanged(_ sender: NSPopUpButton) {
        applyEchoDeskDesktopAppearance(sender.indexOfSelectedItem)
        UserDefaults.standard.set(sender.indexOfSelectedItem, forKey: "EchoDeskDesktopAppearance")
    }

    private func applyEchoDeskDesktopAppearance(_ selection: Int) {
        switch selection {
        case 1: NSApp.appearance = NSAppearance(named: .aqua)
        case 2: NSApp.appearance = NSAppearance(named: .darkAqua)
        default: NSApp.appearance = nil
        }
    }

    private func applyEchoDeskStateToControls(_ state: [String: Any]) {
        guard let settings = state["settings"] as? [String: Any] else { return }
        for (key, control) in echoDeskSettingControls {
            guard let value = settings[key] else { continue }
            if let popup = control as? NSPopUpButton,
               let item = popup.itemArray.first(where: { String(describing: $0.representedObject ?? "") == String(describing: value) }) {
                popup.select(item)
            } else if let slider = control as? NSSlider, let number = value as? NSNumber {
                slider.doubleValue = number.doubleValue
            }
        }
        let unsupported = Set((state["unsupportedSettings"] as? [String]) ?? [])
        for (key, control) in echoDeskSettingControls { control.isEnabled = !unsupported.contains(key) }
    }

    private func applyEchoDeskSetting(_ key: String, value: Any) {
        guard let revision = echoDeskLastState?["revision"] as? Int else {
            requestEchoDeskState(); return
        }
        sendEchoDeskCommand("echodesk_set_settings", extra: [
            "baseRevision": revision, "persist": true, "settings": [key: value],
        ]) { [weak self] ok in
            if ok { self?.requestEchoDeskState() }
        }
    }

    @objc private func echoDeskChooseUSB() {
        UserDefaults.standard.set("usb", forKey: "EchoDeskGuidedSetupTransportV2")
        setEchoDeskSetupStep(.usbDevice)
        refreshEchoDeskSetupDevices()
    }
    @objc private func echoDeskChooseWiFi() {
        UserDefaults.standard.set("wifi", forKey: "EchoDeskGuidedSetupTransportV2")
        setEchoDeskSetupStep(.wifi)
        echoDeskGuidedWiFiFind()
    }
    @objc private func echoDeskSetupBack() {
        UserDefaults.standard.removeObject(forKey: "EchoDeskGuidedSetupTransportV2")
        setEchoDeskSetupStep(.connect)
    }
    @objc private func echoDeskSetupRefresh() { monitor?.serialPort.scanForPort(); refreshEchoDeskSetupDevices() }
    @objc private func echoDeskRevealUnknownDevices() { echoDeskShowUnknownDevices = true; refreshEchoDeskSetupDevices() }
    @objc private func echoDeskSetupPortChanged() { updateEchoDeskSetupDeviceCard() }

    func refreshEchoDeskSetupDevices() {
        guard echoDeskSetupUSBPopup != nil, let monitor else { return }
        let selected = echoDeskSetupUSBPopup.selectedItem?.representedObject as? String
        echoDeskSetupUSBPopup.removeAllItems()
        if monitor.serialPort.state == .connected,
           let port = monitor.serialPort.connectedPort,
           DeviceRegistry.shared.currentProfile() != nil {
            echoDeskSetupUSBPopup.addItem(withTitle: "EchoDesk Mini — \(port)")
            echoDeskSetupUSBPopup.lastItem?.representedObject = port
        }
        if echoDeskShowUnknownDevices {
            for port in monitor.serialPort.availablePortPaths()
                where !(echoDeskSetupUSBPopup.itemArray.contains { ($0.representedObject as? String) == port }) {
                echoDeskSetupUSBPopup.addItem(withTitle: "Unidentified serial device — \(port)")
                echoDeskSetupUSBPopup.lastItem?.representedObject = port
            }
        }
        if echoDeskSetupUSBPopup.numberOfItems == 0 {
            echoDeskSetupUSBPopup.addItem(withTitle: "No known EchoDesk detected")
            echoDeskSetupUSBPopup.isEnabled = false
        } else {
            echoDeskSetupUSBPopup.isEnabled = true
            if let selected, let item = echoDeskSetupUSBPopup.itemArray.first(where: { ($0.representedObject as? String) == selected }) {
                echoDeskSetupUSBPopup.select(item)
            }
        }
        updateEchoDeskSetupDeviceCard()
    }

    private func updateEchoDeskSetupDeviceCard() {
        let port = echoDeskSetupUSBPopup?.selectedItem?.representedObject as? String
        let connected = port != nil && port == monitor?.serialPort.connectedPort && monitor?.serialPort.state == .connected
        let profile = connected ? DeviceRegistry.shared.currentProfile() : nil
        echoDeskSetupDeviceName?.stringValue = profile?.friendlyName ?? (port == nil ? "No known EchoDesk found" : "Unidentified serial device")
        echoDeskSetupDeviceModel?.stringValue = "Model: \(connected ? "Freenove FNK0114A_2P4" : "Confirmation required")"
        echoDeskSetupDeviceFirmware?.stringValue = "Firmware: \(monitor?.serialPort.deviceFirmwareVersion.map { "EchoDesk OS \($0)" } ?? "Not reported")"
        echoDeskSetupDeviceSerial?.stringValue = "USB Serial: \(port ?? "—")"
    }

    @objc private func echoDeskBeginUSBSetup() {
        guard let port = echoDeskSetupUSBPopup.selectedItem?.representedObject as? String, let monitor else {
            echoDeskFooterStatus.stringValue = "Select an EchoDesk first."
            return
        }
        echoDeskPendingSetupPort = port
        UserDefaults.standard.set(port, forKey: "EchoDeskPendingSetupPortV2")
        Settings.shared.manualPortPath = port
        echoDeskSetupBeginSent = false
        if monitor.serialPort.connectedPort != port || monitor.serialPort.state != .connected {
            monitor.serialPort.requestReconnect()
            echoDeskFooterStatus.stringValue = "Connecting to \(port)…"
        }
        continueEchoDeskSetupAfterReconnect(profile: DeviceRegistry.shared.currentProfile())
    }

    private func continueEchoDeskSetupAfterReconnect(profile: DeviceProfile?) {
        guard echoDeskSetupStep == .usbDevice || echoDeskSetupStep == .working else { return }
        if echoDeskPendingSetupPort == nil {
            echoDeskPendingSetupPort = UserDefaults.standard.string(forKey: "EchoDeskPendingSetupPortV2")
        }
        guard let port = echoDeskPendingSetupPort, monitor?.serialPort.connectedPort == port,
              monitor?.serialPort.state == .connected else { return }
        if echoDeskSetupWaitingForFlash {
            if echoDeskSetupFlashSucceeded {
                echoDeskSetupWaitingForFlash = false
                echoDeskSetupFlashSucceeded = false
                setEchoDeskSetupStep(.quickSettings)
            }
            return
        }
        guard !echoDeskSetupBeginSent else { return }
        echoDeskSetupBeginSent = true
        sendEchoDeskCommand("echodesk_setup_begin") { [weak self] ok in
            guard let self else { return }
            if ok {
                self.setEchoDeskSetupStep(.choice)
            } else {
                self.echoDeskSetupBeginSent = false
                self.echoDeskFooterStatus.stringValue = "Waiting for EchoDesk to reconnect…"
            }
        }
    }

    @objc private func echoDeskGetStartedNow() { setEchoDeskSetupStep(.quickSettings) }
    @objc private func echoDeskCheckUpdatesDuringSetup() {
        setEchoDeskSetupStep(.working)
        FirmwareManager.shared.checkForUpdate { [weak self] hasUpdate in
            guard let self else { return }
            if hasUpdate {
                self.echoDeskSetupWaitingForFlash = true
                self.echoDeskSetupFlashSucceeded = false
                guard let delegate = NSApp.delegate as? AppDelegate else {
                    self.echoDeskSetupWaitingForFlash = false
                    self.echoDeskFooterStatus.stringValue = "EchoDesk Desktop could not start the firmware service."
                    self.setEchoDeskSetupStep(.choice)
                    return
                }
                delegate.runFirmwareFlash { [weak self] success, message in
                    guard let self else { return }
                    if success {
                        self.echoDeskSetupFlashSucceeded = true
                        self.echoDeskFooterStatus.stringValue = "Firmware verified. Waiting for the selected EchoDesk to reconnect…"
                        self.continueEchoDeskSetupAfterReconnect(profile: DeviceRegistry.shared.currentProfile())
                    } else {
                        self.echoDeskSetupWaitingForFlash = false
                        self.echoDeskSetupFlashSucceeded = false
                        self.echoDeskFooterStatus.stringValue = message
                        self.setEchoDeskSetupStep(.choice)
                    }
                }
            } else {
                self.setEchoDeskSetupStep(.quickSettings)
            }
        }
    }

    @objc private func echoDeskApplySetupQuickSettings() {
        let theme = echoDeskSetupQuickTheme.selectedSegment == 0 ? "light" : "dark"
        let clock = echoDeskSetupQuickClockFormat.indexOfSelectedItem == 0 ? "12h" : "24h"
        if UserDefaults.standard.string(forKey: "EchoDeskGuidedSetupTransportV2") == "wifi" {
            guard UserDefaults.standard.string(forKey: "EchoDeskPendingWiFiDeviceID") != nil else {
                echoDeskFooterStatus.stringValue = "WiFi setup identity is missing. Find and provision EchoDesk again."
                setEchoDeskSetupStep(.wifi)
                return
            }
            // WiFi v1 provisions credentials but intentionally exposes no LAN
            // settings mutation endpoint. Retain these choices and apply them
            // through the authenticated serial protocol on the next USB link.
            UserDefaults.standard.set(theme, forKey: "EchoDeskPendingTheme")
            UserDefaults.standard.set(clock, forKey: "EchoDeskPendingClockFormat")
            echoDeskFooterStatus.stringValue = "Quick settings saved. They will be applied on the next USB connection."
            setEchoDeskSetupStep(.welcome)
            return
        }
        guard monitor?.serialPort.state == .connected else {
            echoDeskFooterStatus.stringValue = "Reconnect EchoDesk through USB to apply quick settings."
            return
        }
        guard let revision = echoDeskLastState?["revision"] as? Int else {
            requestEchoDeskState()
            echoDeskFooterStatus.stringValue = "Refreshing device settings…"
            return
        }
        sendEchoDeskCommand("echodesk_set_settings", extra: [
            "baseRevision": revision, "persist": true,
            "settings": ["theme": theme, "clockFormat": clock],
        ]) { [weak self] ok in
            if ok { self?.completeEchoDeskGuidedSetup() }
            else { self?.echoDeskFooterStatus.stringValue = "EchoDesk did not accept quick settings." }
        }
    }

    private func applyDeferredEchoDeskQuickSettingsIfNeeded() {
        guard !echoDeskApplyingDeferredSettings,
              monitor?.serialPort.state == .connected,
              let profile = DeviceRegistry.shared.currentProfile(),
              echoDeskLastStatePort == monitor?.serialPort.connectedPort,
              echoDeskLastStateMAC == profile.mac,
              let revision = echoDeskLastState?["revision"] as? Int else { return }
        let defaults = UserDefaults.standard
        guard let pendingDeviceID = defaults.string(forKey: "EchoDeskPendingWiFiDeviceID")?.lowercased(),
              profile.mac.replacingOccurrences(of: ":", with: "").lowercased() == pendingDeviceID else { return }
        let theme = defaults.string(forKey: "EchoDeskPendingTheme")
        let clock = defaults.string(forKey: "EchoDeskPendingClockFormat")
        guard theme != nil || clock != nil else { return }
        var settings: [String: Any] = [:]
        if let theme { settings["theme"] = theme }
        if let clock { settings["clockFormat"] = clock }
        echoDeskApplyingDeferredSettings = true
        sendEchoDeskCommand("echodesk_set_settings", extra: [
            "baseRevision": revision, "persist": true, "settings": settings,
        ]) { [weak self] ok in
            guard let self else { return }
            self.echoDeskApplyingDeferredSettings = false
            if ok {
                defaults.removeObject(forKey: "EchoDeskPendingTheme")
                defaults.removeObject(forKey: "EchoDeskPendingClockFormat")
                defaults.removeObject(forKey: "EchoDeskPendingWiFiDeviceID")
                self.echoDeskFooterStatus?.stringValue = "Saved WiFi setup choices were applied to EchoDesk."
                self.requestEchoDeskState()
            }
        }
    }

    private func completeEchoDeskGuidedSetup() {
        sendEchoDeskCommand("echodesk_setup_complete") { [weak self] ok in
            guard let self else { return }
            self.echoDeskSetupWaitingForFlash = false
            self.echoDeskSetupFlashSucceeded = false
            if ok {
                UserDefaults.standard.removeObject(forKey: "EchoDeskPendingSetupPortV2")
                UserDefaults.standard.removeObject(forKey: "EchoDeskGuidedSetupTransportV2")
                self.echoDeskPendingSetupPort = nil
                self.setEchoDeskSetupStep(.welcome)
            }
            else { self.echoDeskFooterStatus.stringValue = "EchoDesk did not confirm setup completion." }
        }
    }

    @objc private func echoDeskCancelGuidedSetup() {
        guard !FirmwareManager.shared.isFlashing && !FirmwareManager.shared.isDownloading else {
            echoDeskFooterStatus.stringValue = "Firmware is being written. Keep EchoDesk connected until verification finishes."
            return
        }
        echoDeskSetupWaitingForFlash = false
        echoDeskSetupFlashSucceeded = false
        setEchoDeskSetupStep(.choice)
    }

    @objc private func echoDeskFinishSetup() {
        UserDefaults.standard.removeObject(forKey: "EchoDeskGuidedSetupTransportV2")
        setEchoDeskSetupStep(.connect)
        selectEchoDeskPage(.overview)
    }

    @objc private func echoDeskGuidedWiFiFind() {
        echoDeskGuidedWiFiStatus.stringValue = "Looking for EchoDesk on the current network…"
        EchoDeskWiFiSetupManager.shared.discover { [weak self] _ in self?.refreshEchoDeskGuidedWiFi() }
    }

    @objc private func echoDeskOpenMacWiFiSettings() {
        guard let url = URL(string: "x-apple.systempreferences:com.apple.wifi-settings-extension") else { return }
        NSWorkspace.shared.open(url)
    }

    private func refreshEchoDeskGuidedWiFi() {
        let manager = EchoDeskWiFiSetupManager.shared
        echoDeskGuidedWiFiDevicePopup.removeAllItems()
        for device in manager.devices where device.setupMode {
            echoDeskGuidedWiFiDevicePopup.addItem(withTitle: "\(device.displayName) — \(device.setupSSID ?? device.address)")
            echoDeskGuidedWiFiDevicePopup.lastItem?.representedObject = device.deviceId
        }
        if echoDeskGuidedWiFiDevicePopup.numberOfItems == 0 {
            echoDeskGuidedWiFiDevicePopup.addItem(withTitle: "No EchoDesk setup device found")
            echoDeskGuidedWiFiDevicePopup.isEnabled = false
        } else { echoDeskGuidedWiFiDevicePopup.isEnabled = true }
        switch manager.status {
        case .idle: echoDeskGuidedWiFiStatus.stringValue = "Join EchoDesk-XXXX, then find WiFi devices."
        case .scanning: echoDeskGuidedWiFiStatus.stringValue = "Looking for EchoDesk…"
        case .found: echoDeskGuidedWiFiStatus.stringValue = "EchoDesk found. Complete the form below."
        case .notFound: echoDeskGuidedWiFiStatus.stringValue = "No EchoDesk found. Confirm macOS is joined to EchoDesk-XXXX."
        case .provisioning: echoDeskGuidedWiFiStatus.stringValue = "Sending WiFi settings securely…"
        case .provisioned(let ssid): echoDeskGuidedWiFiStatus.stringValue = "EchoDesk joined \(ssid)."
        case .cancelled: echoDeskGuidedWiFiStatus.stringValue = "WiFi setup cancelled."
        case .failed(let message): echoDeskGuidedWiFiStatus.stringValue = message
        }
    }

    @objc private func echoDeskGuidedWiFiProvision() {
        guard let deviceID = echoDeskGuidedWiFiDevicePopup.selectedItem?.representedObject as? String else {
            echoDeskGuidedWiFiStatus.stringValue = "Find and select EchoDesk first."; return
        }
        let code = echoDeskGuidedWiFiCode.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        let ssid = echoDeskGuidedWiFiSSID.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        let password = echoDeskGuidedWiFiPassword.stringValue
        EchoDeskWiFiSetupManager.shared.provision(deviceId: deviceID, setupCode: code,
            ssid: ssid, password: password) { [weak self] result in
            guard let self else { return }
            switch result {
            case .success:
                UserDefaults.standard.set(deviceID.lowercased(), forKey: "EchoDeskPendingWiFiDeviceID")
                self.echoDeskGuidedWiFiPassword.stringValue = ""
                self.setEchoDeskSetupStep(.quickSettings)
            case .failure(let error): self.echoDeskGuidedWiFiStatus.stringValue = error.localizedDescription
            }
        }
    }

    @objc private func echoDeskRecoveryScan() { refreshEchoDeskRecoveryDevices() }
    func refreshEchoDeskRecoveryDevices() {
        guard echoDeskRecoveryDevicePopup != nil, let monitor else { return }
        let selected = echoDeskRecoveryDevicePopup.selectedItem?.representedObject as? String
        echoDeskRecoveryDevicePopup.removeAllItems()
        for port in monitor.serialPort.availablePortPaths() {
            let prefix = port == monitor.serialPort.connectedPort && monitor.serialPort.state == .connected
                ? "EchoDesk Mini" : "USB serial device"
            echoDeskRecoveryDevicePopup.addItem(withTitle: "\(prefix) — \(port)")
            echoDeskRecoveryDevicePopup.lastItem?.representedObject = port
        }
        if echoDeskRecoveryDevicePopup.numberOfItems == 0 {
            echoDeskRecoveryDevicePopup.addItem(withTitle: "No serial devices detected")
            echoDeskRecoveryDevicePopup.isEnabled = false
        } else {
            echoDeskRecoveryDevicePopup.isEnabled = true
            if let selected, let item = echoDeskRecoveryDevicePopup.itemArray.first(where: { ($0.representedObject as? String) == selected }) {
                echoDeskRecoveryDevicePopup.select(item)
            }
        }
    }

    @objc private func echoDeskRecoveryActionPressed(_ sender: NSButton) {
        guard let port = echoDeskRecoveryDevicePopup.selectedItem?.representedObject as? String else {
            echoDeskRecoveryStatus.stringValue = "Select a USB serial device first."; return
        }
        let action = sender.tag
        echoDeskPendingRecoveryPort = port
        echoDeskPendingRecoveryDeviceMAC = monitor?.serialPort.connectedPort == port
            ? DeviceRegistry.shared.currentProfile()?.mac : nil
        echoDeskPendingRecoveryAction = action
        echoDeskRecoveryOperationStarted = action == 3
        echoDeskRecoveryFlashSucceeded = false
        echoDeskRecoveryResetSent = false
        // Pin every recovery operation to the user's explicit selection. The
        // scanner must not silently fall back to a different serial adapter.
        Settings.shared.manualPortPath = port
        echoDeskRecoveryStatus.stringValue = action == 3
            ? "Verifying that \(port) is an original ESP32 before recovery…"
            : "Preparing EchoDesk OS for \(port)…"
        echoDeskRecoveryProgress.isHidden = false; echoDeskRecoveryProgress.startAnimation(nil)
        echoDeskRecoveryCompletion.isHidden = true
        echoDeskRecoveryDevicePopup.isEnabled = false
        setEchoDeskRecoveryActionsEnabled(false)
        if action == 3 { performEchoDeskVerifiedRecovery(port: port) }
        else {
            FirmwareManager.shared.flashPhase = .idle
            if monitor?.serialPort.connectedPort != port || monitor?.serialPort.state != .connected {
                monitor?.serialPort.requestReconnect()
                echoDeskRecoveryStatus.stringValue = "Connecting to \(port)…"
            } else {
                continueEchoDeskRecoveryAfterReconnect()
            }
        }
    }

    private func continueEchoDeskRecoveryAfterReconnect() {
        guard let port = echoDeskPendingRecoveryPort,
              let action = echoDeskPendingRecoveryAction,
              action != 3,
              monitor?.serialPort.connectedPort == port,
              monitor?.serialPort.state == .connected,
              !echoDeskRecoveryOperationStarted else { return }
        echoDeskRecoveryOperationStarted = true
        echoDeskRecoveryStatus.stringValue = "EchoDesk verified. Preparing firmware…"
        guard let delegate = NSApp.delegate as? AppDelegate else {
            finishEchoDeskRecovery(success: false,
                detail: "EchoDesk Desktop could not start the firmware service.")
            return
        }
        delegate.runFirmwareFlash { [weak self] success, message in
            guard let self else { return }
            if success {
                self.echoDeskRecoveryFlashSucceeded = true
                self.echoDeskRecoveryStatus.stringValue = "Firmware verified. Waiting for the selected EchoDesk to reconnect…"
                self.refreshEchoDeskRecoveryProgress()
            } else {
                self.finishEchoDeskRecovery(success: false, detail: message)
            }
        }
    }

    private func performEchoDeskVerifiedRecovery(port: String) {
        guard let monitor else { return }
        monitor.serialPort.stopScanning()
        FirmwareManager.shared.detectChip(port: port) { [weak self] chip in
            DispatchQueue.main.async {
                guard let self else { return }
                guard chip?.esptoolName == kFirmwareChipESP32.esptoolName else {
                    monitor.serialPort.startScanning()
                    self.finishEchoDeskRecovery(success: false,
                        detail: "Recovery stopped: this port did not identify as the expected ESP32. Nothing was written.")
                    return
                }
                FirmwareManager.shared.downloadFirmware(variant: kDisplayVariantST7789) { ok, message in
                    DispatchQueue.main.async {
                        guard ok else {
                            monitor.serialPort.startScanning()
                            self.finishEchoDeskRecovery(success: false,
                                detail: message ?? "The verified firmware image is unavailable.")
                            return
                        }
                        FirmwareManager.shared.beginFlashAttempt(port: port, variant: kDisplayVariantST7789)
                        FirmwareManager.shared.flashFirmware(port: port, variant: kDisplayVariantST7789) { success, detail in
                            DispatchQueue.main.async {
                                monitor.serialPort.startScanning()
                                if success {
                                    self.echoDeskRecoveryFlashSucceeded = true
                                    self.echoDeskRecoveryStatus.stringValue = "Firmware verified. Waiting for the selected EchoDesk to reconnect…"
                                    self.refreshEchoDeskRecoveryProgress()
                                } else {
                                    self.finishEchoDeskRecovery(success: false, detail: detail)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    private func refreshEchoDeskRecoveryProgress() {
        let fw = FirmwareManager.shared
        if fw.isFlashing || fw.isDownloading {
            echoDeskRecoveryProgress?.isHidden = false
            echoDeskRecoveryStatus?.stringValue = fw.flashProgress.isEmpty ? "Preparing EchoDesk OS…" : fw.flashProgress
        } else if echoDeskRecoveryOperationStarted, echoDeskRecoveryFlashSucceeded,
                  let expectedPort = echoDeskPendingRecoveryPort,
                  monitor?.serialPort.connectedPort == expectedPort,
                  monitor?.serialPort.state == .connected,
                  let expectedMAC = echoDeskPendingRecoveryDeviceMAC,
                  DeviceRegistry.shared.currentProfile()?.mac != expectedMAC {
            finishEchoDeskRecovery(success: false,
                detail: "A different EchoDesk reconnected on the selected port. Recovery stopped before reset.")
        } else if echoDeskRecoveryOperationStarted, echoDeskRecoveryFlashSucceeded,
                  let expectedPort = echoDeskPendingRecoveryPort,
                  fw.lastFlashPort == expectedPort,
                  monitor?.serialPort.connectedPort == expectedPort,
                  monitor?.serialPort.state == .connected,
                  (echoDeskPendingRecoveryDeviceMAC == nil
                    || DeviceRegistry.shared.currentProfile()?.mac == echoDeskPendingRecoveryDeviceMAC) {
            let action = echoDeskPendingRecoveryAction ?? 0
            if action == 0 {
                finishEchoDeskRecovery(success: true)
            } else if !echoDeskRecoveryResetSent {
                echoDeskRecoveryResetSent = true
                echoDeskRecoveryStatus?.stringValue = "Firmware verified. Resetting EchoDesk setup state…"
                sendEchoDeskCommand("echodesk_factory_reset") { [weak self] ok in
                    self?.finishEchoDeskRecovery(success: ok,
                        detail: ok ? nil : "EchoDesk did not confirm the setup reset.")
                }
            }
        }
    }

    private func finishEchoDeskRecovery(success: Bool, detail: String? = nil) {
        echoDeskRecoveryProgress?.stopAnimation(nil)
        echoDeskRecoveryProgress?.isHidden = true
        if success {
            echoDeskRecoveryStatus?.stringValue = "EchoDesk OS was written, verified and reconnected."
            echoDeskRecoveryCompletion?.isHidden = false
        } else if let detail {
            echoDeskRecoveryStatus?.stringValue = detail
        }
        echoDeskPendingRecoveryPort = nil
        echoDeskPendingRecoveryDeviceMAC = nil
        echoDeskPendingRecoveryAction = nil
        echoDeskRecoveryOperationStarted = false
        echoDeskRecoveryFlashSucceeded = false
        echoDeskRecoveryResetSent = false
        echoDeskRecoveryDevicePopup?.isEnabled = (echoDeskRecoveryDevicePopup?.numberOfItems ?? 0) > 0
        setEchoDeskRecoveryActionsEnabled(true)
    }

    private func setEchoDeskRecoveryActionsEnabled(_ enabled: Bool) {
        func update(_ view: NSView) {
            if let control = view as? NSControl { control.isEnabled = enabled }
            view.subviews.forEach(update)
        }
        echoDeskRecoveryActions?.arrangedSubviews.forEach(update)
    }

    @objc private func echoDeskSetupAfterRecovery() {
        selectEchoDeskPage(.setup)
        setEchoDeskSetupStep(.connect)
    }

    // MARK: Components

    private func echoDeskPageHeader(_ title: String, _ subtitle: String) -> NSView {
        let heading = NSTextField(labelWithString: title)
        heading.font = .systemFont(ofSize: 30, weight: .semibold)
        let detail = echoDeskMuted(subtitle)
        return echoDeskPageStack([heading, detail], spacing: 4)
    }

    private func echoDeskHero(_ text: String) -> NSTextField {
        let label = NSTextField(wrappingLabelWithString: text)
        label.font = .systemFont(ofSize: 24, weight: .semibold)
        return label
    }

    private func echoDeskSectionTitle(_ text: String) -> NSTextField {
        let label = NSTextField(labelWithString: text)
        label.font = .systemFont(ofSize: 18, weight: .semibold)
        return label
    }

    private func echoDeskMuted(_ text: String) -> NSTextField {
        let label = NSTextField(wrappingLabelWithString: text)
        label.font = .systemFont(ofSize: 13)
        label.textColor = .secondaryLabelColor
        label.maximumNumberOfLines = 4
        return label
    }

    private func echoDeskMetricValue(_ text: String) -> NSTextField {
        let label = NSTextField(labelWithString: text)
        label.font = .systemFont(ofSize: 17, weight: .semibold)
        label.lineBreakMode = .byTruncatingTail
        return label
    }

    private func echoDeskMetric(_ title: String, _ value: NSTextField) -> NSView {
        let caption = NSTextField(labelWithString: title)
        caption.font = .systemFont(ofSize: 10, weight: .medium)
        caption.textColor = .secondaryLabelColor
        return echoDeskCard(echoDeskPageStack([caption, value], spacing: 8))
    }

    private func echoDeskPageStack(_ views: [NSView], spacing: CGFloat = 14) -> NSStackView {
        let stack = NSStackView(views: views)
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = spacing
        stack.edgeInsets = NSEdgeInsets(top: 0, left: 0, bottom: 22, right: 0)
        for view in views { view.widthAnchor.constraint(lessThanOrEqualTo: stack.widthAnchor).isActive = true }
        return stack
    }

    private func echoDeskCard(_ content: NSView, padding: CGFloat = 18) -> NSView {
        let card = EchoDeskSurfaceView(.card)
        content.translatesAutoresizingMaskIntoConstraints = false
        card.addSubview(content)
        NSLayoutConstraint.activate([
            content.leadingAnchor.constraint(equalTo: card.leadingAnchor, constant: padding),
            content.trailingAnchor.constraint(equalTo: card.trailingAnchor, constant: -padding),
            content.topAnchor.constraint(equalTo: card.topAnchor, constant: padding),
            content.bottomAnchor.constraint(equalTo: card.bottomAnchor, constant: -padding),
        ])
        return card
    }

    private func echoDeskPrimaryButton(_ title: String, _ action: Selector) -> NSButton {
        let button = NSButton(title: title, target: self, action: action)
        button.bezelStyle = .rounded
        button.keyEquivalent = ""
        button.contentTintColor = .controlAccentColor
        return button
    }

    private func echoDeskChoiceCard(_ title: String, _ detail: String, _ buttonTitle: String,
                                    _ action: Selector) -> NSView {
        let heading = echoDeskSectionTitle(title)
        let description = echoDeskMuted(detail)
        description.maximumNumberOfLines = 5
        return echoDeskCard(echoDeskPageStack([heading, description, echoDeskPrimaryButton(buttonTitle, action)]))
    }

    private func echoDeskRecoveryAction(_ tag: Int, _ title: String, _ detail: String) -> NSView {
        let heading = echoDeskSectionTitle(title)
        let text = echoDeskMuted(detail)
        let button = NSButton(title: title, target: self, action: #selector(echoDeskRecoveryActionPressed(_:)))
        button.tag = tag; button.bezelStyle = .rounded
        let row = NSStackView(views: [echoDeskPageStack([heading, text], spacing: 5), NSView(), button])
        row.orientation = .horizontal; row.alignment = .centerY
        return echoDeskCard(row)
    }

    private func echoDeskLabeledControl(_ label: String, _ control: NSView) -> NSView {
        let caption = NSTextField(labelWithString: label)
        caption.font = .systemFont(ofSize: 13, weight: .medium)
        caption.translatesAutoresizingMaskIntoConstraints = false
        caption.widthAnchor.constraint(equalToConstant: 180).isActive = true
        let row = NSStackView(views: [caption, control])
        row.orientation = .horizontal; row.alignment = .centerY; row.spacing = 16
        return row
    }

    private func echoDeskSettingPopup(_ key: String, _ options: [(String, String)]) -> NSPopUpButton {
        let popup = EchoDeskPopupButton(frame: .zero, pullsDown: false)
        popup.identifier = NSUserInterfaceItemIdentifier(key)
        popup.target = self
        popup.action = #selector(echoDeskSettingPopupChanged(_:))
        for (title, value) in options {
            popup.addItem(withTitle: title)
            popup.lastItem?.representedObject = value
        }
        popup.translatesAutoresizingMaskIntoConstraints = false
        popup.widthAnchor.constraint(greaterThanOrEqualToConstant: 230).isActive = true
        echoDeskSettingControls[key] = popup
        return popup
    }

    private func echoDeskAppGrid(columns: Int) -> NSView {
        let apps = [("home", "Home", "house"), ("codex", "Codex Usage", "curlybraces"),
                    ("clock", "Clock", "clock"), ("performance", "Performance", "waveform.path.ecg"),
                    ("nowPlaying", "Now Playing", "music.note"), ("settings", "Settings", "gearshape")]
        let grid = NSGridView(views: [])
        grid.rowSpacing = 10; grid.columnSpacing = 10
        for start in stride(from: 0, to: apps.count, by: columns) {
            let slice = apps[start..<min(start + columns, apps.count)]
            var row = slice.map { app -> NSView in
                let (id, title, symbol) = app
                let button = NSButton(title: title, target: self, action: #selector(echoDeskOpenApp(_:)))
                button.identifier = NSUserInterfaceItemIdentifier(id)
                button.image = NSImage(systemSymbolName: symbol, accessibilityDescription: nil)
                button.imagePosition = .imageLeading; button.alignment = .left; button.bezelStyle = .rounded
                button.translatesAutoresizingMaskIntoConstraints = false
                button.heightAnchor.constraint(equalToConstant: 46).isActive = true
                return button
            }
            while row.count < columns { row.append(NSView()) }
            grid.addRow(with: row)
        }
        return grid
    }

    private func echoDeskDangerRow(_ title: String, _ detail: String, _ buttonTitle: String,
                                   _ action: Selector) -> NSView {
        let text = echoDeskPageStack([echoDeskSectionTitle(title), echoDeskMuted(detail)], spacing: 5)
        let button = NSButton(title: buttonTitle, target: self, action: action)
        button.bezelStyle = .rounded; button.contentTintColor = .systemRed
        let row = NSStackView(views: [text, NSView(), button])
        row.orientation = .horizontal; row.alignment = .centerY
        return row
    }

    private func echoDeskDivider() -> NSView {
        let line = NSBox(); line.boxType = .separator
        return line
    }

    private func echoDeskConfirm(title: String, detail: String, action: String, run: @escaping () -> Void) {
        let alert = NSAlert(); alert.messageText = title; alert.informativeText = detail
        alert.addButton(withTitle: action); alert.addButton(withTitle: "Cancel")
        guard let window else { return }
        alert.beginSheetModal(for: window) { response in if response == .alertFirstButtonReturn { run() } }
    }

    private func styleEchoDeskDropdowns(in view: NSView) {
        if let popup = view as? NSPopUpButton {
            popup.bezelStyle = .rounded
            popup.controlSize = .large
            popup.font = .systemFont(ofSize: 13, weight: .medium)
        }
        view.subviews.forEach(styleEchoDeskDropdowns)
    }
}

private extension String {
    var echoDeskTitle: String {
        switch self {
        case "nowPlaying": return "Now Playing"
        default: return prefix(1).uppercased() + String(dropFirst())
        }
    }
}
#endif
