#if ECHODESK_DESKTOP
import Cocoa

extension SettingsWindowController {
    func buildEchoDeskWiFiSetupBox() -> NSView {
        let heading = makeSectionHeading("Optional Wi-Fi setup")
        let helper = NSTextField(wrappingLabelWithString:
            "On EchoDesk, choose Setup with Wi-Fi. Join the protected EchoDesk-XXXX network in macOS using the displayed 8-digit code, then find the device here. USB remains the default, update and recovery connection.")
        helper.font = NSFont.appFont(.subheadline)
        helper.textColor = .secondaryLabelColor
        helper.maximumNumberOfLines = 4
        helper.translatesAutoresizingMaskIntoConstraints = false
        helper.widthAnchor.constraint(lessThanOrEqualToConstant: 620).isActive = true

        echoDeskWiFiStatusLabel = NSTextField(wrappingLabelWithString: "Waiting for Wi-Fi setup.")
        echoDeskWiFiStatusLabel.font = NSFont.appFont(.body, weight: .medium)
        echoDeskWiFiStatusLabel.maximumNumberOfLines = 3
        echoDeskWiFiStatusLabel.translatesAutoresizingMaskIntoConstraints = false
        echoDeskWiFiStatusLabel.widthAnchor.constraint(lessThanOrEqualToConstant: 620).isActive = true

        echoDeskWiFiDevicePopup = NSPopUpButton()
        echoDeskWiFiDevicePopup.addItem(withTitle: "No devices found")
        echoDeskWiFiDevicePopup.translatesAutoresizingMaskIntoConstraints = false
        echoDeskWiFiDevicePopup.widthAnchor.constraint(equalToConstant: 310).isActive = true

        echoDeskWiFiFindButton = NSButton(title: "Find Wi-Fi devices", target: self,
                                          action: #selector(findEchoDeskWiFiDevices))
        echoDeskWiFiFindButton.bezelStyle = .rounded
        echoDeskWiFiSettingsButton = NSButton(title: "Open Wi-Fi Settings", target: self,
                                              action: #selector(openMacWiFiSettings))
        echoDeskWiFiSettingsButton.bezelStyle = .rounded
        echoDeskWiFiSettingsButton.controlSize = .small
        let discoveryRow = NSStackView(views: [echoDeskWiFiDevicePopup, echoDeskWiFiFindButton,
                                               echoDeskWiFiSettingsButton])
        discoveryRow.orientation = .horizontal
        discoveryRow.spacing = 8

        echoDeskWiFiCodeField = NSTextField()
        echoDeskWiFiCodeField.placeholderString = "8-digit code"
        echoDeskWiFiCodeField.translatesAutoresizingMaskIntoConstraints = false
        echoDeskWiFiCodeField.widthAnchor.constraint(equalToConstant: 115).isActive = true
        echoDeskWiFiSSIDField = NSTextField()
        echoDeskWiFiSSIDField.placeholderString = "Home Wi-Fi name"
        echoDeskWiFiSSIDField.translatesAutoresizingMaskIntoConstraints = false
        echoDeskWiFiSSIDField.widthAnchor.constraint(equalToConstant: 190).isActive = true
        echoDeskWiFiPasswordField = NSSecureTextField()
        echoDeskWiFiPasswordField.placeholderString = "Home Wi-Fi password"
        echoDeskWiFiPasswordField.translatesAutoresizingMaskIntoConstraints = false
        echoDeskWiFiPasswordField.widthAnchor.constraint(equalToConstant: 190).isActive = true
        let credentials = NSStackView(views: [echoDeskWiFiCodeField, echoDeskWiFiSSIDField,
                                              echoDeskWiFiPasswordField])
        credentials.orientation = .horizontal
        credentials.spacing = 8

        echoDeskWiFiProvisionButton = NSButton(title: "Set up Wi-Fi", target: self,
                                               action: #selector(provisionEchoDeskWiFi))
        echoDeskWiFiProvisionButton.bezelStyle = .rounded
        echoDeskWiFiProvisionButton.keyEquivalent = ""
        echoDeskWiFiCancelButton = NSButton(title: "Cancel", target: self,
                                            action: #selector(cancelEchoDeskWiFiSetup))
        echoDeskWiFiCancelButton.bezelStyle = .rounded
        let actions = NSStackView(views: [echoDeskWiFiProvisionButton, echoDeskWiFiCancelButton])
        actions.orientation = .horizontal
        actions.spacing = 8

        let stack = NSStackView(views: [heading, helper, echoDeskWiFiStatusLabel,
                                        discoveryRow, credentials, actions])
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 9

        EchoDeskWiFiSetupManager.shared.onChange = { [weak self] in
            DispatchQueue.main.async { self?.refreshEchoDeskWiFiSetup() }
        }
        refreshEchoDeskWiFiSetup()
        return stack
    }

    func refreshEchoDeskWiFiSetup() {
        guard echoDeskWiFiStatusLabel != nil else { return }
        let manager = EchoDeskWiFiSetupManager.shared
        echoDeskWiFiDevicePopup.removeAllItems()
        if manager.devices.isEmpty {
            echoDeskWiFiDevicePopup.addItem(withTitle: "No supported EchoDesk found")
            echoDeskWiFiDevicePopup.isEnabled = false
        } else {
            echoDeskWiFiDevicePopup.isEnabled = true
            for device in manager.devices {
                let suffix = device.setupMode ? (device.setupSSID.map { " · \($0)" } ?? "")
                    : " · \(device.address)"
                echoDeskWiFiDevicePopup.addItem(withTitle: "\(device.displayName)\(suffix)")
                echoDeskWiFiDevicePopup.lastItem?.representedObject = device.deviceId
            }
        }

        switch manager.status {
        case .idle:
            echoDeskWiFiStatusLabel.stringValue = "Join EchoDesk-XXXX, then find Wi-Fi devices."
        case .scanning:
            echoDeskWiFiStatusLabel.stringValue = "Looking for EchoDesk on the current network…"
        case .found:
            echoDeskWiFiStatusLabel.stringValue = "EchoDesk found. Enter its code and your home network details."
        case .notFound:
            echoDeskWiFiStatusLabel.stringValue = "No supported EchoDesk was found. Confirm macOS is joined to EchoDesk-XXXX."
        case .provisioning:
            echoDeskWiFiStatusLabel.stringValue = "Sending network settings securely…"
        case .provisioned(let ssid):
            echoDeskWiFiStatusLabel.stringValue = "EchoDesk saved \(ssid) and is restarting. Rejoin that network, then find it again."
            echoDeskWiFiPasswordField.stringValue = ""
            echoDeskWiFiCodeField.stringValue = ""
        case .cancelled:
            echoDeskWiFiStatusLabel.stringValue = "Wi-Fi setup cancelled. USB remains available."
        case .failed(let message):
            echoDeskWiFiStatusLabel.stringValue = message
        }
        let busy = manager.status == .scanning || manager.status == .provisioning
        echoDeskWiFiFindButton.isEnabled = !busy
        echoDeskWiFiProvisionButton.isEnabled = !busy && !manager.devices.isEmpty
        echoDeskWiFiCancelButton.isEnabled = busy
    }

    @objc private func findEchoDeskWiFiDevices() {
        EchoDeskWiFiSetupManager.shared.discover { [weak self] _ in
            self?.refreshEchoDeskWiFiSetup()
        }
        refreshEchoDeskWiFiSetup()
    }

    @objc private func provisionEchoDeskWiFi() {
        guard let deviceId = echoDeskWiFiDevicePopup.selectedItem?.representedObject as? String else {
            echoDeskWiFiStatusLabel.stringValue = "Find and select EchoDesk first."
            return
        }
        let code = echoDeskWiFiCodeField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        let ssid = echoDeskWiFiSSIDField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        let password = echoDeskWiFiPasswordField.stringValue
        let ssidByteCount = ssid.lengthOfBytes(using: .utf8)
        let passwordByteCount = password.lengthOfBytes(using: .utf8)
        guard code.count == 8, code.allSatisfy({ $0.isNumber }),
              (1...32).contains(ssidByteCount),
              password.isEmpty || (8...63).contains(passwordByteCount) else {
            echoDeskWiFiStatusLabel.stringValue = "Enter the 8-digit code, a valid Wi-Fi name, and an empty or 8–63 byte password."
            return
        }
        EchoDeskWiFiSetupManager.shared.provision(deviceId: deviceId, setupCode: code,
                                                   ssid: ssid, password: password) { [weak self] _ in
            self?.refreshEchoDeskWiFiSetup()
        }
        refreshEchoDeskWiFiSetup()
    }

    @objc private func cancelEchoDeskWiFiSetup() {
        EchoDeskWiFiSetupManager.shared.cancel()
        echoDeskWiFiPasswordField.stringValue = ""
        refreshEchoDeskWiFiSetup()
    }

    @objc private func openMacWiFiSettings() {
        guard let url = URL(string: "x-apple.systempreferences:com.apple.wifi-settings-extension") else { return }
        NSWorkspace.shared.open(url)
    }
}
#endif
