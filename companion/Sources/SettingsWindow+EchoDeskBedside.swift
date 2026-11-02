#if ECHODESK_DESKTOP
import Cocoa

struct EchoDeskBedsideAlarm {
    var id: Int
    var enabled: Bool
    var minuteOfDay: Int
    var weekdays: Int
    var label: String

    var json: [String: Any] {
        ["id": id, "enabled": enabled, "minuteOfDay": minuteOfDay,
         "weekdays": weekdays, "label": label]
    }
}

struct EchoDeskBedsideScheduleSegment {
    var startMinute: Int
    var brightness: Int
    var json: [String: Any] { ["startMinute": startMinute, "brightness": brightness] }
}

struct EchoDeskBedsideConfig {
    var clockFormat: String
    var clockShowSeconds: Bool
    var clockStyle: String
    var nextAlarmWidget: Bool
    var weatherWidget: Bool
    var alarms: [EchoDeskBedsideAlarm]
    var scheduleEnabled: Bool
    var manualBrightness: Int
    var segments: [EchoDeskBedsideScheduleSegment]
    var nightScheduleEnabled: Bool
    var nightManualEnabled: Bool
    var nightStartMinute: Int
    var nightEndMinute: Int
    var themeScheduleEnabled: Bool
    var themeManualDark: Bool
    var themeStartMinute: Int
    var themeEndMinute: Int

    private init(
        clockFormat: String,
        clockShowSeconds: Bool,
        clockStyle: String,
        nextAlarmWidget: Bool,
        weatherWidget: Bool,
        alarms: [EchoDeskBedsideAlarm],
        scheduleEnabled: Bool,
        manualBrightness: Int,
        segments: [EchoDeskBedsideScheduleSegment],
        nightScheduleEnabled: Bool,
        nightManualEnabled: Bool,
        nightStartMinute: Int,
        nightEndMinute: Int,
        themeScheduleEnabled: Bool,
        themeManualDark: Bool,
        themeStartMinute: Int,
        themeEndMinute: Int
    ) {
        self.clockFormat = clockFormat
        self.clockShowSeconds = clockShowSeconds
        self.clockStyle = clockStyle
        self.nextAlarmWidget = nextAlarmWidget
        self.weatherWidget = weatherWidget
        self.alarms = alarms
        self.scheduleEnabled = scheduleEnabled
        self.manualBrightness = manualBrightness
        self.segments = segments
        self.nightScheduleEnabled = nightScheduleEnabled
        self.nightManualEnabled = nightManualEnabled
        self.nightStartMinute = nightStartMinute
        self.nightEndMinute = nightEndMinute
        self.themeScheduleEnabled = themeScheduleEnabled
        self.themeManualDark = themeManualDark
        self.themeStartMinute = themeStartMinute
        self.themeEndMinute = themeEndMinute
    }

    private static let defaultSegments: [EchoDeskBedsideScheduleSegment] = [
        .init(startMinute: 0, brightness: 1),
        .init(startMinute: 300, brightness: 40),
        .init(startMinute: 420, brightness: 70),
        .init(startMinute: 480, brightness: 100),
        .init(startMinute: 1080, brightness: 60),
        .init(startMinute: 1140, brightness: 40),
        .init(startMinute: 1230, brightness: 30),
        .init(startMinute: 1290, brightness: 1),
    ]

    static let defaults: EchoDeskBedsideConfig = EchoDeskBedsideConfig(
        clockFormat: "12h", clockShowSeconds: false, clockStyle: "large",
        nextAlarmWidget: true, weatherWidget: false,
        alarms: [], scheduleEnabled: true, manualBrightness: 40,
        segments: defaultSegments,
        nightScheduleEnabled: true, nightManualEnabled: false,
        nightStartMinute: 1140, nightEndMinute: 420,
        themeScheduleEnabled: true, themeManualDark: false,
        themeStartMinute: 1140, themeEndMinute: 420)

    init?(json: [String: Any]) {
        guard let clock = json["clock"] as? [String: Any],
              let widgets = json["widgets"] as? [String: Any],
              let display = json["display"] as? [String: Any],
              let format = clock["format"] as? String,
              ["12h", "24h"].contains(format),
              let style = clock["style"] as? String,
              ["large", "minimal"].contains(style),
              let showSeconds = clock["showSeconds"] as? Bool,
              let nextAlarm = widgets["nextAlarm"] as? Bool,
              let weather = widgets["weather"] as? Bool,
              let scheduleEnabled = display["scheduleEnabled"] as? Bool,
              let manualBrightness = (display["manualBrightness"] as? NSNumber)?.intValue,
              (1...100).contains(manualBrightness),
              let night = display["nightShift"] as? [String: Any],
              let theme = display["theme"] as? [String: Any] else { return nil }

        let alarmValues = json["alarms"] as? [[String: Any]] ?? []
        let parsedAlarms = alarmValues.compactMap { value -> EchoDeskBedsideAlarm? in
            guard let id = (value["id"] as? NSNumber)?.intValue,
                  let enabled = value["enabled"] as? Bool,
                  let minute = (value["minuteOfDay"] as? NSNumber)?.intValue,
                  let weekdays = (value["weekdays"] as? NSNumber)?.intValue,
                  let label = value["label"] as? String,
                  (1...8).contains(id), (0...1439).contains(minute), (1...127).contains(weekdays),
                  label.utf8.count <= 24
            else { return nil }
            return .init(id: id, enabled: enabled, minuteOfDay: minute,
                         weekdays: weekdays, label: label)
        }
        guard parsedAlarms.count == alarmValues.count, parsedAlarms.count <= 8,
              Set(parsedAlarms.map(\.id)).count == parsedAlarms.count else { return nil }

        let segmentValues = display["segments"] as? [[String: Any]] ?? []
        let parsedSegments = segmentValues.compactMap { value -> EchoDeskBedsideScheduleSegment? in
            guard let minute = (value["startMinute"] as? NSNumber)?.intValue,
                  let brightness = (value["brightness"] as? NSNumber)?.intValue,
                  (0...1439).contains(minute), (1...100).contains(brightness) else { return nil }
            return .init(startMinute: minute, brightness: brightness)
        }
        guard parsedSegments.count == segmentValues.count, (1...8).contains(parsedSegments.count),
              Set(parsedSegments.map(\.startMinute)).count == parsedSegments.count else { return nil }

        func minute(_ object: [String: Any], _ key: String) -> Int? {
            guard let value = (object[key] as? NSNumber)?.intValue,
                  (0...1439).contains(value) else { return nil }
            return value
        }
        guard let nightSchedule = night["scheduleEnabled"] as? Bool,
              let nightManual = night["manualEnabled"] as? Bool,
              let nightStart = minute(night, "startMinute"), let nightEnd = minute(night, "endMinute"),
              let themeSchedule = theme["scheduleEnabled"] as? Bool,
              let themeManual = theme["manualDark"] as? Bool,
              let themeStart = minute(theme, "startMinute"), let themeEnd = minute(theme, "endMinute")
        else { return nil }

        self.init(clockFormat: format, clockShowSeconds: showSeconds, clockStyle: style,
                  nextAlarmWidget: nextAlarm, weatherWidget: weather,
                  alarms: parsedAlarms, scheduleEnabled: scheduleEnabled,
                  manualBrightness: manualBrightness, segments: parsedSegments.sorted { $0.startMinute < $1.startMinute },
                  nightScheduleEnabled: nightSchedule, nightManualEnabled: nightManual,
                  nightStartMinute: nightStart, nightEndMinute: nightEnd,
                  themeScheduleEnabled: themeSchedule, themeManualDark: themeManual,
                  themeStartMinute: themeStart, themeEndMinute: themeEnd)
    }

    var json: [String: Any] {
        [
            "clock": ["format": clockFormat, "showSeconds": clockShowSeconds, "style": clockStyle],
            "widgets": ["nextAlarm": nextAlarmWidget, "weather": weatherWidget],
            "alarms": alarms.map(\.json),
            "display": [
                "scheduleEnabled": scheduleEnabled, "manualBrightness": manualBrightness,
                "segments": segments.map(\.json),
                "nightShift": ["scheduleEnabled": nightScheduleEnabled,
                               "manualEnabled": nightManualEnabled,
                               "startMinute": nightStartMinute, "endMinute": nightEndMinute],
                "theme": ["scheduleEnabled": themeScheduleEnabled,
                          "manualDark": themeManualDark,
                          "startMinute": themeStartMinute, "endMinute": themeEndMinute],
            ],
        ]
    }
}

final class EchoDeskBedsideAlarmRow {
    let id: Int
    let enabled: NSButton
    let time: NSDatePicker
    let days: NSPopUpButton
    let label: NSTextField
    let remove: NSButton

    init(id: Int, enabled: NSButton, time: NSDatePicker, days: NSPopUpButton,
         label: NSTextField, remove: NSButton) {
        self.id = id; self.enabled = enabled; self.time = time; self.days = days
        self.label = label; self.remove = remove
    }
}

final class EchoDeskBedsideScheduleRow {
    let index: Int
    let time: NSDatePicker
    let brightness: NSTextField
    init(index: Int, time: NSDatePicker, brightness: NSTextField) {
        self.index = index; self.time = time; self.brightness = brightness
    }
}

final class EchoDeskCommitTextField: NSTextField {
    override func textDidEndEditing(_ notification: Notification) {
        super.textDidEndEditing(notification)
        if let action { NSApp.sendAction(action, to: target, from: self) }
    }
}

/// A discrete representation of the schedule evaluated by EchoDesk OS. The
/// firmware keeps the last point at or before the current minute (wrapping to
/// the final point overnight), so the path intentionally contains hard steps.
final class EchoDeskBrightnessTimelineView: NSView {
    var isEnabled = true { didSet { alphaValue = isEnabled ? 1 : 0.55 } }
    var segments: [EchoDeskBedsideScheduleSegment] = [] {
        didSet {
            if let selectedIndex, !segments.indices.contains(selectedIndex) {
                self.selectedIndex = segments.isEmpty ? nil : segments.count - 1
            }
            updateAccessibilitySummary()
            needsDisplay = true
        }
    }
    private(set) var selectedIndex: Int?
    var onSelectionChanged: ((Int?) -> Void)?
    var onSegmentsPreview: (([EchoDeskBedsideScheduleSegment]) -> Void)?
    var onSegmentsCommitted: (([EchoDeskBedsideScheduleSegment]) -> Void)?
    private var isDraggingPoint = false

