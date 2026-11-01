import Foundation
import Network
import Security
import Darwin

struct EchoDeskWiFiDevice: Equatable {
    let deviceId: String
    let displayName: String
    let model: String
    let setupSSID: String?
    let address: String
    let port: UInt16
    let setupMode: Bool
    let paired: Bool
    let firmwareVersion: String
    let setupState: String
    fileprivate let discoveredAt: Date
}

enum EchoDeskWiFiSetupStatus: Equatable {
    case idle
    case scanning
    case found
    case notFound
    case provisioning
    case provisioned(ssid: String)
    case cancelled
    case failed(message: String)
}

enum EchoDeskWiFiSetupError: LocalizedError, Equatable {
    case invalidRequest(String)
    case networkUnavailable
    case deviceNotFound
    case deviceUnreachable
    case rejected
    case invalidResponse
    case secureStorage
    case cancelled

    var errorDescription: String? {
        switch self {
        case .invalidRequest(let message): return message
        case .networkUnavailable: return "Wi-Fi discovery is unavailable on this network."
        case .deviceNotFound: return "Scan again and choose the EchoDesk shown on the setup network."
        case .deviceUnreachable: return "EchoDesk could not be reached. Rejoin its temporary Wi-Fi network and try again."
        case .rejected: return "EchoDesk rejected setup. Check the 8-digit code and Wi-Fi password."
        case .invalidResponse: return "EchoDesk returned an invalid setup response."
        case .secureStorage: return "The device joined Wi-Fi, but its pairing credential could not be saved in Keychain."
        case .cancelled: return "Wi-Fi setup was cancelled."
        }
    }
}

/// Opt-in EchoDesk Mini Wi-Fi provisioning. USB remains the default transport.
/// All blocking network work stays on `worker`; UI callbacks are delivered on
/// the main queue. Callers can select only a device returned by the latest scan
/// and can never provide a destination address or port.
final class EchoDeskWiFiSetupManager {
    static let shared = EchoDeskWiFiSetupManager()

    private let worker = DispatchQueue(label: "com.echodesk.wifi-setup", qos: .userInitiated)
    private let lock = NSLock()
    private var operation: UInt64 = 0
    private var discovered: [EchoDeskWiFiDevice] = []

    private(set) var status: EchoDeskWiFiSetupStatus = .idle
    private(set) var devices: [EchoDeskWiFiDevice] = []
    var onChange: (() -> Void)?

    private init() {}

    func discover(completion: @escaping (Result<[EchoDeskWiFiDevice], Error>) -> Void) {
        let id = nextOperation()
        publish(status: .scanning, devices: [])
        worker.async { [weak self] in
            guard let self else { return }
            do {
                let candidates = try EchoDeskWiFiProtocol.discover()
                guard self.isCurrent(id) else { return }
                self.lock.lock()
                self.discovered = candidates
                self.lock.unlock()
                self.publish(status: candidates.isEmpty ? .notFound : .found, devices: candidates)
                DispatchQueue.main.async { completion(.success(candidates)) }
            } catch {
                guard self.isCurrent(id) else { return }
                self.publish(status: .failed(message: error.localizedDescription), devices: [])
                DispatchQueue.main.async { completion(.failure(error)) }
            }
        }
    }

