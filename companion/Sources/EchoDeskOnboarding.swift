#if ECHODESK_DESKTOP
import Cocoa

enum EchoDeskSetupTransport: Equatable {
    case usb
    case wifi
}

/// First-run experience for the EchoDesk-branded macOS app. The legal page is
/// intentionally present but blank at the product owner's explicit request;
/// it contains no fabricated terms, checkbox, link, or blocking gate.
final class EchoDeskOnboardingController: NSWindowController {
    private enum Step: Int, CaseIterable { case hello, legal, connection, ready }

    private var step: Step = .hello
    private let content = NSView()
    private let titleLabel = NSTextField(labelWithString: "")
    private let detailLabel = NSTextField(wrappingLabelWithString: "")
    private let usbButton = NSButton(radioButtonWithTitle: "Set up with USB", target: nil, action: nil)
    private let wifiButton = NSButton(radioButtonWithTitle: "Setup with Wi-Fi", target: nil, action: nil)
    private let backButton = NSButton(title: "Back", target: nil, action: nil)
    private let continueButton = NSButton(title: "Continue", target: nil, action: nil)
    private let progress = NSTextField(labelWithString: "")
    private(set) var selectedTransport: EchoDeskSetupTransport = .usb
    var onFinished: ((EchoDeskSetupTransport) -> Void)?

    convenience init() {
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 620, height: 430),
            styleMask: [.titled, .closable],
            backing: .buffered,
            defer: false
        )
        window.title = "Welcome to EchoDesk"
        window.isReleasedWhenClosed = false
        window.center()
        self.init(window: window)
        buildUI()
        render()
    }

    func show() {
        window?.center()
        window?.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    private func buildUI() {
        guard let root = window?.contentView else { return }
        titleLabel.font = .systemFont(ofSize: 30, weight: .bold)
        detailLabel.font = .systemFont(ofSize: 15)
        detailLabel.textColor = .secondaryLabelColor
        detailLabel.maximumNumberOfLines = 6

        usbButton.target = self
        usbButton.action = #selector(transportChanged(_:))
        usbButton.state = .on
        wifiButton.target = self
        wifiButton.action = #selector(transportChanged(_:))

        let choices = NSStackView(views: [usbButton, wifiButton])
        choices.identifier = NSUserInterfaceItemIdentifier("EchoDeskTransportChoices")
        choices.orientation = .vertical
        choices.alignment = .leading
        choices.spacing = 12

        content.addSubview(titleLabel)
        content.addSubview(detailLabel)
        content.addSubview(choices)
        [titleLabel, detailLabel, choices].forEach { $0.translatesAutoresizingMaskIntoConstraints = false }
        NSLayoutConstraint.activate([
            titleLabel.leadingAnchor.constraint(equalTo: content.leadingAnchor),
            titleLabel.trailingAnchor.constraint(equalTo: content.trailingAnchor),
            titleLabel.topAnchor.constraint(equalTo: content.topAnchor),
            detailLabel.leadingAnchor.constraint(equalTo: content.leadingAnchor),
            detailLabel.trailingAnchor.constraint(equalTo: content.trailingAnchor),
            detailLabel.topAnchor.constraint(equalTo: titleLabel.bottomAnchor, constant: 18),
            choices.leadingAnchor.constraint(equalTo: content.leadingAnchor),
            choices.topAnchor.constraint(equalTo: detailLabel.bottomAnchor, constant: 26),
        ])

        backButton.target = self
        backButton.action = #selector(goBack)
        continueButton.target = self
        continueButton.action = #selector(goForward)
        continueButton.keyEquivalent = "\r"
        progress.font = .monospacedDigitSystemFont(ofSize: 12, weight: .medium)
        progress.textColor = .secondaryLabelColor

        let buttons = NSStackView(views: [backButton, continueButton])
        buttons.orientation = .horizontal
        buttons.spacing = 10
        [content, progress, buttons].forEach { root.addSubview($0); $0.translatesAutoresizingMaskIntoConstraints = false }
        NSLayoutConstraint.activate([
            content.leadingAnchor.constraint(equalTo: root.leadingAnchor, constant: 48),
            content.trailingAnchor.constraint(equalTo: root.trailingAnchor, constant: -48),
            content.topAnchor.constraint(equalTo: root.topAnchor, constant: 48),
            content.bottomAnchor.constraint(equalTo: progress.topAnchor, constant: -24),
            progress.leadingAnchor.constraint(equalTo: root.leadingAnchor, constant: 48),
            progress.centerYAnchor.constraint(equalTo: buttons.centerYAnchor),
            buttons.trailingAnchor.constraint(equalTo: root.trailingAnchor, constant: -32),
            buttons.bottomAnchor.constraint(equalTo: root.bottomAnchor, constant: -26),
        ])
    }

    private func render() {
        let choices = content.subviews.first { $0.identifier?.rawValue == "EchoDeskTransportChoices" }
        choices?.isHidden = step != .connection
        backButton.isHidden = step == .hello
        progress.stringValue = "\(step.rawValue + 1) of \(Step.allCases.count)"
        switch step {
        case .hello:
            titleLabel.stringValue = "Hello."
            detailLabel.stringValue = "Welcome to EchoDesk Desktop for macOS. Set up your EchoDesk Mini, keep EchoDesk OS current, and send Codex usage from one native app."
            continueButton.title = "Get Started"
        case .legal:
            titleLabel.stringValue = "Terms & Privacy"
            detailLabel.stringValue = ""
            continueButton.title = "Continue"
        case .connection:
            titleLabel.stringValue = "Connect EchoDesk"
            detailLabel.stringValue = selectedTransport == .usb
                ? "USB is the default and remains the firmware update and recovery connection."
                : "Choose Setup with Wi-Fi on EchoDesk, join its temporary network in macOS, then find it from the Connection page."
            continueButton.title = "Continue"
        case .ready:
            titleLabel.stringValue = "You’re ready."
            detailLabel.stringValue = selectedTransport == .usb
                ? "Connect EchoDesk Mini by USB. Desktop will detect it automatically."
                : "Open Connection to discover the temporary EchoDesk network and provision your home Wi-Fi. USB remains available."
            continueButton.title = "Open EchoDesk Desktop"
        }
    }

    @objc private func transportChanged(_ sender: NSButton) {
        selectedTransport = sender === wifiButton ? .wifi : .usb
        usbButton.state = selectedTransport == .usb ? .on : .off
        wifiButton.state = selectedTransport == .wifi ? .on : .off
        render()
    }

    @objc private func goBack() {
        guard let previous = Step(rawValue: step.rawValue - 1) else { return }
        step = previous
        render()
    }

    @objc private func goForward() {
        if step == .ready {
            UserDefaults.standard.set(true, forKey: "EchoDeskOnboardingCompletedV1")
            window?.orderOut(nil)
            onFinished?(selectedTransport)
            return
        }
        guard let next = Step(rawValue: step.rawValue + 1) else { return }
        step = next
        render()
    }
}
#endif
