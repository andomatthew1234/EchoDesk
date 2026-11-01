import CryptoKit
import Foundation

#if ECHODESK_DESKTOP

/// A fully authenticated remote EchoDesk OS candidate. The UI never receives
/// arbitrary URLs or flash parameters; only this module can construct one.
struct EchoDeskRemoteFirmwareCandidate {
    let tag: String
    let version: String
    let releaseNotes: String
    let imageURL: URL
    let imageAssetName: String
    let size: Int
    let sha256: String
    let minimumDesktopVersion: String

    var displayRelease: GitHubRelease {
        GitHubRelease(
            tag_name: tag,
            name: "EchoDesk OS \(version)",
            html_url: "https://github.com/andomatthew1234/EchoDesk/releases/tag/\(tag)",
            body: releaseNotes,
            prerelease: tag.hasPrefix("fw-beta-v"),
            assets: [
                GitHubAsset(
                    name: kFirmwareAssetName,
                    browser_download_url: imageURL.absoluteString,
                    size: size
                )
            ]
        )
    }
}

enum EchoDeskFirmwareUpdateError: LocalizedError {
    case invalidResponse
    case noCompatibleRelease
    case missingReleaseAssets
    case manifestTooLarge
    case signatureTooLarge
    case invalidSignature
    case invalidManifest
    case untrustedRelease
    case desktopUpdateRequired(String)
    case imageTooLarge
    case imageInvalid

    var errorDescription: String? {
        switch self {
        case .invalidResponse: return "GitHub did not return a valid firmware response."
        case .noCompatibleRelease: return "No compatible EchoDesk OS release is available."
        case .missingReleaseAssets: return "The firmware release is incomplete."
        case .manifestTooLarge: return "The firmware manifest is unexpectedly large."
        case .signatureTooLarge: return "The firmware signature is unexpectedly large."
        case .invalidSignature: return "The firmware release signature could not be verified."
        case .invalidManifest: return "The firmware release manifest is invalid."
        case .untrustedRelease: return "The firmware release does not match this EchoDesk Mini."
        case .desktopUpdateRequired(let version):
            return "EchoDesk Desktop \(version) or later is required for this firmware."
        case .imageTooLarge: return "The firmware image is larger than the EchoDesk Mini flash."
        case .imageInvalid: return "The downloaded firmware failed validation."
        }
    }
}

/// Trusted GitHub catalogue for EchoDesk OS. Detached Ed25519 signatures are
/// checked over the exact manifest bytes before JSON is decoded.
enum EchoDeskFirmwareUpdates {
    static let pinnedPublicKeyBase64 = "mmy3V6WTCrARRl9JmI7m1D5sFUv87CzJp0dCKJgHPOc="
    static let releasesURL = URL(string: "https://api.github.com/repos/andomatthew1234/EchoDesk/releases?per_page=30")!
    static let manifestAssetName = "firmware-update-manifest.json"
    static let signatureAssetName = "firmware-update-manifest.json.sig"
    static let maximumManifestBytes = 256 * 1024
    static let maximumSignatureBytes = 4 * 1024
    static let maximumImageBytes = 4 * 1024 * 1024

    private struct APIRelease: Decodable {
        let tag_name: String
        let body: String?
        let draft: Bool
        let prerelease: Bool
        let assets: [APIAsset]
    }

    private struct APIAsset: Decodable {
        let name: String
        let browser_download_url: String
        let size: Int
    }

    private struct Manifest: Decodable {
        let schemaVersion: Int
        let releases: [ManifestRelease]
    }

    private struct ManifestRelease: Decodable {
        let firmwareVersion: String
        let hardwareModel: String
        let hardwareId: String
        let supportedChip: String
        let imageType: String
        let flashAddress: Int
        let flashMode: String
        let flashFrequency: String
        let flashSize: String
        let imageFile: String
        let downloadUrl: String?
        let sha256: String
        let size: Int
        let releaseNotes: String
        let minimumDesktopVersion: String
    }

    private struct Version: Comparable {
        let major: Int
        let minor: Int
        let patch: Int
        let prerelease: [String]

