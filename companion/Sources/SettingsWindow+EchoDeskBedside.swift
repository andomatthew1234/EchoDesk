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

    static let defaults = EchoDeskBedsideConfig(
        clockFormat: "12h", clockShowSeconds: false, clockStyle: "large",
        nextAlarmWidget: true, weatherWidget: false,
        alarms: [], scheduleEnabled: true, manualBrightness: 40,
        segments: [
            .init(startMinute: 0, brightness: 1),
            .init(startMinute: 300, brightness: 40),
            .init(startMinute: 420, brightness: 70),
            .init(startMinute: 480, brightness: 100),
            .init(startMinute: 1080, brightness: 60),
            .init(startMinute: 1140, brightness: 40),
            .init(startMinute: 1230, brightness: 30),
            .init(startMinute: 1290, brightness: 1),
        ],
        nightScheduleEnabled: true, nightManualEnabled: false,
        nightStartMinute: 1140, nightEndMinute: 420,
        themeScheduleEnabled: true, themeManualDark: true,
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

    init(id: Int, enabled: NSButton, time: NSDatePicker, days: NSPopUpButton, label: NSTextField) {
        self.id = id; self.enabled = enabled; self.time = time; self.days = days; self.label = label
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

final class EchoDeskBedsidePreviewView: NSView {
    var config = EchoDeskBedsideConfig.defaults { didSet { needsDisplay = true } }

    override var intrinsicContentSize: NSSize { NSSize(width: 416, height: 234) }
    override var wantsUpdateLayer: Bool { false }

    override func draw(_ dirtyRect: NSRect) {
        super.draw(dirtyRect)
        let currentMinute = Calendar.current.component(.hour, from: Date()) * 60
            + Calendar.current.component(.minute, from: Date())
        let dark = config.themeScheduleEnabled
            ? isMinute(currentMinute, inside: config.themeStartMinute, and: config.themeEndMinute)
            : config.themeManualDark
        let background = dark ? NSColor(calibratedWhite: 0.035, alpha: 1) : NSColor(calibratedWhite: 0.96, alpha: 1)
        let foreground = dark ? NSColor.white : NSColor.black
        let path = NSBezierPath(roundedRect: bounds.insetBy(dx: 2, dy: 2), xRadius: 20, yRadius: 20)
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
        let clockFont = NSFont.monospacedDigitSystemFont(ofSize: config.clockStyle == "large" ? 56 : 42,
                                                         weight: config.clockStyle == "large" ? .light : .medium)
        let clockAttributes: [NSAttributedString.Key: Any] = [.font: clockFont, .foregroundColor: foreground]
        let clockSize = clock.size(withAttributes: clockAttributes)
        clock.draw(at: NSPoint(x: (bounds.width - clockSize.width) / 2,
                               y: bounds.height * 0.58), withAttributes: clockAttributes)

        let widgetY: CGFloat = 24
        var widgetX: CGFloat = 22
        if config.nextAlarmWidget {
            drawWidget(title: "NEXT ALARM", value: nextAlarmText(), x: widgetX, y: widgetY,
                       width: config.weatherWidget ? 178 : bounds.width - 44, foreground: foreground)
            widgetX += 190
        }
        if config.weatherWidget {
            drawWidget(title: "WEATHER", value: "Coming soon", x: widgetX, y: widgetY,
                       width: config.nextAlarmWidget ? 178 : bounds.width - 44, foreground: foreground)
        }
        if !config.nextAlarmWidget && !config.weatherWidget {
            let text = "No widgets selected"
            let attrs: [NSAttributedString.Key: Any] = [.font: NSFont.systemFont(ofSize: 13),
                                                        .foregroundColor: foreground.withAlphaComponent(0.55)]
            let size = text.size(withAttributes: attrs)
            text.draw(at: NSPoint(x: (bounds.width - size.width) / 2, y: 54), withAttributes: attrs)
        }
    }

    private func drawWidget(title: String, value: String, x: CGFloat, y: CGFloat,
                            width: CGFloat, foreground: NSColor) {
        let rect = NSRect(x: x, y: y, width: width, height: 62)
        foreground.withAlphaComponent(0.08).setFill()
        NSBezierPath(roundedRect: rect, xRadius: 12, yRadius: 12).fill()
        title.draw(at: NSPoint(x: x + 12, y: y + 38), withAttributes: [
            .font: NSFont.systemFont(ofSize: 9, weight: .semibold),
            .foregroundColor: foreground.withAlphaComponent(0.55),
        ])
        value.draw(at: NSPoint(x: x + 12, y: y + 13), withAttributes: [
            .font: NSFont.systemFont(ofSize: 16, weight: .medium), .foregroundColor: foreground,
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
    }

    private func bedsideHeader(_ title: String, _ subtitle: String, root: Bool = false) -> NSView {
        let back = NSButton(image: NSImage(systemSymbolName: "chevron.left", accessibilityDescription: "Back")!,
                            target: self,
                            action: root ? #selector(echoDeskBedsideBackToCustomisation) : #selector(echoDeskBedsideShowRoot))
        back.bezelStyle = .texturedRounded
        back.controlSize = .large
        let heading = bedsideLabel(title, size: 29, weight: .semibold)
        let detail = bedsideMuted(subtitle)
        let text = bedsideStack([heading, detail], spacing: 3)
        let row = NSStackView(views: [back, text, NSView()])
        row.orientation = .horizontal; row.alignment = .centerY; row.spacing = 14
        return row
    }

    private func buildEchoDeskBedsideRootSection() -> [NSView] {
        let preview = EchoDeskBedsidePreviewView()
        preview.config = echoDeskBedsideConfig
        preview.translatesAutoresizingMaskIntoConstraints = false
        preview.widthAnchor.constraint(equalToConstant: 416).isActive = true
        preview.heightAnchor.constraint(equalToConstant: 234).isActive = true
        echoDeskBedsidePreview = preview

        echoDeskBedsideStatus = bedsideMuted(bedsideStatusText)
        let open = bedsidePrimaryButton("Open on EchoDesk", #selector(echoDeskBedsideOpenOnDevice))
        open.isEnabled = bedsideCapabilityAvailable
        let intro = bedsideCard(bedsideStack([
            bedsideLabel("Bedside Mode", size: 22, weight: .semibold),
            bedsideMuted("A calm, standalone clock with alarms and a display schedule separate from the rest of EchoDesk OS."),
            echoDeskBedsideStatus!, open,
        ], spacing: 12))
        let hero = NSStackView(views: [preview, intro])
        hero.orientation = .horizontal; hero.alignment = .top; hero.spacing = 16
        hero.distribution = .fill

        echoDeskBedsideClockFormat = bedsidePopup([("12-hour", "12h"), ("24-hour", "24h")],
                                                   selected: echoDeskBedsideConfig.clockFormat,
                                                   action: #selector(echoDeskBedsidePreviewChanged))
        echoDeskBedsideClockStyle = bedsidePopup([("Large", "large"), ("Minimal", "minimal")],
                                                  selected: echoDeskBedsideConfig.clockStyle,
                                                  action: #selector(echoDeskBedsidePreviewChanged))
        echoDeskBedsideShowSeconds = NSButton(checkboxWithTitle: "Show seconds", target: self,
                                               action: #selector(echoDeskBedsidePreviewChanged))
        echoDeskBedsideShowSeconds?.state = echoDeskBedsideConfig.clockShowSeconds ? .on : .off
        let saveClock = bedsidePrimaryButton("Save Clock", #selector(echoDeskBedsideSaveClock))
        saveClock.isEnabled = bedsideStateReady
        let clock = bedsideCard(bedsideStack([
            bedsideSectionTitle("Clock"),
            bedsideMuted("Choose the clock appearance used only inside Bedside Mode."),
            bedsideControlRow("Time format", echoDeskBedsideClockFormat!),
            bedsideControlRow("Appearance", echoDeskBedsideClockStyle!),
            echoDeskBedsideShowSeconds!, saveClock,
        ], spacing: 11))

        echoDeskBedsideNextAlarmWidget = NSButton(checkboxWithTitle: "Next alarm", target: self,
                                                   action: #selector(echoDeskBedsidePreviewChanged))
        echoDeskBedsideNextAlarmWidget?.state = echoDeskBedsideConfig.nextAlarmWidget ? .on : .off
        echoDeskBedsideWeatherWidget = NSButton(checkboxWithTitle: "Weather — Coming soon", target: nil, action: nil)
        echoDeskBedsideWeatherWidget?.state = .off
        echoDeskBedsideWeatherWidget?.isEnabled = false
        let saveWidgets = bedsidePrimaryButton("Save Widgets", #selector(echoDeskBedsideSaveWidgets))
        saveWidgets.isEnabled = bedsideStateReady
        let widgets = bedsideCard(bedsideStack([
            bedsideSectionTitle("Widgets"),
            bedsideMuted("Widgets live in the lower half. Weather is designed here but intentionally unavailable until its data source ships."),
            echoDeskBedsideNextAlarmWidget!, echoDeskBedsideWeatherWidget!, saveWidgets,
        ], spacing: 11))
        let choices = NSStackView(views: [clock, widgets])
        choices.orientation = .horizontal; choices.alignment = .top; choices.distribution = .fillEqually; choices.spacing = 16

        let alarms = bedsideNavigationCard("Manage alarms", "Create up to eight repeating alarms stored on EchoDesk.",
                                           "alarm.fill", #selector(echoDeskBedsideShowAlarms))
        let display = bedsideNavigationCard("Auto-brightness and display", "Control brightness, Night Shift and appearance schedules.",
                                            "sun.max.fill", #selector(echoDeskBedsideShowDisplay))
        let navigation = NSStackView(views: [alarms, display])
        navigation.orientation = .horizontal; navigation.alignment = .top
        navigation.distribution = .fillEqually; navigation.spacing = 16
        return [bedsideHeader("Bedside Mode", "Designed for power-only use after setup.", root: true), hero, choices, navigation]
    }

    private func buildEchoDeskBedsideAlarmsSection() -> [NSView] {
        echoDeskBedsideAlarmRows.removeAll()
        echoDeskBedsideStatus = bedsideMuted(bedsideStatusText)
        var views: [NSView] = [bedsideHeader("Manage alarms", "Alarms are stored and evaluated directly on EchoDesk.")]
        if echoDeskBedsideAlarmDrafts.isEmpty {
            views.append(bedsideCard(bedsideStack([
                bedsideSectionTitle("No alarms yet"),
                bedsideMuted("Add an alarm. It will work whenever EchoDesk has power and valid network time."),
            ])))
        } else {
            for alarm in echoDeskBedsideAlarmDrafts.sorted(by: { $0.id < $1.id }) {
                views.append(buildEchoDeskBedsideAlarmRow(alarm))
            }
        }
        let add = NSButton(title: "Add alarm", target: self, action: #selector(echoDeskBedsideAddAlarm))
        add.image = NSImage(systemSymbolName: "plus", accessibilityDescription: nil)
        add.isEnabled = echoDeskBedsideAlarmDrafts.count < 8
        let save = bedsidePrimaryButton("Save alarms", #selector(echoDeskBedsideSaveAlarms))
        save.isEnabled = bedsideStateReady
        let actions = NSStackView(views: [add, NSView(), save])
        actions.orientation = .horizontal; actions.alignment = .centerY
        views.append(echoDeskBedsideStatus!)
        views.append(actions)
        return views
    }

    private func buildEchoDeskBedsideAlarmRow(_ alarm: EchoDeskBedsideAlarm) -> NSView {
        let enabled = NSButton(checkboxWithTitle: "Enabled", target: nil, action: nil)
        enabled.state = alarm.enabled ? .on : .off
        let time = bedsideTimePicker(alarm.minuteOfDay)
        var repeatChoices = [("Every day", "127"), ("Weekdays", "62"), ("Weekends", "65")]
        if ![127, 62, 65].contains(alarm.weekdays) {
            repeatChoices.append(("Custom days (\(alarm.weekdays))", String(alarm.weekdays)))
        }
        let days = bedsidePopup(repeatChoices, selected: String(alarm.weekdays), action: nil)
        let label = NSTextField(string: alarm.label)
        label.placeholderString = "Alarm label"
        let remove = NSButton(title: "Delete", target: self, action: #selector(echoDeskBedsideDeleteAlarm(_:)))
        remove.tag = alarm.id; remove.contentTintColor = .systemRed
        let top = NSStackView(views: [bedsideSectionTitle(alarm.label.isEmpty ? "Alarm \(alarm.id)" : alarm.label), NSView(), enabled, remove])
        top.orientation = .horizontal; top.alignment = .centerY; top.spacing = 10
        let controls = NSStackView(views: [bedsideControlRow("Time", time), bedsideControlRow("Repeats", days),
                                           bedsideControlRow("Label", label)])
        controls.orientation = .vertical; controls.alignment = .leading; controls.spacing = 9
        echoDeskBedsideAlarmRows.append(.init(id: alarm.id, enabled: enabled, time: time, days: days, label: label))
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
    }

    @objc private func echoDeskBedsideDeleteAlarm(_ sender: NSButton) {
        captureEchoDeskBedsideAlarmDrafts()
        echoDeskBedsideAlarmDrafts.removeAll { $0.id == sender.tag }
        renderEchoDeskBedsideSection("alarms")
    }

    private func buildEchoDeskBedsideDisplaySection() -> [NSView] {
        echoDeskBedsideScheduleRows.removeAll()
        echoDeskBedsideStatus = bedsideMuted(bedsideStatusText)
        echoDeskBedsideScheduleEnabled = NSButton(checkboxWithTitle: "Use brightness schedule", target: nil, action: nil)
        echoDeskBedsideScheduleEnabled?.state = echoDeskBedsideConfig.scheduleEnabled ? .on : .off
        echoDeskBedsideManualBrightness = NSSlider(value: Double(echoDeskBedsideConfig.manualBrightness),
                                                    minValue: 1, maxValue: 100, target: nil, action: nil)
        var scheduleViews: [NSView] = [bedsideSectionTitle("Auto-brightness"),
                                      bedsideMuted("The schedule repeats every day and affects Bedside Mode only."),
                                      echoDeskBedsideScheduleEnabled!,
                                      bedsideControlRow("Manual brightness", echoDeskBedsideManualBrightness!)]
        for (index, segment) in echoDeskBedsideScheduleDrafts.enumerated() {
            let time = bedsideTimePicker(segment.startMinute)
            let brightness = NSTextField(string: String(segment.brightness))
            brightness.alignment = .right
            brightness.placeholderString = "1–100"
            echoDeskBedsideScheduleRows.append(.init(index: index, time: time, brightness: brightness))
            let remove = NSButton(image: NSImage(systemSymbolName: "minus.circle", accessibilityDescription: "Remove time")!,
                                  target: self, action: #selector(echoDeskBedsideDeleteSchedule(_:)))
            remove.tag = index; remove.isBordered = false
            remove.contentTintColor = .systemRed
            remove.isEnabled = echoDeskBedsideScheduleDrafts.count > 1
            let row = NSStackView(views: [bedsideControlRow("From", time),
                                          bedsideControlRow("Brightness %", brightness), remove])
            row.orientation = .horizontal; row.alignment = .centerY; row.distribution = .fillEqually; row.spacing = 12
            scheduleViews.append(row)
        }
        let addTime = NSButton(title: "Add time", target: self, action: #selector(echoDeskBedsideAddSchedule))
        addTime.image = NSImage(systemSymbolName: "plus", accessibilityDescription: nil)
        addTime.isEnabled = echoDeskBedsideScheduleDrafts.count < 8
        scheduleViews.append(addTime)
        let schedule = bedsideCard(bedsideStack(scheduleViews, spacing: 10))

        echoDeskBedsideNightScheduleEnabled = NSButton(checkboxWithTitle: "Use Night Shift schedule", target: nil, action: nil)
        echoDeskBedsideNightScheduleEnabled?.state = echoDeskBedsideConfig.nightScheduleEnabled ? .on : .off
        echoDeskBedsideNightManual = NSButton(checkboxWithTitle: "Night Shift on manually", target: nil, action: nil)
        echoDeskBedsideNightManual?.state = echoDeskBedsideConfig.nightManualEnabled ? .on : .off
        echoDeskBedsideNightStart = bedsideTimePicker(echoDeskBedsideConfig.nightStartMinute)
        echoDeskBedsideNightEnd = bedsideTimePicker(echoDeskBedsideConfig.nightEndMinute)
        let nightTimes = NSStackView(views: [bedsideControlRow("From", echoDeskBedsideNightStart!),
                                             bedsideControlRow("Until", echoDeskBedsideNightEnd!)])
        nightTimes.orientation = .horizontal; nightTimes.distribution = .fillEqually; nightTimes.spacing = 12
        let night = bedsideCard(bedsideStack([
            bedsideSectionTitle("Night Shift"), bedsideMuted("Warms the display for a calmer bedside appearance."),
            echoDeskBedsideNightScheduleEnabled!, echoDeskBedsideNightManual!, nightTimes,
        ], spacing: 10))

        echoDeskBedsideThemeScheduleEnabled = NSButton(checkboxWithTitle: "Use appearance schedule", target: nil, action: nil)
        echoDeskBedsideThemeScheduleEnabled?.state = echoDeskBedsideConfig.themeScheduleEnabled ? .on : .off
        echoDeskBedsideThemeManualDark = NSButton(checkboxWithTitle: "Dark appearance manually", target: nil, action: nil)
        echoDeskBedsideThemeManualDark?.state = echoDeskBedsideConfig.themeManualDark ? .on : .off
        echoDeskBedsideThemeStart = bedsideTimePicker(echoDeskBedsideConfig.themeStartMinute)
        echoDeskBedsideThemeEnd = bedsideTimePicker(echoDeskBedsideConfig.themeEndMinute)
        let themeTimes = NSStackView(views: [bedsideControlRow("Dark from", echoDeskBedsideThemeStart!),
                                             bedsideControlRow("Light from", echoDeskBedsideThemeEnd!)])
        themeTimes.orientation = .horizontal; themeTimes.distribution = .fillEqually; themeTimes.spacing = 12
        let theme = bedsideCard(bedsideStack([
            bedsideSectionTitle("Dark and Light appearance"),
            bedsideMuted("Bedside appearance is separate from the rest of EchoDesk OS."),
            echoDeskBedsideThemeScheduleEnabled!, echoDeskBedsideThemeManualDark!, themeTimes,
        ], spacing: 10))
        let save = bedsidePrimaryButton("Save display settings", #selector(echoDeskBedsideSaveDisplay))
        save.isEnabled = bedsideStateReady
        let actions = NSStackView(views: [NSView(), save]); actions.orientation = .horizontal
        return [bedsideHeader("Auto-brightness and display", "Schedules repeat locally without EchoDesk Desktop."),
                schedule, night, theme, echoDeskBedsideStatus!, actions]
    }

    @objc private func echoDeskBedsideAddSchedule() {
        captureEchoDeskBedsideScheduleDrafts()
        guard echoDeskBedsideScheduleDrafts.count < 8 else { return }
        let used = Set(echoDeskBedsideScheduleDrafts.map(\.startMinute))
        let minute = stride(from: 0, through: 1380, by: 60).first(where: { !used.contains($0) }) ?? 0
        echoDeskBedsideScheduleDrafts.append(.init(startMinute: minute, brightness: 40))
        echoDeskBedsideScheduleDrafts.sort { $0.startMinute < $1.startMinute }
        renderEchoDeskBedsideSection("display")
    }

    @objc private func echoDeskBedsideDeleteSchedule(_ sender: NSButton) {
        captureEchoDeskBedsideScheduleDrafts()
        guard echoDeskBedsideScheduleDrafts.count > 1,
              echoDeskBedsideScheduleDrafts.indices.contains(sender.tag) else { return }
        echoDeskBedsideScheduleDrafts.remove(at: sender.tag)
        renderEchoDeskBedsideSection("display")
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

    func requestEchoDeskBedsideState() {
        guard !echoDeskBedsideStateRequestInFlight else { return }
        guard bedsideCapabilityAvailable, let monitor else {
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
            if self.echoDeskBedsideSection == "alarms" { self.echoDeskBedsideAlarmDrafts = config.alarms }
            if self.echoDeskBedsideSection == "display" { self.echoDeskBedsideScheduleDrafts = config.segments }
            self.renderEchoDeskBedsideSection(self.echoDeskBedsideSection)
        }
    }

    private func sendEchoDeskBedsideConfig(_ config: EchoDeskBedsideConfig) {
        guard !echoDeskBedsideSaveInFlight, bedsideStateReady, let monitor else {
            echoDeskBedsideStatus?.stringValue = "Wait for EchoDesk to finish loading before saving."
            return
        }
        echoDeskBedsideSaveInFlight = true
        echoDeskBedsideStatus?.stringValue = "Saving to EchoDesk…"
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
            guard ok else {
                self.echoDeskBedsideStatus?.stringValue = "EchoDesk did not confirm the change. Nothing was saved."
                self.echoDeskBedsideStatePort = nil
                self.requestEchoDeskBedsideState()
                return
            }
            self.echoDeskBedsideStatus?.stringValue = "Saved on EchoDesk. It can now run from power alone."
            self.echoDeskBedsideStatePort = nil
            self.requestEchoDeskBedsideState()
        }
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
        var preview = echoDeskBedsideConfig
        if let value = echoDeskBedsideClockFormat?.selectedItem?.representedObject as? String { preview.clockFormat = value }
        if let value = echoDeskBedsideClockStyle?.selectedItem?.representedObject as? String { preview.clockStyle = value }
        preview.clockShowSeconds = echoDeskBedsideShowSeconds?.state == .on
        preview.nextAlarmWidget = echoDeskBedsideNextAlarmWidget?.state == .on
        echoDeskBedsidePreview?.config = preview
    }

    @objc private func echoDeskBedsideSaveClock() {
        var next = echoDeskBedsideConfig
        next.clockFormat = echoDeskBedsideClockFormat?.selectedItem?.representedObject as? String ?? next.clockFormat
        next.clockStyle = echoDeskBedsideClockStyle?.selectedItem?.representedObject as? String ?? next.clockStyle
        next.clockShowSeconds = echoDeskBedsideShowSeconds?.state == .on
        sendEchoDeskBedsideConfig(next)
    }

    @objc private func echoDeskBedsideSaveWidgets() {
        var next = echoDeskBedsideConfig
        next.nextAlarmWidget = echoDeskBedsideNextAlarmWidget?.state == .on
        // Weather remains device-schema-ready but cannot be enabled before its data source ships.
        next.weatherWidget = false
        sendEchoDeskBedsideConfig(next)
    }

    @objc private func echoDeskBedsideSaveAlarms() {
        var next = echoDeskBedsideConfig
        captureEchoDeskBedsideAlarmDrafts()
        next.alarms = echoDeskBedsideAlarmDrafts.sorted { $0.id < $1.id }
        sendEchoDeskBedsideConfig(next)
    }

    @objc private func echoDeskBedsideSaveDisplay() {
        var next = echoDeskBedsideConfig
        next.scheduleEnabled = echoDeskBedsideScheduleEnabled?.state == .on
        next.manualBrightness = min(100, max(1, Int(echoDeskBedsideManualBrightness?.doubleValue ?? 40)))
        captureEchoDeskBedsideScheduleDrafts()
        next.segments = echoDeskBedsideScheduleDrafts.sorted { $0.startMinute < $1.startMinute }
        // The device rejects duplicate start times; explain instead of silently overwriting.
        guard Set(next.segments.map(\.startMinute)).count == next.segments.count else {
            echoDeskFooterStatus?.stringValue = "Brightness schedule times must be unique."
            NSSound.beep(); return
        }
        next.nightScheduleEnabled = echoDeskBedsideNightScheduleEnabled?.state == .on
        next.nightManualEnabled = echoDeskBedsideNightManual?.state == .on
        next.nightStartMinute = bedsideMinute(echoDeskBedsideNightStart?.dateValue ?? Date())
        next.nightEndMinute = bedsideMinute(echoDeskBedsideNightEnd?.dateValue ?? Date())
        next.themeScheduleEnabled = echoDeskBedsideThemeScheduleEnabled?.state == .on
        next.themeManualDark = echoDeskBedsideThemeManualDark?.state == .on
        next.themeStartMinute = bedsideMinute(echoDeskBedsideThemeStart?.dateValue ?? Date())
        next.themeEndMinute = bedsideMinute(echoDeskBedsideThemeEnd?.dateValue ?? Date())
        sendEchoDeskBedsideConfig(next)
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

    private func bedsideStack(_ views: [NSView], spacing: CGFloat = 14) -> NSStackView {
        let stack = NSStackView(views: views)
        stack.orientation = .vertical; stack.alignment = .leading; stack.spacing = spacing
        return stack
    }

    private func bedsideCard(_ content: NSView) -> NSView {
        let card = NSBox(); card.boxType = .custom; card.borderType = .lineBorder
        card.borderColor = .separatorColor; card.borderWidth = 1; card.cornerRadius = 14
        card.fillColor = .controlBackgroundColor; card.contentViewMargins = NSSize(width: 18, height: 18)
        card.contentView = content
        return card
    }

    private func bedsideLabel(_ text: String, size: CGFloat, weight: NSFont.Weight) -> NSTextField {
        let label = NSTextField(labelWithString: text)
        label.font = .systemFont(ofSize: size, weight: weight)
        label.maximumNumberOfLines = 0
        return label
    }

    private func bedsideSectionTitle(_ text: String) -> NSTextField { bedsideLabel(text, size: 18, weight: .semibold) }
    private func bedsideMuted(_ text: String) -> NSTextField {
        let label = bedsideLabel(text, size: 13, weight: .regular)
        label.textColor = .secondaryLabelColor; label.maximumNumberOfLines = 0
        return label
    }

    private func bedsidePrimaryButton(_ title: String, _ action: Selector) -> NSButton {
        let button = NSButton(title: title, target: self, action: action)
        button.bezelStyle = .rounded; button.controlSize = .large
        button.keyEquivalent = ""
        button.contentTintColor = .controlAccentColor
        return button
    }

    private func bedsideNavigationCard(_ title: String, _ detail: String, _ symbol: String,
                                       _ action: Selector) -> NSView {
        let icon = NSImageView()
        icon.image = NSImage(systemSymbolName: symbol, accessibilityDescription: nil)
        icon.symbolConfiguration = NSImage.SymbolConfiguration(pointSize: 26, weight: .medium)
        icon.contentTintColor = .controlAccentColor
        let button = bedsidePrimaryButton("Open…", action)
        return bedsideCard(bedsideStack([icon, bedsideSectionTitle(title), bedsideMuted(detail), button], spacing: 9))
    }

    private func bedsideControlRow(_ title: String, _ control: NSView) -> NSView {
        let label = bedsideLabel(title, size: 13, weight: .medium)
        label.translatesAutoresizingMaskIntoConstraints = false
        label.widthAnchor.constraint(equalToConstant: 145).isActive = true
        let row = NSStackView(views: [label, control])
        row.orientation = .horizontal; row.alignment = .centerY; row.spacing = 12
        return row
    }

    private func bedsidePopup(_ options: [(String, String)], selected: String,
                              action: Selector?) -> NSPopUpButton {
        let popup = NSPopUpButton(frame: .zero, pullsDown: false)
        popup.bezelStyle = .rounded; popup.controlSize = .large
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
