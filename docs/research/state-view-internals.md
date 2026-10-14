# Selected-state scripted GUI: static findings

Date: 2026-10-11. This investigation was read-only, with no game input, process attachment, injection, configuration change, or rebuild. It used the existing Ghidra project with `-noanalysis -readOnly`, plus raw PE/Capstone checks. These findings establish native control flow; they do not establish that a proposed layout works at runtime.

Target: Windows x64 HOI4 1.19.3.0, `hoi4.exe` SHA256 `7DC947BE34970DA1E1C787BCDDBE7F62B610FF258AEBF8F06AA8B348F3A031D5`, image base `0x140000000`. Addresses below are RVAs. Function names in the evidence are analyst aliases, not recovered symbols.

## Result relevant to the reported bug

`selected_state_context` supplies a **state** as the current scope. `ROOT` is the player country. The local vanilla documentation's sentence that this context supplies a "target country" is inconsistent with the native state-scope setter and appears to be a documentation typo.

Selecting this context does **not** automatically attach the scripted window to the state panel. A missing `parent_window_token` leaves the window attached to the main GUI. Explicitly using `parent_window_token = selected_state_view` both attaches it to the state panel and enables the native path that takes the selected state directly from that panel.

The existing `visible = { is_owned_by = ROOT }` matches those scopes. There is no static basis for replacing it with `FROM`. The conservative candidate fix is the explicit parent token plus coordinates relative to the actual parent. The reported symptom still needs runtime confirmation after that patch.

## Context and scope evidence

| Item | Native evidence | Meaning |
|---|---|---|
| Context enum | Token registration; template parser `0x159ECB0` | `selected_state_context = 0x3A72`; `player_context = 0x3A70` |
| Defaults | Template constructor `0x159D510` writes qword `0x3A70` at `+0x28` | Context DWORD `+0x28` is player; parent-token DWORD `+0x2C` is zero |
| Parsing | `0x159ECB0`, `context_type` case `0x3A6B` and `parent_window_token` case `0x3A6E` | Context is written to `+0x28`; parent is separately written to `+0x2C`; selecting the state context does not write a parent |
| Current scope | Rebuild `0xB8D1E0` | Window's current scope is `window + 0x4D0`; selected-state branch calls `0x53B5F0` with the selected state's ID |
| State setter | `0x53B5F0` | Writes state ID at scope `+0xA8`, rather than country ID at `+8` |
| Player ROOT | End of rebuild `0xB8D1E0` | Builds player country scope at `window + 0x30`, then writes that pointer at `window + 0x4E8` = current scope `+0x18` |
| Selector slots | Scope selector parser `0x1392920`; accessors `0x537810`, `0x5379E0`, `0x537B50` | `FROM` reads pointer `+0x20`, `PREV` reads `+0x28`, `ROOT` reads `+0x18` |

The window's context object begins at `window + 0x110`. Its constructor `0xB851A0` constructs the event scope at context `+0x3C0`, hence `window + 0x4D0`. Scope constructor `0x535110` initializes the ROOT/FROM/PREV pointers (`+0x18/+0x20/+0x28`) to the scope itself. The entity reset helper `0x53BD00` does not replace those pointers. Rebuild replaces ROOT with the player scope, while FROM and initial PREV remain self references to the selected-state scope. This describes the initial GUI context; nested script scopes can change PREV in the ordinary way. A complete nested-effect scope implementation was outside this investigation.

The GUI tick `0xB88950` calls the rebuild before evaluating visibility against `window + 0x4D0`. Button dispatch `0xB867D0` passes the same context scope to the command constructor. The scope guard `0x541DC0` reads FROM for random-state bookkeeping and does not replace these scope pointers.

## Two paths to the selected state

The selected-state branch of `0xB8D1E0` has an explicit check for a non-null parent and parent-token `0x3AB0` (`selected_state_view`):

```text
if parent != null and parent_token == selected_state_view:
    view = GUI view manager's view 0 (CCountryStateView)
    if view.selected_state at +0x588 != null:
        set current scope state ID from selected_state +0x58
        finish

otherwise:
    inspect main GUI selection at +0x538
    require selection getter state, selection type == 4,
            and selected state pointer at selection +0xB8
    if satisfied, set current scope state ID

finish:
    set ROOT to player country scope
```

Consequently, adding the parent token changes how the state is obtained, as well as where the window is drawn. Static analysis alone cannot establish which condition was failing in the reported game session.

## What `selected_state_view` parents to

The token targets the **top-level `countrystateview` window**, not its `state_info_window` child:

1. Parent-token map population `0xB8A1C0` obtains GUI view 0 and stores the result of getter `0x12374F0` for token `0x3AB0`.
2. The getter's raw instructions are `mov rax, [rcx+0x578]; ret`.
3. `CCountryStateView` constructor `0x1744CB0` passes the literal `countrystateview` to base constructor `0x1237180`.
4. That base constructor stores this name at view `+0x550`.
5. Creation routine `0x1237680` passes the stored name to main-GUI creation and stores the returned window pointer at view `+0x578`.

Scripted-window setup `0xB86320` resolves the requested parent token and records its window at `window +0x100`. Without a token or another explicit parent, this pointer stays null and window creation uses the main GUI. The state context itself does not override this behavior.

## Geometry for the candidate patch

The local vanilla `interface/countrystateview.gui` defines the top-level parent with `orientation = lower_left`, shown position `(-6,-655)`, and size `436 x 496`. Its child `state_info_window` begins at `(0,0)` and has width 780. The mod before the patch defines its shortcut window relative to the main screen with lower-left position `(446,-560)`, size `140 x 136`, and `clipping = no`.

With the explicit state-view parent, `orientation = upper_left` and local position `(476,95)` produce a fully shown screen position `(470, screen_height - 560)` at UI scale 1. This arithmetic uses the parent's shown origin; animation, effective clipping, UI scaling, and hit testing remain runtime QA items. Native parent identity is proven; those visual behaviors are not.

## Reproduction and raw evidence

- [Targeted decompilation](../evidence/session-20261011/state-view-targeted-decompile.txt), including original Ghidra warnings and analyst aliases.
- [Raw Capstone](../evidence/session-20261011/state-view-capstone.txt), including constructor, scope setters/accessors, state-context rebuild, root-window creation, and string xrefs.
- [Function request list](../evidence/session-20261011/state-view-functions.txt).
- [Read-only Ghidra logs](../evidence/session-20261011/state-view-ghidra.log), recording the existing project and discarded changes.

Use `probes/ghidra/Decomp.java` with the saved function list against the same exact-hash project, `-process hoi4.exe -noanalysis -readOnly`. No binary patch or live-game observation was used for these findings.