        init?(_ raw: String) {
            var value = raw.lowercased()
            for prefix in ["fw-beta-v", "fw-v", "v"] where value.hasPrefix(prefix) {
                value.removeFirst(prefix.count)
                break
            }
            value = value.split(separator: "+", maxSplits: 1).first.map(String.init) ?? value
            let pieces = value.split(separator: "-", maxSplits: 1, omittingEmptySubsequences: false)
            let core = pieces[0].split(separator: ".", omittingEmptySubsequences: false)
            guard core.count == 3,
                  core.allSatisfy({ !$0.isEmpty && $0.allSatisfy({ $0.isNumber }) }),
                  core.allSatisfy({ $0 == "0" || !$0.hasPrefix("0") }),
                  let major = Int(core[0]), let minor = Int(core[1]), let patch = Int(core[2]) else { return nil }
            let prerelease: [String]
            if pieces.count == 2 {
                prerelease = pieces[1].split(separator: ".", omittingEmptySubsequences: false).map(String.init)
                guard !prerelease.isEmpty,
                      prerelease.allSatisfy({ !$0.isEmpty && $0.allSatisfy { $0.isLetter || $0.isNumber || $0 == "-" } }),
                      prerelease.allSatisfy({ !$0.allSatisfy({ $0.isNumber }) || $0 == "0" || !$0.hasPrefix("0") }) else { return nil }
            } else {
                prerelease = []
            }
            self.major = major
            self.minor = minor
            self.patch = patch
            self.prerelease = prerelease
        }

        static func < (lhs: Version, rhs: Version) -> Bool {
            let leftCore = [lhs.major, lhs.minor, lhs.patch]
            let rightCore = [rhs.major, rhs.minor, rhs.patch]
            if leftCore != rightCore { return leftCore.lexicographicallyPrecedes(rightCore) }
            if lhs.prerelease.isEmpty { return false }
            if rhs.prerelease.isEmpty { return true }
            for index in 0..<min(lhs.prerelease.count, rhs.prerelease.count) {
                let left = lhs.prerelease[index]
                let right = rhs.prerelease[index]
                if left == right { continue }
                if let leftNumber = Int(left), let rightNumber = Int(right) { return leftNumber < rightNumber }
                if Int(left) != nil { return true }
                if Int(right) != nil { return false }
                return left < right
            }
            return lhs.prerelease.count < rhs.prerelease.count
        }
    }

    static func discover(channel: UpdateChannel, completion: @escaping (Result<EchoDeskRemoteFirmwareCandidate, Error>) -> Void) {
        request(releasesURL, maximumBytes: maximumManifestBytes * 2) { result in
            switch result {
            case .failure(let error): completion(.failure(error))
            case .success(let data):
                do {
                    let releases = try JSONDecoder().decode([APIRelease].self, from: data)
                    let eligible = releases.filter { release in
                        guard !release.draft else { return false }
                        if release.tag_name.hasPrefix("fw-v") {
                            return !release.prerelease
                        }
                        if release.tag_name.hasPrefix("fw-beta-v") {
                            return channel == .beta && release.prerelease
                        }
                        return false
                    }.compactMap { release -> (APIRelease, Version)? in
                        guard let version = Version(release.tag_name) else { return nil }
                        return (release, version)
                    }.sorted { $0.1 > $1.1 }
                    guard let release = eligible.first?.0 else {
                        throw EchoDeskFirmwareUpdateError.noCompatibleRelease
                    }
                    fetchCandidate(from: release, completion: completion)
                } catch {
                    completion(.failure(error))
                }
            }
        }
    }

