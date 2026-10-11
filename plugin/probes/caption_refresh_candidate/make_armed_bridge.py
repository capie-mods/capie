"""Generates the armed caption-refresh bridge as an additive copy of the runtime-accepted
debug bridge (PID36204/42644/49352). The original is never edited. Every edit is an exact,
unique text replacement, asserted; the source and inherited test are pinned by SHA256.

Run:  python -I make_armed_bridge.py            (writes the generated files next to this script)
      python -I make_armed_bridge.py --check    (verifies the committed outputs are up to date)
"""
import hashlib
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
PROBES = HERE.parent
SOURCE = PROBES / "inline_editor_debug_bridge.cpp"
SOURCE_SHA = "279d17e51c8e07adf67deb73287d04842f06e7c58b6b1bd576c0d4979c268c6b"
TEST = PROBES / "inline_debug_bridge_test.cpp"
TEST_SHA = "802877e5ef4601b1c241f5c697b14d48b92c8fa99a603f889eb683b3796b573a"
OUT = HERE / "caption_refresh_bridge.cpp"
OUT_TEST = HERE / "caption_refresh_bridge_inherited_test.cpp"
DLL = "rename_anything_caption_refresh_bridge.dll"

CAPTION_BLOCK = r'''
// ===== Caption refresh integration (generated; see make_armed_bridge.py) =====
// Shares the existing Tick/Bind/dispatch hooks: no additional detour. Dormant until
// RNInitialize verified every caption fingerprint/slot pin/token and set caption_armed.
// Leader (model 0) and party (model 1) only; country (model 2) is unchanged.
using CaptionRequest=void(__fastcall*)(void*,int);
CaptionRequest caption_request=nullptr;
std::atomic<bool> caption_armed{false};
struct CaptionMemory {
    bool read(rn::caption::uptr address,void* out,std::size_t size) const noexcept {
        return guarded_copy(out,reinterpret_cast<const void*>(address),size);
    }
};
bool caption_carrier_text(void* root,const char* name,Utf8Snapshot& text) noexcept { return carrier(root,name,text,false); }
bool caption_call(void* gui,int index) noexcept {
    if(!caption_request || index!=rn::caption::politics_view_index) return false;
    __try { caption_request(gui,index); return true; }
    __except(memory_exception(GetExceptionCode())) { return false; }
}
struct CaptionEnv {
    CaptionMemory memory{};
    const CaptionMemory& mem() const noexcept { return memory; }
    rn::caption::uptr image() const noexcept { return image_base; }
    bool carrier(rn::caption::uptr root,const char* name,char* out,std::size_t cap,std::size_t& size) noexcept {
        Utf8Snapshot text{};
        if(!caption_carrier_text(reinterpret_cast<void*>(root),name,text) || text.size>cap) return false;
        std::memcpy(out,text.bytes,text.size); size=text.size; return true;
    }
    bool call_refresh(rn::caption::uptr gui,int index) noexcept { return caption_call(reinterpret_cast<void*>(gui),index); }
    bool refresh_armed() const noexcept { return caption_armed.load(std::memory_order_acquire); }
    void log(const char* line) noexcept { write_log(line); }
};
CaptionEnv caption_env{};
rn::caption::Service<CaptionEnv> caption_service(caption_env);
void caption_prefill(void* window,std::size_t model) noexcept {
    if(!caption_armed.load(std::memory_order_acquire) || model>1) return;
    caption_service.on_prefill(reinterpret_cast<rn::caption::uptr>(window));
}
bool caption_dispatch_allowed(void* context,std::size_t model) noexcept {
    if(!caption_armed.load(std::memory_order_acquire) || model>1) return true;  // disarmed/country: unchanged
    void* window=nullptr;
    if(!member(context,0x10,window)) return false;
    return caption_service.on_dispatch(reinterpret_cast<rn::caption::uptr>(window));
}
void caption_tick(void* window) noexcept {
    if(caption_armed.load(std::memory_order_acquire)) caption_service.on_tick(reinterpret_cast<rn::caption::uptr>(window));
}
void caption_bind(void* window) noexcept {
    if(caption_armed.load(std::memory_order_acquire)) caption_service.on_bind(reinterpret_cast<rn::caption::uptr>(window));
}
bool caption_unique_at(const unsigned char* base,const IMAGE_SECTION_HEADER* text,std::uintptr_t rva,
                       const unsigned char* bytes,std::size_t size) noexcept {
    if(!size || rva<text->VirtualAddress || rva-text->VirtualAddress+size>text->Misc.VirtualSize) return false;
    const auto* data=base+text->VirtualAddress; std::size_t count=0;
    for(std::size_t i=0;i+size<=text->Misc.VirtualSize;++i)
        if(data[i]==bytes[0] && std::memcmp(data+i,bytes,size)==0) { if(i+text->VirtualAddress!=rva || ++count>1) return false; }
    return count==1;
}
// 0 = verified; 20 = fingerprint; 21 = slot pin / token.
unsigned caption_verify(const unsigned char* base,const IMAGE_SECTION_HEADER* text) noexcept {
    for(const auto& fp:rn::caption::fingerprints) {
        unsigned char bytes[128]{};
        const auto size=rn::caption::decode_hex(fp.hex,bytes,sizeof(bytes));
        if(!caption_unique_at(base,text,fp.rva,bytes,size)) return 20;
    }
    const auto image=reinterpret_cast<rn::caption::uptr>(base); CaptionMemory memory{};
    for(const auto& pin:rn::caption::slot_pins) {
        rn::caption::uptr slot=0;
        if(!rn::caption::rd(memory,image+pin.vtable,pin.slot,slot) || slot!=image+pin.target) return 21;
    }
    std::uint32_t politics=0,context=0;
    if(!rn::caption::rd(memory,image,rn::caption::rva::token_politics_tab,politics) ||
       politics!=rn::caption::token_politics_tab_value ||
       !rn::caption::rd(memory,image,rn::caption::rva::token_player_context,context) ||
       context!=rn::caption::token_player_context_value) return 21;
    return 0;
}
// ===== end caption refresh integration =====
'''