    override var acceptsFirstResponder: Bool { true }
    override var intrinsicContentSize: NSSize { NSSize(width: NSView.noIntrinsicMetric, height: 216) }

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        wantsLayer = true
        layer?.cornerRadius = 10
        layer?.cornerCurve = .continuous
        setAccessibilityElement(true)
        setAccessibilityRole(.group)
        setAccessibilityLabel("Brightness schedule")
        setAccessibilityHelp("Select a point, then use the arrow keys or the exact time and brightness controls below the graph.")
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) is not supported") }

    private var plotRect: NSRect {
        NSRect(x: 48, y: 30, width: max(1, bounds.width - 66), height: max(1, bounds.height - 48))
    }

    private func point(for segment: EchoDeskBedsideScheduleSegment) -> NSPoint {
        let plot = plotRect
        return NSPoint(x: plot.minX + CGFloat(segment.startMinute) / 1439 * plot.width,
                       y: plot.minY + CGFloat(segment.brightness) / 100 * plot.height)
    }

    func select(index: Int?) {
        selectedIndex = index.flatMap { segments.indices.contains($0) ? $0 : nil }
        updateAccessibilitySummary()
        needsDisplay = true
        onSelectionChanged?(selectedIndex)
    }

    func updateSelected(minute: Int? = nil, brightness: Int? = nil, commit: Bool) {
        guard let index = selectedIndex, segments.indices.contains(index) else { return }
        let lower = index == 0 ? 0 : segments[index - 1].startMinute + 1
        let upper = index == segments.count - 1 ? 1439 : segments[index + 1].startMinute - 1
        if let minute { segments[index].startMinute = min(upper, max(lower, minute)) }
        if let brightness { segments[index].brightness = min(100, max(1, brightness)) }
        updateAccessibilitySummary()
        needsDisplay = true
        onSegmentsPreview?(segments)
        if commit { onSegmentsCommitted?(segments) }
    }

    override func mouseDown(with event: NSEvent) {
        guard isEnabled else { NSSound.beep(); return }
        window?.makeFirstResponder(self)
        let location = convert(event.locationInWindow, from: nil)
        let nearest = segments.indices.min { lhs, rhs in
            hypot(point(for: segments[lhs]).x - location.x, point(for: segments[lhs]).y - location.y)
                < hypot(point(for: segments[rhs]).x - location.x, point(for: segments[rhs]).y - location.y)
        }
        guard let nearest,
              hypot(point(for: segments[nearest]).x - location.x,
                    point(for: segments[nearest]).y - location.y) <= 14 else {
            select(index: nil)
            return
        }
        select(index: nearest)
        isDraggingPoint = true
    }

    override func mouseDragged(with event: NSEvent) {
        guard isDraggingPoint, selectedIndex != nil else { return }
        let location = convert(event.locationInWindow, from: nil)
        let plot = plotRect
        let minute = Int((((location.x - plot.minX) / plot.width) * 1439).rounded())
        let brightness = Int((((location.y - plot.minY) / plot.height) * 100).rounded())
        updateSelected(minute: minute, brightness: brightness, commit: false)
    }

    override func mouseUp(with event: NSEvent) {
        guard isDraggingPoint else { return }
        isDraggingPoint = false
        onSegmentsCommitted?(segments)
    }

    override func keyDown(with event: NSEvent) {
        guard isEnabled else { NSSound.beep(); return }
        guard selectedIndex != nil else { super.keyDown(with: event); return }
        switch event.keyCode {
        case 123: updateSelected(minute: (selectedSegment?.startMinute ?? 0) - 5, commit: true)
        case 124: updateSelected(minute: (selectedSegment?.startMinute ?? 0) + 5, commit: true)
        case 125: updateSelected(brightness: (selectedSegment?.brightness ?? 1) - 1, commit: true)
        case 126: updateSelected(brightness: (selectedSegment?.brightness ?? 1) + 1, commit: true)
        default: super.keyDown(with: event)
        }
    }

    private var selectedSegment: EchoDeskBedsideScheduleSegment? {
        guard let selectedIndex, segments.indices.contains(selectedIndex) else { return nil }
        return segments[selectedIndex]
    }

    private func updateAccessibilitySummary() {
        guard let selectedSegment else {
            setAccessibilityValue("No schedule point selected")
            return
        }
        let hour = selectedSegment.startMinute / 60
        let minute = selectedSegment.startMinute % 60
        setAccessibilityValue(String(format: "Selected point %02d:%02d, %d percent brightness",
                                     hour, minute, selectedSegment.brightness))
    }

    override func draw(_ dirtyRect: NSRect) {
        super.draw(dirtyRect)
        let plot = plotRect
        NSColor.controlBackgroundColor.setFill()
        NSBezierPath(roundedRect: bounds, xRadius: 10, yRadius: 10).fill()

        let grid = NSColor.separatorColor.withAlphaComponent(0.55)
        let axisAttributes: [NSAttributedString.Key: Any] = [
            .font: NSFont.monospacedDigitSystemFont(ofSize: 10, weight: .regular),
            .foregroundColor: NSColor.secondaryLabelColor,
        ]
        for hour in stride(from: 0, through: 24, by: 6) {
            let x = plot.minX + CGFloat(hour) / 24 * plot.width
            let line = NSBezierPath(); line.move(to: NSPoint(x: x, y: plot.minY)); line.line(to: NSPoint(x: x, y: plot.maxY))
            grid.setStroke(); line.lineWidth = 0.5; line.stroke()
            let label = String(format: "%02d", hour)
            label.draw(at: NSPoint(x: x - 7, y: 9), withAttributes: axisAttributes)
        }
        for value in stride(from: 0, through: 100, by: 25) {
            let y = plot.minY + CGFloat(value) / 100 * plot.height
            let line = NSBezierPath(); line.move(to: NSPoint(x: plot.minX, y: y)); line.line(to: NSPoint(x: plot.maxX, y: y))
            grid.setStroke(); line.lineWidth = 0.5; line.stroke()
            String(value).draw(at: NSPoint(x: 12, y: y - 6), withAttributes: axisAttributes)
        }

        let ordered = segments.sorted { $0.startMinute < $1.startMinute }
        if let last = ordered.last {
            let path = NSBezierPath()
            var y = point(for: last).y
            path.move(to: NSPoint(x: plot.minX, y: y))
            for segment in ordered {
                let next = point(for: segment)
                path.line(to: NSPoint(x: next.x, y: y))
                path.line(to: next)
                y = next.y
            }
            path.line(to: NSPoint(x: plot.maxX, y: y))
            NSColor.controlAccentColor.setStroke()
            path.lineWidth = 2.5
            path.lineJoinStyle = .round
            path.stroke()
        }

        for (index, segment) in segments.enumerated() {
            let center = point(for: segment)
            let selected = index == selectedIndex
            let handle = NSBezierPath(ovalIn: NSRect(x: center.x - (selected ? 7 : 5),
                                                     y: center.y - (selected ? 7 : 5),
                                                     width: selected ? 14 : 10, height: selected ? 14 : 10))
            (selected ? NSColor.controlAccentColor : NSColor.windowBackgroundColor).setFill()
            handle.fill()
            NSColor.controlAccentColor.setStroke(); handle.lineWidth = selected ? 3 : 2; handle.stroke()
        }

        if window?.firstResponder === self {
            NSColor.keyboardFocusIndicatorColor.setStroke()
            let focus = NSBezierPath(roundedRect: bounds.insetBy(dx: 2, dy: 2), xRadius: 9, yRadius: 9)
            focus.lineWidth = 3; focus.stroke()
        }
    }
}

final class EchoDeskBedsideTemperatureStrip: NSView {
    override var intrinsicContentSize: NSSize { NSSize(width: 180, height: 16) }
    override func draw(_ dirtyRect: NSRect) {
        super.draw(dirtyRect)
        let gradient = NSGradient(starting: NSColor(calibratedRed: 1, green: 0.43, blue: 0.12, alpha: 1),
                                  ending: NSColor(calibratedRed: 1, green: 0.88, blue: 0.62, alpha: 1))
        let path = NSBezierPath(roundedRect: bounds, xRadius: 8, yRadius: 8)
        gradient?.draw(in: path, angle: 0)
    }
}

final class EchoDeskBedsideAppearancePreview: NSView {
    let dark: Bool
    init(dark: Bool) { self.dark = dark; super.init(frame: .zero); setAccessibilityHidden(true) }
    required init?(coder: NSCoder) { fatalError("init(coder:) is not supported") }
    override var intrinsicContentSize: NSSize { NSSize(width: 116, height: 58) }
    override func draw(_ dirtyRect: NSRect) {
        let background = dark ? NSColor(calibratedWhite: 0.08, alpha: 1) : NSColor(calibratedWhite: 0.96, alpha: 1)
        let foreground = dark ? NSColor.white : NSColor.black
        background.setFill(); NSBezierPath(roundedRect: bounds, xRadius: 8, yRadius: 8).fill()
        foreground.withAlphaComponent(0.25).setFill()
        NSBezierPath(roundedRect: NSRect(x: 10, y: 12, width: bounds.width - 20, height: 8), xRadius: 4, yRadius: 4).fill()
        NSBezierPath(roundedRect: NSRect(x: 10, y: 28, width: bounds.width * 0.55, height: 16), xRadius: 5, yRadius: 5).fill()
        NSColor.separatorColor.setStroke(); NSBezierPath(roundedRect: bounds.insetBy(dx: 0.5, dy: 0.5), xRadius: 8, yRadius: 8).stroke()
    }
}