    private static func fetchCandidate(from release: APIRelease, completion: @escaping (Result<EchoDeskRemoteFirmwareCandidate, Error>) -> Void) {
        guard let manifestAsset = release.assets.first(where: { $0.name == manifestAssetName }),
              let signatureAsset = release.assets.first(where: { $0.name == signatureAssetName }),
              let manifestURL = trustedAssetURL(manifestAsset.browser_download_url, tag: release.tag_name, asset: manifestAssetName),
              let signatureURL = trustedAssetURL(signatureAsset.browser_download_url, tag: release.tag_name, asset: signatureAssetName) else {
            completion(.failure(EchoDeskFirmwareUpdateError.missingReleaseAssets))
            return
        }
        request(manifestURL, maximumBytes: maximumManifestBytes) { manifestResult in
            do {
                let manifestData = try manifestResult.get()
                request(signatureURL, maximumBytes: maximumSignatureBytes) { signatureResult in
                    do {
                        let signatureData = try signatureResult.get()
                        try verify(manifest: manifestData, signature: signatureData)
                        let manifest = try JSONDecoder().decode(Manifest.self, from: manifestData)
                        guard manifest.schemaVersion == 1, manifest.releases.count <= 32 else {
                            throw EchoDeskFirmwareUpdateError.invalidManifest
                        }
                        let expectedVersion = versionForTag(release.tag_name)
                        let candidates = manifest.releases.filter { $0.firmwareVersion == expectedVersion }
                        guard candidates.count == 1, let entry = candidates.first else {
                            throw EchoDeskFirmwareUpdateError.untrustedRelease
                        }
                        let candidate = try validate(entry: entry, release: release)
                        try enforceRollbackProtection(candidate, prerelease: release.prerelease)
                        completion(.success(candidate))
                    } catch {
                        completion(.failure(error))
                    }
                }
            } catch {
                completion(.failure(error))
            }
        }
    }

    private static func verify(manifest: Data, signature rawSignature: Data) throws {
        guard let keyData = Data(base64Encoded: pinnedPublicKeyBase64), keyData.count == 32,
              let key = try? Curve25519.Signing.PublicKey(rawRepresentation: keyData) else {
            throw EchoDeskFirmwareUpdateError.invalidSignature
        }
        let signature: Data
        if rawSignature.count == 64 {
            signature = rawSignature
        } else if let text = String(data: rawSignature, encoding: .utf8),
                  let decoded = Data(base64Encoded: text.trimmingCharacters(in: .whitespacesAndNewlines)),
                  decoded.count == 64 {
            signature = decoded
        } else {
            throw EchoDeskFirmwareUpdateError.invalidSignature
        }
        guard key.isValidSignature(signature, for: manifest) else {
            throw EchoDeskFirmwareUpdateError.invalidSignature
        }
    }

    private static func validate(entry: ManifestRelease, release: APIRelease) throws -> EchoDeskRemoteFirmwareCandidate {
        guard entry.hardwareModel == "EchoDesk Mini",
              entry.hardwareId == "freenove-fnk0114a-2p4",
              entry.supportedChip == "esp32",
              entry.imageType == "merged",
              entry.flashAddress == 0,
              entry.flashMode.lowercased() == "dio",
              entry.flashFrequency.lowercased() == "40m",
              entry.flashSize.lowercased() == "4mb",
              entry.size > 0, entry.size <= maximumImageBytes,
              entry.sha256.count == 64, entry.sha256.allSatisfy({ $0.isHexDigit }),
              Version(entry.firmwareVersion) != nil,
              let minimum = Version(entry.minimumDesktopVersion),
              let desktop = Version(kAppVersion), desktop >= minimum,
              !entry.imageFile.hasPrefix("/"), !entry.imageFile.contains(".."),
              let urlString = entry.downloadUrl,
              let assetName = URL(string: urlString)?.lastPathComponent,
              let imageURL = trustedAssetURL(urlString, tag: release.tag_name, asset: assetName),
              let releaseAsset = release.assets.first(where: { $0.name == assetName }),
              releaseAsset.size == entry.size,
              releaseAsset.browser_download_url == urlString else {
            if let minimum = Version(entry.minimumDesktopVersion), let desktop = Version(kAppVersion), desktop < minimum {
                throw EchoDeskFirmwareUpdateError.desktopUpdateRequired(entry.minimumDesktopVersion)
            }
            throw EchoDeskFirmwareUpdateError.untrustedRelease
        }
        return EchoDeskRemoteFirmwareCandidate(
            tag: release.tag_name,
            version: entry.firmwareVersion,
            releaseNotes: entry.releaseNotes,
            imageURL: imageURL,
            imageAssetName: assetName,
            size: entry.size,
            sha256: entry.sha256.lowercased(),
            minimumDesktopVersion: entry.minimumDesktopVersion
        )
    }

