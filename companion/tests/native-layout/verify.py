"""Check the geometry of *settled native windows*, not their requested frames."""
import argparse
import json
from pathlib import Path


def verify(root):
    errors = []
    for width, height in [(900, 650), (1100, 750), (1280, 800), (1440, 900)]:
        for style in ("light", "dark"):
            folder = root / f"{width}x{height}" / style
            records = json.loads((folder / "geometry.json").read_text())
            for name, record in records.items():
                if (name.startswith(("clock-", "confirm-")) or name == "firmware-confirmation"):
                    continue  # Sheets have a content-dependent fitting size.
                here = f"{folder.relative_to(root)}/{name}"
                if (record["windowWidth"], record["windowHeight"]) != (width, height):
                    errors.append(f"{here}: window settled at {record['windowWidth']} × {record['windowHeight']}")
                if not (folder / (name + ".png")).is_file():
                    errors.append(f"{here}: missing screenshot")
                if layout := record.get("pageLayout"):
                    side = layout["sidebar"]
                    left = side["x"] + side["width"]
                    page, scroll, clip = (layout[k] for k in ("page", "scroll", "clip"))
                    if abs(scroll["x"] - left) > 1 or abs(scroll["width"] + left - width) > 1:
                        errors.append(f"{here}: viewport does not span the workspace")
                    if abs(page["x"] - clip["x"] - 24) > 1 or abs(page["width"] - clip["width"] + 48) > 1:
                        errors.append(f"{here}: page does not use its viewport")
            log = (folder / "app.log").read_text(errors="replace")
            if "Unable to simultaneously satisfy constraints" in log:
                errors.append(f"{folder.relative_to(root)}: AppKit reported conflicting constraints")
    if errors:
        raise SystemExit("\n".join(errors[:24]) + f"\n{len(errors)} layout errors")
    print("All eight native size/appearance combinations held their requested window and content bounds.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifacts", type=Path)
    verify(parser.parse_args().artifacts)