FOCUS_BLOCK = r'''
// ===== Focus release on politics hide (generated; see focus_release.hpp) =====
using FocusSwitch=void(__fastcall*)(void*,void*);
FocusSwitch focus_switch=nullptr;
std::atomic<bool> focus_armed{false};
bool focus_call_null(void* manager) noexcept {
    if(!focus_switch) return false;
    __try { focus_switch(manager,nullptr); return true; }
    __except(memory_exception(GetExceptionCode())) { return false; }
}
struct FocusEnv {
    CaptionMemory memory{};
    const CaptionMemory& mem() const noexcept { return memory; }
    rn::caption::uptr image() const noexcept { return image_base; }
    rn::caption::uptr find_edit(rn::caption::uptr root,const char* name) noexcept {
        return reinterpret_cast<rn::caption::uptr>(guarded_find(::find_edit,reinterpret_cast<void*>(root),name));
    }
    bool call_focus_null(rn::caption::uptr manager) noexcept { return focus_call_null(reinterpret_cast<void*>(manager)); }
    bool armed() const noexcept { return focus_armed.load(std::memory_order_acquire); }
    void log(const char* line) noexcept { write_log(line); }
};
FocusEnv focus_env{};
rn::focus::Release<FocusEnv> focus_release(focus_env);
void focus_tick(void* window) noexcept { focus_release.on_tick(reinterpret_cast<rn::caption::uptr>(window)); }
void focus_bind(void* window) noexcept { focus_release.on_bind(reinterpret_cast<rn::caption::uptr>(window)); }
// 0 = verified; 22 = focus fingerprint / slot pin / manager global mismatch.
unsigned focus_verify(const unsigned char* base,const IMAGE_SECTION_HEADER* text) noexcept {
    for(const auto& fp:rn::focus::focus_fingerprints) {
        unsigned char bytes[128]{};
        const auto size=rn::caption::decode_hex(fp.hex,bytes,sizeof(bytes));
        if(!caption_unique_at(base,text,fp.rva,bytes,size)) return 22;
    }
    const auto image=reinterpret_cast<rn::caption::uptr>(base); CaptionMemory memory{};
    for(const auto& pin:rn::focus::focus_slot_pins) {
        rn::caption::uptr slot=0;
        if(!rn::caption::rd(memory,image+pin.vtable,pin.slot,slot) || slot!=image+pin.target) return 22;
    }
    std::int32_t displacement=0;  // 22FBEC3: mov rcx,[rip+disp] -> manager global 3453230
    if(!rn::caption::rd(memory,image,0x22FBEC6,displacement) ||
       0x22FBECAll+displacement!=static_cast<long long>(rn::focus::rva::manager_global)) return 22;
    return 0;
}
// ===== end focus release =====
'''