    func provision(deviceId: String, setupCode: String, ssid: String, password: String,
                   completion: @escaping (Result<Void, Error>) -> Void) {
        guard EchoDeskWiFiProtocol.validDeviceId(deviceId),
              setupCode.utf8.count == 8,
              setupCode.utf8.allSatisfy({ (48...57).contains($0) }),
              let ssidBytes = ssid.data(using: .utf8), (1...32).contains(ssidBytes.count),
              !ssid.unicodeScalars.contains(where: CharacterSet.controlCharacters.contains),
              let passwordBytes = password.data(using: .utf8), passwordBytes.count <= 63,
              password.isEmpty || passwordBytes.count >= 8,
              !password.unicodeScalars.contains(where: CharacterSet.controlCharacters.contains) else {
            completion(.failure(EchoDeskWiFiSetupError.invalidRequest(
                "Use the 8-digit code, a 1–32 byte Wi-Fi name, and an empty or 8–63 byte password."
            )))
            return
        }

        lock.lock()
        let target = discovered.first {
            $0.deviceId == deviceId && $0.setupMode && !$0.paired
                && Date().timeIntervalSince($0.discoveredAt) <= EchoDeskWiFiProtocol.discoveryTTL
        }
        lock.unlock()
        guard let target else {
            completion(.failure(EchoDeskWiFiSetupError.deviceNotFound))
            return
        }

        let id = nextOperation()
        publish(status: .provisioning, devices: devices)
        worker.async { [weak self] in
            guard let self else { return }
            do {
                let token = try EchoDeskWiFiProtocol.randomHex(byteCount: 32)
                try EchoDeskWiFiProtocol.provision(
                    target: target, setupCode: setupCode, ssid: ssid,
                    password: password, clientToken: token
                )
                guard self.isCurrent(id) else { return }
                guard EchoDeskWiFiKeychain.save(token: token, deviceId: deviceId) else {
                    throw EchoDeskWiFiSetupError.secureStorage
                }
                self.lock.lock()
                let currentDevices = self.discovered
                self.lock.unlock()
                self.publish(status: .provisioned(ssid: ssid), devices: currentDevices)
                DispatchQueue.main.async { completion(.success(())) }
            } catch {
                guard self.isCurrent(id) else { return }
                self.publish(status: .failed(message: error.localizedDescription), devices: self.devices)
                DispatchQueue.main.async { completion(.failure(error)) }
            }
        }
    }

    func cancel() {
        _ = nextOperation()
        publish(status: .cancelled, devices: devices)
    }

    private func nextOperation() -> UInt64 {
        lock.lock()
        operation &+= 1
        let value = operation
        lock.unlock()
        return value
    }

    private func isCurrent(_ value: UInt64) -> Bool {
        lock.lock()
        defer { lock.unlock() }
        return operation == value
    }

    private func publish(status: EchoDeskWiFiSetupStatus, devices: [EchoDeskWiFiDevice]) {
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }
            self.status = status
            self.devices = devices
            self.onChange?()
        }
    }
}

private enum EchoDeskWiFiProtocol {
    static let discoveryPort: UInt16 = 39_320
    static let servicePort: UInt16 = 39_321
    static let protocolVersion = 1
    static let maxWireBytes = 1_024
    static let discoveryTTL: TimeInterval = 5 * 60
    static let expectedModel = "Freenove FNK0114A_2P4"
    static let expectedName = "EchoDesk"
    static let setupAddress = "192.168.4.1"

    static func validDeviceId(_ value: String) -> Bool {
        value.utf8.count == 12 && value.utf8.allSatisfy {
            (48...57).contains($0) || (97...102).contains($0)
        }
    }

    static func randomHex(byteCount: Int) throws -> String {
        var bytes = [UInt8](repeating: 0, count: byteCount)
        guard SecRandomCopyBytes(kSecRandomDefault, bytes.count, &bytes) == errSecSuccess else {
            throw EchoDeskWiFiSetupError.secureStorage
        }
        return bytes.map { String(format: "%02x", $0) }.joined()
    }

    static func discover() throws -> [EchoDeskWiFiDevice] {
        let nonce = try randomHex(byteCount: 12)
        let request: [String: Any] = [
            "type": "echodesk_discover", "version": protocolVersion, "nonce": nonce
        ]
        let payload = try boundedJSON(request)
        let fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)
        guard fd >= 0 else { throw EchoDeskWiFiSetupError.networkUnavailable }
        defer { close(fd) }

