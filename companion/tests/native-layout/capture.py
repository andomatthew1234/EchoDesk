"""Run the native EchoDesk window on macOS. No production source is modified."""
import argparse
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
COMPANION = HERE.parents[1]
SIZES = ("900x650", "1100x750", "1280x800", "1440x900")


def prepare(temp):
    sources = temp / "Sources"
    shutil.copytree(COMPANION / "Sources", sources)
    main = sources / "main.swift"
    entry = "let delegate = AppDelegate()"
    code = main.read_text()
    if code.count(entry) != 1:
        raise RuntimeError("EchoDesk launch entry changed; inspect before capturing")
    main.write_text(code.replace(entry, "let delegate = LayoutCaptureDelegate()"))
    # Append the driver in the same file solely to exercise private presentation
    # entry points. Release code never contains these entry points or fixtures.
    ui = sources / "SettingsWindow+EchoDeskDesktop.swift"
    source = ui.read_text()
    # The observer lives in this same temporary Swift file; don't expose any
    # new framework API just to inspect private page and modal constructors.
    for name in ("echoDeskFirstRunContinue", "echoDeskFirstRunNotNow", "echoDeskRestart",
                 "echoDeskFactoryReset", "echoDeskOpenCustomisationSection",
                 "echoDeskCloseCustomisationSection", "presentEchoDeskClockModeChooser",
                 "presentEchoDeskClockConfiguration", "closeEchoDeskClockWindow"):
        source = source.replace(f"private func {name}(", f"fileprivate func {name}(")
    ui.write_text(source)
    # A hosted Mac can have a 1024x768 virtual screen. Override just the
    # WindowServer's "fit on this monitor" placement policy in the review build;
    # otherwise a requested 1440x900 window silently shrinks to 1024x677.
    # Every content view, sheet, toolbar and real size-dependent constraint
    # continues to be the one from the selected revision.
    settings = sources / "SettingsWindow.swift"
    shell = settings.read_text()
    marker = "convenience init() {\n        let window = NSWindow("
    if shell.count(marker) != 1:
        raise RuntimeError("Main NSWindow initializer changed; inspect the capture harness")
    settings.write_text(shell.replace(marker, marker.replace("= NSWindow(", "= ReviewWindow(")))
    with ui.open("a") as f:
        f.write("\n" + (HERE / "LayoutCapture.swift").read_text())

    bundle = temp / "EchoDesk Layout Review.app" / "Contents"
    (bundle / "MacOS").mkdir(parents=True)
    (bundle / "Resources").mkdir()
    info = plistlib.loads((COMPANION / "Resources/EchoDesk-Info.plist").read_bytes())
    info.update(CFBundleIdentifier="com.echodesk.visual-review", CFBundleExecutable="LayoutReview",
                CFBundleName="EchoDesk Desktop")
    (bundle / "Info.plist").write_bytes(plistlib.dumps(info))
    for folder in (COMPANION / "Resources").glob("*.lproj"):
        shutil.copytree(folder, bundle / "Resources" / folder.name)
    exe = bundle / "MacOS/LayoutReview"
    cmd = ["xcrun", "swiftc", "-D", "ECHODESK_DESKTOP", "-target",
           f"{os.uname().machine}-apple-macosx14.0"]
    for framework in ("Cocoa", "Security", "Network", "ServiceManagement", "IOKit", "CoreAudio"):
        cmd.extend(["-framework", framework])
    subprocess.run([*cmd, *sorted(sources.glob("*.swift")), "-o", exe], check=True)
    return exe


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", required=True, type=Path)
    args = p.parse_args()
    output = args.output.resolve()
    output.mkdir(exist_ok=True, parents=True)
    with tempfile.TemporaryDirectory() as temp:
        temp = Path(temp)
        exe = prepare(temp)
        for size in SIZES:
            for theme in ("light", "dark"):
                dest = output / size / theme
                dest.mkdir(exist_ok=True, parents=True)
                user_dir = temp / size / theme
                user_dir.mkdir(parents=True)
                # Do not read or modify personal application or user preferences.
                env = dict(os.environ, HOME=str(user_dir), CFFIXED_USER_HOME=str(user_dir),
                           ECHODESK_VISUAL_OUTPUT=str(dest), ECHODESK_VISUAL_SIZE=size,
                           ECHODESK_VISUAL_THEME=theme, TZ="UTC")
                print(f"Capturing {size} ({theme})", flush=True)
                with (dest / "app.log").open("w") as log:
                    try:
                        subprocess.run([exe], check=True, env=env, timeout=180,
                                       stdout=log, stderr=subprocess.STDOUT)
                    finally:
                        log.flush()
                        if not (dest / "geometry.json").exists():
                            print((dest / "app.log").read_text(errors="replace")[-8000:], flush=True)
    print("Native capture matrix complete:", output)


if __name__ == "__main__":
    main()
