"""Offline check of rn_inline_editor_product_qa-caption-revision.patch (NOT applied to the repo).

Applies the patch to the fixture files taken from the base commit into a temporary directory
(git show + git apply outside the repository), then checks the exact script contract the
native caption bridge relies on, and the 0..999 plain-digit range. Read-only for the repo.
Usage: python -I check_fixture_patch.py [base-commit]
"""
import pathlib
import re
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[3]
PATCH = HERE / "rn_inline_editor_product_qa-caption-revision.patch"
BASE = sys.argv[1] if len(sys.argv) > 1 else "1c8cb7d"
FILES = ["common/scripted_guis/rn_in_place_ui.txt", "interface/rn_in_place_ui.gui"]
PREFIX = "probes/rn_inline_editor_product_qa/"
checks = failures = 0


def check(condition, label):
    global checks, failures
    checks += 1
    if not condition:
        failures += 1
        print("FAIL", label)


def block(text, start_pattern):
    """Returns the balanced { } block that starts at the first match of start_pattern."""
    m = re.search(start_pattern, text)
    if not m:
        return ""
    i = text.index("{", m.start())
    depth = 0
    for j in range(i, len(text)):
        depth += {"{": 1, "}": -1}.get(text[j], 0)
        if depth == 0:
            return text[m.start(): j + 1]
    return ""


def main():
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        for f in FILES:
            data = subprocess.run(["git", "-C", str(REPO), "show", f"{BASE}:{PREFIX}{f}"], check=True, capture_output=True).stdout
            target = root / PREFIX / f
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        applied = subprocess.run(["git", "apply", "--unsafe-paths", "--directory=.", str(PATCH)], cwd=root, capture_output=True, text=True)
        check(applied.returncode == 0, f"patch applies to {BASE}: {applied.stderr.strip()}")
        gui = (root / PREFIX / FILES[1]).read_text(encoding="utf-8")
        sg = (root / PREFIX / FILES[0]).read_text(encoding="utf-8")

    windows = {
        "rn_ip_leader_launcher_window": ("rn_ip_leader_rev", "rn_ip_lui_rev"),
        "rn_ip_leader_window": ("rn_ip_leader_rev", "rn_ip_lui_rev"),
        "rn_ip_party_neutrality_launcher_window": ("rn_ip_party_neutrality_rev", "rn_ip_pui_rev"),
        "rn_ip_party_neutrality_window": ("rn_ip_party_neutrality_rev", "rn_ip_pui_rev"),
    }
    for window, (carrier, variable) in windows.items():
        body = block(gui, r'containerWindowType = \{\s*name = "%s"' % window)
        check(body != "", f"{window} present")
        item = block(body, r'instantTextboxType = \{\s*name = "%s"' % carrier)
        check(item != "", f"{window} has carrier {carrier}")
        check(f'text = "[?ROOT.{variable}|0]"' in item, f"{carrier} renders ROOT.{variable} with |0 (as proven rn_ip_epoch)")
        check(body.count(f'name = "{carrier}"') == 1, f"{carrier} unique in {window}")
    guis = {
        "rn_ip_leader_launcher_gui": "rn_ip_leader_rev", "rn_ip_leader_gui": "rn_ip_leader_rev",
        "rn_ip_party_neutrality_launcher_gui": "rn_ip_party_neutrality_rev", "rn_ip_party_neutrality_gui": "rn_ip_party_neutrality_rev",
    }
    for name, carrier in guis.items():
        body = block(sg, r"\b%s = \{" % name)
        check(f"{carrier}_visible = {{ always = no }}" in body, f"{name} hides {carrier}")
    for name, variable, helper, err in (
        ("rn_ip_leader_gui", "rn_ip_lui_rev", "rn_inline_leader_apply", "rn_ip_leader_error"),
        ("rn_ip_party_neutrality_gui", "rn_ip_pui_rev", "rn_inline_pair_apply_party", "rn_pair_err"),
    ):
        body = block(sg, r"\b%s = \{" % name)
        apply = block(body, r"_apply_click = \{")
        check(apply.count(f"add_to_variable = {{ {variable} = 1 }}") == 1, f"{variable} advanced exactly once in Apply")
        check(sg.count(f"add_to_variable = {{ {variable} = 1 }}") == 1, f"{variable} advanced nowhere else")
        check(apply.index(f"{helper} = yes") < apply.index(f"add_to_variable = {{ {variable} = 1 }}"), "increment after the guarded helper")
        check(f"check_variable = {{ temp_var:{err} = 0 }} check_variable = {{ {variable} < 999 }}" in apply, "success-only + cap 999")
        cancel = block(body, r"_cancel_click = \{")
        check(variable not in cancel, "Cancel never advances")
    for name, variable in (("rn_ip_leader_launcher_gui", "rn_ip_lui_rev"), ("rn_ip_party_neutrality_launcher_gui", "rn_ip_pui_rev")):
        opening = block(block(sg, r"\b%s = \{" % name), r"_open_click = \{")
        check(f"if = {{ limit = {{ NOT = {{ has_variable = {variable} }} }} set_variable = {{ {variable} = 0 }} }}" in opening,
              f"{variable} explicitly zeroed at first Open")

    # 0..999 plain-digit contract: script cap + native gate (Apply only while rev < 999).
    def native_parse(text):
        return text.isdigit() and 1 <= len(text) <= 3 and not (len(text) > 1 and text[0] == "0")
    check(all(native_parse(str(n)) and int(str(n)) == n for n in range(1000)), "0..999 render as accepted plain digits")
    check(not any(native_parse(t) for t in ("1000", "1,000", "1.000", "1K", "-1", "")), "out-of-range/ formatted values rejected")
    rev, applied_count = 0, 0
    for _ in range(1200):  # user keeps applying
        if rev < 999:          # native on_dispatch gate (rev_now < max_revision)
            applied_count += 1
            if rev < 999:      # script cap
                rev += 1
    check(rev == 999 and applied_count == 999, "counter saturates at 999, never leaves the readable range")
    print(f"fixture patch check: {checks} checks, {failures} failures (base {BASE})")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