EDITS = [
    # RN3 (PID3044): a panel close hides the own editor root and reopen rebuilds the edit widget
    # with the SAME flag/epoch. Keeping prefilled across the hide made Apply drop silently with
    # own-edit-identity-visibility-text. Clear readiness on a real visible->hidden transition so
    # the next visible tick re-prefills in the same lifecycle (draft intentionally dropped).
    ("        // A valid bound root was merely hidden. Retain this opening's draft/readiness.\n"
     "        if(state.visible) ++state.generation;\n        state.visible=false;\n",
     "        // Panel close replaces the editor widget on reopen; re-prefill on next show so Apply\n"
     "        // is not silently dropped (RN3 PID3044). Draft is intentionally dropped.\n"
     "        if(state.visible) { ++state.generation; state.attempted=false; state.prefilled=false; }\n"
     "        state.visible=false;\n"),
    ("// Isolated own-field debug bridge; all predecessors remain frozen.\n",
     "// GENERATED by caption_refresh_candidate/make_armed_bridge.py from inline_editor_debug_bridge.cpp\n"
     "// (SHA256 279D17E5...C268C6B, runtime-accepted). Do not edit by hand; edit the generator.\n"
     "// Adds the caption refresh core on the SAME 7 hooks (no extra detour).\n"
     "// Isolated own-field debug bridge; all predecessors remain frozen.\n"),
    ('#include "../src/image_guard.hpp"\n#include "inline_debug_trace.hpp"\n#include "../src/editor_input.hpp"\n#include "../src/country_capability.hpp"\n',
     '#include "../../src/image_guard.hpp"\n#include "../inline_debug_trace.hpp"\n#include "../../src/editor_input.hpp"\n#include "../../src/country_capability.hpp"\n#include "caption_refresh_core.hpp"\n#include "focus_release.hpp"\n'),
    ("void prefill_open(void* window,const View& view,std::uint64_t epoch,std::uint64_t generation,unsigned long id) {\n",
     "void caption_prefill(void* window,std::size_t model) noexcept;\n"
     "void prefill_open(void* window,const View& view,std::uint64_t epoch,std::uint64_t generation,unsigned long id) {\n"),
    ("    if(state.visible && state.epoch==epoch && state.generation==generation) state.prefilled=true;\n    ReleaseSRWLockExclusive(&state_lock);\n}\n",
     "    const bool prefilled=state.visible && state.epoch==epoch && state.generation==generation;\n"
     "    if(prefilled) state.prefilled=true;\n    ReleaseSRWLockExclusive(&state_lock);\n"
     "    // Caption seed: only after this opening's one-shot prefill fully succeeded (no lock held).\n"
     "    if(prefilled) caption_prefill(window,view.model);\n}\n"),
    ("void __fastcall tick_observed(void* window,char force) {\n",
     CAPTION_BLOCK + FOCUS_BLOCK + "void __fastcall tick_observed(void* window,char force) {\n"),
    ("        observe_key_tick(window);\n        service_debug(window);\n    }\n",
     "        observe_key_tick(window);\n        service_debug(window);\n        caption_tick(window);\n        focus_tick(window);\n    }\n"),
    ("    original_bind(window);\n    if(!active.load(std::memory_order_acquire)) return;\n",
     "    original_bind(window);\n    if(!active.load(std::memory_order_acquire)) return;\n    caption_bind(window);\n    focus_bind(window);\n"),
    ("    Origin current{};\n    current.context=context; current.model_name=model_name; current.selector=selector; current.model=model;\n",
     "    if(!caption_dispatch_allowed(context,model)) {\n"
     "        bridge_rejected(observation_id.fetch_add(1)+1,\"caption-seed-gate-apply-dropped\"); return;\n    }\n"
     "    Origin current{};\n    current.context=context; current.model_name=model_name; current.selector=selector; current.model=model;\n"),
    ('        const auto same_name=GetModuleHandleW(L"rename_anything_inline_editor_debug_bridge.dll");\n',
     f'        const auto same_name=GetModuleHandleW(L"{DLL}");\n'),
    ('        if (GetModuleHandleW(L"rename_anything_inline_editor_key_logger_v2.dll") ||\n',
     '        if (GetModuleHandleW(L"rename_anything_inline_editor_debug_bridge.dll") ||\n'
     '            GetModuleHandleW(L"rename_anything_caption_refresh_candidate.dll") ||\n'
     '            GetModuleHandleW(L"rename_anything_inline_editor_key_logger_v2.dll") ||\n'),
    ("            !unique_at(base, text, ctor_rva, ctor_bytes) || !unique_at(base,text,execute_rva,execute_bytes)) return 20;\n",
     "            !unique_at(base, text, ctor_rva, ctor_bytes) || !unique_at(base,text,execute_rva,execute_bytes)) return 20;\n"
     "        if (const auto caption=caption_verify(base,text)) return caption;\n"
     "        if (const auto focus=focus_verify(base,text)) return focus;\n"),
    ('L"inline_editor_debug_bridge.log"', 'L"caption_refresh_bridge.log"'),
    ("        active.store(true, std::memory_order_release);\n",
     "        caption_request = reinterpret_cast<CaptionRequest>(image_base + rn::caption::rva::request_view_refresh);\n"
     "        caption_armed.store(true, std::memory_order_release);\n"
     "        focus_switch = reinterpret_cast<FocusSwitch>(image_base + rn::focus::rva::focus_switch);\n"
     "        focus_armed.store(true, std::memory_order_release);\n"
     "        active.store(true, std::memory_order_release);\n"),
    ("own copied history, scalar remote requests",
     "caption refresh armed (25 caption FP, 4 slot pins, 2 tokens, B6E210(gui,12) only, seed gate on leader/party Apply); focus release on politics hide (8 FP, 2 slot pins, 225E320(manager,null) once per transition); own copied history, scalar remote requests"),
]
TEST_EDITS = [
    ('#include "inline_editor_debug_bridge.cpp"\n', '#include "caption_refresh_bridge.cpp"\n'),
    ('LoadLibraryW(L"rename_anything_inline_editor_debug_bridge.dll")', f'LoadLibraryW(L"{DLL}")'),
]


