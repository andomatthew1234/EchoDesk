import CryptoKit
import Foundation

/// Single source of truth for the EchoDesk Mini firmware shipped with the
/// native macOS app. App releases and firmware releases have independent
/// version numbers, so the EchoDesk build must never infer an OS version from
/// its GitHub app-release tag.
enum EchoDeskFirmwareBundle {
    static let version = "2.31.0"
    static let expectedSize = 1_516_400
    static let expectedSHA256 = "77dd25b6bb32cd059807135a9db9577b7f7d39748f50bc7256c528bd45577175"
    static let resourceSubdirectory = "firmware/echodesk-mini/2.31.0"
    static let resourceName = "echodesk-os"

    enum BundleError: LocalizedError {
        case missing
        case invalidSize(Int)
        case invalidDigest
        case invalidImageLayout

        var errorDescription: String? {
            switch self {
            case .missing:
                return "EchoDesk OS 2.31.0 is missing from this app. Reinstall EchoDesk Desktop."
            case .invalidSize(let size):
                return "The bundled EchoDesk OS has an invalid size (\(size) bytes)."
            case .invalidDigest:
                return "The bundled EchoDesk OS failed its SHA-256 integrity check."
            case .invalidImageLayout:
                return "The bundled firmware is not a merged 4 MB ESP32 image."
            }
        }
    }

    static var release: GitHubRelease {
        GitHubRelease(
            tag_name: "v\(version)",
            name: "EchoDesk OS \(version)",
            html_url: nil,
            body: "Bundled EchoDesk Mini recovery and update image.",
            prerelease: false,
            assets: [
                GitHubAsset(
                    name: kFirmwareAssetName,
                    browser_download_url: bundledURL?.absoluteString ?? "",
                    size: expectedSize
                )
            ]
        )
    }

    static var bundledURL: URL? {
        Bundle.main.url(
            forResource: resourceName,
            withExtension: "bin",
            subdirectory: resourceSubdirectory
        )
    }

    /// Validates the signed app resource before it becomes flashable, then
    /// copies it atomically to the normal firmware cache used by the existing
    /// flasher. An already cached file is never trusted without revalidation.
    static func installValidatedCopy(at destination: String) throws -> String {
        guard let source = bundledURL else { throw BundleError.missing }
        let sourceData = try Data(contentsOf: source, options: [.mappedIfSafe])
        try validate(sourceData)

        let fileManager = FileManager.default
        if fileManager.fileExists(atPath: destination),
           let cached = try? Data(contentsOf: URL(fileURLWithPath: destination), options: [.mappedIfSafe]),
           (try? validate(cached)) != nil {
            return destination
        }

        let destinationURL = URL(fileURLWithPath: destination)
        try fileManager.createDirectory(
            at: destinationURL.deletingLastPathComponent(),
            withIntermediateDirectories: true
        )
        let temporaryURL = destinationURL.deletingLastPathComponent()
            .appendingPathComponent(".\(destinationURL.lastPathComponent).\(UUID().uuidString).tmp")
        defer { try? fileManager.removeItem(at: temporaryURL) }
        try sourceData.write(to: temporaryURL, options: [.atomic])
        if fileManager.fileExists(atPath: destination) {
            try fileManager.removeItem(at: destinationURL)
        }
        try fileManager.moveItem(at: temporaryURL, to: destinationURL)
        return destination
    }

    static func validate(_ data: Data) throws {
        guard data.count == expectedSize else { throw BundleError.invalidSize(data.count) }
        let digest = SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
        guard digest == expectedSHA256 else { throw BundleError.invalidDigest }

        // PlatformIO's merged ESP32 layout: bootloader at 0x1000, partition
        // table at 0x8000, application at 0x10000. The bootloader image header
        // identifies classic ESP32 (chip ID 0) and prevents an S2/S3 image from
        // becoming available to the flasher.
        guard data.count > 0x10000,
              data[0x1000] == 0xE9,
              data[0x1000 + 12] == 0x00,
              data[0x1000 + 13] == 0x00,
              data[0x8000] == 0xAA,
              data[0x8001] == 0x50,
              data[0x10000] == 0xE9 else {
            throw BundleError.invalidImageLayout
        }
    }
}
