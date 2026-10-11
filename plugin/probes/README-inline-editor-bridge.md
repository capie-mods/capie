# Isolated five-field inline transport candidate

Source/build candidate only. It has not been injected or accepted in HOI4.
It extends the separate leader prototype without replacing that prototype's
source, DLL or evidence. Do not co-load editor probe DLLs.

The unchanged five detours and eight exact entry fingerprints retain the
leader prototype's image, ABI, bounded lookup, bind/hide lifecycle, prefill
readback, copied Enter diagnostic and command constructor safeguards.
Enter does not dispatch or apply. World load without a rebind remains a
runtime gap; this revision does not claim to solve it.

| Model | Fields | Fresh records |
|---|---|---:|
| rn_ip_leader_gui | rn_ip_leader_edit | 35 |
| rn_ip_party_neutrality_gui | rn_ip_party_neutrality_edit / long_edit | 69 |
| rn_ip_country_gui | rn_ip_country_name_edit / adj_edit | 70 |

Each model has its own exact Apply selector. Both fields are captured from
one freshly verified visible View, opening epoch and lifecycle generation.
Before original dispatch, both texts and the View/epoch/generation are
resolved and checked again. No engine pointer is retained across calls.

Field A uses rn_native_ok/len/0..31. Field B uses rn_nativeb_ok/len/0..31.
All commands include rn_ip_cmd_epoch. Country additionally includes
rn_native_cap=1, after a fresh successful RNCountryCapabilities query of
the country DLL; the query is repeated just before dispatch. Missing export,
zero, wrong version or a guarded memory fault drops that own country action
before dispatch. No saved flag or module presence substitutes for readiness.
This trusts the project's country component and its export contract, not
arbitrary third-party DLL code with the same filename.

The exact 70-key union is sanitized, including every duplicate. Similarly
named unrelated keys retain their original bytes/order/raw values. Source
vectors remain bounded to 256 records; the largest copied POD view has 326.
New keys fit 15-byte native SSO. All logical values are checked before
signed fixed-point multiplication by 100000. Unsupported/empty/overlength
input produces ok0/len0/all32zero for that field, allowing the script to
reject the whole pair before any persistent write.

Constructor replacement remains limited to argument5 for the synchronous
TLS origin's exact model/selector/scope/source identities. The original
constructor runs once and deep-copies the POD view. Native return values
are preserved; there is no post-constructor cancellation or retry.

Build with build-inline-editor-bridge.ps1 into plugin/build/inline-editor-bridge.
The builder test includes the actual candidate source and never initializes
hooks or dispatches. It covers A1/B32, A32/B1, Polish code84, invalid B33,
all payload slots/padding, stale duplicate sanitization, an unrelated
lookalike, leader record count, inactive country rejection, epoch overflow,
SSO bounds and oversized input vectors. Its assertion count also includes
individual key-read checks; it is not a count of independent scenarios.
This is separate from HOI4 parser/runtime, UI or multiplayer acceptance.

First activation must use a reviewed log-only fixture. Current production
and earlier sketches are not automatically enabled by adding these models.
Real party/country Apply, target/epoch/capability guards, atomic script
validation, draft/Cancel, menu/load, save/restart and Enter dispatch remain
required work. Multiplayer is deferred by the user.