        var enabled: Int32 = 1
        guard setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &enabled,
                         socklen_t(MemoryLayout.size(ofValue: enabled))) == 0 else {
            throw EchoDeskWiFiSetupError.networkUnavailable
        }
        var timeout = timeval(tv_sec: 0, tv_usec: 120_000)
        _ = setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                       socklen_t(MemoryLayout.size(ofValue: timeout)))

        var local = sockaddr_in()
        local.sin_len = UInt8(MemoryLayout<sockaddr_in>.size)
        local.sin_family = sa_family_t(AF_INET)
        local.sin_port = 0
        local.sin_addr = in_addr(s_addr: INADDR_ANY)
        let bound = withUnsafePointer(to: &local) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                Darwin.bind(fd, $0, socklen_t(MemoryLayout<sockaddr_in>.size))
            }
        }
        guard bound == 0 else { throw EchoDeskWiFiSetupError.networkUnavailable }

        let sentBroadcast = sendUDP(fd: fd, payload: payload, address: "255.255.255.255")
        let sentSetup = sendUDP(fd: fd, payload: payload, address: setupAddress)
        guard sentBroadcast || sentSetup else { throw EchoDeskWiFiSetupError.networkUnavailable }

        let deadline = Date().addingTimeInterval(1.4)
        var candidates: [EchoDeskWiFiDevice] = []
        var buffer = [UInt8](repeating: 0, count: maxWireBytes + 1)
        while Date() < deadline && candidates.count < 16 {
            var source = sockaddr_in()
            var sourceLength = socklen_t(MemoryLayout<sockaddr_in>.size)
            let count = withUnsafeMutablePointer(to: &source) { sourcePointer in
                sourcePointer.withMemoryRebound(to: sockaddr.self, capacity: 1) { socketPointer in
                    recvfrom(fd, &buffer, buffer.count, 0, socketPointer, &sourceLength)
                }
            }
            guard count > 0, count <= maxWireBytes else { continue }
            let sourceIP = ipString(source.sin_addr)
            let sourcePort = UInt16(bigEndian: source.sin_port)
            if let device = parseDiscovery(
                Data(buffer[0..<count]), sourceIP: sourceIP,
                sourcePort: sourcePort, expectedNonce: nonce
            ), !candidates.contains(where: { $0.deviceId == device.deviceId }) {
                candidates.append(device)
            }
        }

        return candidates.filter { candidate in
            if candidate.setupMode { return true }
            guard candidate.paired,
                  let token = EchoDeskWiFiKeychain.load(deviceId: candidate.deviceId) else { return false }
            return authenticate(target: candidate, token: token)
        }
    }

    static func provision(target: EchoDeskWiFiDevice, setupCode: String, ssid: String,
                          password: String, clientToken: String) throws {
        guard target.address == setupAddress, target.port == servicePort,
              target.setupMode, !target.paired, clientToken.count == 64 else {
            throw EchoDeskWiFiSetupError.deviceNotFound
        }
        let request: [String: Any] = [
            "type": "echodesk_provision", "version": protocolVersion,
            "pairingCode": setupCode, "ssid": ssid, "password": password,
            "clientToken": clientToken
        ]
        let response = try tcpExchange(address: setupAddress, object: request)
        guard response["type"] as? String == "echodesk_provision_result",
              int(response["version"]) == protocolVersion,
              let ok = response["ok"] as? Bool else {
            throw EchoDeskWiFiSetupError.invalidResponse
        }
        if !ok {
            let allowed = Set(["type", "version", "ok", "code", "message"])
            guard Set(response.keys).isSubset(of: allowed) else {
                throw EchoDeskWiFiSetupError.invalidResponse
            }
            throw EchoDeskWiFiSetupError.rejected
        }
        guard Set(response.keys) == Set(["type", "version", "ok", "deviceId", "restarting"]),
              response["deviceId"] as? String == target.deviceId,
              response["restarting"] as? Bool == true else {
            throw EchoDeskWiFiSetupError.invalidResponse
        }
    }

    private static func authenticate(target: EchoDeskWiFiDevice, token: String) -> Bool {
        guard token.count == 64,
              let nonce = try? randomHex(byteCount: 12),
              let response = try? tcpExchange(address: target.address, object: [
                "type": "echodesk_identify", "version": protocolVersion,
                "clientToken": token, "nonce": nonce
              ]) else { return false }
        let required = Set([
            "type", "version", "nonce", "deviceId", "name", "model",
            "firmwareVersion", "setupState", "setupMode", "paired", "ip",
            "port", "authenticated"
        ])
        return Set(response.keys) == required
            && response["type"] as? String == "echodesk_device"
            && int(response["version"]) == protocolVersion
            && response["nonce"] as? String == nonce
            && response["deviceId"] as? String == target.deviceId
            && response["name"] as? String == expectedName
            && response["model"] as? String == expectedModel
            && response["firmwareVersion"] as? String == target.firmwareVersion
            && response["setupState"] as? String == target.setupState
            && response["setupMode"] as? Bool == false
            && response["paired"] as? Bool == true
            && response["authenticated"] as? Bool == true
            && response["ip"] as? String == target.address
            && int(response["port"]) == Int(servicePort)
    }

    private static func parseDiscovery(_ data: Data, sourceIP: String, sourcePort: UInt16,
                                       expectedNonce: String) -> EchoDeskWiFiDevice? {
        guard data.count <= maxWireBytes,
              let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              Set(object.keys) == Set([
                "type", "version", "nonce", "deviceId", "name", "model",
                "firmwareVersion", "setupState", "setupMode", "paired", "ip", "port"
              ]),
              object["type"] as? String == "echodesk_device",
              int(object["version"]) == protocolVersion,
              object["nonce"] as? String == expectedNonce,
              let deviceId = object["deviceId"] as? String, validDeviceId(deviceId),
              object["name"] as? String == expectedName,
              object["model"] as? String == expectedModel,
              let firmwareVersion = safeText(object["firmwareVersion"], max: 32),
              let setupState = safeText(object["setupState"], max: 24),
              let setupMode = object["setupMode"] as? Bool,
              let paired = object["paired"] as? Bool,
              !(setupMode && paired),
              object["ip"] as? String == sourceIP,
              sourcePort == discoveryPort,
              int(object["port"]) == Int(servicePort),
              isLocalIPv4(sourceIP),
              !setupMode || sourceIP == setupAddress else { return nil }
        return EchoDeskWiFiDevice(
            deviceId: deviceId, displayName: expectedName, model: expectedModel,
            setupSSID: setupMode ? "EchoDesk-\(deviceId.suffix(4).uppercased())" : nil,
            address: sourceIP, port: servicePort, setupMode: setupMode, paired: paired,
            firmwareVersion: firmwareVersion, setupState: setupState, discoveredAt: Date()
        )
    }

    private static func tcpExchange(address: String, object: [String: Any]) throws -> [String: Any] {
        let connection = NWConnection(
            host: NWEndpoint.Host(address), port: NWEndpoint.Port(rawValue: servicePort)!,
            using: .tcp
        )
        let queue = DispatchQueue(label: "com.echodesk.wifi-setup.tcp")
        let ready = DispatchSemaphore(value: 0)
        var connectionError: Error?
        connection.stateUpdateHandler = { state in
            switch state {
            case .ready: ready.signal()
            case .failed(let error): connectionError = error; ready.signal()
            default: break
            }
        }
        connection.start(queue: queue)
        defer { connection.cancel() }
        guard ready.wait(timeout: .now() + 3) == .success, connectionError == nil else {
            throw EchoDeskWiFiSetupError.deviceUnreachable
        }

        var payload = try boundedJSON(object)
        payload.append(0x0A)
        let sent = DispatchSemaphore(value: 0)
        connection.send(content: payload, completion: .contentProcessed { error in
            connectionError = error
            sent.signal()
        })
        guard sent.wait(timeout: .now() + 2) == .success, connectionError == nil else {
            throw EchoDeskWiFiSetupError.deviceUnreachable
        }

        let received = DispatchSemaphore(value: 0)
        var responseData = Data()
        func receiveMore() {
            connection.receive(minimumIncompleteLength: 1, maximumLength: maxWireBytes + 1) {
                data, _, isComplete, error in
                if let data { responseData.append(data) }
                if let error { connectionError = error }
                if error != nil || responseData.count > maxWireBytes
                    || responseData.contains(0x0A) || isComplete {
                    received.signal()
                } else {
                    receiveMore()
                }
            }
        }
        receiveMore()
        guard received.wait(timeout: .now() + 10) == .success, connectionError == nil,
              responseData.count <= maxWireBytes,
              let newline = responseData.firstIndex(of: 0x0A) else {
            throw EchoDeskWiFiSetupError.invalidResponse
        }
        var line = Data(responseData.prefix(upTo: newline))
        if line.last == 0x0D { line.removeLast() }
        guard line.count <= maxWireBytes,
              let response = try JSONSerialization.jsonObject(with: line) as? [String: Any] else {
            throw EchoDeskWiFiSetupError.invalidResponse
        }
        return response
    }

    private static func boundedJSON(_ object: [String: Any]) throws -> Data {
        let data = try JSONSerialization.data(withJSONObject: object, options: [.sortedKeys])
        guard data.count <= maxWireBytes else { throw EchoDeskWiFiSetupError.invalidRequest("Request is too large.") }
        return data
    }

    private static func sendUDP(fd: Int32, payload: Data, address: String) -> Bool {
        var destination = sockaddr_in()
        destination.sin_len = UInt8(MemoryLayout<sockaddr_in>.size)
        destination.sin_family = sa_family_t(AF_INET)
        destination.sin_port = discoveryPort.bigEndian
        guard inet_pton(AF_INET, address, &destination.sin_addr) == 1 else { return false }
        return payload.withUnsafeBytes { bytes in
            withUnsafePointer(to: &destination) {
                $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                    sendto(fd, bytes.baseAddress, bytes.count, 0, $0,
                           socklen_t(MemoryLayout<sockaddr_in>.size)) == bytes.count
                }
            }
        }
    }

    private static func ipString(_ address: in_addr) -> String {
        var address = address
        var buffer = [CChar](repeating: 0, count: Int(INET_ADDRSTRLEN))
        guard inet_ntop(AF_INET, &address, &buffer, socklen_t(INET_ADDRSTRLEN)) != nil else { return "" }
        return String(cString: buffer)
    }

    private static func isLocalIPv4(_ value: String) -> Bool {
        let parts = value.split(separator: ".").compactMap { UInt8($0) }
        guard parts.count == 4 else { return false }
        return parts[0] == 10
            || (parts[0] == 172 && (16...31).contains(parts[1]))
            || (parts[0] == 192 && parts[1] == 168)
            || (parts[0] == 169 && parts[1] == 254)
    }

    private static func safeText(_ value: Any?, max: Int) -> String? {
        guard let text = value as? String, !text.isEmpty,
              text.utf8.count <= max,
              !text.unicodeScalars.contains(where: CharacterSet.controlCharacters.contains) else { return nil }
        return text
    }

    private static func int(_ value: Any?) -> Int? {
        guard let number = value as? NSNumber, CFGetTypeID(number) != CFBooleanGetTypeID() else { return nil }
        return number.intValue
    }
}

private enum EchoDeskWiFiKeychain {
    private static let service = "com.echodesk.desktop.wifi-pairing"

    static func save(token: String, deviceId: String) -> Bool {
        guard token.count == 64, let data = token.data(using: .utf8) else { return false }
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: deviceId
        ]
        SecItemDelete(query as CFDictionary)
        var add = query
        add[kSecValueData as String] = data
        add[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
        return SecItemAdd(add as CFDictionary, nil) == errSecSuccess
    }

    static func load(deviceId: String) -> String? {
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: deviceId,
            kSecReturnData as String: true,
            kSecMatchLimit as String: kSecMatchLimitOne
        ]
        var item: CFTypeRef?
        guard SecItemCopyMatching(query as CFDictionary, &item) == errSecSuccess,
              let data = item as? Data,
              let token = String(data: data, encoding: .utf8), token.count == 64,
              token.utf8.allSatisfy({ (48...57).contains($0) || (97...102).contains($0) }) else { return nil }
        return token
    }
}