def apply(text, edits, label):
    for old, new in edits:
        count = text.count(old)
        if count != 1:
            raise SystemExit(f"{label}: edit anchor matched {count} times: {old[:80]!r}")
        text = text.replace(old, new)
    return text


def main():
    source = SOURCE.read_bytes()
    test = TEST.read_bytes()
    if hashlib.sha256(source).hexdigest() != SOURCE_SHA:
        raise SystemExit("debug bridge source hash changed; re-review before regenerating")
    if hashlib.sha256(test).hexdigest() != TEST_SHA:
        raise SystemExit("inherited test hash changed; re-review before regenerating")
    generated = apply(source.decode("utf-8"), EDITS, "bridge")
    generated_test = apply(test.decode("utf-8"), TEST_EDITS, "test")
    if "--check" in sys.argv:
        ok = OUT.read_text(encoding="utf-8") == generated and OUT_TEST.read_text(encoding="utf-8") == generated_test
        print("generated files up to date" if ok else "generated files STALE")
        raise SystemExit(0 if ok else 1)
    OUT.write_bytes(generated.encode("utf-8"))
    OUT_TEST.write_bytes(generated_test.encode("utf-8"))
    print(f"wrote {OUT.name} ({len(EDITS)} edits) and {OUT_TEST.name} ({len(TEST_EDITS)} edits)")


if __name__ == "__main__":
    main()
