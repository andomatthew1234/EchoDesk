#if ECHODESK_DESKTOP
import AppKit

/// Equal-width native controls in reading order. Resizing changes only their
/// constraints: the controls (and their first responder and targets) survive.
final class EchoDeskFlowGrid: NSView {
    private let cells: [NSView]
    private let minimum: CGFloat
    private let limit: Int
    private let gap: CGFloat
    private var columns = 0
    private var rules: [NSLayoutConstraint] = []
    private var rowGuides: [NSLayoutGuide] = []

    init(views: [NSView], minimumCellWidth: CGFloat, maximumColumns: Int, spacing: CGFloat = 12) {
        cells = views
        minimum = max(1, minimumCellWidth)
        limit = max(1, maximumColumns)
        gap = max(0, spacing)
        super.init(frame: .zero)
        for cell in cells {
            cell.translatesAutoresizingMaskIntoConstraints = false
            addSubview(cell)
        }
        arrange(width: 0)
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) is not supported") }

    override func setFrameSize(_ newSize: NSSize) {
        super.setFrameSize(newSize)
        arrange(width: newSize.width)
    }

    private func arrange(width: CGFloat) {
        let count = max(1, min(limit, cells.count, Int((max(0, width) + gap) / (minimum + gap))))
        guard columns != count else { return }
        columns = count
        NSLayoutConstraint.deactivate(rules)
        rowGuides.forEach { removeLayoutGuide($0) }
        rules.removeAll()
        rowGuides.removeAll()
        for (index, cell) in cells.enumerated() {
            if index % count == 0 {
                let row = NSLayoutGuide()
                addLayoutGuide(row)
                rules += [row.leadingAnchor.constraint(equalTo: leadingAnchor),
                          row.trailingAnchor.constraint(equalTo: trailingAnchor)]
                if let previous = rowGuides.last {
                    rules.append(row.topAnchor.constraint(equalTo: previous.bottomAnchor, constant: gap))
                } else {
                    rules.append(row.topAnchor.constraint(equalTo: topAnchor))
                }
                rowGuides.append(row)
            }
            let row = rowGuides[index / count]
            let first = index % count == 0
            rules += [
                cell.leadingAnchor.constraint(equalTo: first ? leadingAnchor : cells[max(0, index - 1)].trailingAnchor,
                                              constant: first ? 0 : gap),
                cell.widthAnchor.constraint(equalTo: widthAnchor, multiplier: 1 / CGFloat(count),
                                            constant: -gap * CGFloat(count - 1) / CGFloat(count)),
                cell.topAnchor.constraint(equalTo: row.topAnchor),
                cell.bottomAnchor.constraint(equalTo: row.bottomAnchor)
            ]
        }
        rules.append(bottomAnchor.constraint(equalTo: rowGuides.last?.bottomAnchor ?? topAnchor))
        NSLayoutConstraint.activate(rules)
    }
}

/// Keep the document origin at its heading even when a page is shorter than
/// the viewport. Scrolling down then means the same thing on every page.
final class EchoDeskPageDocument: NSView {
    override var isFlipped: Bool { true }
}
#endif