final class EchoDeskBedsidePreviewView: NSView {
    var config = EchoDeskBedsideConfig.defaults { didSet { needsDisplay = true } }

    override var intrinsicContentSize: NSSize { NSSize(width: 320, height: 240) }
    override var wantsUpdateLayer: Bool { false }

    override func draw(_ dirtyRect: NSRect) {
        super.draw(dirtyRect)
        let scale = max(0.01, min(bounds.width / 320, bounds.height / 240))
        let currentMinute = Calendar.current.component(.hour, from: Date()) * 60
            + Calendar.current.component(.minute, from: Date())
        let dark = config.themeScheduleEnabled
            ? isMinute(currentMinute, inside: config.themeStartMinute, and: config.themeEndMinute)
            : config.themeManualDark
        let background = dark ? NSColor(calibratedWhite: 0.035, alpha: 1) : NSColor(calibratedWhite: 0.96, alpha: 1)
        let foreground = dark ? NSColor.white : NSColor.black
        let path = NSBezierPath(roundedRect: bounds.insetBy(dx: 2, dy: 2),
                                xRadius: 20 * scale, yRadius: 20 * scale)
        background.setFill(); path.fill()
        NSColor.separatorColor.setStroke(); path.lineWidth = 1; path.stroke()
        let nightShift = config.nightScheduleEnabled
            ? isMinute(currentMinute, inside: config.nightStartMinute, and: config.nightEndMinute)
            : config.nightManualEnabled
        if nightShift {
            NSColor(calibratedRed: 1, green: 0.38, blue: 0.08, alpha: 0.10).setFill()
            path.fill()
        }

        let formatter = DateFormatter()
        formatter.dateFormat = config.clockFormat == "24h"
            ? (config.clockShowSeconds ? "HH:mm:ss" : "HH:mm")
            : (config.clockShowSeconds ? "h:mm:ss" : "h:mm")
        let clock = formatter.string(from: Date())
        let clockFont = NSFont.monospacedDigitSystemFont(ofSize: (config.clockStyle == "large" ? 56 : 42) * scale,
                                                         weight: config.clockStyle == "large" ? .light : .medium)
        let clockAttributes: [NSAttributedString.Key: Any] = [.font: clockFont, .foregroundColor: foreground]
        let clockSize = clock.size(withAttributes: clockAttributes)
        clock.draw(at: NSPoint(x: (bounds.width - clockSize.width) / 2,
                               y: bounds.height * 0.58), withAttributes: clockAttributes)

        let widgetY: CGFloat = 24 * scale
        var widgetX: CGFloat = 22 * scale
        let available = bounds.width - 44 * scale
        let half = (available - 8 * scale) / 2
        if config.nextAlarmWidget {
            drawWidget(title: "NEXT ALARM", value: nextAlarmText(), x: widgetX, y: widgetY,
                       width: config.weatherWidget ? half : available,
                       foreground: foreground, scale: scale)
            widgetX += half + 8 * scale
        }
        if config.weatherWidget {
            drawWidget(title: "WEATHER", value: "Coming soon", x: widgetX, y: widgetY,
                       width: config.nextAlarmWidget ? half : available,
                       foreground: foreground, scale: scale)
        }
        if !config.nextAlarmWidget && !config.weatherWidget {
            let text = "No widgets selected"
            let attrs: [NSAttributedString.Key: Any] = [.font: NSFont.systemFont(ofSize: 13 * scale),
                                                        .foregroundColor: foreground.withAlphaComponent(0.55)]
            let size = text.size(withAttributes: attrs)
            text.draw(at: NSPoint(x: (bounds.width - size.width) / 2, y: 54 * scale), withAttributes: attrs)
        }
    }

    private func drawWidget(title: String, value: String, x: CGFloat, y: CGFloat,
                            width: CGFloat, foreground: NSColor, scale: CGFloat) {
        let rect = NSRect(x: x, y: y, width: width, height: 62 * scale)
        foreground.withAlphaComponent(0.08).setFill()
        NSBezierPath(roundedRect: rect, xRadius: 12 * scale, yRadius: 12 * scale).fill()
        title.draw(at: NSPoint(x: x + 12 * scale, y: y + 38 * scale), withAttributes: [
            .font: NSFont.systemFont(ofSize: 9 * scale, weight: .semibold),
            .foregroundColor: foreground.withAlphaComponent(0.55),
        ])
        value.draw(at: NSPoint(x: x + 12 * scale, y: y + 13 * scale), withAttributes: [
            .font: NSFont.systemFont(ofSize: 16 * scale, weight: .medium), .foregroundColor: foreground,
        ])
    }

    private func nextAlarmText() -> String {
        guard let alarm = config.alarms.filter(\.enabled).sorted(by: { $0.minuteOfDay < $1.minuteOfDay }).first
        else { return "No alarm" }
        let hour = alarm.minuteOfDay / 60, minute = alarm.minuteOfDay % 60
        if config.clockFormat == "24h" { return String(format: "%02d:%02d", hour, minute) }
        let suffix = hour >= 12 ? "PM" : "AM"
        let displayHour = hour % 12 == 0 ? 12 : hour % 12
        return String(format: "%d:%02d %@", displayHour, minute, suffix)
    }

    private func isMinute(_ value: Int, inside start: Int, and end: Int) -> Bool {
        start <= end ? (value >= start && value < end) : (value >= start || value < end)
    }
}

extension SettingsWindowController {
    // MARK: - Bedside Mode page

