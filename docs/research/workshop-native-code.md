# Steam Workshop and native code

Research on whether Steam Workshop can distribute a native DLL for a HOI4 mod, what the current platform rules say, and how comparable projects install native components. Applies to HOI4 1.19.3.0 and the Rename Anything native component. Research date: 2026-10-11 (Europe/Warsaw). Original notes (Polish): [../evidence/research/workshop-native-research.md](../evidence/research/workshop-native-research.md).

All browsing used gstack `/browse` in a headless browser. No upload took place, no third-party library was installed, and the game was not touched. The sections below keep findings from sources separate from the conclusions drawn for Rename Anything.

## Answer for this project

**Steam Workshop can distribute DLL files, but it has not been confirmed that the HOI4 uploader accepts this project's DLL. Subscribing alone will not start the current component.**

Steamworks describes an item as a folder of files uploaded through `SetItemContent`. After download, the application decides how to use the content. The documentation gives no general list of forbidden extensions here. The CitiesHarmony mod, as described by its author, distributes a DLL through Workshop. That rules out the simple claim that Workshop as a platform always forbids `.dll` files. It does not prove that every game accepts a native injector. Sources: [Steamworks: Workshop implementation](https://partner.steamgames.com/doc/features/workshop/implementation), [CitiesHarmony, author's repository](https://github.com/boformer/CitiesHarmony).

In Rename Anything, a separate injector loads the DLL and calls its `RNInitialize` export. Placing these files in the downloaded folder does not perform that step. The sources found contain no confirmed official HOI4 mechanism that loads native libraries from a mod folder automatically. No public allowlist of extensions for the HOI4 uploader was found either. These two points are separate from the general capabilities of Steamworks.

The practical publishing model is a script mod on Workshop plus an optional native component that is described openly and shipped from a versioned project release with sources and hashes. This recommendation follows the existing EU4/CK2 precedents; it is not an extra requirement of any terms of service. If the DLL is eventually to be included in the HOI4 Workshop item, a separate upload/download trial through the HOI4 uploader is needed, together with instructions for starting the injector by hand. This research did not perform such a trial.

## What the current rules say

| Source | Confirmed finding | Limit of the conclusion |
|---|---|---|
| [Steam Subscriber Agreement, updated 10.09.2026, §6](https://store.steampowered.com/subscriber_agreement/#6) | The author must hold rights that allow licensing all of the content. Games can have additional Workshop terms. Valve can restrict or remove an item. | Acceptance of a file is not a guarantee of compliance with the game's terms. |
| [Steam SSA, §2G and §4](https://store.steampowered.com/subscriber_agreement/) | The agreement restricts reverse engineering and interference with the operation of the services, with exceptions for appropriate permission and law; it forbids cheats. | General UGC support is not an explicit permission for every injection technique. No ruling on this particular HOI4 hook was found. |
| [Steam Online Conduct](https://store.steampowered.com/online_conduct/) | Malicious software, cheating and infringement of others' rights are forbidden. | The prohibition concerns behaviour and content; the page does not ban the DLL extension as such. |
| [Paradox User Agreement, updated 21.01.2026, §5](https://legal.paradoxplaza.com/eula) | Paradox allows creating, using and sharing non-commercial UGC. The definition includes mods, scripts and programs. Published UGC must be free and use material the author has rights to. | Strong support for distributing one's own program as UGC, but the text does not explicitly approve DLL injection in HOI4. |
| [Paradox Code of Conduct](https://legal.paradoxplaza.com/rules-of-conduct?locale=en) | The rules cover official community platforms and forbid, among other things, cheats, hacks, trojans and harmful programs. | A mod library cannot be equated with such a forbidden program purely because of its file format. |

The current rules that were read contain no simple ban of the form "a mod must not contain a DLL". They also contain no official approval of the specific hooking method used in Rename Anything. The honest status is: **no demonstrated ban on the format, and no individual approval of the implementation**. There is no basis for claiming that including a DLL by itself triggers an automatic account or VAC ban.

An older forum page about Paradox's modding policy was unavailable behind a CAPTCHA challenge and was not used as evidence. The description of Paradox's policy rests on the current user agreement linked from its official legal page.

## Existing projects and how they install

| Project | What the author's source actually confirms |
|---|---|
| [EU4dll](https://github.com/matanki-saito/EU4dll) | Unofficial native fix for displaying multibyte characters in Europa Universalis IV. The README requires unpacking files into the game directory separately and subscribing to the matching localisation mod. It lists Windows/Steam support, a macOS variant in a separate project, and no Linux support. A precedent for a mod that works with a DLL, not evidence that Workshop loads the DLL automatically. |
| [CK2dll](https://github.com/matanki-saito/CK2dll) | The equivalent fix for Crusader Kings II. The author describes installing the library into the game directory separately plus a Steam mod, support for the Steam release, and no macOS support. |
| [HOI4-Mod-Injector](https://github.com/MyresMkG/HOI4-Mod-Injector) | A public project from October 2026 that describes a proxy DLL and loading libraries from `injected_mods`. The README contains the author's own test reports, version/CRT limitations, and no complete MP run. Evidence that native loader code for HOI4 exists, not independent confirmation of its quality or of acceptance by Workshop or Paradox. It was not installed. Its install-into-the-game-directory approach is not the one Rename Anything uses. |
| [CitiesHarmony](https://github.com/boformer/CitiesHarmony) | The author describes a Workshop mod that ships `CitiesHarmony.Harmony.dll` and an API library. A real precedent for DLL distribution through Workshop in a game published by Paradox, but it concerns the managed Mono environment and the Cities: Skylines API. Its loading model does not carry over to the native HOI4 engine automatically. |

A further direct example from a Workshop author's description: [Chinese Language Mod for EU4 1.37](https://steamcommunity.com/sharedfiles/filedetails/?id=2976470733) requires downloading the multibyte fix separately and placing `version.dll`, `d3d9.dll` and `plugins` in the game directory. Its FAQ lists incorrect installation and an unsupported DLL version as causes of crashes. This example confirms the "Workshop plus external component" model, not binary content inside the item itself. Those files were not copied or run, and replacing a user's existing libraries is not recommended.

## Reported problems in comparable projects

| Report to the author | Report and its limits |
|---|---|
| [EU4dll #234, Windows detected virus](https://github.com/matanki-saito/EU4dll/issues/234) | A user reported a detection of the build56 package. The author suggested using an older `version.dll`; the reporter considered the issue solved. A record of a detection and a workaround, not an independent analysis of the file. |
| [EU4dll #266, Virus detected when downloading by google chrome](https://github.com/matanki-saito/EU4dll/issues/266) | Release92 was blocked on download. The author described submitting the DLL to Microsoft, then identified the ZIP as the trigger and prepared a different package. The reporter confirmed the download worked. The author's explanation is not a safety certificate for every release. |
| [EU4dll #226, CTD on a long tooltip](https://github.com/matanki-saito/EU4dll/issues/226) | A reproducible crash on a very long Japanese tooltip. The author linked it to `_alloca_probe` and gave a workaround that changes the stack reservation in the EXE. An example of the limits of hook compatibility with text length. The workaround was not applied and is not proposed for Rename Anything. |
| [EU4dll #264, institution growth description](https://github.com/matanki-saito/EU4dll/issues/264) | After an update, only a colon was shown instead of the causes of institution growth. The author announced a revert; the reporter confirmed the fix. Shows that localisation can regress outside the mod's main function. |

These sources contain no confirmed case of a ban for merely using these DLLs. Neither the existence of these projects nor the absence of such a report guarantees acceptance of another program. The conclusion for this project: version the component against the exact EXE, state the DLL status unambiguously, and test the remaining localisation labels after changes. The current SHA-256 and signature guard limits startup on unknown builds; it does not replace save/load, MP or decision regression tests.

## Licences and release content

The [MinHook licence](https://github.com/TsudaKageyu/minhook/blob/master/LICENSE.txt) allows redistribution of source and binaries, including modified versions. For binaries, it requires keeping the copyright notice, the conditions and the disclaimer in the documentation or other accompanying materials. The file also covers parts of the Hacker Disassembler Engine. The DLL package must therefore include **the full licence file of the MinHook/HDE version used**; a link or the library name alone does not satisfy this condition. These terms impose no obligation to disclose Rename Anything's own code, although publishing it makes auditing easier.

Under the UGC rules, a release should distribute only the project's own mod and its own permitted dependencies, without `hoi4.exe`, Steam files, or third-party libraries with unclear rights. A release should state the project's licence, sources, supported EXE hash, DLL hash, instructions for starting and uninstalling, and the test status. These are concrete elements of a transparent release; this research does not confirm that the current implementation is ready for release.
