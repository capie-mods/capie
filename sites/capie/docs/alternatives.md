---
description: CAPIE next to other native extenders for HOI4, hoi4_lua and proxy-DLL loaders. What each one is for, so you can pick the one that fits your mod.
---

# Alternatives

CAPIE is not the only project that adds native code to Hearts of Iron IV. Two others came up in
the research for CAPIE's feature list. They solve different problems, and for some mods they are
the better choice. This page describes them as they were in October 2026; their own pages are the
authority, and anything below may be out of date.

## At a glance

| | CAPIE | `hoi4_lua` | Proxy-DLL loader (`HOI4-Mod-Injector`) |
|---|---|---|---|
| What it is | A small set of fixed features: text fields for scripted GUIs, names, status keys | A Lua 5.4 runtime inside the game that mod scripts can call into | A loader: a DLL the game picks up by name (for example `dxgi.dll` or `version.dll`) that loads every DLL in an `injected_mods` folder |
| What a mod writes | Plain HOI4 script and GUI names; no native code | Lua scripts, called from HOI4 script | Its own native DLL |
| Without the extender | The mod loads and runs; CAPIE features report "CAPIE missing" | Depends on the mod; its Lua parts do not run | Depends on the DLL |
| Reach into the game | Only the hooks and calls listed in CAPIE's signature table | Its README warns that scripts "can arbitrarily read and write game memory" | Whatever the loaded DLLs do |
| Multiplayer | Designed for deterministic commands and a version check in script; not yet tested with two players | See the project's documentation | Not applicable to the loader itself |
| Game versions | Pinned to `hoi4.exe` 1.19.3.0 by hash; refuses anything else | Also pinned to 1.19.3.0 according to its README | The loader is version independent; the DLLs it loads may not be |
| How it gets into the game | Its own `version.dll` proxy in the game folder, switched on explicitly, or the CAPIE launcher; either way only CAPIE's two DLLs, after a hash check | See the project | Placed in the game folder as a proxy DLL |

## `hoi4_lua`

[`ari-20/hoi4_lua`](https://github.com/ari-20/hoi4_lua) describes itself as a "Runtime-extension
framework for Hearts of Iron IV". It injects a DLL with a Lua 5.4 virtual machine, routes mod
effects and triggers into the engine, and offers a local HTTP control plane and a profiler.

It is the stronger tool when your mod needs general computation or engine access that a fixed
feature list will never cover. The price is the one its README names: Lua code can read and
write game memory, so a mistake in a mod's Lua is a mistake inside the game process, and keeping
every machine in a multiplayer game in step is the mod author's job.

CAPIE takes the opposite position. It offers a few narrow features, each behind a byte-exact
check of the game build, and it keeps mods in plain HOI4 script so that a mod still loads, and
multiplayer sessions still take the same branch, when CAPIE is missing.

## Proxy-DLL loaders

[`MyresMkG/HOI4-Mod-Injector`](https://github.com/MyresMkG/HOI4-Mod-Injector) is a loading
mechanism, not an API. A proxy DLL that the game loads by name loads every DLL in an
`injected_mods` folder. It removes the need for a separate launcher, which is convenient for
players who run several native mods.

CAPIE also loads through a proxy DLL, its own `version.dll`, but with a different contract: it
loads only CAPIE's two DLLs, whose hashes are compiled into the proxy, only for the pinned game
build and only when the player switched CAPIE on (see the repository's `docs/proxy-loader.md`).
A loader that loads every DLL in a folder is more open by design, which suits players who run
several native mods.

Both projects can use the same file name. CAPIE's installer treats an existing `version.dll`
it did not install as a conflict and never overwrites it; the CAPIE launcher is the way to run
CAPIE next to another proxy loader.

## Using them together

Nobody has tested CAPIE together with either project. Each of them changes the same process, and
two hooks on one game function can conflict. If you run more than one, test the combination
before you rely on it, and expect CAPIE to refuse to start if another tool changed the code it
checks.

## Which one fits

| Your mod needs | Look at |
|---|---|
| Players typing names, mottos or labels into your own window | CAPIE |
| Logic that HOI4 script cannot express, or engine data no getter exposes | `hoi4_lua` |
| Your own native DLL, loaded without a launcher | A proxy-DLL loader |
| To keep working for players who install nothing extra | CAPIE's fallback branches, or plain HOI4 script |
