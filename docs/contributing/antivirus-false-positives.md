# Antivirus false positives

CAPIE loads a DLL into `hoi4.exe`. The launcher path does this with `capie_injector.exe`
(`VirtualAllocEx`, `WriteProcessMemory`, `CreateRemoteThread` on `LoadLibraryW`), and the
other path uses a proxy `version.dll` in the game folder. Malware uses the same techniques, and
our files are new and unsigned with no reputation, so machine-learning engines on VirusTotal
flag them. This page lists where to report each detection, gives ready-to-send texts, and covers
the technical changes that reduce detections over time.

VirusTotal produces no verdicts of its own and cannot remove a detection. Each vendor has to fix
its own engine ([VirusTotal: false positive contacts](https://docs.virustotal.com/docs/false-positive-contacts)).
Rescan first (the "Reanalyze" button). If the detection is still there, report it to the vendor
with the VirusTotal link.

## Release 1.5.0: status

Files from <https://github.com/capie-mods/capie/releases/tag/v1.5.0>. Tick a box when the report
has been sent, and write down the date and the vendor's ticket number.

| File | SHA-256 | Detections |
|---|---|---|
| `CAPIE-1.5.0-win64.zip` | `e949dafe4a1c588f764f6a5d0a9e8dcc9f0edb5a7d6f158327ee6d78fb20d6c2` | Bkav `W32.Malware.6471E83C` |
| `RenameAnythingLauncher.exe` | `9f4e3bbb8af2588c6401a601cb15d94f198c47cf62523bbd86e600b05f0a5664` | Bkav; CrowdStrike `win/malicious_confidence_70% (D)`; APEX `Malicious` |
| `runtime/capie_core.dll` | `7a1c946f8a15f6120a0232453e3013b9f854ec2a6b526d0ab9c86f19959d0de1` | Bkav; CrowdStrike 60%; McAfeeD `ti!7A1C946F8A15`; Cynet `Malicious (score: 100)` |
| `runtime/capie_editor.dll` | `0dad2732552305f43e1e87fb6e10e6a5b473e683ea27f1e82d0ebd9b8934d656` | Bkav; CrowdStrike 70%; Cynet; McAfeeD `ti!` |
| `runtime/capie_injector.exe` | `eb5e9ff5f9b944b55ca5eb4b343d18d507584284fb60f5f119a8be8843484c25` | Bkav; CrowdStrike 90%; McAfeeD `ti!`; APEX |
| `proxy/version.dll` | `796b55decc1e391921e7c40f66751073e1d2248c747ba287e48793e28c7e8adf` | 1 engine (check on VirusTotal which one) |

VirusTotal links have the form `https://www.virustotal.com/gui/file/<SHA-256>`.

| Vendor (VirusTotal name) | Channel | Files | Sent | Ticket / answer | Cleared |
|---|---|---|---|---|---|
| CrowdStrike Falcon | `VTscanner@crowdstrike.com` | launcher, core, editor, injector | [ ] | | [ ] |
| McAfee (`McAfeeD`) | `virus_research@mcafee.com`; [McAfee KB 000001921](https://www.mcafee.com/support/s/article/000001921) | core, editor, injector | [ ] | | [ ] |
| Cynet | `soc@cynet.com` | core, editor | [ ] | | [ ] |
| SecureAge APEX | [secureage.com/contact-us](https://www.secureage.com/contact-us), "Submit false positive" | launcher, injector | [ ] | | [ ] |
| Bkav Pro | `fpreport@bkav.com` (alternative `bkav@bkav.com`) | ZIP, launcher, core, editor, injector | [ ] | | [ ] |
| (version.dll engine) | look it up in the VirusTotal contact list | `proxy/version.dll` | [ ] | | [ ] |

## Channels

None of these vendors publishes a response time. The times below come from reports by other
developers, not from any guarantee.

### CrowdStrike Falcon (ML)

- Channel: e-mail `VTscanner@crowdstrike.com`. The CrowdStrike contact page lists it as
  "Feedback & reports of false positives about CrowdStrike (ML) engine in VirusTotal"
  ([crowdstrike.com/contact-us](https://www.crowdstrike.com/en-us/contact-us/)). The VirusTotal
  contact list gives the same address.
- Needs: SHA-256 and VirusTotal links. No account needed. Do not attach the binaries; the
  analysts can download them from VirusTotal.
- Expect: no published SLA. Developers report that each build is reviewed and allow-listed
  individually, so the next build with new hashes can be flagged again
  ([Cyotek](https://www.cyotek.com/blog/crowdstrike-falcon-false-positives)). Report every
  release.

### McAfee (`McAfeeD`)

- Who: `McAfeeD` is a McAfee engine. McAfee LLC (the consumer company) and Trellix (the former
  McAfee Enterprise) are now separate companies. The VirusTotal contact list has no row for
  `McAfeeD`. Its McAfee row points to McAfee LLC:
  [KB 000001921](https://www.mcafee.com/support/s/article/000001921) and
  `virus_research@mcafee.com`. Send there first. The `ti!<first 12 hex of SHA-256>` name looks
  like a hash-reputation (threat-intelligence) verdict rather than a signature, which fits a new,
  unknown file.
- Needs: by long-standing McAfee practice, put `FALSE` at the start of the subject. If you attach
  samples, use a ZIP with the password `infected` (other passwords are not processed). Include
  the hashes and VirusTotal links in the body as well.
- Expect: automated replies first. Developers report that the automation sometimes treats a
  false-positive report as a malware sample and answers with an analysis report. If that
  happens, reply and ask for a human review.
- Fallback: Trellix, `datasubmission@trellix.com`, subject `Possible False`, with the sample
  attached (per the community list
  [False-Positive-Center](https://github.com/yaronelh/False-Positive-Center)). Use it only if
  McAfee LLC answers that the engine is not theirs.

### Cynet

- Channel: e-mail `soc@cynet.com` (VirusTotal contact list).
- Needs: hashes and VirusTotal links. No account needed.
- Expect: no published SLA.

### APEX (SecureAge)

- Who: APEX is SecureAge Technology's AI engine for Windows PE files, on VirusTotal since May 2019
  ([SecureAge announcement](https://www.secureage.com/secureage-integrates-artificial-intelligence-powered-apex-anti-malware-engine-virustotal/)).
  The VirusTotal list has no APEX row; its SecureAge row points to the contact page.
- Channel: [secureage.com/contact-us](https://www.secureage.com/contact-us), "False Positive"
  section, "Submit false positive" button.
- Needs: name, e-mail, description and file or hash. The technical-support form on the same page
  takes uploads of at most 4 MB, so give hashes and VirusTotal links rather than files.
- Expect: no published SLA.

### Bkav Pro

- Channel: e-mail `fpreport@bkav.com`; alternative `bkav@bkav.com` (VirusTotal contact list).
- Needs: hashes and VirusTotal links. No account needed.
- Expect: developers report that Bkav confirmed every report as its own error and the detection
  was gone at a later rescan, but newer versions were flagged again
  ([16x Prompt](https://prompt.16x.engineer/blog/bkav-false-positive-false-alarm)). Report every
  release.

### The `version.dll` engine

Open `https://www.virustotal.com/gui/file/796b55decc1e391921e7c40f66751073e1d2248c747ba287e48793e28c7e8adf`,
find the engine, and look up its address in the
[VirusTotal contact list](https://docs.virustotal.com/docs/false-positive-contacts). Adapt the
generic text below.

## Report texts

Replace `<YOUR E-MAIL>` before sending. Send from the address you want answers on. Each text
names only the files that engine flagged.

### Generic body (base for every vendor)

```text
Subject: False positive: CAPIE 1.5.0 (open-source Hearts of Iron IV mod loader)

Hello,

I am the maintainer of CAPIE (Cyprian's API Extender), an open-source (MIT) native library
that extends the PC game Hearts of Iron IV for mods. Your engine flags files from our 1.5.0
release on VirusTotal. We believe these are false positives and ask you to review them.

What the files do:
- The user installs CAPIE and starts it on purpose to play the game with mods.
- RenameAnythingLauncher.exe starts hoi4.exe. capie_injector.exe then loads capie_core.dll into
  that game process with LoadLibraryW on a remote thread. This is the standard way game mods load
  native code. The alternative loader, proxy/version.dll, is a version.dll proxy placed in the
  game folder by the user.
- capie_core.dll and capie_editor.dll hook functions of hoi4.exe (MinHook) to add mod features
  such as renaming in-game objects. They only touch the game process.
- None of the files use the network, persist on the system, elevate privileges, or touch other
  processes.
- Every release file is listed with its SHA-256 in a release manifest that is signed with the
  project's key. The installer verifies that signature before it installs anything, and the
  launcher checks the SHA-256 of each runtime file before it loads it.

Source code: https://github.com/capie-mods/capie
Release:     https://github.com/capie-mods/capie/releases/tag/v1.5.0

Files flagged by your engine:
{FILE LIST}

Thank you. I am happy to answer questions or provide builds.

<NAME>
Maintainer, CAPIE
<YOUR E-MAIL>
```

File lines (copy the ones the vendor flagged):

```text
CAPIE-1.5.0-win64.zip
  SHA-256 e949dafe4a1c588f764f6a5d0a9e8dcc9f0edb5a7d6f158327ee6d78fb20d6c2
  https://www.virustotal.com/gui/file/e949dafe4a1c588f764f6a5d0a9e8dcc9f0edb5a7d6f158327ee6d78fb20d6c2
RenameAnythingLauncher.exe
  SHA-256 9f4e3bbb8af2588c6401a601cb15d94f198c47cf62523bbd86e600b05f0a5664
  https://www.virustotal.com/gui/file/9f4e3bbb8af2588c6401a601cb15d94f198c47cf62523bbd86e600b05f0a5664
runtime/capie_core.dll
  SHA-256 7a1c946f8a15f6120a0232453e3013b9f854ec2a6b526d0ab9c86f19959d0de1
  https://www.virustotal.com/gui/file/7a1c946f8a15f6120a0232453e3013b9f854ec2a6b526d0ab9c86f19959d0de1
runtime/capie_editor.dll
  SHA-256 0dad2732552305f43e1e87fb6e10e6a5b473e683ea27f1e82d0ebd9b8934d656
  https://www.virustotal.com/gui/file/0dad2732552305f43e1e87fb6e10e6a5b473e683ea27f1e82d0ebd9b8934d656
runtime/capie_injector.exe
  SHA-256 eb5e9ff5f9b944b55ca5eb4b343d18d507584284fb60f5f119a8be8843484c25
  https://www.virustotal.com/gui/file/eb5e9ff5f9b944b55ca5eb4b343d18d507584284fb60f5f119a8be8843484c25
proxy/version.dll
  SHA-256 796b55decc1e391921e7c40f66751073e1d2248c747ba287e48793e28c7e8adf
  https://www.virustotal.com/gui/file/796b55decc1e391921e7c40f66751073e1d2248c747ba287e48793e28c7e8adf
```

### CrowdStrike

To `VTscanner@crowdstrike.com`. Subject:
`False positive: CAPIE 1.5.0 (CrowdStrike Falcon ML on VirusTotal)`. Generic body; files:
launcher (`win/malicious_confidence_70% (D)`), `capie_core.dll` (60%), `capie_editor.dll` (70%),
`capie_injector.exe` (90%). Add before the file list:

```text
Since each release has new hashes, could you tell me whether there is a way to have future
builds of this open-source project reviewed, other than reporting each build?
```

### McAfee

To `virus_research@mcafee.com`. Subject (`FALSE` first):
`FALSE - false positive: CAPIE 1.5.0, McAfeeD detection ti!` Generic body; files:
`capie_core.dll` (`ti!7A1C946F8A15`), `capie_editor.dll`, `capie_injector.exe`. Add:

```text
The detections are reported by the "McAfeeD" engine on VirusTotal. This is a false-positive
report, not a malware submission. Please route it to a human analyst.
```

### Cynet

To `soc@cynet.com`. Subject: `False positive: CAPIE 1.5.0 (Cynet on VirusTotal)`. Generic body;
files: `capie_core.dll` (`Malicious (score: 100)`), `capie_editor.dll`.

### SecureAge APEX

Form at <https://www.secureage.com/contact-us>, "Submit false positive". Paste the generic body
into the message field; files: launcher, `capie_injector.exe` (APEX `Malicious`). Mention the
engine name: "APEX engine on VirusTotal".

### Bkav

To `fpreport@bkav.com`. Subject: `False positive: CAPIE 1.5.0 (Bkav Pro on VirusTotal)`. Generic
body; files: ZIP (`W32.Malware.6471E83C`), launcher, `capie_core.dll`, `capie_editor.dll`,
`capie_injector.exe`.

## Reducing detections: what actually helps

ML engines score static features (imports, sections, entropy, resources, signature, strings)
and reputation (how old the hash is, how many machines have it, who signed it). Our files score
badly on both: they are unsigned and have no version resources, `capie_injector.exe` is a small
binary whose whole job is cross-process injection, and every release brings fresh hashes. In
order of value for the effort:

1. **Code signing (biggest effect).** A signature from a trusted CA lowers ML scores directly and
   lets reputation build up per publisher instead of per hash. Apply to the SignPath Foundation
   as planned in [ADR-0031](../adr/0031-trust-without-paid-code-signing.md) (free, signs CI
   builds; this ties in with step 2 of [releasing](releasing.md)). Certum Open Source is the
   cheaper paid fallback. Sign every PE file, not only the launcher. Since 2024 an EV certificate
   no longer skips SmartScreen reputation, so EV is not worth the extra cost here.
2. **Fold the injector into the launcher.** A separate small EXE that only does
   `OpenProcess` → `VirtualAllocEx` → `WriteProcessMemory` → `CreateRemoteThread(LoadLibraryW)`
   is the textbook injector profile and scores highest (CrowdStrike 90%). In a launcher with a
   real UI and other code, the same imports weigh far less. Better still, start the game
   suspended with `CREATE_SUSPENDED` and load the DLL before resuming, or make the proxy
   `version.dll` the default path
   ([ADR-0024](../adr/0024-proxy-dll-loader.md)). The proxy needs no cross-process API at all,
   and it is already the least-flagged file (1 engine).
3. **VERSIONINFO and manifests.** No `.rc` file exists in `plugin/capie` today, so the binaries
   have no `VS_VERSION_INFO`. Add one per binary (CompanyName "CAPIE project", ProductName,
   FileDescription, FileVersion, OriginalFilename, LegalCopyright with MIT). The launcher already
   embeds a manifest with `asInvoker` (`build_launcher.ps1`); `build.ps1` links
   `capie_injector.exe` without `/MANIFEST:EMBED`, so give it the same manifest, and add a
   `supportedOS` section to both. These are cheap, standard ML features, and their absence counts
   against us.
4. **Names and strings.** The words `injector` and `inject` in file names, exports and strings are
   features in themselves. Rename to something descriptive (`capie_loader.exe` or no separate
   EXE at all, see point 2). Keep the launcher name consistent with the product
   ([ADR-0001](../adr/0001-product-names.md)): `RenameAnythingLauncher.exe` inside a CAPIE ZIP
   looks mismatched. Do not hide strings; plain, readable strings help.
5. **Reputation over time.** Keep the same signing identity, a stable download URL (GitHub
   Releases) and few, larger releases rather than many small ones. Report every release (this
   page) so that vendors' allow-lists and models see the files as clean.
6. **Build hygiene.** Release builds with `/DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA /guard:cf`,
   full PDB stripped from the binary, no debug-only code paths, and a stable toolchain. Normal,
   "boring" PE layout is what the models expect from legitimate software.
7. **Never use packers, protectors or obfuscators** (UPX, Themida, VMProtect, string encryption).
   High-entropy sections and packer stubs are among the strongest malware features. Packing
   would raise detections sharply and contradicts the open-source story.

Suggested order: 3 and 4 (hours, next release) → 2 (design change, one release) → 1 (as soon as
SignPath accepts the project) → 5 and 6 on every release.

## Sources

- [VirusTotal: false positive contacts](https://docs.virustotal.com/docs/false-positive-contacts)
- [CrowdStrike contact page](https://www.crowdstrike.com/en-us/contact-us/)
- [McAfee KB 000001921](https://www.mcafee.com/support/s/article/000001921)
- [SecureAge contact page](https://www.secureage.com/contact-us)
- [SecureAge: APEX in VirusTotal](https://www.secureage.com/secureage-integrates-artificial-intelligence-powered-apex-anti-malware-engine-virustotal/)
- [False-Positive-Center (community mirror of vendor contacts)](https://github.com/yaronelh/False-Positive-Center)
- [Cyotek: CrowdStrike Falcon false positives](https://www.cyotek.com/blog/crowdstrike-falcon-false-positives)
- [16x Prompt: Bkav false alarm](https://prompt.16x.engineer/blog/bkav-false-positive-false-alarm)
- [AV-Comparatives: reporting false positives](https://av-comparatives.org/dealing-with-false-positives-reporting-issues-to-antivirus-vendors/)
