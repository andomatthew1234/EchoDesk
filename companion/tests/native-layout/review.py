"""Build local contact sheets from the native renderer's PNG artifacts (Pillow)."""
import argparse
from pathlib import Path
from PIL import Image, ImageDraw

SECTIONS = {
    "workspaces": ["overview", "overview-lower", "device-control", "device-maintenance",
                   "customisation", "bedside", "setup-connect", "recovery"],
    "settings": ["settings-display", "settings-bedside", "settings-clock", "settings-nowPlaying",
                 "settings-codex", "settings-performance", "settings-pcDashboard", "settings-preferences"],
    "bedside-recovery": ["bedside", "bedside-alarms", "bedside-display", "bedside-display-lower",
                         "recovery", "recovery-maintenance", "recovery-complete", "firmware-confirmation"],
    "setup": ["first-launch", "first-device", "setup-connect", "setup-usb", "setup-wifi",
              "setup-install-choice", "setup-install-progress", "setup-quick-settings", "setup-ready"],
    "dialogs": ["confirm-restart", "confirm-reset", "clock-chooser", "clock-stopwatch",
                "clock-countdown", "clock-focus", "firmware-confirmation"],
}


def sheet(entries, dest, columns, thumb):
    w, h = thumb
    canvas = Image.new("RGB", ((w+12)*columns, (h+36)*((len(entries)+columns-1)//columns)), "#263241")
    pen = ImageDraw.Draw(canvas)
    for index, (label, path) in enumerate(entries):
        x = 6 + (index % columns) * (w+12)
        y = 6 + (index // columns) * (h+36)
        pen.text((x, y), label, fill="white")
        if path.is_file():
            with Image.open(path) as im:
                im.thumbnail(thumb)
                canvas.paste(im.convert("RGB"), (x, y+19))
    dest.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(dest)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("captures", type=Path)
    p.add_argument("destination", type=Path)
    p.add_argument("--before", type=Path, help="Add two-column before/after sheets.")
    args = p.parse_args()
    for size in ["900x650", "1100x750", "1280x800", "1440x900"]:
        for theme in ["light", "dark"]:
            rel = Path(size) / theme
            for group, pages in SECTIONS.items():
                entries = [(page, args.captures / rel / (page + ".png")) for page in pages]
                file = args.destination / rel / (group + ".png")
                sheet(entries, file, columns=2, thumb=(480, 342))
                if args.before:
                    paired = []
                    for page in pages:
                        paired.extend([("Before · " + page, args.before / rel / (page + ".png")),
                                       ("After · " + page, args.captures / rel / (page + ".png"))])
                    sheet(paired, file.with_name(group + "-comparison.png"), 2, (600, 420))


if __name__ == "__main__":
    main()