    func buildEchoDeskBedsidePage() -> NSView {
        let document = bedsideStack([])
        echoDeskBedsideDocument = document
        echoDeskBedsidePreviewTimer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in
            self?.echoDeskBedsidePreview?.needsDisplay = true
        }
        renderEchoDeskBedsideSection("root")
        return document
    }

    @objc func echoDeskOpenBedsideCustomisation() {
        if echoDeskBedsideSection != "root" { renderEchoDeskBedsideSection("root") }
        selectEchoDeskPage(.bedside)
    }

    @objc private func echoDeskBedsideBackToCustomisation() {
        selectEchoDeskPage(.customisation)
    }

    @objc private func echoDeskBedsideShowRoot() { renderEchoDeskBedsideSection("root") }
    @objc private func echoDeskBedsideShowAlarms() {
        echoDeskBedsideAlarmDrafts = echoDeskBedsideConfig.alarms
        renderEchoDeskBedsideSection("alarms")
    }
    @objc private func echoDeskBedsideShowDisplay() {
        echoDeskBedsideScheduleDrafts = echoDeskBedsideConfig.segments
        renderEchoDeskBedsideSection("display")
    }

    private func renderEchoDeskBedsideSection(_ section: String) {
        let enteringSection = echoDeskBedsideSection != section
        echoDeskBedsideSection = section
        guard let document = echoDeskBedsideDocument else { return }
        document.arrangedSubviews.forEach { view in document.removeArrangedSubview(view); view.removeFromSuperview() }
        let views: [NSView]
        switch section {
        case "alarms": views = buildEchoDeskBedsideAlarmsSection()
        case "display": views = buildEchoDeskBedsideDisplaySection()
        default: views = buildEchoDeskBedsideRootSection()
        }
        for view in views { document.addArrangedSubview(view) }
        views.forEach { $0.widthAnchor.constraint(equalTo: document.widthAnchor).isActive = true }
        document.needsLayout = true
        document.layoutSubtreeIfNeeded()
        if enteringSection, let scroll = document.enclosingScrollView {
            scroll.documentView?.layoutSubtreeIfNeeded()
            scroll.contentView.scroll(to: .zero)
            scroll.reflectScrolledClipView(scroll.contentView)
        }
    }

    private func bedsideHeader(_ title: String, _ subtitle: String, root: Bool = false) -> NSView {
        let back = NSButton(title: root ? "All Customisation" : "Bedside Mode", target: self,
                            action: root ? #selector(echoDeskBedsideBackToCustomisation) : #selector(echoDeskBedsideShowRoot))
        back.image = NSImage(systemSymbolName: "chevron.left", accessibilityDescription: nil)
        back.imagePosition = .imageLeading; back.bezelStyle = .inline
        let heading = bedsideLabel(title, size: 26, weight: .semibold)
        let detail = bedsideMuted(subtitle)
        let text = bedsideStack([heading, detail], spacing: 3)
        return bedsideStack([back, text], spacing: 8)
    }

    private func buildEchoDeskBedsideRootSection() -> [NSView] {
        let preview = EchoDeskBedsidePreviewView()
        preview.config = echoDeskBedsideConfig
        preview.translatesAutoresizingMaskIntoConstraints = false
        preview.heightAnchor.constraint(equalTo: preview.widthAnchor, multiplier: 0.75).isActive = true
        preview.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        let stage = NSView()
        stage.addSubview(preview)
        let preferredWidth = preview.widthAnchor.constraint(equalTo: stage.widthAnchor)
        preferredWidth.priority = .defaultHigh
        NSLayoutConstraint.activate([preferredWidth, preview.widthAnchor.constraint(lessThanOrEqualTo: stage.widthAnchor),
            preview.widthAnchor.constraint(lessThanOrEqualToConstant: 304),
            preview.centerXAnchor.constraint(equalTo: stage.centerXAnchor),
            preview.topAnchor.constraint(equalTo: stage.topAnchor), preview.bottomAnchor.constraint(equalTo: stage.bottomAnchor)])
        preview.setAccessibilityElement(true); preview.setAccessibilityRole(.image)
        preview.setAccessibilityLabel("Bedside clock and selected widgets preview")
        echoDeskBedsidePreview = preview

        echoDeskBedsideStatus = bedsideMuted(bedsideStatusText)
        let open = bedsidePrimaryButton("Open on EchoDesk", #selector(echoDeskBedsideOpenOnDevice))
        open.isEnabled = bedsideCapabilityAvailable
        let intro = bedsideCard(bedsideStack([
            bedsideSectionTitle("On your EchoDesk"),
            bedsideMuted("The Mini keeps these settings for Bedside Mode, separate from its usual display settings."),
            bedsideStatusRow(), open,
        ], spacing: 12))
        let hero = EchoDeskAdaptiveStackView(views: [stage, intro], collapseBelow: 625)

        echoDeskBedsideClockFormat = bedsidePopup([("12-hour", "12h"), ("24-hour", "24h")],
                                                   selected: echoDeskBedsideConfig.clockFormat,
                                                   action: #selector(echoDeskBedsidePreviewChanged))
        echoDeskBedsideClockStyle = bedsidePopup([("Large", "large"), ("Minimal", "minimal")],
                                                  selected: echoDeskBedsideConfig.clockStyle,
                                                  action: #selector(echoDeskBedsidePreviewChanged))
        echoDeskBedsideShowSeconds = NSButton(checkboxWithTitle: "Show seconds", target: self,
                                               action: #selector(echoDeskBedsidePreviewChanged))
        echoDeskBedsideShowSeconds?.state = echoDeskBedsideConfig.clockShowSeconds ? .on : .off
        [echoDeskBedsideClockFormat, echoDeskBedsideClockStyle].forEach { $0?.isEnabled = bedsideStateReady }
        echoDeskBedsideShowSeconds?.isEnabled = bedsideStateReady
        let clock = bedsideCard(bedsideStack([
            bedsideSectionTitle("Clock"),
            bedsideMuted("Set the face shown in the top half of the display."),
            bedsideControlRow("Time format", echoDeskBedsideClockFormat!),
            bedsideControlRow("Appearance", echoDeskBedsideClockStyle!),
            echoDeskBedsideShowSeconds!,
        ], spacing: 11))

        echoDeskBedsideNextAlarmWidget = NSButton(checkboxWithTitle: "Next alarm", target: self,
                                                   action: #selector(echoDeskBedsidePreviewChanged))
        echoDeskBedsideNextAlarmWidget?.state = echoDeskBedsideConfig.nextAlarmWidget ? .on : .off
        echoDeskBedsideNextAlarmWidget?.isEnabled = bedsideStateReady
        echoDeskBedsideWeatherWidget = NSButton(checkboxWithTitle: "Weather — Coming soon", target: nil, action: nil)
        echoDeskBedsideWeatherWidget?.state = .off
        echoDeskBedsideWeatherWidget?.isEnabled = false
        let widgets = bedsideCard(bedsideStack([
            bedsideSectionTitle("Widgets"),
            bedsideMuted("Choose what appears below the clock."),
            echoDeskBedsideNextAlarmWidget!, echoDeskBedsideWeatherWidget!,
        ], spacing: 11))
        let choices = EchoDeskAdaptiveStackView(views: [clock, widgets], collapseBelow: 660)

        let alarms = bedsideNavigationCard("Manage alarms", "Create up to eight repeating alarms stored on EchoDesk.",
                                           "alarm.fill", #selector(echoDeskBedsideShowAlarms))
        let display = bedsideNavigationCard("Auto-brightness and display", "Control brightness, Night Shift and appearance schedules.",
                                            "sun.max.fill", #selector(echoDeskBedsideShowDisplay))
        let navigation = EchoDeskAdaptiveStackView(views: [alarms, display], collapseBelow: 660)
        return [bedsideHeader("Bedside Mode", "Designed for power-only use after setup.", root: true), hero, choices, navigation]
    }

    private func buildEchoDeskBedsideAlarmsSection() -> [NSView] {
        echoDeskBedsideAlarmRows.removeAll()
        echoDeskBedsideStatus = bedsideMuted(bedsideStatusText)
        var views: [NSView] = [bedsideHeader("Manage alarms", "Alarms are stored and evaluated directly on EchoDesk.")]
        let cards = echoDeskBedsideAlarmDrafts.sorted(by: { $0.id < $1.id }).map(buildEchoDeskBedsideAlarmRow)
        if cards.isEmpty {
            views.append(bedsideCard(bedsideStack([bedsideSectionTitle("No alarms yet"),
                bedsideMuted("Add an alarm. It will work whenever the Mini has power and a valid time.")], spacing: 8)))
        }
        for index in stride(from: 0, to: cards.count, by: 2) {
            if index + 1 < cards.count {
                views.append(EchoDeskAdaptiveStackView(views: [cards[index], cards[index + 1]], collapseBelow: 720))
            } else {
                // One unpaired alarm stays a readable editor. Its neighbour
                // isn't an alarm; keep Add as a separate, persistent action.
                let row = NSView()
                let card = cards[index]
                card.translatesAutoresizingMaskIntoConstraints = false
                row.addSubview(card)
                let fill = card.widthAnchor.constraint(equalTo: row.widthAnchor)
                fill.priority = .defaultHigh
                NSLayoutConstraint.activate([
                    card.leadingAnchor.constraint(equalTo: row.leadingAnchor),
                    card.topAnchor.constraint(equalTo: row.topAnchor),
                    card.bottomAnchor.constraint(equalTo: row.bottomAnchor),
                    card.widthAnchor.constraint(lessThanOrEqualTo: row.widthAnchor),
                    card.widthAnchor.constraint(lessThanOrEqualToConstant: 576), fill
                ])
                views.append(row)
            }
        }
        let add = NSButton(title: "Add alarm", target: self, action: #selector(echoDeskBedsideAddAlarm))
        add.image = NSImage(systemSymbolName: "plus", accessibilityDescription: nil)
        add.imagePosition = .imageLeading
        add.isEnabled = bedsideStateReady && cards.count < 8
        let actions = NSStackView(views: [add, bedsideMuted("Changes save automatically on EchoDesk.")])
        actions.orientation = .horizontal; actions.alignment = .centerY; actions.spacing = 10
        views.append(actions)
        views.append(bedsideStatusRow())
        return views
    }

    private func buildEchoDeskBedsideAlarmRow(_ alarm: EchoDeskBedsideAlarm) -> NSView {
        let enabled = NSButton(checkboxWithTitle: "Enabled", target: self,
                               action: #selector(echoDeskBedsideAlarmChanged))
        enabled.state = alarm.enabled ? .on : .off
        let time = bedsideTimePicker(alarm.minuteOfDay)
        time.target = self; time.action = #selector(echoDeskBedsideAlarmChanged)
        var repeatChoices = [("Every day", "127"), ("Weekdays", "62"), ("Weekends", "65")]
        if ![127, 62, 65].contains(alarm.weekdays) {
            repeatChoices.append(("Custom days (\(alarm.weekdays))", String(alarm.weekdays)))
        }
        let days = bedsidePopup(repeatChoices, selected: String(alarm.weekdays),
                                action: #selector(echoDeskBedsideAlarmChanged))
        let label = EchoDeskCommitTextField(frame: .zero)
        label.stringValue = alarm.label
        label.placeholderString = "Alarm label"
        label.target = self; label.action = #selector(echoDeskBedsideAlarmChanged)
        let remove = NSButton(title: "Delete", target: self, action: #selector(echoDeskBedsideDeleteAlarm(_:)))
        remove.tag = alarm.id; remove.contentTintColor = .systemRed
        [enabled, time, days, label, remove].forEach { $0.isEnabled = bedsideStateReady }
        let top = NSStackView(views: [bedsideSectionTitle(alarm.label.isEmpty ? "Alarm \(alarm.id)" : alarm.label), NSView(), enabled, remove])
        top.orientation = .horizontal; top.alignment = .centerY; top.spacing = 10
        let controls = bedsideStack([bedsideControlRow("Time", time), bedsideControlRow("Repeats", days),
                                     bedsideControlRow("Label", label)], spacing: 9)
        echoDeskBedsideAlarmRows.append(.init(id: alarm.id, enabled: enabled, time: time,
                                               days: days, label: label, remove: remove))
        return bedsideCard(bedsideStack([top, controls], spacing: 12))
    }

    @objc private func echoDeskBedsideAddAlarm() {
        captureEchoDeskBedsideAlarmDrafts()
        guard echoDeskBedsideAlarmDrafts.count < 8 else { return }
        let used = Set(echoDeskBedsideAlarmDrafts.map(\.id))
        guard let id = (1...8).first(where: { !used.contains($0) }) else { return }
        echoDeskBedsideAlarmDrafts.append(.init(id: id, enabled: true, minuteOfDay: 420,
                                                weekdays: 127, label: "Alarm"))
        renderEchoDeskBedsideSection("alarms")
        queueEchoDeskBedsideAutoApply(bedsideConfigWithCurrentAlarms())
    }

    @objc private func echoDeskBedsideDeleteAlarm(_ sender: NSButton) {
        captureEchoDeskBedsideAlarmDrafts()
        echoDeskBedsideAlarmDrafts.removeAll { $0.id == sender.tag }
        renderEchoDeskBedsideSection("alarms")
        queueEchoDeskBedsideAutoApply(bedsideConfigWithCurrentAlarms())
    }

    @objc private func echoDeskBedsideAlarmChanged() {
        captureEchoDeskBedsideAlarmDrafts()
        queueEchoDeskBedsideAutoApply(bedsideConfigWithCurrentAlarms())
    }

    private func buildEchoDeskBedsideDisplaySection() -> [NSView] {
        echoDeskBedsideScheduleRows.removeAll()
        echoDeskBedsideStatus = bedsideMuted(bedsideStatusText)
        echoDeskBedsideScheduleEnabled = NSButton(checkboxWithTitle: "Use brightness schedule", target: self,
                                                   action: #selector(echoDeskBedsideDisplayModeChanged))
        echoDeskBedsideScheduleEnabled?.state = echoDeskBedsideConfig.scheduleEnabled ? .on : .off
        echoDeskBedsideManualBrightness = NSSlider(value: Double(echoDeskBedsideConfig.manualBrightness),
                                                    minValue: 1, maxValue: 100, target: self,
                                                    action: #selector(echoDeskBedsideDisplayChanged))
        echoDeskBedsideManualBrightness?.isContinuous = false
        echoDeskBedsideEffectiveBrightness = bedsideMuted("")

        let timeline = EchoDeskBrightnessTimelineView(frame: .zero)
        timeline.segments = echoDeskBedsideScheduleDrafts
        timeline.onSelectionChanged = { [weak self] index in
            self?.refreshEchoDeskBedsideTimelineEditor(selection: index)
        }
        timeline.onSegmentsPreview = { [weak self] segments in
            guard let self else { return }
            self.echoDeskBedsideScheduleDrafts = segments
            self.refreshEchoDeskBedsideTimelineEditor(selection: self.echoDeskBedsideBrightnessTimeline?.selectedIndex)
            self.refreshEchoDeskBedsideEffectiveBrightness()
        }
        timeline.onSegmentsCommitted = { [weak self] segments in
            guard let self else { return }
            self.echoDeskBedsideScheduleDrafts = segments
            self.queueEchoDeskBedsideAutoApply(self.bedsideConfigWithCurrentDisplay())
        }
        echoDeskBedsideBrightnessTimeline = timeline

        echoDeskBedsideSchedulePointPopup = bedsidePopup([], selected: "",
            action: #selector(echoDeskBedsideTimelinePointSelected(_:)))
        echoDeskBedsideSchedulePointTime = bedsideTimePicker(echoDeskBedsideScheduleDrafts.first?.startMinute ?? 0)
        echoDeskBedsideSchedulePointTime?.target = self
        echoDeskBedsideSchedulePointTime?.action = #selector(echoDeskBedsideTimelineExactChanged)
        let formatter = NumberFormatter()
        formatter.minimum = NSNumber(value: 1); formatter.maximum = NSNumber(value: 100)
        formatter.allowsFloats = false
        echoDeskBedsideSchedulePointBrightness = EchoDeskCommitTextField(frame: .zero)
        echoDeskBedsideSchedulePointBrightness?.stringValue = String(echoDeskBedsideScheduleDrafts.first?.brightness ?? 40)
        echoDeskBedsideSchedulePointBrightness?.formatter = formatter
        echoDeskBedsideSchedulePointBrightness?.alignment = .right
        echoDeskBedsideSchedulePointBrightness?.target = self
        echoDeskBedsideSchedulePointBrightness?.action = #selector(echoDeskBedsideTimelineExactChanged)
        echoDeskBedsideSchedulePointStepper = NSStepper()
        echoDeskBedsideSchedulePointStepper?.minValue = 1
        echoDeskBedsideSchedulePointStepper?.maxValue = 100
        echoDeskBedsideSchedulePointStepper?.increment = 1
        echoDeskBedsideSchedulePointStepper?.target = self
        echoDeskBedsideSchedulePointStepper?.action = #selector(echoDeskBedsideTimelineStepperChanged)
        let brightnessExact = NSStackView(views: [echoDeskBedsideSchedulePointBrightness!,
                                                   echoDeskBedsideSchedulePointStepper!])
        brightnessExact.orientation = .horizontal; brightnessExact.alignment = .centerY; brightnessExact.spacing = 5
        echoDeskBedsideSchedulePointBrightness?.widthAnchor.constraint(equalToConstant: 64).isActive = true
        let deletePoint = NSButton(title: "Remove point", target: self,
                                   action: #selector(echoDeskBedsideDeleteSelectedSchedule))
        deletePoint.contentTintColor = .systemRed
        echoDeskBedsideScheduleDeleteButton = deletePoint
        let exactEditor = bedsideStack([
            bedsideSectionTitle("Selected point"),
            bedsideControlRow("Point", echoDeskBedsideSchedulePointPopup!),
            bedsideControlRow("Starts at", echoDeskBedsideSchedulePointTime!),
            bedsideControlRow("Brightness", brightnessExact), deletePoint,
        ], spacing: 8)
        let addTime = NSButton(title: "Add schedule point", target: self, action: #selector(echoDeskBedsideAddSchedule))
        addTime.image = NSImage(systemSymbolName: "plus", accessibilityDescription: nil)
        addTime.imagePosition = .imageLeading
        addTime.isEnabled = bedsideStateReady && echoDeskBedsideScheduleDrafts.count < 8
        echoDeskBedsideScheduleAddButton = addTime
        let timelineTools = EchoDeskAdaptiveStackView(views: [timeline, exactEditor], collapseBelow: 820)
        echoDeskBedsideScheduleDetails = bedsideStack([timelineTools, addTime], spacing: 10)
        echoDeskBedsideManualBrightnessDetails = bedsideControlRow("Manual brightness",
                                                                    echoDeskBedsideManualBrightness!)
        let schedule = bedsideCard(bedsideStack([
            bedsideSectionTitle("Auto-brightness"),
            bedsideMuted("EchoDesk applies discrete changes at each point; the schedule repeats every 24 hours."),
            echoDeskBedsideScheduleEnabled!, echoDeskBedsideEffectiveBrightness!,
            echoDeskBedsideScheduleDetails!, echoDeskBedsideManualBrightnessDetails!,
        ], spacing: 10))

        echoDeskBedsideNightScheduleEnabled = NSButton(checkboxWithTitle: "Use Night Shift schedule", target: self,
                                                        action: #selector(echoDeskBedsideDisplayModeChanged))
        echoDeskBedsideNightScheduleEnabled?.state = echoDeskBedsideConfig.nightScheduleEnabled ? .on : .off
        echoDeskBedsideNightManual = NSButton(checkboxWithTitle: "Night Shift on", target: self,
                                               action: #selector(echoDeskBedsideDisplayChanged))
        echoDeskBedsideNightManual?.state = echoDeskBedsideConfig.nightManualEnabled ? .on : .off
        echoDeskBedsideNightStart = bedsideTimePicker(echoDeskBedsideConfig.nightStartMinute)
        echoDeskBedsideNightEnd = bedsideTimePicker(echoDeskBedsideConfig.nightEndMinute)
        [echoDeskBedsideNightStart, echoDeskBedsideNightEnd].forEach {
            $0?.target = self; $0?.action = #selector(echoDeskBedsideDisplayChanged)
        }
        let nightTimes = bedsideStack([bedsideControlRow("From", echoDeskBedsideNightStart!),
                                       bedsideControlRow("Until", echoDeskBedsideNightEnd!)], spacing: 6)
        echoDeskBedsideNightScheduleDetails = nightTimes
        echoDeskBedsideNightManualDetails = echoDeskBedsideNightManual
        let warmth = EchoDeskBedsideTemperatureStrip()
        warmth.setAccessibilityHidden(true)
        let night = bedsideCard(bedsideStack([
            bedsideSectionTitle("Night Shift"), bedsideMuted("Warms the display for a calmer bedside appearance."),
            warmth, echoDeskBedsideNightScheduleEnabled!, echoDeskBedsideNightScheduleDetails!,
            echoDeskBedsideNightManualDetails!,
        ], spacing: 10))

        echoDeskBedsideThemeScheduleEnabled = NSButton(checkboxWithTitle: "Use appearance schedule", target: self,
                                                        action: #selector(echoDeskBedsideDisplayModeChanged))
        echoDeskBedsideThemeScheduleEnabled?.state = echoDeskBedsideConfig.themeScheduleEnabled ? .on : .off
        echoDeskBedsideThemeManualDark = NSSegmentedControl(labels: ["Light", "Dark"], trackingMode: .selectOne,
                                                            target: self, action: #selector(echoDeskBedsideDisplayChanged))
        echoDeskBedsideThemeManualDark?.selectedSegment = echoDeskBedsideConfig.themeManualDark ? 1 : 0
        echoDeskBedsideThemeStart = bedsideTimePicker(echoDeskBedsideConfig.themeStartMinute)
        echoDeskBedsideThemeEnd = bedsideTimePicker(echoDeskBedsideConfig.themeEndMinute)
        [echoDeskBedsideThemeStart, echoDeskBedsideThemeEnd].forEach {
            $0?.target = self; $0?.action = #selector(echoDeskBedsideDisplayChanged)
        }
        let themeTimes = bedsideStack([bedsideControlRow("Dark from", echoDeskBedsideThemeStart!),
                                       bedsideControlRow("Light from", echoDeskBedsideThemeEnd!)], spacing: 6)
        echoDeskBedsideThemeScheduleDetails = themeTimes
        echoDeskBedsideThemeManualDetails = bedsideControlRow("Appearance", echoDeskBedsideThemeManualDark!)
        echoDeskBedsideThemeEffective = bedsideMuted("")
        let appearancePreviews = NSStackView(views: [EchoDeskBedsideAppearancePreview(dark: false),
                                                     EchoDeskBedsideAppearancePreview(dark: true)])
        appearancePreviews.orientation = .horizontal; appearancePreviews.distribution = .fillEqually; appearancePreviews.spacing = 10
        let theme = bedsideCard(bedsideStack([
            bedsideSectionTitle("Dark and Light appearance"),
            bedsideMuted("Bedside appearance is separate from the rest of EchoDesk OS."),
            appearancePreviews, echoDeskBedsideThemeEffective!, echoDeskBedsideThemeScheduleEnabled!,
            echoDeskBedsideThemeScheduleDetails!,
            echoDeskBedsideThemeManualDetails!,
        ], spacing: 10))
        let interactiveControls: [NSControl?] = [
            echoDeskBedsideScheduleEnabled, echoDeskBedsideManualBrightness,
            echoDeskBedsideSchedulePointPopup, echoDeskBedsideSchedulePointTime,
            echoDeskBedsideSchedulePointBrightness, echoDeskBedsideSchedulePointStepper,
            echoDeskBedsideNightScheduleEnabled, echoDeskBedsideNightManual,
            echoDeskBedsideNightStart, echoDeskBedsideNightEnd,
            echoDeskBedsideThemeScheduleEnabled, echoDeskBedsideThemeManualDark,
            echoDeskBedsideThemeStart, echoDeskBedsideThemeEnd,
        ]
        interactiveControls.forEach { $0?.isEnabled = bedsideStateReady }
        timeline.isEnabled = bedsideStateReady
        refreshEchoDeskBedsideTimelineEditor(selection: echoDeskBedsideScheduleDrafts.isEmpty ? nil : 0)
        refreshEchoDeskBedsideEffectiveBrightness()
        updateEchoDeskBedsideDisplayDisclosure()
        return [bedsideHeader("Auto-brightness and display", "Schedules repeat locally without EchoDesk Desktop."),
                schedule, EchoDeskAdaptiveStackView(views: [night, theme], collapseBelow: 680), bedsideStatusRow()]
    }

    @objc private func echoDeskBedsideAddSchedule() {
        guard echoDeskBedsideScheduleDrafts.count < 8 else { return }
        let used = Set(echoDeskBedsideScheduleDrafts.map(\.startMinute))
        let minute = stride(from: 0, through: 1380, by: 60).first(where: { !used.contains($0) }) ?? 0
        echoDeskBedsideScheduleDrafts.append(.init(startMinute: minute, brightness: 40))
        echoDeskBedsideScheduleDrafts.sort { $0.startMinute < $1.startMinute }
        echoDeskBedsideBrightnessTimeline?.segments = echoDeskBedsideScheduleDrafts
        let index = echoDeskBedsideScheduleDrafts.firstIndex { $0.startMinute == minute }
        echoDeskBedsideBrightnessTimeline?.select(index: index)
        refreshEchoDeskBedsideTimelineEditor(selection: index)
        refreshEchoDeskBedsideEffectiveBrightness()
        queueEchoDeskBedsideAutoApply(bedsideConfigWithCurrentDisplay())
    }

    @objc private func echoDeskBedsideDeleteSchedule(_ sender: NSButton) {
        guard echoDeskBedsideScheduleDrafts.count > 1,
              echoDeskBedsideScheduleDrafts.indices.contains(sender.tag) else { return }
        echoDeskBedsideScheduleDrafts.remove(at: sender.tag)
        echoDeskBedsideBrightnessTimeline?.segments = echoDeskBedsideScheduleDrafts
        echoDeskBedsideBrightnessTimeline?.select(index: min(sender.tag, echoDeskBedsideScheduleDrafts.count - 1))
        refreshEchoDeskBedsideEffectiveBrightness()
        queueEchoDeskBedsideAutoApply(bedsideConfigWithCurrentDisplay())
    }

    @objc private func echoDeskBedsideDeleteSelectedSchedule() {
        guard let index = echoDeskBedsideBrightnessTimeline?.selectedIndex else { return }
        let button = NSButton(); button.tag = index
        echoDeskBedsideDeleteSchedule(button)
    }

    @objc private func echoDeskBedsideTimelinePointSelected(_ sender: NSPopUpButton) {
        let index = sender.selectedItem?.representedObject as? Int
        echoDeskBedsideBrightnessTimeline?.select(index: index)
    }

    @objc private func echoDeskBedsideTimelineExactChanged() {
        guard let timeline = echoDeskBedsideBrightnessTimeline else { return }
        let minute = bedsideMinute(echoDeskBedsideSchedulePointTime?.dateValue ?? Date())
        let brightness = min(100, max(1, echoDeskBedsideSchedulePointBrightness?.integerValue ?? 40))
        timeline.updateSelected(minute: minute, brightness: brightness, commit: true)
        refreshEchoDeskBedsideTimelineEditor(selection: timeline.selectedIndex)
    }

    @objc private func echoDeskBedsideTimelineStepperChanged() {
        let brightness = min(100, max(1, Int(echoDeskBedsideSchedulePointStepper?.doubleValue ?? 40)))
        echoDeskBedsideSchedulePointBrightness?.integerValue = brightness
        echoDeskBedsideTimelineExactChanged()
    }

    private func refreshEchoDeskBedsideTimelineEditor(selection: Int?) {
        guard let timeline = echoDeskBedsideBrightnessTimeline else { return }
        if timeline.selectedIndex != selection {
            timeline.select(index: selection)
            return
        }
        let popup = echoDeskBedsideSchedulePointPopup
        popup?.removeAllItems()
        for (index, segment) in timeline.segments.enumerated() {
            let title = "\(bedsideClockText(segment.startMinute))  ·  \(segment.brightness)%"
            popup?.addItem(withTitle: title)
            popup?.lastItem?.representedObject = index
        }
        guard let selection, timeline.segments.indices.contains(selection) else {
            [echoDeskBedsideSchedulePointPopup, echoDeskBedsideSchedulePointTime,
             echoDeskBedsideSchedulePointBrightness, echoDeskBedsideSchedulePointStepper,
             echoDeskBedsideScheduleDeleteButton].forEach { $0?.isEnabled = false }
            return
        }
        popup?.selectItem(at: selection)
        let segment = timeline.segments[selection]
        echoDeskBedsideSchedulePointTime?.dateValue = bedsideDate(segment.startMinute)
        echoDeskBedsideSchedulePointBrightness?.integerValue = segment.brightness
        echoDeskBedsideSchedulePointStepper?.integerValue = segment.brightness
        let enabled = bedsideStateReady
        [echoDeskBedsideSchedulePointPopup, echoDeskBedsideSchedulePointTime,
         echoDeskBedsideSchedulePointBrightness, echoDeskBedsideSchedulePointStepper].forEach {
            $0?.isEnabled = enabled
        }
        echoDeskBedsideScheduleDeleteButton?.isEnabled = enabled && timeline.segments.count > 1
        echoDeskBedsideScheduleAddButton?.isEnabled = enabled && timeline.segments.count < 8
    }

    private func refreshEchoDeskBedsideEffectiveBrightness() {
        guard let label = echoDeskBedsideEffectiveBrightness else { return }
        if echoDeskBedsideScheduleEnabled?.state != .on {
            label.stringValue = "Manual brightness: \(Int(echoDeskBedsideManualBrightness?.doubleValue ?? 40))%"
            return
        }
        if echoDeskBedsideRuntime["timeValid"] as? Bool != true {
            label.stringValue = "Waiting for a valid clock; manual fallback is \(Int(echoDeskBedsideManualBrightness?.doubleValue ?? 40))%."
            return
        }
        let now = Date()
        let minute = Calendar.current.component(.hour, from: now) * 60
            + Calendar.current.component(.minute, from: now)
        let ordered = echoDeskBedsideScheduleDrafts.sorted { $0.startMinute < $1.startMinute }
        let effective = ordered.last(where: { $0.startMinute <= minute }) ?? ordered.last
        if let effective {
            label.stringValue = "Currently effective: \(effective.brightness)% since \(bedsideClockText(effective.startMinute))"
        } else {
            label.stringValue = "No brightness point is available."
        }
    }

    private func bedsideClockText(_ minute: Int) -> String {
        let hour = minute / 60, value = minute % 60
        if echoDeskBedsideConfig.clockFormat == "24h" { return String(format: "%02d:%02d", hour, value) }
        let suffix = hour >= 12 ? "PM" : "AM"
        let displayHour = hour % 12 == 0 ? 12 : hour % 12
        return String(format: "%d:%02d %@", displayHour, value, suffix)
    }

    @objc private func echoDeskBedsideDisplayModeChanged() {
        updateEchoDeskBedsideDisplayDisclosure()
        echoDeskBedsideDisplayChanged()
    }

    private func updateEchoDeskBedsideDisplayDisclosure() {
        let brightnessScheduled = echoDeskBedsideScheduleEnabled?.state == .on
        echoDeskBedsideScheduleDetails?.isHidden = !brightnessScheduled
        echoDeskBedsideManualBrightnessDetails?.isHidden = brightnessScheduled
        let nightScheduled = echoDeskBedsideNightScheduleEnabled?.state == .on
        echoDeskBedsideNightScheduleDetails?.isHidden = !nightScheduled
        echoDeskBedsideNightManualDetails?.isHidden = nightScheduled
        let themeScheduled = echoDeskBedsideThemeScheduleEnabled?.state == .on
        echoDeskBedsideThemeScheduleDetails?.isHidden = !themeScheduled
        echoDeskBedsideThemeManualDetails?.isHidden = themeScheduled
        refreshEchoDeskBedsideEffectiveBrightness()
        refreshEchoDeskBedsideEffectiveAppearance()
    }

    @objc private func echoDeskBedsideDisplayChanged() {
        refreshEchoDeskBedsideEffectiveBrightness()
        refreshEchoDeskBedsideEffectiveAppearance()
        queueEchoDeskBedsideAutoApply(bedsideConfigWithCurrentDisplay())
    }

    private func refreshEchoDeskBedsideEffectiveAppearance() {
        guard let label = echoDeskBedsideThemeEffective else { return }
        guard echoDeskBedsideThemeScheduleEnabled?.state == .on else {
            label.stringValue = echoDeskBedsideThemeManualDark?.selectedSegment == 1
                ? "Currently: Dark" : "Currently: Light"
            return
        }
        guard echoDeskBedsideRuntime["timeValid"] as? Bool == true else {
            label.stringValue = "Waiting for a valid clock before applying the appearance schedule."
            return
        }
        let now = Date()
        let minute = Calendar.current.component(.hour, from: now) * 60
            + Calendar.current.component(.minute, from: now)
        let start = bedsideMinute(echoDeskBedsideThemeStart?.dateValue ?? now)
        let end = bedsideMinute(echoDeskBedsideThemeEnd?.dateValue ?? now)
        label.stringValue = bedsideMinuteFallsInside(minute, start: start, end: end)
            ? "Currently: Dark" : "Currently: Light"
    }

    /// Mirrors the firmware's start-inclusive, end-exclusive schedule window.
    /// Equal endpoints intentionally mean the setting is active all day.
    private func bedsideMinuteFallsInside(_ value: Int, start: Int, end: Int) -> Bool {
        if start == end { return true }
        if start < end { return value >= start && value < end }
        return value >= start || value < end
    }

    private func bedsideConfigWithCurrentDisplay() -> EchoDeskBedsideConfig {
        var next = bedsideEditingBaseConfig
        next.scheduleEnabled = echoDeskBedsideScheduleEnabled?.state == .on
        next.manualBrightness = min(100, max(1, Int(echoDeskBedsideManualBrightness?.doubleValue ?? 40)))
        next.segments = echoDeskBedsideScheduleDrafts.sorted { $0.startMinute < $1.startMinute }
        next.nightScheduleEnabled = echoDeskBedsideNightScheduleEnabled?.state == .on
        next.nightManualEnabled = echoDeskBedsideNightManual?.state == .on
        next.nightStartMinute = bedsideMinute(echoDeskBedsideNightStart?.dateValue ?? Date())
        next.nightEndMinute = bedsideMinute(echoDeskBedsideNightEnd?.dateValue ?? Date())
        next.themeScheduleEnabled = echoDeskBedsideThemeScheduleEnabled?.state == .on
        next.themeManualDark = echoDeskBedsideThemeManualDark?.selectedSegment == 1
        next.themeStartMinute = bedsideMinute(echoDeskBedsideThemeStart?.dateValue ?? Date())
        next.themeEndMinute = bedsideMinute(echoDeskBedsideThemeEnd?.dateValue ?? Date())
        return next
    }

    // MARK: - Bedside state and actions

    private var bedsideCapabilityAvailable: Bool {
        guard let monitor else { return false }
        return monitor.serialPort.state == .connected && (monitor.serialPort.deviceEchoDeskBedsideProtocol ?? 0) >= 1
    }

    private var bedsideStateReady: Bool {
        guard bedsideCapabilityAvailable, let monitor, let port = monitor.serialPort.connectedPort else { return false }
        return echoDeskBedsideStatePort == port
    }

    private var bedsideStatusText: String {
        if bedsideStateReady { return "Settings are stored on EchoDesk and keep working from power alone." }
        if bedsideCapabilityAvailable { return "Loading Bedside settings from EchoDesk…" }
        return "Connect an EchoDesk with Bedside Mode support to customise it."
    }

    func refreshEchoDeskBedsideAvailability() {
        let enabled = bedsideStateReady
        let controls: [NSControl?] = [
            echoDeskBedsideClockFormat, echoDeskBedsideClockStyle, echoDeskBedsideShowSeconds,
            echoDeskBedsideNextAlarmWidget, echoDeskBedsideScheduleEnabled,
            echoDeskBedsideManualBrightness, echoDeskBedsideSchedulePointPopup,
            echoDeskBedsideSchedulePointTime, echoDeskBedsideSchedulePointBrightness,
            echoDeskBedsideSchedulePointStepper, echoDeskBedsideScheduleAddButton,
            echoDeskBedsideScheduleDeleteButton, echoDeskBedsideNightScheduleEnabled,
            echoDeskBedsideNightManual, echoDeskBedsideNightStart, echoDeskBedsideNightEnd,
            echoDeskBedsideThemeScheduleEnabled, echoDeskBedsideThemeManualDark,
            echoDeskBedsideThemeStart, echoDeskBedsideThemeEnd,
        ]
        controls.forEach { $0?.isEnabled = enabled }
        echoDeskBedsideBrightnessTimeline?.isEnabled = enabled
        for row in echoDeskBedsideAlarmRows {
            let rowControls: [NSControl] = [row.enabled, row.time, row.days, row.label, row.remove]
            rowControls.forEach { $0.isEnabled = enabled }
        }
        if !enabled {
            echoDeskBedsideStatus?.stringValue = bedsideCapabilityAvailable
                ? "Loading the latest Bedside settings from EchoDesk…"
                : "EchoDesk is disconnected. Unsaved changes are kept here until you reconnect and retry."
            echoDeskBedsideRetryButton?.isHidden = echoDeskBedsidePendingConfig == nil
        }
    }

    func requestEchoDeskBedsideState() {
        guard !echoDeskBedsideStateRequestInFlight else { return }
        guard let monitor, monitor.serialPort.state == .connected else {
            echoDeskBedsideStatus?.stringValue = "EchoDesk is disconnected. Unsaved changes are kept here until you reconnect and retry."
            echoDeskBedsideRetryButton?.isHidden = echoDeskBedsidePendingConfig == nil
            return
        }
        guard (monitor.serialPort.deviceEchoDeskBedsideProtocol ?? 0) >= 1 else {
            echoDeskBedsideStatus?.stringValue = "Update EchoDesk OS to a version with Bedside Mode support."
            return
        }
        guard let requestedPort = monitor.serialPort.connectedPort else { return }
        echoDeskBedsideStateRequestInFlight = true
        let request = bedsideNextRequest()
        monitor.serialPort.performJSONCommand([
            "cmd": "echodesk_bedside_get_state", "version": 1, "requestId": request,
        ], acceptedTypes: ["echodesk_bedside_state", "echodesk_error"], timeout: 5) { [weak self] json in
            guard let self else { return }
            self.echoDeskBedsideStateRequestInFlight = false
            guard let json, json["type"] as? String == "echodesk_bedside_state",
                  (json["requestId"] as? NSNumber)?.uint32Value == request,
                  let revision = (json["revision"] as? NSNumber)?.intValue,
                  revision >= 0,
                  monitor.serialPort.connectedPort == requestedPort,
                  let configJSON = json["config"] as? [String: Any],
                  let config = EchoDeskBedsideConfig(json: configJSON) else {
                self.echoDeskBedsideStatus?.stringValue = "EchoDesk returned invalid Bedside settings."
                return
            }
            self.echoDeskBedsideRevision = revision
            self.echoDeskBedsideStatePort = requestedPort
            self.echoDeskBedsideConfig = config
            self.echoDeskBedsideRuntime = json["runtime"] as? [String: Any] ?? [:]
            if self.echoDeskBedsidePendingConfig == nil {
                if self.echoDeskBedsideSection == "alarms" { self.echoDeskBedsideAlarmDrafts = config.alarms }
                if self.echoDeskBedsideSection == "display" { self.echoDeskBedsideScheduleDrafts = config.segments }
                self.renderEchoDeskBedsideSection(self.echoDeskBedsideSection)
            } else if self.echoDeskBedsideRetryAfterRefresh {
                self.echoDeskBedsideRetryAfterRefresh = false
                self.flushEchoDeskBedsideAutoApply()
            } else {
                self.echoDeskBedsideStatus?.stringValue = "The last change is still pending. Select Retry to save it."
                self.echoDeskBedsideRetryButton?.isHidden = false
            }
        }
    }

    private func sendEchoDeskBedsideConfig(_ config: EchoDeskBedsideConfig) {
        guard !echoDeskBedsideSaveInFlight, bedsideStateReady, let monitor else { return }
        echoDeskBedsideSaveInFlight = true
        echoDeskBedsideInFlightConfig = config
        echoDeskBedsideStatus?.stringValue = "Applying changes to EchoDesk…"
        let request = bedsideNextRequest()
        monitor.serialPort.performJSONCommand([
            "cmd": "echodesk_bedside_set_config", "version": 1, "requestId": request,
            "baseRevision": echoDeskBedsideRevision, "persist": true, "config": config.json,
        ], acceptedTypes: ["echodesk_ack", "echodesk_error"], timeout: 7) { [weak self] json in
            guard let self else { return }
            self.echoDeskBedsideSaveInFlight = false
            let ok = json?["type"] as? String == "echodesk_ack"
                && (json?["requestId"] as? NSNumber)?.uint32Value == request
                && json?["command"] as? String == "echodesk_bedside_set_config"
            guard ok, let revision = (json?["revision"] as? NSNumber)?.intValue, revision >= 0 else {
                let failedConfig = self.echoDeskBedsideInFlightConfig ?? config
                self.echoDeskBedsideInFlightConfig = nil
                // A newer draft can be queued while this request is in flight.
                // Keep that latest user intent instead of replacing it with the
                // older failed payload.
                if self.echoDeskBedsidePendingConfig == nil {
                    self.echoDeskBedsidePendingConfig = failedConfig
                }
                let errorCode = json?["code"] as? String ?? json?["error"] as? String ?? ""
                let stale = errorCode == "stale_revision"
                if stale && !self.echoDeskBedsideDidRetryStaleRevision {
                    self.echoDeskBedsideDidRetryStaleRevision = true
                    self.echoDeskBedsideRetryAfterRefresh = true
                    self.echoDeskBedsideStatus?.stringValue = "EchoDesk settings changed elsewhere. Refreshing and retrying once…"
                } else {
                    self.echoDeskBedsideRetryAfterRefresh = false
                    self.echoDeskBedsideStatus?.stringValue = "The change was not saved. Check the connection, then select Retry."
                    self.echoDeskBedsideRetryButton?.isHidden = false
                }
                self.echoDeskBedsideStatePort = nil
                self.requestEchoDeskBedsideState()
                return
            }
            self.echoDeskBedsideRevision = revision
            self.echoDeskBedsideConfig = config
            self.echoDeskBedsideInFlightConfig = nil
            self.echoDeskBedsideDidRetryStaleRevision = false
            self.echoDeskBedsideRetryButton?.isHidden = true
            self.echoDeskBedsideStatus?.stringValue = "Saved on EchoDesk. Changes apply automatically."
            if self.echoDeskBedsidePendingConfig != nil {
                self.flushEchoDeskBedsideAutoApply()
            } else {
                self.echoDeskBedsideStatePort = nil
                self.requestEchoDeskBedsideState()
            }
        }
    }

    private var bedsideEditingBaseConfig: EchoDeskBedsideConfig {
        echoDeskBedsidePendingConfig ?? echoDeskBedsideInFlightConfig ?? echoDeskBedsideConfig
    }

    private func queueEchoDeskBedsideAutoApply(_ config: EchoDeskBedsideConfig) {
        guard bedsideStateReady else {
            echoDeskBedsideStatus?.stringValue = "Connect EchoDesk and wait for its settings before making changes."
            NSSound.beep()
            return
        }
        guard (1...8).contains(config.segments.count),
              Set(config.segments.map(\.startMinute)).count == config.segments.count else {
            echoDeskBedsideStatus?.stringValue = "Brightness schedule points must use unique times."
            NSSound.beep()
            return
        }
        echoDeskBedsidePendingConfig = config
        echoDeskBedsideDidRetryStaleRevision = false
        echoDeskBedsideRetryButton?.isHidden = true
        echoDeskBedsideStatus?.stringValue = "Changes pending…"
        echoDeskBedsideAutoApplyWorkItem?.cancel()
        let work = DispatchWorkItem { [weak self] in self?.flushEchoDeskBedsideAutoApply() }
        echoDeskBedsideAutoApplyWorkItem = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.3, execute: work)
    }

    private func flushEchoDeskBedsideAutoApply() {
        guard !echoDeskBedsideSaveInFlight,
              let pending = echoDeskBedsidePendingConfig,
              bedsideStateReady else { return }
        echoDeskBedsideAutoApplyWorkItem?.cancel()
        echoDeskBedsideAutoApplyWorkItem = nil
        echoDeskBedsidePendingConfig = nil
        sendEchoDeskBedsideConfig(pending)
    }

    @objc private func echoDeskBedsideOpenOnDevice() {
        guard bedsideCapabilityAvailable, let monitor else { return }
        let request = bedsideNextRequest()
        monitor.serialPort.performJSONCommand([
            "cmd": "echodesk_open_app", "version": 1, "requestId": request, "app": "bedside",
        ], acceptedTypes: ["echodesk_ack", "echodesk_error"], timeout: 5) { [weak self] json in
            let ok = json?["type"] as? String == "echodesk_ack"
                && json?["command"] as? String == "echodesk_open_app"
            self?.echoDeskBedsideStatus?.stringValue = ok ? "Bedside Mode opened on EchoDesk." : "EchoDesk did not open Bedside Mode."
        }
    }

    @objc private func echoDeskBedsidePreviewChanged() {
        var next = bedsideEditingBaseConfig
        next.clockFormat = echoDeskBedsideClockFormat?.selectedItem?.representedObject as? String ?? next.clockFormat
        next.clockStyle = echoDeskBedsideClockStyle?.selectedItem?.representedObject as? String ?? next.clockStyle
        next.clockShowSeconds = echoDeskBedsideShowSeconds?.state == .on
        next.nextAlarmWidget = echoDeskBedsideNextAlarmWidget?.state == .on
        next.weatherWidget = false
        echoDeskBedsidePreview?.config = next
        queueEchoDeskBedsideAutoApply(next)
    }

    private func bedsideConfigWithCurrentAlarms() -> EchoDeskBedsideConfig {
        var next = bedsideEditingBaseConfig
        next.alarms = echoDeskBedsideAlarmDrafts.sorted { $0.id < $1.id }
        return next
    }

    private func captureEchoDeskBedsideAlarmDrafts() {
        guard !echoDeskBedsideAlarmRows.isEmpty else { return }
        echoDeskBedsideAlarmDrafts = echoDeskBedsideAlarmRows.map { row in
            EchoDeskBedsideAlarm(id: row.id, enabled: row.enabled.state == .on,
                                 minuteOfDay: bedsideMinute(row.time.dateValue),
                                 weekdays: Int(row.days.selectedItem?.representedObject as? String ?? "127") ?? 127,
                                 label: bedsideBoundedUTF8(row.label.stringValue, bytes: 24))
        }
    }

    private func captureEchoDeskBedsideScheduleDrafts() {
        guard !echoDeskBedsideScheduleRows.isEmpty else { return }
        echoDeskBedsideScheduleDrafts = echoDeskBedsideScheduleRows
            .sorted { $0.index < $1.index }
            .map { row in
                EchoDeskBedsideScheduleSegment(startMinute: bedsideMinute(row.time.dateValue),
                                                brightness: min(100, max(1, row.brightness.integerValue)))
            }
    }

    private func bedsideNextRequest() -> UInt32 {
        echoDeskRequestID &+= 1
        if echoDeskRequestID == 0 { echoDeskRequestID = 1 }
        return echoDeskRequestID
    }

    // MARK: - Bedside native controls

    private func bedsideStatusRow() -> NSView {
        let retry = NSButton(title: "Retry", target: self, action: #selector(echoDeskBedsideRetryPendingChange))
        retry.bezelStyle = .rounded
        retry.isHidden = echoDeskBedsidePendingConfig == nil
        echoDeskBedsideRetryButton = retry
        let row = NSStackView(views: [echoDeskBedsideStatus!, NSView(), retry])
        row.orientation = .horizontal; row.alignment = .centerY; row.spacing = 10
        return row
    }

    @objc private func echoDeskBedsideRetryPendingChange() {
        guard echoDeskBedsidePendingConfig != nil else { return }
        echoDeskBedsideRetryButton?.isHidden = true
        if bedsideStateReady {
            flushEchoDeskBedsideAutoApply()
        } else {
            echoDeskBedsideRetryAfterRefresh = true
            echoDeskBedsideStatus?.stringValue = "Reconnecting to EchoDesk…"
            requestEchoDeskBedsideState()
        }
    }

    private func bedsideStack(_ views: [NSView], spacing: CGFloat = 14) -> NSStackView {
        let stack = NSStackView(views: views)
        stack.orientation = .vertical; stack.alignment = .leading; stack.spacing = spacing
        for view in views {
            if view is NSTextField || view is NSStackView || view is EchoDeskBrightnessTimelineView ||
                view is EchoDeskBedsideTemperatureStrip || view is EchoDeskFlowGrid {
                view.widthAnchor.constraint(equalTo: stack.widthAnchor).isActive = true
            }
        }
        return stack
    }

    private func bedsideCard(_ content: NSView) -> NSView {
        let card = EchoDeskSurfaceView(.card)
        content.translatesAutoresizingMaskIntoConstraints = false
        card.addSubview(content)
        NSLayoutConstraint.activate([
            content.leadingAnchor.constraint(equalTo: card.leadingAnchor, constant: 12),
            content.trailingAnchor.constraint(equalTo: card.trailingAnchor, constant: -12),
            content.topAnchor.constraint(equalTo: card.topAnchor, constant: 10),
            content.bottomAnchor.constraint(equalTo: card.bottomAnchor, constant: -10),
        ])
        return card
    }

    private func bedsideLabel(_ text: String, size: CGFloat, weight: NSFont.Weight) -> NSTextField {
        let label = NSTextField(wrappingLabelWithString: text)
        label.font = .systemFont(ofSize: size, weight: weight)
        label.maximumNumberOfLines = 0
        return label
    }

    private func bedsideSectionTitle(_ text: String) -> NSTextField { bedsideLabel(text, size: 15, weight: .semibold) }
    private func bedsideMuted(_ text: String) -> NSTextField {
        let label = bedsideLabel(text, size: 13, weight: .regular)
        label.textColor = .secondaryLabelColor; label.maximumNumberOfLines = 0
        return label
    }

    private func bedsidePrimaryButton(_ title: String, _ action: Selector) -> NSButton {
        let button = NSButton(title: title, target: self, action: action)
        button.bezelStyle = .rounded; button.controlSize = .regular
        button.keyEquivalent = ""
        button.contentTintColor = .controlAccentColor
        return button
    }

    private func bedsideNavigationCard(_ title: String, _ detail: String, _ symbol: String,
                                       _ action: Selector) -> NSView {
        EchoDeskActionTile(title: title, detail: detail, symbol: symbol, target: self, action: action)
    }

    private func bedsideControlRow(_ title: String, _ control: NSView) -> NSView {
        let label = bedsideLabel(title, size: 13, weight: .medium)
        label.setContentHuggingPriority(.defaultLow, for: .horizontal)
        label.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        let displayed: NSView = (control as? NSSegmentedControl).map { echoDeskAppearanceField($0) } ?? control
        let row = NSStackView(views: [label, displayed])
        row.orientation = .horizontal; row.alignment = .centerY; row.spacing = 10
        (control as? NSControl)?.setAccessibilityLabel(title)
        if control is NSTextField || control is NSSlider {
            let width = control.widthAnchor.constraint(equalToConstant: 148)
            width.priority = .defaultHigh
            width.isActive = true
        } else {
            control.setContentHuggingPriority(.required, for: .horizontal)
        }
        return row
    }

    private func bedsidePopup(_ options: [(String, String)], selected: String,
                              action: Selector?) -> NSPopUpButton {
        let popup = NSPopUpButton(frame: .zero, pullsDown: false)
        popup.bezelStyle = .rounded; popup.controlSize = .regular
        popup.target = action == nil ? nil : self; popup.action = action
        for (title, value) in options {
            popup.addItem(withTitle: title); popup.lastItem?.representedObject = value
        }
        if let item = popup.itemArray.first(where: { ($0.representedObject as? String) == selected }) { popup.select(item) }
        return popup
    }

    private func bedsideTimePicker(_ minute: Int) -> NSDatePicker {
        let picker = NSDatePicker()
        picker.datePickerStyle = .textFieldAndStepper; picker.datePickerElements = .hourMinute
        picker.dateValue = bedsideDate(minute)
        return picker
    }

    private func bedsideDate(_ minute: Int) -> Date {
        Calendar.current.date(bySettingHour: minute / 60, minute: minute % 60, second: 0, of: Date()) ?? Date()
    }

    private func bedsideMinute(_ date: Date) -> Int {
        let parts = Calendar.current.dateComponents([.hour, .minute], from: date)
        return min(1439, max(0, (parts.hour ?? 0) * 60 + (parts.minute ?? 0)))
    }

    private func bedsideBoundedUTF8(_ value: String, bytes: Int) -> String {
        var result = ""
        for scalar in value.trimmingCharacters(in: .whitespacesAndNewlines).unicodeScalars {
            let candidate = result + String(scalar)
            if candidate.utf8.count > bytes { break }
            result = candidate
        }
        return result
    }
}
#endif
