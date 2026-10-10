// verify.js - READ-ONLY observation of Rename Anything hook candidates in hoi4.exe 1.19.3.0
// SHA-256 7DC947BE34970DA1E1C787BCDDBE7F62B610FF258AEBF8F06AA8B348F3A031D5
// Usage (game already running, attach only):  frida -n hoi4.exe -l verify.js
// Observational instrumentation: transient instruction hooks, no gameplay-state or disk writes.
'use strict';

const mod = Process.getModuleByName('hoi4.exe');
if (typeof RN_VERIFIED_IMAGE !== 'string' || mod.path.toLowerCase() !== RN_VERIFIED_IMAGE.toLowerCase()) throw new Error('Use observe.py: exact image SHA-256 guard required');
const base = mod.base;

// name, expected RVA, unique signature (verified unique in .text of the pinned build), logger
const C = [
  ['PdxLoc_FindByHash',            0x239ED90, '48 63 41 2C 4C 8B D2 85 C0 75 03 33 C0'],
  ['PdxLoc_LookupKeyN',            0x239EE20, '48 8B 05 ?? ?? ?? ?? 49 B8 25 23 22 84 E4 9C F2 CB'],
  ['PdxLoc_KeyExists',             0x239D0F0, '48 83 EC 28 48 8B 05 ?? ?? ?? ?? 48 BA 25 23 22 84 E4 9C F2 CB'],
  ['Localize',                     0x2245E60, '48 89 6C 24 18 48 89 74 24 20 57 41 56 41 57 48 83 EC 50 45 33 FF'],
  ['CCountry_UpdateNames',         0x0716580, '48 89 5C 24 18 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 10 FE FF FF'],
  ['CCountry_SetCosmeticTag',      0x0711260, '48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 48 83 EC 30 48 83 7A 18 0F'],
  ['CSetStateNameEffect_Execute',  0x03111C0, '4C 8B DC 49 89 5B 08 49 89 6B 18 49 89 73 20 57 41 56 41 57 48 83 EC 50'],
  ['CState_GetName',               0x09D91D0, '48 83 79 48 00 74 05 48 8D 41 38 C3'],
  ['MapNames_SetDirty',            0x0E139C0, '66 C7 81 81 00 00 00 01 01 C3 CC CC'],
  ['IngameIface_OnCountryNameChanged', 0x0A66C50, '40 57 48 83 EC 20 48 8B 49 08 48 8B FA'],
  ['CCharacter_SetName',           0x0FA61E0, '48 89 5C 24 18 48 89 54 24 10 56 57 41 56 48 83 EC 40 48 8B DA'],
  ['CCharacter_PostLoad',          0x0FA43C0, '40 57 48 83 EC 50 83 79 18 00 48 8B F9'],
  ['CSession_PostCommand',         0x2250B00, '48 89 5C 24 08 57 48 81 EC 30 01 00 00 48 8B FA 48 8B D9 45 84 C0'],
  ['CEditBox_OnTextInput',         0x22F40D0, '48 89 5C 24 08 57 48 83 EC 40 48 8B DA 48 8B F9 48 8D 91 B0 00 00 00'],
  ['CTextBuffer_NotifyEnter',      0x22F2050, '40 53 48 83 EC 20 48 8B 59 08 48 85 DB 74 13 90 48 8B 0B 48 8B 01 FF 50 30'],
];

// MSVC std::string: +0 buf/ptr, +0x10 size, +0x18 capacity (heap when cap > 15)
function stdstr(p) {
  try {
    if (p.isNull()) return '<null>';
    const size = p.add(0x10).readU64().toNumber();
    const cap = p.add(0x18).readU64().toNumber();
    if (size > 4096) return '<bad size ' + size + '>';
    const data = cap > 15 ? p.readPointer() : p;
    return data.readUtf8String(size);
  } catch (e) { return '<err ' + e + '>'; }
}
function cstr(p, n) { try { return p.isNull() ? '<null>' : p.readUtf8String(n); } catch (e) { return '<err>'; } }
function rva(p) { return '0x' + p.sub(base).toString(16); }

const counters = {};
const LIMIT = 40;            // max detailed log lines per hook (hot paths)
function lim(name) { counters[name] = (counters[name] || 0) + 1; return counters[name] <= LIMIT; }

