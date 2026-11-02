#if ECHODESK_DESKTOP
import Cocoa
import UserNotifications

private let echoDeskFirstRunOnboardingKey = "EchoDeskFirstRunOnboardingCompletedV1"

private enum EchoDeskDesign {
    static let sidebarWidth: CGFloat = 218
    static let pageInset: CGFloat = 20
    static let cardRadius: CGFloat = 9
    static let sectionSpacing: CGFloat = 18
    static let contentWidth: CGFloat = 880
    static let compactWidth: CGFloat = 680
    static let controlHeight: CGFloat = 34
}

enum EchoDeskDesktopPage: Int, CaseIterable {
    case overview, deviceControl, customisation, bedside, setup, recovery
}

enum EchoDeskSetupStep: Int {
    case connect, usbDevice, wifi, choice, working, quickSettings, welcome
}

private enum EchoDeskCustomisationSection: String, CaseIterable {
    case display, bedside, clock, nowPlaying, codex, performance, pcDashboard, preferences

    var title: String {
        switch self {
        case .display: return "Display & Appearance"
        case .bedside: return "Bedside Mode"
        case .clock: return "Clock"
        case .nowPlaying: return "Now Playing"
        case .codex: return "Codex Usage"
        case .performance: return "Performance"
        case .pcDashboard: return "PC Dashboard"
        case .preferences: return "General Preferences"
        }
    }

    var subtitle: String {
        switch self {
        case .display: return "Brightness, appearance and startup app"
        case .bedside: return "Clock, alarms, widgets and display schedules"
        case .clock: return "Time format and timezone"
        case .nowPlaying: return "Music layout and background"
        case .codex: return "Usage percentages and preferred limits"
        case .performance: return "Default hardware monitoring view"
        case .pcDashboard: return "Choose and order up to four live metrics"
        case .preferences: return "EchoDesk Desktop appearance"
        }
    }

    var symbol: String {
        switch self {
        case .display: return "sun.max.fill"
        case .bedside: return "bed.double.fill"
        case .clock: return "clock.fill"
        case .nowPlaying: return "music.note"
        case .codex: return "curlybraces"
        case .performance: return "waveform.path.ecg"
        case .pcDashboard: return "gauge.with.dots.needle.67percent"
        case .preferences: return "gearshape.fill"
        }
    }
}

final class EchoDeskSurfaceView: NSView {
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
            layer?.backgroundColor = NSColor.clear.cgColor
            layer?.borderColor = NSColor.separatorColor.cgColor
            layer?.borderWidth = 0
        case .card:
            // Settings groups are deliberately flat. A separator is installed
            // by echoDeskCard(_:padding:) so pages read like native macOS forms
            // instead of a collection of web-dashboard cards.
            layer?.backgroundColor = NSColor.clear.cgColor
            layer?.borderColor = NSColor.clear.cgColor
            layer?.borderWidth = 0
            layer?.cornerRadius = 0
        case .canvas:
            layer?.backgroundColor = NSColor.windowBackgroundColor.cgColor
        }
    }
}

final class EchoDeskSidebarButton: NSButton {
    private let selectionBar = CALayer()
    private var tracking: NSTrackingArea?
    private var hovered = false
    var isCurrentPage = false { didSet { refreshAppearance() } }

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        wantsLayer = true
        layer?.cornerRadius = 6
        layer?.cornerCurve = .continuous
        selectionBar.cornerRadius = 1.5
        layer?.addSublayer(selectionBar)
        isBordered = false
        bezelStyle = .recessed
        imagePosition = .imageLeading
        imageScaling = .scaleProportionallyDown
        alignment = .left
        font = .systemFont(ofSize: 12.5, weight: .medium)
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) is not supported") }

    override func updateTrackingAreas() {
        if let tracking { removeTrackingArea(tracking) }
        let next = NSTrackingArea(rect: bounds, options: [.activeInKeyWindow, .mouseEnteredAndExited],
                                  owner: self, userInfo: nil)
        addTrackingArea(next)
        tracking = next
        super.updateTrackingAreas()
    }

    override func mouseEntered(with event: NSEvent) { hovered = true; refreshAppearance() }
    override func mouseExited(with event: NSEvent) { hovered = false; refreshAppearance() }
    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        refreshAppearance()
    }
    override func layout() {
        super.layout()
        selectionBar.frame = NSRect(x: 2, y: 8, width: 3, height: max(0, bounds.height - 16))
    }

    private func refreshAppearance() {
        contentTintColor = isCurrentPage ? .controlAccentColor : .labelColor
        selectionBar.backgroundColor = NSColor.controlAccentColor.cgColor
        selectionBar.isHidden = !isCurrentPage
        layer?.backgroundColor = isCurrentPage
            ? NSColor.controlAccentColor.withAlphaComponent(0.13).cgColor
            : hovered ? NSColor.labelColor.withAlphaComponent(0.055).cgColor : NSColor.clear.cgColor
    }
}

/// A native, compact navigation tile used by focused settings landing pages.
/// The entire tile is one keyboard-accessible button rather than a decorative
/// card containing a hidden hit target.
final class EchoDeskActionTile: NSButton {
    private let symbolView = NSImageView()
    private let titleLabel = NSTextField(labelWithString: "")
    private let detailLabel = NSTextField(wrappingLabelWithString: "")
    private let disclosure = NSImageView()
    private var tracking: NSTrackingArea?
    private var hovered = false

    init(title: String, detail: String, symbol: String, target: AnyObject?, action: Selector?) {
        super.init(frame: .zero)
        self.target = target
        self.action = action
        isBordered = false
        wantsLayer = true
        layer?.cornerRadius = 7
        layer?.cornerCurve = .continuous
        layer?.borderWidth = 0
        focusRingType = .exterior

        symbolView.image = NSImage(systemSymbolName: symbol, accessibilityDescription: nil)
        symbolView.symbolConfiguration = NSImage.SymbolConfiguration(pointSize: 17, weight: .medium)
        symbolView.contentTintColor = .controlAccentColor
        symbolView.imageScaling = .scaleProportionallyDown
        symbolView.translatesAutoresizingMaskIntoConstraints = false
        symbolView.widthAnchor.constraint(equalToConstant: 28).isActive = true
        symbolView.heightAnchor.constraint(equalToConstant: 28).isActive = true

        titleLabel.stringValue = title
        titleLabel.font = .systemFont(ofSize: 14, weight: .semibold)
        detailLabel.stringValue = detail
        detailLabel.font = .systemFont(ofSize: 11)
        detailLabel.textColor = .secondaryLabelColor
        detailLabel.maximumNumberOfLines = 2
        let labels = NSStackView(views: [titleLabel, detailLabel])
        labels.orientation = .vertical
        labels.alignment = .leading
        labels.spacing = 3

        disclosure.image = NSImage(systemSymbolName: "chevron.right", accessibilityDescription: nil)
        disclosure.symbolConfiguration = NSImage.SymbolConfiguration(pointSize: 11, weight: .semibold)
        disclosure.contentTintColor = .tertiaryLabelColor
        let row = NSStackView(views: [symbolView, labels, NSView(), disclosure])
        row.orientation = .horizontal
        row.alignment = .centerY
        row.spacing = 12
        row.translatesAutoresizingMaskIntoConstraints = false
        addSubview(row)
        NSLayoutConstraint.activate([
            row.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 10),
            row.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -10),
            row.topAnchor.constraint(equalTo: topAnchor, constant: 8),
            row.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -8),
            heightAnchor.constraint(greaterThanOrEqualToConstant: 58),
        ])
        setAccessibilityLabel(title)
        setAccessibilityHelp(detail)
        refreshAppearance()
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) is not supported") }

    override func updateTrackingAreas() {
        if let tracking { removeTrackingArea(tracking) }
        tracking = NSTrackingArea(rect: bounds, options: [.activeInKeyWindow, .mouseEnteredAndExited],
                                  owner: self, userInfo: nil)
        addTrackingArea(tracking!)
        super.updateTrackingAreas()
    }

    override func mouseEntered(with event: NSEvent) { hovered = true; refreshAppearance() }
    override func mouseExited(with event: NSEvent) { hovered = false; refreshAppearance() }
    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        refreshAppearance()
    }

    private func refreshAppearance() {
        layer?.borderColor = NSColor.clear.cgColor
        layer?.backgroundColor = (hovered ? NSColor.selectedContentBackgroundColor.withAlphaComponent(0.12)
                                         : NSColor.clear).cgColor
    }
}

/// Read-only segmented progress control. It provides the compact visual
/// language of a native macOS assistant without implying that future setup
/// stages can be selected before validation succeeds.
final class EchoDeskSetupProgressControl: NSSegmentedControl {
    override var acceptsFirstResponder: Bool { false }
    override func mouseDown(with event: NSEvent) { /* progress is not navigation */ }
    override func keyDown(with event: NSEvent) { /* progress is not navigation */ }
}

final class EchoDeskScreenPreview: NSView {
    enum Kind { case home, nowPlaying, clock, dashboard, appearance }
    var kind: Kind { didSet { needsDisplay = true } }
    var dark = true { didSet { needsDisplay = true } }
    var primaryText = "12:42" { didSet { needsDisplay = true } }
    var secondaryText = "EchoDesk" { didSet { needsDisplay = true } }

    init(_ kind: Kind) {
        self.kind = kind
        super.init(frame: .zero)
        wantsLayer = true
        setAccessibilityElement(true)
        setAccessibilityRole(.image)
        setAccessibilityLabel("EchoDesk screen preview")
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) is not supported") }

    override var intrinsicContentSize: NSSize { NSSize(width: 300, height: 176) }

    override func draw(_ dirtyRect: NSRect) {
        let frame = bounds.insetBy(dx: 1, dy: 1)
        let shape = NSBezierPath(roundedRect: frame, xRadius: 18, yRadius: 18)
        (dark ? NSColor(calibratedWhite: 0.055, alpha: 1) : NSColor(calibratedWhite: 0.96, alpha: 1)).setFill()
        shape.fill()
        NSColor.separatorColor.setStroke(); shape.lineWidth = 1; shape.stroke()
        let foreground = dark ? NSColor.white : NSColor.black
        let muted = foreground.withAlphaComponent(0.58)
        func text(_ value: String, size: CGFloat, weight: NSFont.Weight, y: CGFloat, color: NSColor = foreground) {
            let attributes: [NSAttributedString.Key: Any] = [.font: NSFont.systemFont(ofSize: size, weight: weight),
                                                              .foregroundColor: color]
            let measured = (value as NSString).size(withAttributes: attributes)
            (value as NSString).draw(at: NSPoint(x: (bounds.width - measured.width) / 2, y: y), withAttributes: attributes)
        }
        switch kind {
        case .clock, .appearance:
            text(primaryText, size: 38, weight: .medium, y: bounds.midY - 10)
            text(secondaryText, size: 11, weight: .medium, y: bounds.midY - 34, color: muted)
        case .nowPlaying:
            let art = NSBezierPath(roundedRect: NSRect(x: 22, y: 31, width: 84, height: 84), xRadius: 9, yRadius: 9)
            NSColor.controlAccentColor.withAlphaComponent(0.78).setFill(); art.fill()
            text(primaryText, size: 17, weight: .semibold, y: 92)
            text(secondaryText, size: 11, weight: .regular, y: 69, color: muted)
        case .dashboard:
            text("PC Dashboard", size: 13, weight: .semibold, y: 139)
            for (index, value) in ["CPU  24%", "RAM  61%", "GPU  12%", "Battery  82%"].enumerated() {
                let x = 22 + CGFloat(index % 2) * (bounds.width / 2 - 4)
                let y = 88 - CGFloat(index / 2) * 51
                let attributes: [NSAttributedString.Key: Any] = [.font: NSFont.monospacedSystemFont(ofSize: 11, weight: .medium),
                                                                  .foregroundColor: foreground]
                (value as NSString).draw(at: NSPoint(x: x, y: y), withAttributes: attributes)
            }
        case .home:
            text(primaryText, size: 25, weight: .semibold, y: 92)
            text(secondaryText, size: 12, weight: .regular, y: 68, color: muted)
        }
    }
}

final class EchoDeskAdaptiveStackView: NSStackView {
    private let collapseBelow: CGFloat
    private let equalColumns: Bool

    init(views: [NSView], collapseBelow: CGFloat, equalColumns: Bool = true) {
        self.collapseBelow = collapseBelow
        self.equalColumns = equalColumns
        // Avoid NSStackView's Objective-C convenience initializer here. Swift 6.3's
        // release optimizer can miscompile that initializer for subclasses and abort
        // with an ownership-check failure. Building the arranged-view list explicitly
        // is equivalent and remains stable in optimized Xcode 26 builds.
        super.init(frame: .zero)
        for view in views {
            addArrangedSubview(view)
        }
        spacing = 16
        orientation = .horizontal
        alignment = .top
        distribution = equalColumns ? .fillEqually : .fill
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) is not supported") }

    override func setFrameSize(_ newSize: NSSize) {
        let compact = newSize.width > 0 && newSize.width < collapseBelow
        let next: NSUserInterfaceLayoutOrientation = compact ? .vertical : .horizontal
        if orientation != next {
            orientation = next
            alignment = compact ? .width : .top
            distribution = compact ? .fill : (equalColumns ? .fillEqually : .fill)
            invalidateIntrinsicContentSize()
        }
        super.setFrameSize(newSize)
    }
}

private final class EchoDeskPopupButton: NSPopUpButton {
    override init(frame buttonFrame: NSRect, pullsDown flag: Bool) {
        super.init(frame: buttonFrame, pullsDown: flag)
        bezelStyle = .rounded
        controlSize = .regular
        font = .systemFont(ofSize: 13, weight: .regular)
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) is not supported") }
}

extension SettingsWindowController {
    // MARK: Desktop shell

