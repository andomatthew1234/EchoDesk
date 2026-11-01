#!/usr/bin/env python3
"""Static guardrails for the 320x240 EchoDesk Mini touch UI.

These assertions intentionally cover source-level interaction guarantees that
are difficult to exercise in the native model tests: the home/utility surfaces
must not require vertical swipes and utility icons must be drawn components,
not temporary ASCII labels.
"""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]


def source(name: str) -> str:
    return (ROOT / "src" / name).read_text(encoding="utf-8")


def function_body(text: str, signature: str) -> str:
    start = text.index(signature)
    brace = text.index("{", start)
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[brace + 1 : index]
    raise AssertionError(f"unterminated function: {signature}")


def assert_no_vertical_scroll(name: str, text: str) -> None:
    assert "LV_DIR_VER" not in text, f"{name} still requires vertical swiping"


home = source("ui_echodesk.cpp")
home_body = function_body(home, "lv_obj_t *create_home_screen()")
assert_no_vertical_scroll("EchoDesk home", home_body)
card_body = function_body(home, "void add_home_card(")
assert "create_utility_icon" in card_body, "home cards do not use custom utility icons"
for placeholder in ('"C"', '"12"', '"^"', '"i"', '"*"', '"PC"'):
    assert not re.search(rf"add_home_card\([^;]*{re.escape(placeholder)}", home_body), (
        f"placeholder utility glyph remains: {placeholder}"
    )

for filename, label in (
    ("ui_pc_dashboard.cpp", "PC Dashboard"),
    ("ui_my_device.cpp", "My Device"),
    ("ui_notification_stack.cpp", "Notification Stack"),
    ("ui_bedside.cpp", "Bedside Mode"),
):
    assert_no_vertical_scroll(label, source(filename))

dashboard = source("ui_pc_dashboard.cpp")
assert "pc_dashboard_tile_geometry" in dashboard, "dashboard does not use adaptive geometry"

bedside = source("ui_bedside.cpp")
bedside_create = function_body(bedside, "lv_obj_t *ui_bedside_create()")
assert "lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE)" in bedside_create, (
    "Bedside root must never require scrolling"
)
assert "tap_layer" in bedside_create and "LV_OBJ_FLAG_CLICKABLE" in bedside_create, (
    "Bedside screen-wide wake/control touch target is missing"
)
for action in ("CANCEL\\nTOMORROW", "DISPLAY\\nOFF", "EXIT\\nBEDSIDE", "STOP ALARM"):
    assert action in bedside_create, f"Bedside touch action missing: {action}"
assert "case ECHO_BEDSIDE" in function_body(home, "void create_utility_icon("), (
    "Bedside does not have a custom home icon"
)

print("EchoDesk touch UI validation passed")