    private static func enforceRollbackProtection(_ candidate: EchoDeskRemoteFirmwareCandidate, prerelease: Bool) throws {
        guard let candidateVersion = Version(candidate.version),
              let bundledVersion = Version(EchoDeskFirmwareBundle.version),
              candidateVersion >= bundledVersion else {
            throw EchoDeskFirmwareUpdateError.untrustedRelease
        }
        let key = prerelease ? "EchoDeskTrustedBetaFirmwareHighestVersion" : "EchoDeskTrustedStableFirmwareHighestVersion"
        let defaults = UserDefaults.standard
        if let previous = defaults.string(forKey: key),
           let previousVersion = Version(previous),
           candidateVersion < previousVersion {
            throw EchoDeskFirmwareUpdateError.untrustedRelease
        }
        defaults.set(candidate.version, forKey: key)
    }

    static func download(_ candidate: EchoDeskRemoteFirmwareCandidate, to destination: URL, completion: @escaping (Result<URL, Error>) -> Void) {
        request(candidate.imageURL, maximumBytes: maximumImageBytes) { result in
            do {
                let data = try result.get()
                guard data.count == candidate.size else { throw EchoDeskFirmwareUpdateError.imageInvalid }
                let digest = SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
                guard digest == candidate.sha256 else { throw EchoDeskFirmwareUpdateError.imageInvalid }
                try EchoDeskFirmwareBundle.validateLayout(data)
                let manager = FileManager.default
                try manager.createDirectory(at: destination.deletingLastPathComponent(), withIntermediateDirectories: true)
                let temporary = destination.deletingLastPathComponent().appendingPathComponent(".\(destination.lastPathComponent).\(UUID().uuidString).part")
                defer { try? manager.removeItem(at: temporary) }
                try data.write(to: temporary, options: [.atomic])
                if manager.fileExists(atPath: destination.path) { try manager.removeItem(at: destination) }
                try manager.moveItem(at: temporary, to: destination)
                completion(.success(destination))
            } catch {
                completion(.failure(error))
            }
        }
    }

    static func validateCachedImage(at url: URL, candidate: EchoDeskRemoteFirmwareCandidate) -> Bool {
        guard let data = try? Data(contentsOf: url, options: [.mappedIfSafe]), data.count == candidate.size else { return false }
        let digest = SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
        guard digest == candidate.sha256 else { return false }
        return (try? EchoDeskFirmwareBundle.validateLayout(data)) != nil
    }

    private static func request(_ url: URL, maximumBytes: Int, completion: @escaping (Result<Data, Error>) -> Void) {
        var request = URLRequest(url: url, cachePolicy: .reloadRevalidatingCacheData, timeoutInterval: 20)
        request.setValue("EchoDesk-Desktop/\(kAppVersion)", forHTTPHeaderField: "User-Agent")
        request.setValue("application/vnd.github+json", forHTTPHeaderField: "Accept")
        URLSession.shared.dataTask(with: request) { data, response, error in
            if let error = error { completion(.failure(error)); return }
            guard let response = response as? HTTPURLResponse,
                  (200...299).contains(response.statusCode),
                  response.url?.scheme == "https",
                  let data = data, data.count <= maximumBytes else {
                completion(.failure(EchoDeskFirmwareUpdateError.invalidResponse))
                return
            }
            completion(.success(data))
        }.resume()
    }

    private static func trustedAssetURL(_ raw: String, tag: String, asset: String) -> URL? {
        guard !tag.contains("/"), !asset.contains("/"),
              let url = URL(string: raw), url.scheme == "https", url.host?.lowercased() == "github.com",
              url.path == "/andomatthew1234/EchoDesk/releases/download/\(tag)/\(asset)",
              url.query == nil, url.fragment == nil else { return nil }
        return url
    }

    private static func versionForTag(_ tag: String) -> String {
        if tag.hasPrefix("fw-beta-v") { return String(tag.dropFirst("fw-beta-v".count)) }
        if tag.hasPrefix("fw-v") { return String(tag.dropFirst("fw-v".count)) }
        return ""
    }
}

#endif