    func buildEchoDeskDesktopUI(in root: NSView) {
        if UserDefaults.standard.string(forKey: "NSWindow Frame SettingsWindow") == nil {
            window?.setContentSize(NSSize(width: 1080, height: 720))
        }
        window?.minSize = NSSize(width: 820, height: 580)
        window?.title = "EchoDesk"
        window?.subtitle = "Desktop"
        window?.titleVisibility = .visible
        window?.titlebarAppearsTransparent = false
        window?.styleMask.remove(.fullSizeContentView)
        window?.titlebarSeparatorStyle = .automatic
        window?.isMovableByWindowBackground = false
        installEchoDeskNativeToolbar()

        let sidebar = NSVisualEffectView()
        sidebar.material = .sidebar
        sidebar.blendingMode = .behindWindow
        sidebar.state = .active
        let canvas = EchoDeskSurfaceView(.canvas)
        sidebar.translatesAutoresizingMaskIntoConstraints = false
        canvas.translatesAutoresizingMaskIntoConstraints = false
        root.addSubview(sidebar)
        root.addSubview(canvas)
        let sidebarWidth = sidebar.widthAnchor.constraint(equalToConstant: EchoDeskDesign.sidebarWidth)
        echoDeskSidebarView = sidebar
        echoDeskSidebarWidthConstraint = sidebarWidth
        NSLayoutConstraint.activate([
            sidebar.leadingAnchor.constraint(equalTo: root.leadingAnchor),
            sidebar.topAnchor.constraint(equalTo: root.topAnchor),
            sidebar.bottomAnchor.constraint(equalTo: root.bottomAnchor),
            sidebarWidth,
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
        presentEchoDeskFirstRunOnboardingIfNeeded(in: root)
    }

    private func installEchoDeskNativeToolbar() {
        let toolbar = NSToolbar(identifier: "com.echodesk.desktop.window-toolbar")
        toolbar.delegate = self
        toolbar.displayMode = .iconOnly
        toolbar.allowsUserCustomization = false
        toolbar.autosavesConfiguration = false
        window?.toolbar = toolbar
        window?.toolbarStyle = .unifiedCompact
    }

    @objc func echoDeskToggleSidebar() {
        guard let sidebar = echoDeskSidebarView,
              let width = echoDeskSidebarWidthConstraint else { return }
        let showing = width.constant > 0
        let geometry = {
            width.constant = showing ? 0 : EchoDeskDesign.sidebarWidth
            sidebar.alphaValue = showing ? 0 : 1
            self.window?.contentView?.layoutSubtreeIfNeeded()
        }
        if NSWorkspace.shared.accessibilityDisplayShouldReduceMotion {
            geometry()
            sidebar.isHidden = showing
        } else {
            if !showing { sidebar.isHidden = false }
            NSAnimationContext.runAnimationGroup { context in
                context.duration = 0.18
                context.allowsImplicitAnimation = true
                geometry()
            } completionHandler: {
                if showing { sidebar.isHidden = true }
            }
        }
    }

    private func presentEchoDeskFirstRunOnboardingIfNeeded(in root: NSView) {
        let defaults = UserDefaults.standard
        let savedSetupStep = EchoDeskSetupStep(rawValue: defaults.integer(forKey: "EchoDeskGuidedSetupStepV2")) ?? .connect
        let setupInProgress = savedSetupStep != .connect
        // A setup_begin flow already in progress is more authoritative than
        // first-run presentation. Let main.swift restore its saved step rather
        // than covering it or resetting it to Connect.
        guard !setupInProgress,
              !defaults.bool(forKey: echoDeskFirstRunOnboardingKey) else { return }

        let overlay = EchoDeskSurfaceView(.canvas)
        overlay.translatesAutoresizingMaskIntoConstraints = false
        overlay.setAccessibilityElement(true)
        overlay.setAccessibilityRole(.group)
        overlay.setAccessibilityLabel("EchoDesk Desktop first-run setup")
        root.addSubview(overlay, positioned: .above, relativeTo: nil)
        NSLayoutConstraint.activate([
            overlay.leadingAnchor.constraint(equalTo: root.leadingAnchor),
            overlay.trailingAnchor.constraint(equalTo: root.trailingAnchor),
            overlay.topAnchor.constraint(equalTo: root.topAnchor),
            overlay.bottomAnchor.constraint(equalTo: root.bottomAnchor),
        ])

        let mark = NSTextField(labelWithString: "⌗")
        mark.font = .systemFont(ofSize: 46, weight: .bold)
        mark.alignment = .center
        mark.textColor = .controlAccentColor
        let welcomeTitle = echoDeskHero("Welcome to the EchoDesk Desktop app")
        welcomeTitle.alignment = .center
        let welcomeDetail = echoDeskMuted("Your desktop companion for setup, customisation, Codex usage and recovery.")
        welcomeDetail.alignment = .center
        let continueButton = echoDeskPrimaryButton("Continue", #selector(echoDeskFirstRunContinue))
        continueButton.keyEquivalent = "\r"
        let welcomeStack = echoDeskPageStack([mark, welcomeTitle, welcomeDetail, continueButton])
        welcomeStack.alignment = .centerX
        let welcomeScreen = echoDeskCard(welcomeStack, padding: 40)

        let setupMark = NSImageView()
        setupMark.image = NSImage(systemSymbolName: "display.and.arrow.down",
                                  accessibilityDescription: nil)
        setupMark.symbolConfiguration = NSImage.SymbolConfiguration(pointSize: 42, weight: .medium)
        setupMark.contentTintColor = .controlAccentColor
        let setupTitle = echoDeskHero("Setup your first EchoDesk now?")
        setupTitle.alignment = .center
        let setupDetail = echoDeskMuted("You can set up by USB or WiFi now, or come back from Setup new device later.")
        setupDetail.alignment = .center
        let notNow = NSButton(title: "Not now", target: self, action: #selector(echoDeskFirstRunNotNow))
        let yes = echoDeskPrimaryButton("Yes", #selector(echoDeskFirstRunYes))
        yes.keyEquivalent = "\r"
        let setupActions = NSStackView(views: [notNow, yes])
        setupActions.orientation = .horizontal
        setupActions.alignment = .centerY
        setupActions.spacing = 10
        let setupStack = echoDeskPageStack([setupMark, setupTitle, setupDetail, setupActions])
        setupStack.alignment = .centerX
        let setupScreen = echoDeskCard(setupStack, padding: 40)
        setupScreen.isHidden = true

        for screen in [welcomeScreen, setupScreen] {
            screen.translatesAutoresizingMaskIntoConstraints = false
            overlay.addSubview(screen)
            NSLayoutConstraint.activate([
                screen.centerXAnchor.constraint(equalTo: overlay.centerXAnchor),
                screen.centerYAnchor.constraint(equalTo: overlay.centerYAnchor),
                screen.widthAnchor.constraint(equalToConstant: 580),
                screen.widthAnchor.constraint(lessThanOrEqualTo: overlay.widthAnchor, constant: -48),
            ])
        }
        echoDeskFirstRunOverlay = overlay
        echoDeskFirstRunWelcomeScreen = welcomeScreen
        echoDeskFirstRunSetupScreen = setupScreen
        echoDeskFirstRunYesButton = yes
        window?.makeFirstResponder(continueButton)
    }

    @objc private func echoDeskFirstRunContinue() {
        echoDeskFirstRunWelcomeScreen?.isHidden = true
        echoDeskFirstRunSetupScreen?.isHidden = false
        window?.makeFirstResponder(echoDeskFirstRunYesButton)
    }

    @objc private func echoDeskFirstRunYes() {
        completeEchoDeskFirstRunOnboarding(openSetup: true)
    }

    @objc private func echoDeskFirstRunNotNow() {
        completeEchoDeskFirstRunOnboarding(openSetup: false)
    }

    private func completeEchoDeskFirstRunOnboarding(openSetup: Bool) {
        UserDefaults.standard.set(true, forKey: echoDeskFirstRunOnboardingKey)
        echoDeskFirstRunOverlay?.removeFromSuperview()
        echoDeskFirstRunOverlay = nil
        echoDeskFirstRunWelcomeScreen = nil
        echoDeskFirstRunSetupScreen = nil
        echoDeskFirstRunYesButton = nil
        if openSetup {
            selectEchoDeskPage(.setup)
            setEchoDeskSetupStep(.connect)
        } else {
            selectEchoDeskPage(.overview)
        }
    }

    private func buildEchoDeskSidebar(in sidebar: NSView) {
        let mark = NSImageView()
        mark.image = NSImage(systemSymbolName: "display", accessibilityDescription: "EchoDesk")
        mark.symbolConfiguration = NSImage.SymbolConfiguration(pointSize: 19, weight: .semibold)
        mark.contentTintColor = .controlAccentColor
        mark.imageScaling = .scaleProportionallyDown
        mark.translatesAutoresizingMaskIntoConstraints = false
        mark.widthAnchor.constraint(equalToConstant: 30).isActive = true
        mark.heightAnchor.constraint(equalToConstant: 30).isActive = true

        let brand = NSTextField(labelWithString: "EchoDesk")
        brand.font = .systemFont(ofSize: 17, weight: .semibold)
        let version = NSTextField(labelWithString: "Desktop \(kAppVersion)")
        version.font = .systemFont(ofSize: 10, weight: .regular)
        version.textColor = .secondaryLabelColor
        let brandText = NSStackView(views: [brand, version])
        brandText.orientation = .vertical
        brandText.alignment = .leading
        brandText.spacing = 1
        let brandRow = NSStackView(views: [mark, brandText])
        brandRow.orientation = .horizontal
        brandRow.alignment = .centerY
        brandRow.spacing = 9

        func makeNavigationButton(_ item: (EchoDeskDesktopPage, String, String)) -> NSButton {
            let (page, title, symbol) = item
            let button = EchoDeskSidebarButton(frame: .zero)
            button.title = title
            button.target = self
            button.action = #selector(echoDeskNavigationPressed(_:))
            button.tag = page.rawValue
            button.image = NSImage(systemSymbolName: symbol, accessibilityDescription: nil)?
                .withSymbolConfiguration(NSImage.SymbolConfiguration(pointSize: 16, weight: .medium))
            button.translatesAutoresizingMaskIntoConstraints = false
            button.heightAnchor.constraint(equalToConstant: 32).isActive = true
            button.setAccessibilityLabel(title)
            return button
        }

        let generalItems: [(EchoDeskDesktopPage, String, String)] = [
            (.overview, "Overview", "house.fill"),
            (.deviceControl, "Device Control", "switch.2"),
            (.customisation, "Customisation", "paintbrush.fill"),
        ]
        let managementItems: [(EchoDeskDesktopPage, String, String)] = [
            (.setup, "Set Up New Device", "display.badge.plus"),
            (.recovery, "Recovery & Updates", "heart.text.square.fill"),
        ]
        let generalButtons = generalItems.map(makeNavigationButton)
        let managementButtons = managementItems.map(makeNavigationButton)
        echoDeskSidebarButtons = generalButtons + managementButtons

        func groupLabel(_ title: String) -> NSTextField {
            let label = NSTextField(labelWithString: title.uppercased())
            label.font = .systemFont(ofSize: 10, weight: .semibold)
            label.textColor = .tertiaryLabelColor
            return label
        }
        func navigationGroup(_ title: String, _ buttons: [NSButton]) -> NSStackView {
            let buttonStack = NSStackView(views: buttons)
            buttonStack.orientation = .vertical
            buttonStack.alignment = .leading
            buttonStack.spacing = 1
            buttons.forEach { $0.widthAnchor.constraint(equalTo: buttonStack.widthAnchor).isActive = true }
            let group = NSStackView(views: [groupLabel(title), buttonStack])
            group.orientation = .vertical
            group.alignment = .leading
            group.spacing = 5
            buttonStack.widthAnchor.constraint(equalTo: group.widthAnchor).isActive = true
            return group
        }
        let nav = NSStackView(views: [navigationGroup("General", generalButtons),
                                      navigationGroup("Device Management", managementButtons)])
        nav.orientation = .vertical
        nav.alignment = .leading
        nav.spacing = 14
        nav.arrangedSubviews.forEach { $0.widthAnchor.constraint(equalTo: nav.widthAnchor).isActive = true }

        echoDeskSidebarConnectionDot = StatusIndicator(state: .inactive)
        echoDeskSidebarConnectionLabel = NSTextField(labelWithString: "CHECKING")
        echoDeskSidebarConnectionLabel.font = .systemFont(ofSize: 10, weight: .semibold)
        echoDeskSidebarDeviceName = NSTextField(labelWithString: "EchoDesk Mini")
        echoDeskSidebarDeviceName.font = .systemFont(ofSize: 13, weight: .semibold)
        echoDeskFooterStatus = NSTextField(labelWithString: "Waiting…")
        echoDeskFooterStatus.font = .monospacedSystemFont(ofSize: 10, weight: .regular)
        echoDeskFooterStatus.textColor = .secondaryLabelColor
        echoDeskFooterStatus.lineBreakMode = .byTruncatingMiddle
        let footerTop = NSStackView(views: [echoDeskSidebarConnectionDot, echoDeskSidebarConnectionLabel])
        footerTop.orientation = .horizontal
        footerTop.alignment = .centerY
        footerTop.spacing = 7
        let footerStack = NSStackView(views: [echoDeskSidebarDeviceName, footerTop, echoDeskFooterStatus])
        footerStack.orientation = .vertical
        footerStack.alignment = .leading
        footerStack.spacing = 4
        let footer = echoDeskCard(footerStack, padding: 8)
        footer.setAccessibilityElement(true)
        footer.setAccessibilityRole(.group)
        footer.setAccessibilityLabel("Connected EchoDesk")

        for view in [brandRow, nav, footer] {
            view.translatesAutoresizingMaskIntoConstraints = false
            sidebar.addSubview(view)
        }
        NSLayoutConstraint.activate([
            brandRow.leadingAnchor.constraint(equalTo: sidebar.leadingAnchor, constant: 18),
            brandRow.trailingAnchor.constraint(lessThanOrEqualTo: sidebar.trailingAnchor, constant: -14),
            brandRow.topAnchor.constraint(equalTo: sidebar.topAnchor, constant: 14),
            nav.leadingAnchor.constraint(equalTo: sidebar.leadingAnchor, constant: 10),
            nav.trailingAnchor.constraint(equalTo: sidebar.trailingAnchor, constant: -10),
            nav.topAnchor.constraint(equalTo: brandRow.bottomAnchor, constant: 18),
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
            buildEchoDeskBedsidePage(),
            buildEchoDeskSetupPage(),
            buildEchoDeskRecoveryPage(),
        ]
        echoDeskPageViews = pages.map(echoDeskScrollPage)
        for page in echoDeskPageViews {
            page.translatesAutoresizingMaskIntoConstraints = false
            page.isHidden = true
            page.setAccessibilityHidden(true)
            canvas.addSubview(page)
            NSLayoutConstraint.activate([
                page.leadingAnchor.constraint(equalTo: canvas.leadingAnchor, constant: EchoDeskDesign.pageInset),
                page.trailingAnchor.constraint(equalTo: canvas.trailingAnchor, constant: -EchoDeskDesign.pageInset),
                page.topAnchor.constraint(equalTo: banner.bottomAnchor, constant: 14),
                page.bottomAnchor.constraint(equalTo: canvas.safeAreaLayoutGuide.bottomAnchor, constant: -14),
            ])
        }
        NSLayoutConstraint.activate([
            banner.leadingAnchor.constraint(equalTo: canvas.leadingAnchor, constant: EchoDeskDesign.pageInset),
            banner.trailingAnchor.constraint(equalTo: canvas.trailingAnchor, constant: -EchoDeskDesign.pageInset),
            banner.topAnchor.constraint(equalTo: canvas.safeAreaLayoutGuide.topAnchor, constant: 10),
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
        scroll.contentInsets = NSEdgeInsets(top: 0, left: 0, bottom: 18, right: 0)
        scroll.contentView = FlippedClipView()
        scroll.contentView.drawsBackground = false
        let wrapper = NSView()
        wrapper.translatesAutoresizingMaskIntoConstraints = false
        document.translatesAutoresizingMaskIntoConstraints = false
        wrapper.addSubview(document)
        scroll.documentView = wrapper
        let readableWidth = document.widthAnchor.constraint(lessThanOrEqualToConstant: EchoDeskDesign.contentWidth)
        readableWidth.priority = .required
        NSLayoutConstraint.activate([
            wrapper.leadingAnchor.constraint(equalTo: scroll.contentView.leadingAnchor),
            wrapper.trailingAnchor.constraint(equalTo: scroll.contentView.trailingAnchor),
            wrapper.topAnchor.constraint(equalTo: scroll.contentView.topAnchor),
            wrapper.widthAnchor.constraint(equalTo: scroll.contentView.widthAnchor),
            document.topAnchor.constraint(equalTo: wrapper.topAnchor, constant: 6),
            document.centerXAnchor.constraint(equalTo: wrapper.centerXAnchor),
            document.leadingAnchor.constraint(greaterThanOrEqualTo: wrapper.leadingAnchor),
            document.trailingAnchor.constraint(lessThanOrEqualTo: wrapper.trailingAnchor),
            readableWidth,
            wrapper.bottomAnchor.constraint(equalTo: document.bottomAnchor, constant: 18),
        ])
        return scroll
    }

    @objc private func echoDeskNavigationPressed(_ sender: NSButton) {
        guard let page = EchoDeskDesktopPage(rawValue: sender.tag) else { return }
        selectEchoDeskPage(page)
    }

    func selectEchoDeskPage(_ page: EchoDeskDesktopPage) {
        let previousPage = echoDeskSelectedPage
        echoDeskSelectedPage = page
        switch page {
        case .overview: window?.subtitle = "Overview"
        case .deviceControl: window?.subtitle = "Device Control"
        case .customisation: window?.subtitle = "Customisation"
        case .bedside: window?.subtitle = "Bedside Mode"
        case .setup: window?.subtitle = "Set Up New Device"
        case .recovery: window?.subtitle = "Recovery & Updates"
        }
        echoDeskPageTransitionGeneration += 1
        let transitionGeneration = echoDeskPageTransitionGeneration
        let target = echoDeskPageViews[page.rawValue]
        let current = previousPage == page ? nil : echoDeskPageViews[previousPage.rawValue]
        for view in echoDeskPageViews {
            view.alphaValue = 1
            view.setAccessibilityHidden(view !== target)
            if view !== target && view !== current { view.isHidden = true }
        }
        target.isHidden = false
        target.setAccessibilityHidden(false)
        target.layoutSubtreeIfNeeded()
        // Each page owns an independent scroll view. Navigation always starts
        // from a predictable top position so a previously scrolled page cannot
        // return with its heading hidden beneath the title bar.
        if let scroll = target as? NSScrollView {
            scroll.contentView.scroll(to: .zero)
            scroll.reflectScrolledClipView(scroll.contentView)
        }
        if let current, !current.isHidden {
            current.setAccessibilityHidden(true)
            target.alphaValue = 0
            let reduceMotion = NSWorkspace.shared.accessibilityDisplayShouldReduceMotion
            NSAnimationContext.runAnimationGroup { context in
                context.duration = reduceMotion ? 0 : 0.12
                current.animator().alphaValue = 0
                target.animator().alphaValue = 1
            } completionHandler: { [weak self] in
                guard let self,
                      self.echoDeskPageTransitionGeneration == transitionGeneration,
                      self.echoDeskSelectedPage == page else { return }
                for view in self.echoDeskPageViews {
                    view.isHidden = view !== target
                    view.alphaValue = 1
                    view.setAccessibilityHidden(view !== target)
                }
            }
        } else {
            for view in echoDeskPageViews {
                view.isHidden = view !== target
                view.alphaValue = 1
                view.setAccessibilityHidden(view !== target)
            }
        }
        for button in echoDeskSidebarButtons {
            let selectedPage = page == .bedside ? EchoDeskDesktopPage.customisation.rawValue : page.rawValue
            let selected = button.tag == selectedPage
            button.state = selected ? .on : .off
            button.contentTintColor = selected ? .controlAccentColor : .labelColor
            button.layer?.backgroundColor = selected
                ? NSColor.controlAccentColor.withAlphaComponent(0.12).cgColor : NSColor.clear.cgColor
            (button as? EchoDeskSidebarButton)?.isCurrentPage = selected
        }
        if page == .setup { refreshEchoDeskSetupDevices() }
        if page == .recovery { refreshEchoDeskRecoveryDevices() }
        if page == .bedside { requestEchoDeskBedsideState() }
    }

    // MARK: Pages

    private func buildEchoDeskOverviewPage() -> NSView {
        let header = echoDeskPageHeader("Overview", "Your EchoDesk at a glance.")
        echoDeskConnectionLabel = NSTextField(labelWithString: "CHECKING")
        echoDeskConnectionLabel.font = .systemFont(ofSize: 11, weight: .semibold)
        echoDeskConnectionLabel.textColor = .secondaryLabelColor
        echoDeskConnectionDetail = echoDeskMuted("EchoDesk Desktop is looking for your device.")
        let name = NSTextField(labelWithString: "EchoDesk Mini")
        name.font = .systemFont(ofSize: 24, weight: .semibold)
        echoDeskConnectionDot = StatusIndicator(state: .inactive)
        let stateRow = NSStackView(views: [echoDeskConnectionDot, echoDeskConnectionLabel])
        stateRow.orientation = .horizontal; stateRow.alignment = .centerY; stateRow.spacing = 7
        let reconnect = NSButton(title: "Reconnect", target: self, action: #selector(echoDeskReconnect))
        let refresh = echoDeskPrimaryButton("Refresh", #selector(echoDeskRefresh))
        let buttons = NSStackView(views: [reconnect, refresh]); buttons.orientation = .horizontal; buttons.spacing = 8

        echoDeskFirmwareLabel = echoDeskMetricValue("Not reported")
        echoDeskHardwareLabel = echoDeskMetricValue("Not reported")
        echoDeskActiveAppLabel = echoDeskMetricValue("Not reported")
        let facts = NSStackView(views: [
            echoDeskCompactFact("Firmware", echoDeskFirmwareLabel),
            echoDeskCompactFact("Model", echoDeskHardwareLabel),
            echoDeskCompactFact("Running", echoDeskActiveAppLabel),
        ])
        facts.orientation = .horizontal
        facts.alignment = .top
        facts.distribution = .fillEqually
        facts.spacing = 12
        let stateText = echoDeskPageStack([stateRow, name, echoDeskConnectionDetail, facts, buttons], spacing: 10)

        let preview = EchoDeskScreenPreview(.home)
        preview.primaryText = "ECHO DESK"
        preview.secondaryText = "Ready"
        preview.translatesAutoresizingMaskIntoConstraints = false
        preview.widthAnchor.constraint(lessThanOrEqualToConstant: 300).isActive = true
        preview.heightAnchor.constraint(equalTo: preview.widthAnchor, multiplier: 0.586).isActive = true
        let heroContent = echoDeskAdaptiveColumns([preview, stateText], collapseBelow: 670, equalColumns: false)
        let deviceCard = echoDeskCard(heroContent, padding: 20)

        echoDeskOverviewBrightnessValue = NSTextField(labelWithString: "\(Settings.shared.lastKnownBrightness)%")
        echoDeskOverviewBrightnessValue.font = .monospacedDigitSystemFont(ofSize: 12, weight: .semibold)
        echoDeskOverviewBrightnessValue.alignment = .right
        echoDeskOverviewBrightness = NSSlider(value: Double(Settings.shared.lastKnownBrightness), minValue: 10,
                                               maxValue: 100, target: self,
                                               action: #selector(echoDeskOverviewBrightnessChanged(_:)))
        echoDeskOverviewBrightness.isContinuous = false
        echoDeskOverviewBrightness.setAccessibilityLabel("EchoDesk brightness")
        let brightnessHeading = NSStackView(views: [NSTextField(labelWithString: "Brightness"), NSView(), echoDeskOverviewBrightnessValue])
        brightnessHeading.orientation = .horizontal; brightnessHeading.alignment = .centerY
        echoDeskOverviewTheme = NSSegmentedControl(labels: ["Light", "Dark"], trackingMode: .selectOne,
                                                    target: self, action: #selector(echoDeskOverviewThemeChanged(_:)))
        echoDeskOverviewTheme.selectedSegment = Settings.shared.themeMode == "light" ? 0 : 1
        let quickControls = echoDeskCard(echoDeskPageStack([
            echoDeskSectionTitle("Quick controls"), brightnessHeading, echoDeskOverviewBrightness,
            echoDeskLabeledControl("Appearance", echoDeskOverviewTheme),
        ], spacing: 10))

        echoDeskCommandStatus = echoDeskMuted("Commands are confirmed by the connected EchoDesk.")
        echoDeskCommandStatus.maximumNumberOfLines = 2
        let statusPanel = echoDeskCard(echoDeskPageStack([
            echoDeskSectionTitle("Device status"), echoDeskCommandStatus,
            echoDeskMuted("Connection, firmware and active-app state refresh automatically."),
        ], spacing: 8))
        let quickRow = echoDeskAdaptiveColumns([quickControls, statusPanel], collapseBelow: 660)

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

        echoDeskNotificationStatus = NSTextField(labelWithString: "Not available on macOS")
        echoDeskNotificationStatus.font = .systemFont(ofSize: 11, weight: .bold)
        let notificationTitle = NSTextField(labelWithString: "Notification Stack")
        notificationTitle.font = .systemFont(ofSize: 17, weight: .semibold)
        let notificationHeading = NSStackView(views: [notificationTitle, echoDeskNotificationStatus])
        notificationHeading.orientation = .horizontal; notificationHeading.spacing = 12
        echoDeskNotificationDetail = echoDeskMuted("Notification Stack cannot access other apps’ notifications on macOS.")
        echoDeskNotificationExclusions = echoDeskMuted("")
        echoDeskNotificationExclusions.isHidden = true
        let notificationCard = echoDeskCard(echoDeskPageStack([
            notificationHeading, echoDeskNotificationDetail,
        ], spacing: 8))
        notificationCard.alphaValue = 0.62
        notificationCard.setAccessibilityLabel("Notification Stack. Not available on macOS")

        let apps = echoDeskAppGrid(columns: 3)
        let components = echoDeskAdaptiveColumns([component, notificationCard], collapseBelow: 720)
        let stack = echoDeskPageStack([header, deviceCard, quickRow,
                                       echoDeskSectionTitle("Apps"), apps,
                                       echoDeskSectionTitle("Components"), components],
                                      spacing: EchoDeskDesign.sectionSpacing)
        return stack
    }

    private func buildEchoDeskDeviceControlPage() -> NSView {
        let header = echoDeskPageHeader("Device Control", "Control the connected EchoDesk and see each confirmed command.")
        let preview = EchoDeskScreenPreview(.home)
        preview.primaryText = "EchoDesk"
        preview.secondaryText = "Select an app to open it"
        preview.translatesAutoresizingMaskIntoConstraints = false
        preview.heightAnchor.constraint(equalTo: preview.widthAnchor, multiplier: 0.586).isActive = true
        echoDeskDeviceControlStatus = echoDeskMuted("Waiting for a connected EchoDesk…")
        echoDeskDeviceControlStatus.maximumNumberOfLines = 2
        echoDeskDeviceBrightness = NSSlider(value: Double(Settings.shared.lastKnownBrightness), minValue: 10,
                                             maxValue: 100, target: self,
                                             action: #selector(echoDeskOverviewBrightnessChanged(_:)))
        echoDeskDeviceBrightness.isContinuous = false
        echoDeskDeviceBrightness.setAccessibilityLabel("EchoDesk brightness")
        let liveState = echoDeskCard(echoDeskPageStack([
            echoDeskSectionTitle("Physical device"),
            echoDeskDeviceControlStatus,
            echoDeskLabeledControl("Brightness", echoDeskDeviceBrightness),
            echoDeskMuted("A command is only reported as complete after EchoDesk confirms it."),
        ], spacing: 8))
        let deviceHero = echoDeskAdaptiveColumns([preview, liveState], collapseBelow: 660)
        let apps = echoDeskCard(echoDeskPageStack([
            echoDeskSectionTitle("Open an app"),
            echoDeskMuted("Choose an app to launch it on the physical touchscreen."),
            echoDeskAppGrid(columns: 2),
        ], spacing: 10))
        let restart = echoDeskDangerRow("Restart EchoDesk",
            "The USB connection will briefly disconnect and recover automatically.",
            "Restart…", #selector(echoDeskRestart))
        let reset = echoDeskDangerRow("Factory Reset",
            "Clear setup completion and return the physical device to its Hello screen. Firmware and unrelated flash partitions are not erased.",
            "Factory Reset…", #selector(echoDeskFactoryReset))
        let system = echoDeskAdaptiveColumns([echoDeskCard(restart), echoDeskCard(reset)], collapseBelow: 700)
        return echoDeskPageStack([header, deviceHero, apps, echoDeskSectionTitle("Device maintenance"), system],
                                 spacing: EchoDeskDesign.sectionSpacing)
    }

    private func buildEchoDeskCustomisationPage() -> NSView {
        let header = echoDeskPageHeader("Customisation",
            "Choose what you want to change. Each setting opens in a focused workspace.")
        let bedsideIcon = NSImageView()
        bedsideIcon.image = NSImage(systemSymbolName: "bed.double.fill", accessibilityDescription: nil)
        bedsideIcon.symbolConfiguration = NSImage.SymbolConfiguration(pointSize: 28, weight: .medium)
        bedsideIcon.contentTintColor = .controlAccentColor
        let bedsideText = echoDeskPageStack([
            echoDeskSectionTitle("Bedside Mode"),
            echoDeskMuted("A completely separate, power-only clock experience with alarms, widgets and its own display schedules."),
        ], spacing: 5)
        let bedsideButton = echoDeskPrimaryButton("Customise Bedside Mode…", #selector(echoDeskOpenBedsideCustomisation))
        let bedsideRow = NSStackView(views: [bedsideIcon, bedsideText, NSView(), bedsideButton])
        bedsideRow.orientation = .horizontal; bedsideRow.alignment = .centerY; bedsideRow.spacing = 14
        let bedside = echoDeskCard(bedsideRow)
        echoDeskQuickTheme = NSSegmentedControl(labels: ["Light", "Dark"], trackingMode: .selectOne,
                                                 target: self, action: #selector(echoDeskQuickThemeChanged))
        echoDeskQuickTheme.selectedSegment = Settings.shared.themeMode == "light" ? 0 : 1
        echoDeskQuickClockFormat = EchoDeskPopupButton(frame: .zero, pullsDown: false)
        echoDeskQuickClockFormat.addItems(withTitles: ["12-hour", "24-hour"])
        echoDeskQuickClockFormat.target = self
        echoDeskQuickClockFormat.action = #selector(echoDeskClockFormatChanged)
        let quick = echoDeskCard(echoDeskPageStack([
            echoDeskSectionTitle("Appearance"),
            echoDeskLabeledControl("Device appearance", echoDeskQuickTheme),
            echoDeskLabeledControl("Time format", echoDeskQuickClockFormat),
        ]))
        let brightness = NSSlider(value: Double(Settings.shared.lastKnownBrightness), minValue: 10, maxValue: 100,
                                  target: self, action: #selector(echoDeskSettingSliderChanged(_:)))
        brightness.isContinuous = false
        brightness.identifier = NSUserInterfaceItemIdentifier("brightness")
        echoDeskSettingControls["brightness"] = brightness
        let general = echoDeskCard(echoDeskPageStack([
            echoDeskSectionTitle("Display"),
            echoDeskLabeledControl("Brightness", brightness),
            echoDeskLabeledControl("Default app on startup", echoDeskSettingPopup("defaultApp", [
                ("Home", "home"), ("Codex Usage", "codex"), ("Clock", "clock"),
                ("Performance", "performance"), ("Now Playing", "nowPlaying"),
                ("My device", "myDevice"), ("Notification Stack", "notificationStack"),
                ("PC Dashboard", "pcDashboard"), ("Bedside Mode", "bedside"), ("Settings", "settings"),
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
        let dashboardChoices: [(String, String)] = [
            ("None", ""), ("Battery / charging", "battery"), ("Microphone in use", "microphone"),
            ("Storage usage", "storage"), ("RAM usage", "ram"), ("CPU usage", "cpu"),
            ("GPU usage", "gpu"), ("Network download", "networkDown"), ("Network upload", "networkUp"),
            ("CPU temperature", "cpuTemperature"), ("GPU temperature", "gpuTemperature"),
            ("Motherboard temperature", "motherboardTemperature"),
        ]
        echoDeskPcDashboardPopups = (0..<4).map { index in
            let popup = EchoDeskPopupButton(frame: .zero, pullsDown: false)
            for (label, value) in dashboardChoices where index > 0 || !value.isEmpty {
                let item = NSMenuItem(title: label, action: nil, keyEquivalent: "")
                item.representedObject = value; popup.menu?.addItem(item)
            }
            popup.target = self
            popup.action = #selector(echoDeskApplyPcDashboard)
            return popup
        }
        echoDeskPcDashboardStatus = echoDeskMuted("Loading the current dashboard layout…")
        echoDeskPcDashboardRetryButton = NSButton(title: "Retry", target: self,
                                                   action: #selector(echoDeskRetryPcDashboardApply))
        echoDeskPcDashboardRetryButton?.isHidden = true
        let dashboardStatusRow = NSStackView(views: [echoDeskPcDashboardStatus!, NSView(), echoDeskPcDashboardRetryButton!])
        dashboardStatusRow.orientation = .horizontal
        dashboardStatusRow.alignment = .centerY
        let dashboard = echoDeskCard(echoDeskPageStack([
            echoDeskSectionTitle("Metrics"),
            echoDeskMuted("Choose one to four different metrics. Changes apply automatically; unavailable sensors remain unavailable instead of appearing as zero."),
            echoDeskLabeledControl("Metric 1", echoDeskPcDashboardPopups[0]),
            echoDeskLabeledControl("Metric 2", echoDeskPcDashboardPopups[1]),
            echoDeskLabeledControl("Metric 3", echoDeskPcDashboardPopups[2]),
            echoDeskLabeledControl("Metric 4", echoDeskPcDashboardPopups[3]),
            dashboardStatusRow,
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
            echoDeskSectionTitle("EchoDesk Desktop"),
            echoDeskMuted("This setting changes the desktop app only. It does not change the physical display."),
            echoDeskLabeledControl("App appearance", desktopAppearance),
        ]))

        func preview(_ section: EchoDeskCustomisationSection, _ kind: EchoDeskScreenPreview.Kind,
                     primary: String, secondary: String) -> EchoDeskScreenPreview {
            let value = EchoDeskScreenPreview(kind)
            value.primaryText = primary
            value.secondaryText = secondary
            echoDeskCustomisationPreviews[section.rawValue] = value
            return value
        }
        let sectionContent: [(EchoDeskCustomisationSection, NSView)] = [
            (.display, echoDeskFocusedSettingsSection(.display,
                preview: preview(.display, .appearance, primary: "12:42", secondary: "Display preview"),
                content: echoDeskPageStack([quick, general], spacing: 12))),
            (.bedside, echoDeskFocusedSettingsSection(.bedside,
                preview: preview(.bedside, .clock, primary: "1:02", secondary: "NEXT ALARM  7:00"),
                content: bedside)),
            (.clock, echoDeskFocusedSettingsSection(.clock,
                preview: preview(.clock, .clock, primary: "12:42", secondary: "Tuesday, 2 November"),
                content: clock)),
            (.nowPlaying, echoDeskFocusedSettingsSection(.nowPlaying,
                preview: preview(.nowPlaying, .nowPlaying, primary: "Song title", secondary: "Artist"),
                content: nowPlaying)),
            (.codex, echoDeskFocusedSettingsSection(.codex,
                preview: preview(.codex, .home, primary: "Codex Usage", secondary: "Session and weekly limits"),
                content: codex)),
            (.performance, echoDeskFocusedSettingsSection(.performance,
                preview: preview(.performance, .dashboard, primary: "Performance", secondary: "Live PC metrics"),
                content: performance)),
            (.pcDashboard, echoDeskFocusedSettingsSection(.pcDashboard,
                preview: preview(.pcDashboard, .dashboard, primary: "PC Dashboard", secondary: "Four metrics"),
                content: dashboard)),
            (.preferences, echoDeskFocusedSettingsSection(.preferences,
                preview: preview(.preferences, .appearance, primary: "EchoDesk", secondary: "Desktop appearance"),
                content: desktop)),
        ]
        echoDeskCustomisationSections = Dictionary(uniqueKeysWithValues: sectionContent.map { ($0.0.rawValue, $0.1) })
        sectionContent.forEach { $0.1.isHidden = true }

        let tiles = EchoDeskCustomisationSection.allCases.map { section -> NSView in
            let tile = EchoDeskActionTile(title: section.title, detail: section.subtitle, symbol: section.symbol,
                                          target: self, action: #selector(echoDeskOpenCustomisationSection(_:)))
            tile.identifier = NSUserInterfaceItemIdentifier(section.rawValue)
            return tile
        }
        let tileRows = stride(from: 0, to: tiles.count, by: 2).map { start -> NSView in
            let row = Array(tiles[start..<min(start + 2, tiles.count)])
            return echoDeskAdaptiveColumns(row, collapseBelow: 650)
        }
        let landing = echoDeskPageStack([header, echoDeskMuted("Settings save automatically after EchoDesk confirms each change.")] + tileRows,
                                        spacing: 12)
        echoDeskCustomisationLanding = landing
        return echoDeskPageStack([landing] + sectionContent.map { $0.1 }, spacing: 0)
    }

    private func requestEchoDeskPcDashboardState() {
        guard !echoDeskPcDashboardRequestInFlight,
              let monitor, monitor.serialPort.state == .connected,
              (monitor.serialPort.deviceEchoDeskPcDashboardProtocol ?? 0) >= 1,
              let port = monitor.serialPort.connectedPort,
              let profile = DeviceRegistry.shared.currentProfile() else {
            echoDeskPcDashboardStatus?.stringValue = "Connect EchoDesk to load its dashboard layout."
            return
        }
        if echoDeskPcDashboardExpectedPort == nil { echoDeskPcDashboardExpectedPort = port }
        if echoDeskPcDashboardExpectedMAC == nil { echoDeskPcDashboardExpectedMAC = profile.mac }
        guard port == echoDeskPcDashboardExpectedPort, profile.mac == echoDeskPcDashboardExpectedMAC else {
            failEchoDeskPcDashboardApply("A different EchoDesk is now connected. Review the layout before retrying.")
            return
        }
        echoDeskPcDashboardRequestInFlight = true
        let request = nextEchoDeskRequest()
        monitor.serialPort.performJSONCommand([
            "cmd": "echodesk_pc_dashboard_get_state", "version": 1, "requestId": request,
        ], acceptedTypes: ["echodesk_pc_dashboard_state", "echodesk_error"], timeout: 5) { [weak self] json in
            guard let self else { return }
            self.echoDeskPcDashboardRequestInFlight = false
            guard monitor.serialPort.connectedPort == self.echoDeskPcDashboardExpectedPort,
                  DeviceRegistry.shared.currentProfile()?.mac == self.echoDeskPcDashboardExpectedMAC,
                  json?["type"] as? String == "echodesk_pc_dashboard_state",
                  self.echoDeskResponseMatches(json ?? [:], request: request),
                  let revision = json?["revision"] as? Int,
                  let metrics = json?["metrics"] as? [String], metrics.count >= 1, metrics.count <= 4,
                  Set(metrics).count == metrics.count,
                  Set(metrics).isSubset(of: Set(["battery", "microphone", "storage", "ram", "cpu", "gpu",
                                                  "networkDown", "networkUp", "cpuTemperature", "gpuTemperature",
                                                  "motherboardTemperature"])) else {
                self.failEchoDeskPcDashboardApply("EchoDesk did not return its current dashboard settings. Retry when connected.")
                return
            }
            self.echoDeskPcDashboardRevision = revision
            // A refresh is authoritative only while there is no local draft.
            // This prevents the ACK/state for an older write from visibly
            // replacing a newer selection made while that write was in flight.
            if self.echoDeskPcDashboardPendingMetrics == nil,
               self.echoDeskPcDashboardInFlightMetrics == nil,
               self.echoDeskPcDashboardFailedMetrics == nil {
                for (index, popup) in self.echoDeskPcDashboardPopups.enumerated() {
                    let value = index < metrics.count ? metrics[index] : ""
                    if let item = popup.itemArray.first(where: { ($0.representedObject as? String) == value }) {
                        popup.select(item)
                    }
                }
                self.echoDeskPcDashboardStatus?.stringValue = "Dashboard layout is up to date."
                self.refreshEchoDeskCustomisationPreviews()
            }
            self.echoDeskPcDashboardRetryAfterRefresh = false
            self.flushPendingEchoDeskPcDashboardApplyIfReady()
        }
    }

    func handleUnsolicitedEchoDeskState() {
        guard echoDeskSelectedPage == .customisation,
              let dashboard = echoDeskCustomisationSections[EchoDeskCustomisationSection.pcDashboard.rawValue],
              !dashboard.isHidden,
              echoDeskPcDashboardPendingMetrics == nil,
              echoDeskPcDashboardInFlightMetrics == nil,
              !echoDeskPcDashboardApplyInFlight,
              let monitor, let port = monitor.serialPort.connectedPort,
              let profile = DeviceRegistry.shared.currentProfile() else { return }
        echoDeskPcDashboardExpectedPort = port
        echoDeskPcDashboardExpectedMAC = profile.mac
        echoDeskPcDashboardRevision = 0
        echoDeskPcDashboardStatus?.stringValue = "EchoDesk changed this layout. Refreshing…"
        requestEchoDeskPcDashboardState()
    }

    @objc private func echoDeskApplyPcDashboard() {
        let metrics = echoDeskPcDashboardPopups.compactMap { $0.selectedItem?.representedObject as? String }
            .filter { !$0.isEmpty }
        guard metrics.count >= 1, metrics.count <= 4, Set(metrics).count == metrics.count else {
            echoDeskPcDashboardStatus?.stringValue = "Choose one to four different dashboard metrics without duplicates."
            NSSound.beep(); return
        }
        echoDeskPcDashboardAutoApplyWorkItem?.cancel()
        echoDeskPcDashboardStatus?.stringValue = "Waiting to apply changes…"
        let work = DispatchWorkItem { [weak self] in self?.queueEchoDeskPcDashboardApply(metrics) }
        echoDeskPcDashboardAutoApplyWorkItem = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.25, execute: work)
        refreshEchoDeskCustomisationPreviews()
    }

    private func queueEchoDeskPcDashboardApply(_ metrics: [String]) {
        echoDeskPcDashboardAutoApplyWorkItem = nil
        guard let monitor, monitor.serialPort.state == .connected,
              let port = monitor.serialPort.connectedPort,
              let profile = DeviceRegistry.shared.currentProfile() else {
            echoDeskPcDashboardFailedMetrics = metrics
            failEchoDeskPcDashboardApply("Connect EchoDesk to save this dashboard layout.")
            return
        }
        echoDeskPcDashboardPendingMetrics = metrics
        echoDeskPcDashboardFailedMetrics = nil
        echoDeskPcDashboardExpectedPort = port
        echoDeskPcDashboardExpectedMAC = profile.mac
        echoDeskPcDashboardDidRetryStaleRevision = false
        echoDeskPcDashboardRetryButton?.isHidden = true
        flushPendingEchoDeskPcDashboardApplyIfReady()
    }

    private func flushPendingEchoDeskPcDashboardApplyIfReady() {
        guard !echoDeskPcDashboardApplyInFlight,
              let metrics = echoDeskPcDashboardPendingMetrics,
              let monitor, monitor.serialPort.state == .connected,
              monitor.serialPort.connectedPort == echoDeskPcDashboardExpectedPort,
              DeviceRegistry.shared.currentProfile()?.mac == echoDeskPcDashboardExpectedMAC else { return }
        guard echoDeskPcDashboardRevision > 0 else {
            echoDeskPcDashboardStatus?.stringValue = "Checking the current dashboard revision…"
            requestEchoDeskPcDashboardState()
            return
        }
        echoDeskPcDashboardPendingMetrics = nil
        echoDeskPcDashboardInFlightMetrics = metrics
        echoDeskPcDashboardApplyInFlight = true
        echoDeskPcDashboardStatus?.stringValue = "Applying dashboard changes…"
        sendEchoDeskPcDashboardConfig(metrics)
    }

    private func sendEchoDeskPcDashboardConfig(_ metrics: [String]) {
        guard let monitor,
              monitor.serialPort.connectedPort == echoDeskPcDashboardExpectedPort,
              DeviceRegistry.shared.currentProfile()?.mac == echoDeskPcDashboardExpectedMAC else {
            echoDeskPcDashboardApplyInFlight = false
            echoDeskPcDashboardInFlightMetrics = nil
            echoDeskPcDashboardFailedMetrics = echoDeskPcDashboardPendingMetrics ?? metrics
            failEchoDeskPcDashboardApply("The selected EchoDesk disconnected before saving.")
            return
        }
        let request = nextEchoDeskRequest()
        monitor.serialPort.performJSONCommand([
            "cmd": "echodesk_pc_dashboard_set_config", "version": 1, "requestId": request,
            "baseRevision": echoDeskPcDashboardRevision, "metrics": metrics,
        ], acceptedTypes: ["echodesk_ack", "echodesk_error"], timeout: 5) { [weak self] json in
            guard let self else { return }
            self.echoDeskPcDashboardApplyInFlight = false
            self.echoDeskPcDashboardInFlightMetrics = nil
            guard monitor.serialPort.connectedPort == self.echoDeskPcDashboardExpectedPort,
                  DeviceRegistry.shared.currentProfile()?.mac == self.echoDeskPcDashboardExpectedMAC else {
                self.echoDeskPcDashboardFailedMetrics = self.echoDeskPcDashboardPendingMetrics ?? metrics
                self.failEchoDeskPcDashboardApply("EchoDesk disconnected before confirming the dashboard layout.")
                return
            }
            if json?["type"] as? String == "echodesk_error" {
                let errorCode = json?["code"] as? String ?? "unknown"
                if errorCode == "stale_revision", !self.echoDeskPcDashboardDidRetryStaleRevision {
                    self.echoDeskPcDashboardDidRetryStaleRevision = true
                    if self.echoDeskPcDashboardPendingMetrics == nil {
                        self.echoDeskPcDashboardPendingMetrics = metrics
                    }
                    self.echoDeskPcDashboardRevision = 0
                    self.echoDeskPcDashboardRetryAfterRefresh = true
                    self.echoDeskPcDashboardStatus?.stringValue = "Refreshing after a device-side change…"
                    self.requestEchoDeskPcDashboardState()
                    return
                }
                self.echoDeskPcDashboardFailedMetrics = self.echoDeskPcDashboardPendingMetrics ?? metrics
                self.echoDeskPcDashboardPendingMetrics = nil
                self.failEchoDeskPcDashboardApply("EchoDesk did not accept the dashboard layout. Retry to keep your selections.")
                return
            }
            guard json?["type"] as? String == "echodesk_ack",
                  self.echoDeskResponseMatches(json ?? [:], request: request,
                                               command: "echodesk_pc_dashboard_set_config") else {
                self.echoDeskPcDashboardFailedMetrics = self.echoDeskPcDashboardPendingMetrics ?? metrics
                self.echoDeskPcDashboardPendingMetrics = nil
                self.failEchoDeskPcDashboardApply("EchoDesk did not confirm the dashboard layout.")
                return
            }
            UserDefaults.standard.set(metrics, forKey: "EchoDeskPcDashboardMetrics")
            self.echoDeskPcDashboardFailedMetrics = nil
            self.echoDeskPcDashboardRetryButton?.isHidden = true
            self.echoDeskPcDashboardRevision = 0
            if self.echoDeskPcDashboardPendingMetrics == nil {
                self.echoDeskPcDashboardStatus?.stringValue = "Saved to EchoDesk."
                self.echoDeskFooterStatus?.stringValue = "PC Dashboard updated."
            } else {
                self.echoDeskPcDashboardStatus?.stringValue = "Finishing newer dashboard changes…"
            }
            self.requestEchoDeskPcDashboardState()
        }
    }

    @objc private func echoDeskRetryPcDashboardApply() {
        guard let metrics = echoDeskPcDashboardFailedMetrics else { return }
        queueEchoDeskPcDashboardApply(metrics)
    }

    private func failEchoDeskPcDashboardApply(_ message: String) {
        let desired = echoDeskPcDashboardPendingMetrics ?? echoDeskPcDashboardInFlightMetrics
        if echoDeskPcDashboardFailedMetrics == nil { echoDeskPcDashboardFailedMetrics = desired }
        echoDeskPcDashboardStatus?.stringValue = message
        echoDeskPcDashboardRetryButton?.isHidden = echoDeskPcDashboardFailedMetrics == nil
        echoDeskFooterStatus?.stringValue = message
        NSSound.beep()
    }

    private func buildEchoDeskSetupPage() -> NSView {
        let header = echoDeskPageHeader("Setup new device", "A guided setup for EchoDesk Mini.")
        echoDeskSetupStepLabels = []
        echoDeskSetupStepper = EchoDeskSetupProgressControl(
            labels: ["Connect", "Choose", "Install", "Quick Settings", "Ready"],
            trackingMode: .selectOne, target: nil, action: nil)
        echoDeskSetupStepper.segmentStyle = .rounded
        echoDeskSetupStepper.controlSize = .regular
        echoDeskSetupStepper.selectedSegment = 0
        echoDeskSetupStepper.setAccessibilityLabel("Setup progress")
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
        return echoDeskPageStack([header, echoDeskSetupStepper] + echoDeskSetupViews)
    }

    private func buildEchoDeskRecoveryPage() -> NSView {
        let header = echoDeskPageHeader("Device recovery & updates",
            "Keep EchoDesk OS current, or use guided maintenance when something goes wrong.")
        echoDeskRecoveryDevicePopup = EchoDeskPopupButton(frame: .zero, pullsDown: false)
        echoDeskRecoveryDevicePopup.translatesAutoresizingMaskIntoConstraints = false
        let recoveryPopupMinimum = echoDeskRecoveryDevicePopup.widthAnchor.constraint(greaterThanOrEqualToConstant: 240)
        recoveryPopupMinimum.priority = .defaultHigh
        recoveryPopupMinimum.isActive = true
        let scan = NSButton(title: "Scan for devices", target: self, action: #selector(echoDeskRecoveryScan))
        let deviceRow = echoDeskAdaptiveColumns([echoDeskRecoveryDevicePopup, scan],
                                                collapseBelow: 620, equalColumns: false)
        echoDeskRecoveryInstalledVersion = echoDeskMetricValue("Not reported")
        echoDeskRecoveryAvailableVersion = echoDeskMetricValue("Checking…")
        echoDeskRecoveryUpdateStatus = echoDeskMuted("Select a device to compare versions.")
        let versions = echoDeskAdaptiveColumns([
            echoDeskMetric("INSTALLED", echoDeskRecoveryInstalledVersion),
            echoDeskMetric("AVAILABLE", echoDeskRecoveryAvailableVersion),
        ], collapseBelow: 520)
        let selector = echoDeskCard(echoDeskPageStack([
            echoDeskSectionTitle("Your EchoDesk"),
            echoDeskMuted("Choose the exact USB device before any write is allowed."),
            deviceRow, versions, echoDeskRecoveryUpdateStatus,
        ], spacing: 11))

        let update = echoDeskRecoveryAction(0, "Update EchoDesk OS",
            "Update EchoDesk OS to the latest version while keeping your device settings.")
        let maintenanceActions = echoDeskPageStack([
            echoDeskRecoveryAction(1, "Reinstall EchoDesk OS", "Install a fresh version of EchoDesk OS."),
            echoDeskRecoveryAction(2, "Repair device", "Repair and reset EchoDesk OS if on older firmware."),
            echoDeskRecoveryAction(3, "Recovery", "Fully reset EchoDesk without checks after confirming the target is an ESP32."),
        ], spacing: 0)
        echoDeskRecoveryActions = NSStackView(views: [update, echoDeskSectionTitle("Maintenance"), maintenanceActions])
        echoDeskRecoveryActions.orientation = .vertical
        echoDeskRecoveryActions.alignment = .leading
        echoDeskRecoveryActions.spacing = 14
        echoDeskRecoveryActions.arrangedSubviews.forEach {
            $0.widthAnchor.constraint(equalTo: echoDeskRecoveryActions.widthAnchor).isActive = true
        }
        echoDeskRecoveryStatus = echoDeskMuted("No maintenance operation is running.")
        echoDeskRecoveryProgress = NSProgressIndicator()
        echoDeskRecoveryProgress.style = .bar; echoDeskRecoveryProgress.isIndeterminate = true
        echoDeskRecoveryProgress.isHidden = true
        echoDeskRecoveryCompletion = echoDeskCard(echoDeskPageStack([
            echoDeskSectionTitle("EchoDesk OS is ready"),
            echoDeskMuted("The firmware was written, verified and the device reconnected."),
            echoDeskPrimaryButton("Setup EchoDesk Now", #selector(echoDeskSetupAfterRecovery)),
        ]))
        echoDeskRecoveryCompletion.isHidden = true
        let progress = echoDeskCard(echoDeskPageStack([
            echoDeskSectionTitle("Installation status"), echoDeskRecoveryStatus, echoDeskRecoveryProgress,
        ], spacing: 8))
        return echoDeskPageStack([header, selector, echoDeskRecoveryActions, progress,
                                  echoDeskRecoveryCompletion], spacing: EchoDeskDesign.sectionSpacing)
    }

    // MARK: Setup steps

    private func buildEchoDeskSetupConnectStep() -> NSView {
        let usb = echoDeskChoiceCard("Setup with USB", "Setup EchoDesk by plugging into your PC.",
                                     "Continue with USB", #selector(echoDeskChooseUSB))
        let wifi = echoDeskChoiceCard("Setup with WiFi",
            "Setup EchoDesk without plugging into your PC. You'll still need to use USB connection for some utilities.",
            "Setup with WiFi", #selector(echoDeskChooseWiFi))
        let choices = echoDeskAdaptiveColumns([usb, wifi])
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
        let setupPopupMinimum = echoDeskSetupUSBPopup.widthAnchor.constraint(greaterThanOrEqualToConstant: 260)
        setupPopupMinimum.priority = .defaultHigh
        setupPopupMinimum.isActive = true
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
            NSView(),
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
        let row = echoDeskAdaptiveColumns([now, update])
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
        echoDeskSetupQuickContinueButton = echoDeskPrimaryButton("Continue", #selector(echoDeskApplySetupQuickSettings))
        return echoDeskCard(echoDeskPageStack([
            echoDeskHero("Choose your quick settings."),
            echoDeskMuted("You can change these at any time from Customisation."), controls,
            echoDeskSetupQuickContinueButton,
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
        echoDeskSetupStepper?.selectedSegment = progressIndex
        UserDefaults.standard.set(step.rawValue, forKey: "EchoDeskGuidedSetupStepV2")
        if echoDeskPageViews.indices.contains(EchoDeskDesktopPage.setup.rawValue),
           let scroll = echoDeskPageViews[EchoDeskDesktopPage.setup.rawValue] as? NSScrollView {
            scroll.contentView.scroll(to: .zero)
            scroll.reflectScrolledClipView(scroll.contentView)
        }
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
            echoDeskNotificationSyncedPort = nil
            echoDeskBedsideStatePort = nil
            echoDeskPcDashboardRevision = 0
            if observedPort != nil, echoDeskPcDashboardPendingMetrics == nil,
               echoDeskPcDashboardInFlightMetrics == nil, echoDeskPcDashboardFailedMetrics == nil {
                echoDeskPcDashboardExpectedPort = observedPort
                echoDeskPcDashboardExpectedMAC = observedMAC
            }
        }
        echoDeskConnectionDot?.state = ready ? .ok : (serial.state == .probing ? .pending : .attention)
        echoDeskSidebarConnectionDot?.state = ready ? .ok : (serial.state == .probing ? .pending : .attention)
        echoDeskConnectionLabel?.stringValue = ready ? "CONNECTED" : (serial.state == .probing ? "CONNECTING" : "OFFLINE")
        echoDeskSidebarConnectionLabel?.stringValue = ready ? "CONNECTED" : (serial.state == .probing ? "CONNECTING" : "OFFLINE")
        echoDeskConnectionDetail?.stringValue = ready
            ? "EchoDesk is connected through USB."
            : "Connect EchoDesk Mini or use Setup new device."
        echoDeskCommandStatus?.stringValue = ready
            ? "Connected and ready. Commands are confirmed by EchoDesk."
            : "EchoDesk is offline. Controls will become available after it reconnects."
        echoDeskDeviceControlStatus?.stringValue = ready
            ? "Connected to \(profile?.friendlyName ?? "EchoDesk Mini") through USB."
            : "No EchoDesk is connected. App and maintenance controls are unavailable."
        echoDeskOverviewBrightness?.isEnabled = ready
        echoDeskDeviceBrightness?.isEnabled = ready
        echoDeskOverviewTheme?.isEnabled = ready
        echoDeskSidebarDeviceName?.stringValue = ready ? "EchoDesk Mini" : "No device connected"
        echoDeskFooterStatus?.stringValue = serial.connectedPort ?? "Waiting…"
        echoDeskFirmwareLabel?.stringValue = serial.deviceFirmwareVersion.map { "EchoDesk OS \($0)" } ?? "Not reported"
        echoDeskHardwareLabel?.stringValue = ready ? "EchoDesk Mini" : "Not reported"
        echoDeskActiveAppLabel?.stringValue = (echoDeskLastState?["activeApp"] as? String)?.echoDeskTitle ?? "Not reported"
        let supportedApps = Set((echoDeskLastState?["supportedApps"] as? [Any] ?? [])
            .compactMap { $0 as? String })
        for (app, buttons) in echoDeskAppButtons {
            buttons.forEach { $0.isEnabled = ready && supportedApps.contains(app) }
        }
        if let defaultApp = echoDeskSettingControls["defaultApp"] as? NSPopUpButton {
            for item in defaultApp.itemArray {
                guard let app = item.representedObject as? String else { continue }
                item.isEnabled = supportedApps.isEmpty || supportedApps.contains(app)
            }
        }

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
        echoDeskNotificationStatus?.stringValue = "Not available on macOS"
        echoDeskNotificationDetail?.stringValue = "Notification Stack cannot access other apps’ notifications on macOS."
        if echoDeskSelectedPage == .setup {
            if echoDeskSetupStep == .usbDevice { refreshEchoDeskSetupDevices() }
            continueEchoDeskSetupAfterReconnect(profile: profile)
        }
        if echoDeskSelectedPage == .recovery {
            if !echoDeskRecoveryOperationStarted { refreshEchoDeskRecoveryDevices() }
            continueEchoDeskRecoveryAfterReconnect()
            refreshEchoDeskRecoveryProgress()
        }
        if echoDeskSelectedPage == .bedside, ready,
           (serial.deviceEchoDeskBedsideProtocol ?? 0) >= 1,
           echoDeskBedsideStatePort != serial.connectedPort,
           !echoDeskBedsideStateRequestInFlight {
            requestEchoDeskBedsideState()
        }
        if echoDeskSelectedPage == .bedside { refreshEchoDeskBedsideAvailability() }
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
            self.flushPendingEchoDeskSettingChangesIfReady()
            self.retryPendingEchoDeskSetupQuickSettingsIfReady()
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

    private func echoDeskNotificationText(_ value: String, maximumBytes: Int) -> String {
        let cleaned = value.components(separatedBy: .controlCharacters).joined(separator: " ")
        var output = ""
        for scalar in cleaned.unicodeScalars {
            let candidate = output + String(scalar)
            if candidate.utf8.count > maximumBytes { break }
            output = candidate
        }
        return output.trimmingCharacters(in: .whitespacesAndNewlines)
    }

    private func echoDeskNotificationID(source: String, identifier: String) -> UInt32 {
        var hash: UInt32 = 2_166_136_261
        for byte in (source + "\u{0}" + identifier).utf8 {
            hash = (hash ^ UInt32(byte)) &* 16_777_619
        }
        return hash == 0 ? 1 : hash
    }

    @objc private func echoDeskSetupNotifications() {
        guard (monitor?.serialPort.deviceEchoDeskNotificationProtocol ?? 0) >= 1 else {
            echoDeskFooterStatus?.stringValue = "Update EchoDesk OS to set up Notification Stack."
            return
        }
        presentEchoDeskNotificationSetup()
    }

    private func presentEchoDeskNotificationSetup() {
        if let setup = echoDeskNotificationSetupWindow {
            setup.makeKeyAndOrderFront(nil)
            return
        }

        let setup = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 520, height: 390),
                             styleMask: [.titled, .closable, .fullSizeContentView],
                             backing: .buffered, defer: false)
        setup.title = "Set up Notification Stack"
        setup.titlebarAppearsTransparent = true
        setup.isReleasedWhenClosed = false

        let icon = NSImageView()
        icon.image = NSImage(systemSymbolName: "bell.badge.fill", accessibilityDescription: nil)
        icon.symbolConfiguration = NSImage.SymbolConfiguration(pointSize: 34, weight: .medium)
        icon.contentTintColor = .controlAccentColor
        icon.translatesAutoresizingMaskIntoConstraints = false
        icon.widthAnchor.constraint(equalToConstant: 44).isActive = true
        icon.heightAnchor.constraint(equalToConstant: 44).isActive = true

        let title = echoDeskHero("Choose what reaches EchoDesk")
        let detail = echoDeskMuted("Like a Focus filter, Notification Stack can allow every EchoDesk Desktop notification or silence selected sources. macOS keeps other apps’ Notification Centre history private.")
        detail.maximumNumberOfLines = 4

        let all = NSButton(radioButtonWithTitle: "Allow all EchoDesk Desktop notifications",
                           target: self, action: #selector(echoDeskNotificationScopeChanged(_:)))
        all.tag = 0
        let filtered = NSButton(radioButtonWithTitle: "Silence selected sources",
                                target: self, action: #selector(echoDeskNotificationScopeChanged(_:)))
        filtered.tag = 1
        let saved = UserDefaults.standard.string(forKey: "EchoDeskNotificationExclusions") ?? ""
        all.state = saved.isEmpty ? .on : .off
        filtered.state = saved.isEmpty ? .off : .on

        let exclusions = NSTextField(string: saved)
        exclusions.placeholderString = "Source names, separated by commas"
        exclusions.isEnabled = !saved.isEmpty
        echoDeskNotificationSetupExclusions = exclusions
        echoDeskNotificationSetupAllButton = all
        echoDeskNotificationSetupFilteredButton = filtered

        let cancel = NSButton(title: "Cancel", target: self, action: #selector(echoDeskCancelNotificationSetup))
        let done = echoDeskPrimaryButton("Done", #selector(echoDeskFinishNotificationSetup))
        done.keyEquivalent = "\r"
        let actions = NSStackView(views: [NSView(), cancel, done])
        actions.orientation = .horizontal
        actions.alignment = .centerY
        actions.spacing = 8

        let heading = NSStackView(views: [icon, echoDeskPageStack([title, detail], spacing: 5)])
        heading.orientation = .horizontal
        heading.alignment = .top
        heading.spacing = 14
        let options = echoDeskCard(echoDeskPageStack([all, filtered, exclusions], spacing: 10), padding: 16)
        let content = echoDeskPageStack([heading, options, actions], spacing: 18)
        content.edgeInsets = NSEdgeInsets(top: 36, left: 28, bottom: 24, right: 28)
        setup.contentView = content
        echoDeskNotificationSetupWindow = setup
        if let host = window { host.beginSheet(setup) }
        else { setup.center(); setup.makeKeyAndOrderFront(nil) }
    }

    @objc private func echoDeskNotificationScopeChanged(_ sender: NSButton) {
        let filtered = sender.tag == 1
        echoDeskNotificationSetupAllButton?.state = filtered ? .off : .on
        echoDeskNotificationSetupFilteredButton?.state = filtered ? .on : .off
        echoDeskNotificationSetupExclusions?.isEnabled = filtered
        if !filtered { echoDeskNotificationSetupExclusions?.stringValue = "" }
    }

    @objc private func echoDeskCancelNotificationSetup() {
        closeEchoDeskNotificationSetup()
    }

    private func closeEchoDeskNotificationSetup() {
        guard let setup = echoDeskNotificationSetupWindow else { return }
        if setup.sheetParent != nil { setup.sheetParent?.endSheet(setup) }
        else { setup.orderOut(nil) }
        echoDeskNotificationSetupWindow = nil
        echoDeskNotificationSetupExclusions = nil
        echoDeskNotificationSetupAllButton = nil
        echoDeskNotificationSetupFilteredButton = nil
    }

    @objc private func echoDeskFinishNotificationSetup() {
        let filtered = echoDeskNotificationSetupFilteredButton?.state == .on
        let exclusions = filtered
            ? (echoDeskNotificationSetupExclusions?.stringValue ?? "")
            : ""
        echoDeskNotificationSetupWindow?.ignoresMouseEvents = true
        UNUserNotificationCenter.current().requestAuthorization(options: [.alert, .sound]) { [weak self] _, _ in
            DispatchQueue.main.async {
                self?.sendEchoDeskCommand("echodesk_notifications_set_enabled", extra: ["enabled": true]) { [weak self] ok in
                    guard let self else { return }
                    guard ok else {
                        self.echoDeskNotificationSetupWindow?.ignoresMouseEvents = false
                        self.echoDeskFooterStatus?.stringValue = "EchoDesk did not confirm Notification Stack setup."
                        return
                    }
                    UserDefaults.standard.set(exclusions, forKey: "EchoDeskNotificationExclusions")
                    UserDefaults.standard.set(true, forKey: "EchoDeskNotificationStackEnabled")
                    self.echoDeskNotificationExclusions?.stringValue = exclusions.isEmpty
                        ? "Delivery: All EchoDesk Desktop notifications"
                        : "Silenced sources: \(exclusions)"
                    self.echoDeskNotificationSyncedPort = self.monitor?.serialPort.connectedPort
                    self.echoDeskFooterStatus?.stringValue = "Notification Stack is ready and will reconnect automatically."
                    self.closeEchoDeskNotificationSetup()
                    self.relayEchoDeskOwnedNotifications()
                    self.refreshEchoDeskDesktopState(forceStateRequest: false)
                }
            }
        }
    }

    @objc private func echoDeskDisableNotifications() {
        sendEchoDeskCommand("echodesk_notifications_set_enabled", extra: ["enabled": false]) { [weak self] ok in
            guard let self else { return }
            if ok {
                UserDefaults.standard.set(false, forKey: "EchoDeskNotificationStackEnabled")
                self.echoDeskNotificationSyncedPort = nil
                self.echoDeskFooterStatus?.stringValue = "Notification Stack disabled and cleared."
            } else {
                self.echoDeskFooterStatus?.stringValue = "EchoDesk did not confirm the change. Notification Stack remains enabled."
            }
            self.refreshEchoDeskDesktopState(forceStateRequest: false)
        }
    }

    private func syncEchoDeskNotificationPreferenceIfNeeded() {
        guard UserDefaults.standard.bool(forKey: "EchoDeskNotificationStackEnabled"),
              let monitor,
              monitor.serialPort.isReadyForCommands,
              (monitor.serialPort.deviceEchoDeskNotificationProtocol ?? 0) >= 1,
              let port = monitor.serialPort.connectedPort,
              echoDeskNotificationSyncedPort != port,
              echoDeskNotificationSyncedPort != "syncing:\(port)" else { return }
        echoDeskNotificationSyncedPort = "syncing:\(port)"
        sendEchoDeskCommand("echodesk_notifications_set_enabled", extra: ["enabled": true]) { [weak self] ok in
            guard let self else { return }
            self.echoDeskNotificationSyncedPort = ok ? port : nil
            self.echoDeskFooterStatus?.stringValue = ok
                ? "Notification Stack restored automatically."
                : "Notification Stack will retry when EchoDesk reconnects."
            if ok { self.relayEchoDeskOwnedNotifications() }
        }
    }

    @objc private func echoDeskTestNotification() {
        guard UserDefaults.standard.bool(forKey: "EchoDeskNotificationStackEnabled") else {
            echoDeskFooterStatus?.stringValue = "Set up Notification Stack first."
            return
        }
        let timestamp = UInt64(max(0, Date().timeIntervalSince1970))
        sendEchoDeskCommand("echodesk_notifications_push", extra: [
            "id": echoDeskNotificationID(source: "EchoDesk Desktop", identifier: "test-\(timestamp)"),
            "source": "EchoDesk Desktop", "title": "Notification Stack is ready",
            "body": "This test notification was sent privately over USB.", "timestamp": timestamp,
        ]) { [weak self] ok in self?.echoDeskFooterStatus?.stringValue = ok ? "Test notification sent." : "Test notification failed." }
    }

    private func relayEchoDeskOwnedNotifications() {
        UNUserNotificationCenter.current().getDeliveredNotifications { [weak self] notifications in
            guard let self else { return }
            let exclusions = Set((UserDefaults.standard.string(forKey: "EchoDeskNotificationExclusions") ?? "")
                .split(separator: ",").map { $0.trimmingCharacters(in: .whitespacesAndNewlines).lowercased() })
            guard !exclusions.contains("echodesk desktop") else { return }
            DispatchQueue.main.async {
                for item in notifications.suffix(12) {
                    let content = item.request.content
                    let timestamp = UInt64(max(0, Date().timeIntervalSince1970))
                    self.sendEchoDeskCommand("echodesk_notifications_push", extra: [
                        "id": self.echoDeskNotificationID(source: "EchoDesk Desktop", identifier: item.request.identifier),
                        "source": "EchoDesk Desktop",
                        "title": self.echoDeskNotificationText(content.title.isEmpty ? "EchoDesk Desktop" : content.title, maximumBytes: 64),
                        "body": self.echoDeskNotificationText(content.body, maximumBytes: 160),
                        "timestamp": timestamp,
                    ])
                }
            }
        }
    }

    // MARK: Clock utility

    func showEchoDeskClockUtility(requestedMode: String? = nil) {
        guard monitor?.serialPort.isReadyForCommands == true,
              (monitor?.serialPort.deviceEchoDeskClockProtocol ?? 0) >= 1 else {
            echoDeskFooterStatus?.stringValue = "Update EchoDesk OS to use Desktop Clock controls."
            return
        }
        let mode = requestedMode?.lowercased()
        guard let mode, ["stopwatch", "countdown", "focus"].contains(mode) else {
            presentEchoDeskClockModeChooser()
            return
        }
        presentEchoDeskClockConfiguration(mode: mode)
    }

    private func makeEchoDeskClockWindow(title: String, size: NSSize) -> NSWindow {
        let window = NSWindow(contentRect: NSRect(origin: .zero, size: size),
                              styleMask: [.titled, .closable, .fullSizeContentView],
                              backing: .buffered, defer: false)
        window.styleMask = [.titled, .closable, .fullSizeContentView]
        window.title = title
        window.titlebarAppearsTransparent = true
        window.isMovableByWindowBackground = true
        window.isReleasedWhenClosed = false
        return window
    }

    private func presentEchoDeskClockModeChooser() {
        closeEchoDeskClockWindow()
        let chooser = makeEchoDeskClockWindow(title: "Clock", size: NSSize(width: 470, height: 315))
        let icon = NSImageView()
        icon.image = NSImage(systemSymbolName: "clock.fill", accessibilityDescription: nil)
        icon.symbolConfiguration = NSImage.SymbolConfiguration(pointSize: 32, weight: .medium)
        icon.contentTintColor = .controlAccentColor
        let title = echoDeskHero("What would you like to set?")
        let detail = echoDeskMuted("Choose one Clock mode. EchoDesk Desktop will only ask for the settings that mode needs.")
        let choices: [(String, String, String)] = [
            ("Stopwatch", "stopwatch", "stopwatch"),
            ("Countdown", "countdown", "timer"),
            ("Focus timer", "focus", "brain.head.profile"),
        ]
        let buttons = choices.map { choice -> NSButton in
            let (title, mode, symbol) = choice
            let button = NSButton(title: title, target: self, action: #selector(echoDeskChooseClockMode(_:)))
            button.identifier = NSUserInterfaceItemIdentifier(mode)
            button.image = NSImage(systemSymbolName: symbol, accessibilityDescription: nil)
            button.imagePosition = .imageLeading
            button.bezelStyle = .rounded
            button.controlSize = .large
            return button
        }
        let row = NSStackView(views: buttons)
        row.orientation = .horizontal
        row.distribution = .fillEqually
        row.spacing = 10
        let content = echoDeskPageStack([icon, title, detail, row], spacing: 12)
        content.alignment = .centerX
        installEchoDeskClockContent(content, in: chooser, horizontalPadding: 28, verticalPadding: 34)
        showEchoDeskClockWindow(chooser)
    }

    @objc private func echoDeskChooseClockMode(_ sender: NSButton) {
        guard let mode = sender.identifier?.rawValue else { return }
        presentEchoDeskClockConfiguration(mode: mode)
    }

    private func presentEchoDeskClockConfiguration(mode: String) {
        closeEchoDeskClockWindow()
        echoDeskClockSelectedMode = mode
        let size = mode == "focus" ? NSSize(width: 500, height: 430) : NSSize(width: 470, height: 335)
        let title = mode == "focus" ? "Focus timer" : mode.capitalized
        let clockWindow = makeEchoDeskClockWindow(title: "Configure EchoDesk \(title)", size: size)
        installEchoDeskClockContent(buildEchoDeskClockConfiguration(mode: mode), in: clockWindow,
                                    horizontalPadding: 28, verticalPadding: 34)
        showEchoDeskClockWindow(clockWindow)
        requestEchoDeskClockState()
    }

    private func buildEchoDeskClockConfiguration(mode: String) -> NSView {
        let symbol: String
        let title: String
        let detail: String
        var form: [NSView] = []
        switch mode {
        case "countdown":
            symbol = "timer"
            title = "Set a countdown"
            detail = "Choose the duration to prepare on EchoDesk."
            echoDeskClockCountdownMinutes = NSTextField(string: "5")
            echoDeskClockCountdownMinutes?.alignment = .right
            form = [echoDeskLabeledControl("Minutes", echoDeskClockCountdownMinutes!)]
        case "focus":
            symbol = "brain.head.profile"
            title = "Plan a focus session"
            detail = "Set focused work, break length and the number of rounds."
            echoDeskClockFocusMinutes = NSTextField(string: "25")
            echoDeskClockBreakMinutes = NSTextField(string: "5")
            echoDeskClockRounds = NSTextField(string: "4")
            form = [
                echoDeskLabeledControl("Focus minutes", echoDeskClockFocusMinutes!),
                echoDeskLabeledControl("Break minutes", echoDeskClockBreakMinutes!),
                echoDeskLabeledControl("Rounds", echoDeskClockRounds!),
            ]
        default:
            symbol = "stopwatch"
            title = "Start the stopwatch"
            detail = "Select Done and the stopwatch will begin on EchoDesk."
        }
        let icon = NSImageView()
        icon.image = NSImage(systemSymbolName: symbol, accessibilityDescription: nil)
        icon.symbolConfiguration = NSImage.SymbolConfiguration(pointSize: 34, weight: .medium)
        icon.contentTintColor = .controlAccentColor
        let cancel = NSButton(title: "Cancel", target: self, action: #selector(echoDeskCancelClockConfiguration))
        let done = echoDeskPrimaryButton("Done", #selector(echoDeskClockFinishConfiguration))
        done.keyEquivalent = "\r"
        let actions = NSStackView(views: [NSView(), cancel, done])
        actions.orientation = .horizontal
        actions.spacing = 8
        let content = echoDeskPageStack([icon, echoDeskHero(title), echoDeskMuted(detail)] + form + [actions], spacing: 13)
        content.alignment = .centerX
        return content
    }

    private func installEchoDeskClockContent(_ content: NSView, in window: NSWindow,
                                             horizontalPadding: CGFloat, verticalPadding: CGFloat) {
        let root = NSView()
        content.translatesAutoresizingMaskIntoConstraints = false
        root.addSubview(content)
        NSLayoutConstraint.activate([
            content.leadingAnchor.constraint(equalTo: root.leadingAnchor, constant: horizontalPadding),
            content.trailingAnchor.constraint(equalTo: root.trailingAnchor, constant: -horizontalPadding),
            content.topAnchor.constraint(equalTo: root.topAnchor, constant: verticalPadding),
            content.bottomAnchor.constraint(lessThanOrEqualTo: root.bottomAnchor, constant: -24),
        ])
        window.contentView = root
    }

    private func showEchoDeskClockWindow(_ clockWindow: NSWindow) {
        echoDeskClockWindow = clockWindow
        clockWindow.center()
        clockWindow.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    @objc private func echoDeskCancelClockConfiguration() {
        closeEchoDeskClockWindow()
    }

    private func closeEchoDeskClockWindow() {
        echoDeskClockRefreshTimer?.invalidate()
        echoDeskClockRefreshTimer = nil
        echoDeskClockWindow?.orderOut(nil)
        echoDeskClockWindow = nil
        echoDeskClockSelectedMode = nil
        echoDeskClockCountdownMinutes = nil
        echoDeskClockFocusMinutes = nil
        echoDeskClockBreakMinutes = nil
        echoDeskClockRounds = nil
    }

    @objc private func echoDeskClockFinishConfiguration() {
        guard let mode = echoDeskClockSelectedMode else { return }
        let command: [String: Any]
        switch mode {
        case "countdown":
            let minutes = min(1440, max(1, echoDeskClockCountdownMinutes?.integerValue ?? 5))
            command = ["mode": mode, "action": "configure", "durationSeconds": minutes * 60]
        case "focus":
            let focus = min(180, max(1, echoDeskClockFocusMinutes?.integerValue ?? 25))
            let pause = min(60, max(1, echoDeskClockBreakMinutes?.integerValue ?? 5))
            let rounds = min(12, max(1, echoDeskClockRounds?.integerValue ?? 4))
            command = ["mode": mode, "action": "configure", "focusMinutes": focus,
                       "breakMinutes": pause, "rounds": rounds]
        default:
            command = ["mode": "stopwatch", "action": "start"]
        }
        echoDeskClockWindow?.ignoresMouseEvents = true
        sendEchoDeskCommand("echodesk_clock_control", extra: command) { [weak self] ok in
            guard let self else { return }
            self.echoDeskClockWindow?.ignoresMouseEvents = false
            if ok { self.closeEchoDeskClockWindow() }
            else {
                NSSound.beep()
                self.echoDeskFooterStatus?.stringValue = "EchoDesk did not accept the Clock settings."
            }
        }
    }

    private func requestEchoDeskClockState() {
        guard let monitor, !echoDeskClockStateRequestInFlight else { return }
        echoDeskClockStateRequestInFlight = true
        let request = nextEchoDeskRequest()
        monitor.serialPort.performJSONCommand([
            "cmd": "echodesk_clock_get_state", "version": 1, "requestId": request,
        ], acceptedTypes: ["echodesk_clock_state", "echodesk_error"], timeout: 3) { [weak self] json in
            self?.echoDeskClockStateRequestInFlight = false
            guard let self, let json, json["type"] as? String == "echodesk_clock_state" else { return }
            self.applyEchoDeskClockState(json)
        }
    }

    private func applyEchoDeskClockState(_ state: [String: Any]) {
        func clock(_ seconds: Int, tenths: Bool = false) -> String {
            let safe = max(0, seconds)
            if tenths { return String(format: "%02d:%02d:%02d.0", safe / 3600, (safe / 60) % 60, safe % 60) }
            return String(format: "%02d:%02d:%02d", safe / 3600, (safe / 60) % 60, safe % 60)
        }
        if let stopwatch = state["stopwatch"] as? [String: Any] {
            let elapsed = max(0, stopwatch["elapsedMs"] as? Int ?? 0)
            let wholeSeconds = elapsed / 1000
            echoDeskClockStopwatchLabel?.stringValue = String(
                format: "%02d:%02d:%02d.%d", wholeSeconds / 3600,
                (wholeSeconds / 60) % 60, wholeSeconds % 60, (elapsed / 100) % 10)
        }
        if let countdown = state["countdown"] as? [String: Any] {
            echoDeskClockCountdownLabel?.stringValue = clock(countdown["remainingSeconds"] as? Int ?? 0)
            echoDeskClockCountdownMinutes?.stringValue = String(max(1, Int(ceil(Double(countdown["durationSeconds"] as? Int ?? 300) / 60))))
        }
        if let focus = state["focus"] as? [String: Any] {
            echoDeskClockFocusLabel?.stringValue = clock(focus["remainingSeconds"] as? Int ?? 0)
            let round = focus["currentRound"] as? Int ?? 1
            let rounds = focus["rounds"] as? Int ?? 4
            let phase = (focus["phase"] as? String ?? "focus").uppercased()
            echoDeskClockFocusRoundLabel?.stringValue = "Round \(max(1, round)) of \(rounds) · \(phase)"
            echoDeskClockFocusMinutes?.stringValue = String(focus["focusMinutes"] as? Int ?? 25)
            echoDeskClockBreakMinutes?.stringValue = String(focus["breakMinutes"] as? Int ?? 5)
            echoDeskClockRounds?.stringValue = String(rounds)
        }
    }

    @objc private func echoDeskClockAction(_ sender: NSButton) {
        guard let value = sender.identifier?.rawValue else { return }
        let parts = value.split(separator: ":", maxSplits: 1).map(String.init)
        guard parts.count == 2 else { return }
        sendEchoDeskCommand("echodesk_clock_control", extra: ["mode": parts[0], "action": parts[1]]) { [weak self] ok in
            if ok { self?.requestEchoDeskClockState() }
        }
    }

    @objc private func echoDeskClockConfigureCountdown() {
        let minutes = min(1440, max(1, echoDeskClockCountdownMinutes?.integerValue ?? 5))
        sendEchoDeskCommand("echodesk_clock_control", extra: [
            "mode": "countdown", "action": "configure", "durationSeconds": minutes * 60,
        ]) { [weak self] ok in if ok { self?.requestEchoDeskClockState() } }
    }

    @objc private func echoDeskClockConfigureFocus() {
        let focus = min(180, max(1, echoDeskClockFocusMinutes?.integerValue ?? 25))
        let pause = min(60, max(1, echoDeskClockBreakMinutes?.integerValue ?? 5))
        let rounds = min(12, max(1, echoDeskClockRounds?.integerValue ?? 4))
        sendEchoDeskCommand("echodesk_clock_control", extra: [
            "mode": "focus", "action": "configure", "focusMinutes": focus,
            "breakMinutes": pause, "rounds": rounds,
        ]) { [weak self] ok in if ok { self?.requestEchoDeskClockState() } }
    }

    // MARK: Actions

    @objc private func echoDeskOpenCustomisationSection(_ sender: NSButton) {
        guard let key = sender.identifier?.rawValue,
              let section = echoDeskCustomisationSections[key] else { return }
        echoDeskCustomisationLanding?.isHidden = true
        for view in echoDeskCustomisationSections.values { view.isHidden = view !== section }
        refreshEchoDeskCustomisationPreviews()
        if key == EchoDeskCustomisationSection.pcDashboard.rawValue {
            if echoDeskPcDashboardPendingMetrics == nil, echoDeskPcDashboardInFlightMetrics == nil,
               echoDeskPcDashboardFailedMetrics == nil, let monitor,
               let port = monitor.serialPort.connectedPort,
               let profile = DeviceRegistry.shared.currentProfile() {
                echoDeskPcDashboardExpectedPort = port
                echoDeskPcDashboardExpectedMAC = profile.mac
                echoDeskPcDashboardRevision = 0
            }
            requestEchoDeskPcDashboardState()
        }
        if echoDeskPageViews.indices.contains(EchoDeskDesktopPage.customisation.rawValue),
           let scroll = echoDeskPageViews[EchoDeskDesktopPage.customisation.rawValue] as? NSScrollView {
            scroll.contentView.scroll(to: .zero)
            scroll.reflectScrolledClipView(scroll.contentView)
        }
    }

    @objc private func echoDeskCloseCustomisationSection() {
        for view in echoDeskCustomisationSections.values { view.isHidden = true }
        echoDeskCustomisationLanding?.isHidden = false
        if echoDeskPageViews.indices.contains(EchoDeskDesktopPage.customisation.rawValue),
           let scroll = echoDeskPageViews[EchoDeskDesktopPage.customisation.rawValue] as? NSScrollView {
            scroll.contentView.scroll(to: .zero)
            scroll.reflectScrolledClipView(scroll.contentView)
        }
    }

    @objc private func echoDeskReconnect() { monitor?.serialPort.requestReconnect() }
    @objc private func echoDeskRefresh() { monitor?.codexBar.loadOnce(); requestEchoDeskState() }

    @objc private func echoDeskOpenApp(_ sender: NSButton) {
        guard let app = sender.identifier?.rawValue else { return }
        echoDeskDeviceControlStatus?.stringValue = "Opening \(sender.title)…"
        sendEchoDeskCommand("echodesk_open_app", extra: ["app": app]) { [weak self] ok in
            self?.echoDeskFooterStatus?.stringValue = ok ? "Opened \(sender.title)" : "EchoDesk did not accept the app command."
            self?.echoDeskDeviceControlStatus?.stringValue = ok
                ? "\(sender.title) is now open on EchoDesk."
                : "EchoDesk did not confirm the \(sender.title) command. Try again."
            if ok { self?.requestEchoDeskState() }
            if ok && app == "clock" { self?.showEchoDeskClockUtility() }
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
        echoDeskOverviewTheme?.selectedSegment = echoDeskQuickTheme.selectedSegment
        refreshEchoDeskCustomisationPreviews()
        applyEchoDeskSetting("theme", value: value)
    }

    @objc private func echoDeskOverviewThemeChanged(_ sender: NSSegmentedControl) {
        echoDeskQuickTheme?.selectedSegment = sender.selectedSegment
        refreshEchoDeskCustomisationPreviews()
        applyEchoDeskSetting("theme", value: sender.selectedSegment == 0 ? "light" : "dark")
    }

    @objc private func echoDeskOverviewBrightnessChanged(_ sender: NSSlider) {
        let value = Int(sender.doubleValue.rounded())
        echoDeskOverviewBrightnessValue?.stringValue = "\(value)%"
        echoDeskOverviewBrightness?.doubleValue = Double(value)
        echoDeskDeviceBrightness?.doubleValue = Double(value)
        if let detailed = echoDeskSettingControls["brightness"] as? NSSlider { detailed.doubleValue = Double(value) }
        applyEchoDeskSetting("brightness", value: value)
    }

    @objc private func echoDeskClockFormatChanged() {
        let value = echoDeskQuickClockFormat.indexOfSelectedItem == 0 ? "12h" : "24h"
        applyEchoDeskSetting("clockFormat", value: value)
    }

    @objc private func echoDeskSettingPopupChanged(_ sender: NSPopUpButton) {
        guard let key = sender.identifier?.rawValue,
              let value = sender.selectedItem?.representedObject else { return }
        refreshEchoDeskCustomisationPreviews()
        applyEchoDeskSetting(key, value: value)
    }

    @objc private func echoDeskSettingSliderChanged(_ sender: NSSlider) {
        guard let key = sender.identifier?.rawValue else { return }
        if key == "brightness" {
            echoDeskOverviewBrightness?.doubleValue = sender.doubleValue
            echoDeskDeviceBrightness?.doubleValue = sender.doubleValue
            echoDeskOverviewBrightnessValue?.stringValue = "\(Int(sender.doubleValue.rounded()))%"
        }
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
        if let number = settings["brightness"] as? NSNumber {
            echoDeskOverviewBrightness?.doubleValue = number.doubleValue
            echoDeskDeviceBrightness?.doubleValue = number.doubleValue
            echoDeskOverviewBrightnessValue?.stringValue = "\(number.intValue)%"
        }
        if let theme = settings["theme"] as? String {
            echoDeskQuickTheme?.selectedSegment = theme == "light" ? 0 : 1
            echoDeskOverviewTheme?.selectedSegment = theme == "light" ? 0 : 1
        }
        refreshEchoDeskCustomisationPreviews()
        let unsupported = Set((state["unsupportedSettings"] as? [String]) ?? [])
        for (key, control) in echoDeskSettingControls { control.isEnabled = !unsupported.contains(key) }
    }

    private func applyEchoDeskSetting(_ key: String, value: Any) {
        guard let monitor, monitor.serialPort.state == .connected,
              let port = monitor.serialPort.connectedPort,
              let profile = DeviceRegistry.shared.currentProfile() else {
            echoDeskFooterStatus?.stringValue = "Connect EchoDesk before changing device settings."
            NSSound.beep()
            return
        }
        echoDeskPendingSettingChanges[key] = value
        echoDeskPendingSettingPort = port
        echoDeskPendingSettingMAC = profile.mac
        echoDeskFooterStatus?.stringValue = "Setting change pending…"
        if echoDeskLastStatePort != port || echoDeskLastStateMAC != profile.mac
            || echoDeskLastState?["revision"] as? Int == nil {
            requestEchoDeskState()
            return
        }
        flushPendingEchoDeskSettingChangesIfReady()
    }

    private func flushPendingEchoDeskSettingChangesIfReady() {
        guard !echoDeskApplyingSettingChanges,
              !echoDeskPendingSettingChanges.isEmpty,
              echoDeskPendingSetupQuickTheme == nil,
              let monitor,
              monitor.serialPort.state == .connected,
              monitor.serialPort.connectedPort == echoDeskPendingSettingPort,
              DeviceRegistry.shared.currentProfile()?.mac == echoDeskPendingSettingMAC,
              echoDeskLastStatePort == echoDeskPendingSettingPort,
              echoDeskLastStateMAC == echoDeskPendingSettingMAC,
              let revision = echoDeskLastState?["revision"] as? Int else { return }
        let changes = echoDeskPendingSettingChanges
        echoDeskPendingSettingChanges.removeAll()
        echoDeskApplyingSettingChanges = true
        echoDeskFooterStatus?.stringValue = "Applying device settings…"
        sendEchoDeskCommand("echodesk_set_settings", extra: [
            "baseRevision": revision, "persist": true, "settings": changes,
        ]) { [weak self] ok in
            guard let self else { return }
            self.echoDeskApplyingSettingChanges = false
            if ok {
                self.echoDeskFooterStatus?.stringValue = "Device settings updated."
                self.requestEchoDeskState()
            } else {
                for (key, value) in changes where self.echoDeskPendingSettingChanges[key] == nil {
                    self.echoDeskPendingSettingChanges[key] = value
                }
                self.echoDeskFooterStatus?.stringValue = "The device did not confirm this change. Reconnect and try again."
            }
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
        guard let monitor, monitor.serialPort.state == .connected,
              let port = monitor.serialPort.connectedPort,
              let profile = DeviceRegistry.shared.currentProfile() else {
            echoDeskFooterStatus.stringValue = "Reconnect EchoDesk through USB to apply quick settings."
            return
        }
        echoDeskPendingSetupQuickTheme = theme
        echoDeskPendingSetupQuickClock = clock
        echoDeskPendingSetupQuickPort = port
        echoDeskPendingSetupQuickMAC = profile.mac
        echoDeskSetupQuickContinueButton?.isEnabled = false
        // Setup may have restarted or reflashed the device. Never reuse a
        // revision captured before that transition; request one that matches
        // the currently connected port and hardware identity, then retry from
        // the state callback.
        echoDeskLastState = nil
        echoDeskLastStatePort = nil
        echoDeskLastStateMAC = nil
        requestEchoDeskState()
        echoDeskFooterStatus.stringValue = "Checking the current EchoDesk settings…"
    }

    private func retryPendingEchoDeskSetupQuickSettingsIfReady() {
        guard !echoDeskApplyingSetupQuickSettings,
              echoDeskSetupStep == .quickSettings,
              let theme = echoDeskPendingSetupQuickTheme,
              let clock = echoDeskPendingSetupQuickClock,
              let expectedPort = echoDeskPendingSetupQuickPort,
              let expectedMAC = echoDeskPendingSetupQuickMAC,
              let monitor,
              monitor.serialPort.state == .connected,
              monitor.serialPort.connectedPort == expectedPort,
              DeviceRegistry.shared.currentProfile()?.mac == expectedMAC,
              echoDeskLastStatePort == expectedPort,
              echoDeskLastStateMAC == expectedMAC,
              let revision = echoDeskLastState?["revision"] as? Int else { return }
        echoDeskApplyingSetupQuickSettings = true
        echoDeskSetupQuickContinueButton?.isEnabled = false
        echoDeskFooterStatus.stringValue = "Applying quick settings…"
        sendEchoDeskCommand("echodesk_set_settings", extra: [
            "baseRevision": revision, "persist": true,
            "settings": ["theme": theme, "clockFormat": clock],
        ]) { [weak self] ok in
            guard let self else { return }
            self.echoDeskApplyingSetupQuickSettings = false
            if ok {
                self.echoDeskPendingSetupQuickTheme = nil
                self.echoDeskPendingSetupQuickClock = nil
                self.echoDeskPendingSetupQuickPort = nil
                self.echoDeskPendingSetupQuickMAC = nil
                self.completeEchoDeskGuidedSetup()
            } else {
                self.echoDeskSetupQuickContinueButton?.isEnabled = true
                self.echoDeskFooterStatus.stringValue = "EchoDesk did not accept quick settings. Check the connection and try again."
            }
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
        let installed = monitor.serialPort.deviceFirmwareVersion
        let available = FirmwareManager.shared.latestFirmwareVersionTag ?? EchoDeskFirmwareBundle.version
        echoDeskRecoveryInstalledVersion?.stringValue = installed.map { "EchoDesk OS \($0)" } ?? "Not reported"
        echoDeskRecoveryAvailableVersion?.stringValue = "EchoDesk OS \(available)"
        if let installed {
            let normalizedInstalled = installed.trimmingCharacters(in: CharacterSet(charactersIn: "vV"))
            let normalizedAvailable = available.trimmingCharacters(in: CharacterSet(charactersIn: "vV"))
            echoDeskRecoveryUpdateStatus?.stringValue = normalizedInstalled == normalizedAvailable
                ? "EchoDesk OS is up to date."
                : "A newer verified EchoDesk OS release is available."
        } else {
            echoDeskRecoveryUpdateStatus?.stringValue = "Connect EchoDesk to check its installed version."
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
        monitor.prepareForFirmwareUpdate { [weak self] _ in
            self?.performEchoDeskVerifiedRecoveryAfterWarning(port: port)
        }
    }

    private func performEchoDeskVerifiedRecoveryAfterWarning(port: String) {
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
                FirmwareManager.shared.prepareBundledRecoveryFirmware(variant: kDisplayVariantST7789) { ok, message in
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
                                if success {
                                    monitor.queueFirmwareUpdateCompletion(expectedVersion: EchoDeskFirmwareBundle.version)
                                }
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
        heading.font = .systemFont(ofSize: 26, weight: .semibold)
        let detail = echoDeskMuted(subtitle)
        return echoDeskPageStack([heading, detail], spacing: 4)
    }

    private func echoDeskHero(_ text: String) -> NSTextField {
        let label = NSTextField(wrappingLabelWithString: text)
        label.font = .systemFont(ofSize: 20, weight: .semibold)
        return label
    }

    private func echoDeskSectionTitle(_ text: String) -> NSTextField {
        let label = NSTextField(labelWithString: text)
        label.font = .systemFont(ofSize: 15, weight: .semibold)
        return label
    }

    private func echoDeskMuted(_ text: String) -> NSTextField {
        let label = NSTextField(wrappingLabelWithString: text)
        label.font = .systemFont(ofSize: 12.5)
        label.textColor = .secondaryLabelColor
        label.maximumNumberOfLines = 0
        return label
    }

    private func echoDeskMetricValue(_ text: String) -> NSTextField {
        let label = NSTextField(labelWithString: text)
        label.font = .systemFont(ofSize: 17, weight: .semibold)
        label.lineBreakMode = .byTruncatingTail
        return label
    }

    private func echoDeskCompactFact(_ caption: String, _ value: NSTextField) -> NSView {
        let label = NSTextField(labelWithString: caption.uppercased())
        label.font = .systemFont(ofSize: 9.5, weight: .semibold)
        label.textColor = .tertiaryLabelColor
        value.font = .systemFont(ofSize: 13, weight: .semibold)
        let stack = echoDeskPageStack([label, value], spacing: 3)
        stack.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        return stack
    }

    private func echoDeskFocusedSettingsSection(_ section: EchoDeskCustomisationSection,
                                                preview: EchoDeskScreenPreview,
                                                content: NSView) -> NSView {
        let back = NSButton(title: "All Customisation", target: self,
                            action: #selector(echoDeskCloseCustomisationSection))
        back.image = NSImage(systemSymbolName: "chevron.left", accessibilityDescription: nil)
        back.imagePosition = .imageLeading
        back.bezelStyle = .inline
        let title = echoDeskPageHeader(section.title, section.subtitle)
        let heading = echoDeskPageStack([back, title], spacing: 8)
        preview.translatesAutoresizingMaskIntoConstraints = false
        preview.heightAnchor.constraint(equalTo: preview.widthAnchor, multiplier: 0.586).isActive = true
        let previewCard = echoDeskNativeGroup(echoDeskPageStack([
            echoDeskMuted("LIVE DEVICE PREVIEW"), preview,
            echoDeskMuted("Previewed at the physical EchoDesk Mini’s 320 × 240 landscape ratio."),
        ], spacing: 9), padding: 14)
        let workspace = echoDeskAdaptiveColumns([previewCard, content], collapseBelow: 700)
        return echoDeskPageStack([heading, workspace], spacing: 16)
    }

    private func refreshEchoDeskCustomisationPreviews() {
        let dark = echoDeskQuickTheme?.selectedSegment != 0
        echoDeskCustomisationPreviews[EchoDeskCustomisationSection.display.rawValue]?.dark = dark
        echoDeskCustomisationPreviews[EchoDeskCustomisationSection.preferences.rawValue]?.dark =
            NSApp.effectiveAppearance.bestMatch(from: [.darkAqua, .aqua]) == .darkAqua
        if let popup = echoDeskSettingControls["clockFormat"] as? NSPopUpButton {
            echoDeskCustomisationPreviews[EchoDeskCustomisationSection.clock.rawValue]?.primaryText =
                popup.selectedItem?.representedObject as? String == "24h" ? "18:42" : "6:42"
        }
        if let layout = echoDeskSettingControls["nowPlayingLayout"] as? NSPopUpButton {
            echoDeskCustomisationPreviews[EchoDeskCustomisationSection.nowPlaying.rawValue]?.secondaryText =
                layout.titleOfSelectedItem ?? "Now Playing"
        }
        let selectedMetrics = echoDeskPcDashboardPopups.compactMap { $0.selectedItem?.title }.filter { $0 != "None" }
        echoDeskCustomisationPreviews[EchoDeskCustomisationSection.pcDashboard.rawValue]?.secondaryText =
            selectedMetrics.isEmpty ? "Choose metrics" : selectedMetrics.prefix(2).joined(separator: " · ")
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
        for view in views {
            if view is NSTextField || view is NSStackView || view is EchoDeskSurfaceView
                || view is EchoDeskActionTile || view is EchoDeskScreenPreview
                || view is NSGridView || view is NSProgressIndicator || view is NSBox {
                view.widthAnchor.constraint(equalTo: stack.widthAnchor).isActive = true
            }
        }
        return stack
    }

    private func echoDeskAdaptiveColumns(_ views: [NSView], collapseBelow: CGFloat = 660,
                                         equalColumns: Bool = true) -> NSStackView {
        EchoDeskAdaptiveStackView(views: views, collapseBelow: collapseBelow,
                                  equalColumns: equalColumns)
    }

    private func echoDeskCard(_ content: NSView, padding: CGFloat = 10) -> NSView {
        let card = EchoDeskSurfaceView(.card)
        content.translatesAutoresizingMaskIntoConstraints = false
        card.addSubview(content)
        let horizontalInset = min(padding, 14)
        if padding <= 20 {
            let separator = NSBox()
            separator.boxType = .separator
            separator.translatesAutoresizingMaskIntoConstraints = false
            card.addSubview(separator)
            NSLayoutConstraint.activate([
                content.leadingAnchor.constraint(equalTo: card.leadingAnchor, constant: horizontalInset),
                content.trailingAnchor.constraint(equalTo: card.trailingAnchor, constant: -horizontalInset),
                content.topAnchor.constraint(equalTo: card.topAnchor, constant: 6),
                content.bottomAnchor.constraint(equalTo: separator.topAnchor, constant: -12),
                separator.leadingAnchor.constraint(equalTo: card.leadingAnchor, constant: horizontalInset),
                separator.trailingAnchor.constraint(equalTo: card.trailingAnchor, constant: -horizontalInset),
                separator.bottomAnchor.constraint(equalTo: card.bottomAnchor),
            ])
        } else {
            NSLayoutConstraint.activate([
                content.leadingAnchor.constraint(equalTo: card.leadingAnchor, constant: padding),
                content.trailingAnchor.constraint(equalTo: card.trailingAnchor, constant: -padding),
                content.topAnchor.constraint(equalTo: card.topAnchor, constant: padding),
                content.bottomAnchor.constraint(equalTo: card.bottomAnchor, constant: -padding),
            ])
        }
        return card
    }

    /// Genuine AppKit group box, reserved for surfaces that are meaningfully
    /// selectable or self-contained (for example connection choices and the
    /// physical-device preview). Ordinary settings sections stay flat.
    private func echoDeskNativeGroup(_ content: NSView, padding: CGFloat = 14) -> NSBox {
        let box = NSBox()
        box.boxType = .primary
        box.titlePosition = .noTitle
        box.contentViewMargins = NSSize(width: padding, height: padding)
        box.contentView = content
        return box
    }

    private func echoDeskPrimaryButton(_ title: String, _ action: Selector) -> NSButton {
        let button = NSButton(title: title, target: self, action: action)
        button.bezelStyle = .rounded
        button.bezelColor = .controlAccentColor
        button.controlSize = .regular
        button.font = .systemFont(ofSize: 13, weight: .medium)
        button.keyEquivalent = ""
        return button
    }

    private func echoDeskChoiceCard(_ title: String, _ detail: String, _ buttonTitle: String,
                                    _ action: Selector) -> NSView {
        let symbol: String
        if title.localizedCaseInsensitiveContains("USB") { symbol = "cable.connector" }
        else if title.localizedCaseInsensitiveContains("WiFi") { symbol = "wifi" }
        else if title.localizedCaseInsensitiveContains("update") { symbol = "arrow.triangle.2.circlepath" }
        else { symbol = "checkmark.circle" }
        let icon = NSImageView()
        icon.image = NSImage(systemSymbolName: symbol, accessibilityDescription: nil)
        icon.symbolConfiguration = NSImage.SymbolConfiguration(pointSize: 22, weight: .medium)
        icon.contentTintColor = .controlAccentColor
        icon.translatesAutoresizingMaskIntoConstraints = false
        icon.widthAnchor.constraint(equalToConstant: 32).isActive = true
        icon.heightAnchor.constraint(equalToConstant: 32).isActive = true
        let heading = echoDeskSectionTitle(title)
        let headingRow = NSStackView(views: [icon, heading])
        headingRow.orientation = .horizontal; headingRow.alignment = .centerY; headingRow.spacing = 10
        let description = echoDeskMuted(detail)
        description.maximumNumberOfLines = 5
        return echoDeskNativeGroup(
            echoDeskPageStack([headingRow, description, echoDeskPrimaryButton(buttonTitle, action)]),
            padding: 14)
    }

    private func echoDeskRecoveryAction(_ tag: Int, _ title: String, _ detail: String) -> NSView {
        let heading = echoDeskSectionTitle(title)
        let text = echoDeskMuted(detail)
        let button = NSButton(title: title, target: self, action: #selector(echoDeskRecoveryActionPressed(_:)))
        button.tag = tag; button.bezelStyle = .rounded
        if tag == 0 {
            button.bezelColor = .controlAccentColor
            button.contentTintColor = .white
        } else if tag == 3 {
            button.contentTintColor = .systemRed
        }
        let row = NSStackView(views: [echoDeskPageStack([heading, text], spacing: 5), NSView(), button])
        row.orientation = .horizontal; row.alignment = .centerY
        return echoDeskCard(row)
    }

    private func echoDeskLabeledControl(_ label: String, _ control: NSView) -> NSView {
        let caption = NSTextField(wrappingLabelWithString: label)
        caption.font = .systemFont(ofSize: 13, weight: .regular)
        caption.translatesAutoresizingMaskIntoConstraints = false
        caption.maximumNumberOfLines = 2
        caption.widthAnchor.constraint(equalToConstant: 155).isActive = true
        let row = NSStackView(views: [caption, NSView(), control])
        row.orientation = .horizontal; row.alignment = .centerY; row.spacing = 16
        control.setContentHuggingPriority(.required, for: .horizontal)
        control.setContentCompressionResistancePriority(.defaultHigh, for: .horizontal)
        let minimumControlWidth = control.widthAnchor.constraint(greaterThanOrEqualToConstant: 170)
        minimumControlWidth.priority = .defaultHigh
        minimumControlWidth.isActive = true
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
        popup.widthAnchor.constraint(greaterThanOrEqualToConstant: 210).isActive = true
        echoDeskSettingControls[key] = popup
        return popup
    }

    private func echoDeskAppGrid(columns: Int) -> NSView {
        let apps = [("home", "Home", "house"), ("codex", "Codex Usage", "curlybraces"),
                    ("clock", "Clock", "clock"), ("performance", "Performance", "waveform.path.ecg"),
                    ("nowPlaying", "Now Playing", "music.note"),
                    ("myDevice", "My device", "desktopcomputer"),
                    ("notificationStack", "Notification Stack", "bell.badge"),
                    ("pcDashboard", "PC Dashboard", "gauge.with.dots.needle.67percent"),
                    ("bedside", "Bedside Mode", "bed.double.fill"),
                    ("settings", "Settings", "gearshape")]
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
                button.controlSize = .regular
                button.translatesAutoresizingMaskIntoConstraints = false
                button.heightAnchor.constraint(equalToConstant: 38).isActive = true
                button.isEnabled = false
                echoDeskAppButtons[id, default: []].append(button)
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
            popup.controlSize = .regular
            popup.font = .systemFont(ofSize: 13, weight: .regular)
        }
        view.subviews.forEach(styleEchoDeskDropdowns)
    }
}

private extension String {
    var echoDeskTitle: String {
        switch self {
        case "nowPlaying": return "Now Playing"
        case "myDevice": return "My device"
        case "notificationStack": return "Notification Stack"
        case "pcDashboard": return "PC Dashboard"
        case "bedside": return "Bedside Mode"
        default: return prefix(1).uppercased() + String(dropFirst())
        }
    }
}
#endif