const loggers = {
  PdxLoc_FindByHash: {
    onEnter(a) { this.h = a[1]; },
    onLeave(r) { if (lim('PdxLoc_FindByHash')) console.log('[FindByHash] hash=' + this.h + ' -> ' + (r.isNull() ? 'NULL' : JSON.stringify(cstr(r, 80)))); }
  },
  PdxLoc_LookupKeyN: {
    onEnter(a) { this.k = cstr(a[0], a[1].toInt32()); },
    onLeave(r) { if (lim('LookupKeyN')) console.log('[LookupKeyN] ' + this.k + ' -> ' + (r.isNull() ? 'NULL' : JSON.stringify(cstr(r, 80)))); }
  },
  PdxLoc_KeyExists: {
    onEnter(a) { this.k = cstr(a[0], 120); },
    onLeave(r) { if (lim('KeyExists')) console.log('[KeyExists] ' + this.k + ' -> ' + (r.toInt32() & 0xff)); }
  },
  Localize: {
    onEnter(a) { this.out = a[0]; this.k = cstr(a[1], 120); },
    onLeave(r) { if (lim('Localize')) console.log('[Localize] ' + this.k + ' => ' + JSON.stringify(stdstr(this.out))); }
  },
  CCountry_UpdateNames: {
    onEnter(a) { this.c = a[0]; },
    onLeave(r) {
      const c = this.c;
      console.log('[UpdateNames] country=' + c + ' tagIdx=' + c.add(8).readS32() +
        ' name=' + JSON.stringify(stdstr(c.add(0x10))) + ' adj=' + JSON.stringify(stdstr(c.add(0x30))) +
        ' def=' + JSON.stringify(stdstr(c.add(0x50))) + ' cosmetic=' + JSON.stringify(stdstr(c.add(0x1488))) + ' changed=' + r);
    }
  },
  CCountry_SetCosmeticTag: { onEnter(a) { console.log('[SetCosmeticTag] country=' + a[0] + ' new=' + JSON.stringify(stdstr(a[1]))); } },
  CSetStateNameEffect_Execute: { onEnter(a) { console.log('[set/reset_state_name Execute] effect=' + a[0] + ' key=' + JSON.stringify(stdstr(a[0].add(0x58))) + ' scopeStateId=' + a[1].add(0xa8).readS32()); } },
  CState_GetName: {
    onEnter(a) { this.s = a[0]; },
    onLeave(r) { if (lim('CState_GetName')) console.log('[CState::GetName] state=' + this.s + ' custom=' + JSON.stringify(stdstr(this.s.add(0x38))) + ' -> ' + JSON.stringify(stdstr(r))); }
  },
  MapNames_SetDirty: { onEnter(a) { console.log('[MapNames_SetDirty] obj=' + a[0] + ' caller=' + rva(this.returnAddress)); } },
  IngameIface_OnCountryNameChanged: { onEnter(a) { console.log('[OnCountryNameChanged] tagIdx=' + a[1].readS32()); } },
  CCharacter_SetName: {
    onEnter(a) { this.c = a[0]; console.log('[CCharacter::SetName] char=' + a[0] + ' key=' + JSON.stringify(stdstr(a[1]))); },
    onLeave() { console.log('   -> key(+0x48)=' + JSON.stringify(stdstr(this.c.add(0x48))) + ' cached(+0x28)=' + JSON.stringify(stdstr(this.c.add(0x28)))); }
  },
  CCharacter_PostLoad: { onLeave() { if (lim('CharPostLoad')) console.log('[CCharacter post-load]'); } },
  CSession_PostCommand: {
    onEnter(a) {
      const cmd = a[1];
      const vt = cmd.readPointer();
      if (!lim('PostCommand')) return;
      console.log('[PostCommand] cmd=' + cmd + ' vtable=' + rva(vt) + ' caller=' + rva(this.returnAddress));
    }
  },
  CEditBox_OnTextInput: { onEnter(a) { console.log('[CEditBox::OnTextInput] recv=' + a[0] + ' text=' + JSON.stringify(stdstr(a[1]))); } },
  CTextBuffer_NotifyEnter: {
    onEnter(a) {
      // CTextBuffer: UTF-32 std::basic_string at +0x30 (size +0x40, cap +0x48, SSO cap 3)
      try {
        const tb = a[0]; const n = tb.add(0x40).readU64().toNumber(); const cap = tb.add(0x48).readU64().toNumber();
        const d = cap > 3 ? tb.add(0x30).readPointer() : tb.add(0x30);
        let s = ''; for (let i = 0; i < Math.min(n, 256); i++) s += String.fromCodePoint(d.add(i * 4).readU32());
        console.log('[CTextBuffer Enter] buffer=' + tb + ' text=' + JSON.stringify(s));
      } catch (e) { console.log('[CTextBuffer Enter] err ' + e); }
    }
  },
};

for (const [name, expRva, sig] of C) {
  const hits = Memory.scanSync(base.add(0x1000), 0x2706000, sig);
  if (hits.length !== 1) { console.log('!! ' + name + ': signature hits=' + hits.length + ' (expected 1) - skipped'); continue; }
  const at = hits[0].address;
  const ok = at.sub(base).toInt32() === expRva;
  console.log((ok ? 'OK ' : '?? ') + name + ' at ' + rva(at) + (ok ? '' : ' (expected 0x' + expRva.toString(16) + ')'));
  if (!ok) continue; // Fail closed: a moved signature is an unsupported binary.
  if (!['CCountry_UpdateNames', 'CCountry_SetCosmeticTag', 'CSession_PostCommand', 'CCharacter_SetName', 'CCharacter_PostLoad', 'CSetStateNameEffect_Execute'].includes(name)) continue;
  const l = loggers[name];
  if (l) Interceptor.attach(at, l);
}
setInterval(() => { const k = Object.keys(counters); if (k.length) console.log('[counts] ' + JSON.stringify(counters)); }, 10000);

