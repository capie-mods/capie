# Plan portu CAPIE na macOS — audyt zależności

Stan: 2026-10-16. **To plan implementacji, nie działający port.** Kontrolowana próba
załadowała do HOI4 diagnostyczną bibliotekę tylko do odczytu; dokładna tożsamość
obrazu i trzy prefiksy lookup były zgodne. Druga próba przeszła 240/240 kontroli
pełnego statycznego profilu (218 zakresów + 22 cele rel32) i dotarła do menu.
Nie aktywowano hooków ani API 7.
Nie zainstalowano produkcyjnego loadera i nie modyfikowano plików gry.
Zobacz [wynik diagnostyki](read-only-diagnostic.md). Windows pozostaje implementacją
referencyjną; jego adresów, ABI i zakresu testów nie należy zastępować wartościami macOS.

## 1. Wynik sprawdzenia istniejącej logiki

Na tym komputerze: Apple clang 21.0.0 (clang-2100.3.34.2), host arm64.
Istniejące pliki skompilowano bez zmian z `-std=c++20 -Wall -Wextra -Werror`.
Pliki wykonywalne powstały w katalogach tymczasowych systemu.

| Test | Wynik rzeczywistego wykonania |
|---|---|
| `plugin/capie/tests/public_names_test.cpp` | 177 checks, 0 failures |
| `plugin/capie/tests/legacy/protocol_test.cpp` | 18 field checks + 13 key-boundary checks passed |
| `plugin/capie/tests/legacy/editor_input_test.cpp` | 599 checks passed |
| `plugin/capie/tests/flag_layer_test.cpp` | Kompilacja zablokowana: `core/trace_ring.hpp:28`, brak `windows.h` |
| `plugin/capie/tests/legacy/country_status_test.cpp` | Kompilacja zablokowana: `core/memory.hpp:32`, brak `windows.h` |

Odtworzenie testu czystej logiki, bez gry i bez zapisu w repo:

```sh
out_dir=$(mktemp -d /tmp/capie-pure.XXXXXX)
clang++ -std=c++20 -Wall -Wextra -Werror \
  plugin/capie/tests/public_names_test.cpp -o "$out_dir/public_names"
"$out_dir/public_names"
```

To potwierdza przenośność gramatyki, alfabetu i protokołu, a nie ABI gry.
Gra jest x86_64; przyszłe biblioteki i testy integracji ABI wymagają `-arch x86_64`.
Dodatkowo runner `tools/test-macos-offline.sh` skompilował i uruchomił wszystkie trzy
powyższe testy zarówno z `-arch arm64`, jak i `-arch x86_64`. Wyniki były takie same;
uruchomienie x86_64 na hoście arm64 potwierdza działanie Rosetty dla tych testów.
Cały przebieg trwał około 7 sekund. Nie jest to test gry ani ABI jej klas.

Uruchom z katalogu repo:

```sh
tools/test-macos-offline.sh
```

Runner obejmuje teraz także `plugin/capie/tests/macos_memory_test.cpp`:
25 sprawdzeń na każdą architekturę. Nowy, niepodłączony do runtime backend
`plugin/capie/src/core/macos_memory.hpp` oferuje politykę
`capie::core::macos::ProcessMemory::read(address, out, size)` opartą na
`mach_vm_read_overwrite`. Sprawdzono rzeczywiste mapowania mmap: odczyt RW/RO,
brak dostępu PROT_NONE, zwolnione strony, zakresy przez granicę strony, dokładny
koniec strony, błędny destination, null, overflow i zero bytes. Pełny runner
przeszedł dla arm64 i x86_64: cztery zestawy, 832 sprawdzenia na architekturę.
Pierwsza kompilacja testu przed implementacją zakończyła się oczekiwanym błędem
brakującego nagłówka; po implementacji wszystkie przypadki przeszły.

Backend sprawdza status i pełną liczbę skopiowanych bajtów. Przy błędzie zawartość
bufora docelowego jest nieokreślona i musi zostać odrzucona. Nie zapewnia atomowego
snapshotu zmienianych obiektów ani ochrony podczas wywołań funkcji gry. Nie zastępuje
jeszcze Windows `memory.hpp` i nie dowodzi działania CAPIE w procesie gry.

Dodatkowo działa samodzielny adapter tylko do odczytu
`plugin/capie/src/engine/macos_string.hpp`: char i char32_t, wstrzykiwana polityka
pamięci, 24-bajtowy nagłówek libc++, SSO 22/+1 oraz UTF-32 4/+4, długie stringi
z capacity w elementach. `string_bytes` przyjmuje span obejmujący miejsce na NUL;
`name_equals` wymaga terminatora i odrzuca embedded NUL, `string_equals` porównuje
wyłącznie content. Nie tworzy stringów do przekazywania silnikowi i nie waliduje
alfabetu/Unicode — to odpowiedzialność istniejącej warstwy features.

`macos_string_test.cpp`: 70 testów pamięci syntetycznej na architekturę, w tym
krótkie/długie stringi obu szerokości, limity 22/4/128, capacity, odczyt bez terminatora,
embedded NUL, niedostępne dane, overflow i brak miejsca w buforze. Test napisano
przed nagłówkiem (RED: brak pliku); po implementacji PASS. Aktualny runner obejmuje
pięć zestawów: **902 sprawdzenia na architekturę**, arm64 oraz x86_64/Rosetta.
Backend pamięci i adapter stringów nadal nie są podłączone do runtime CAPIE.
Niezależny review nie wykrył błędów blokujących; test stringów przeszedł także
z ASan i UBSan (70 sprawdzeń). Przy integracji porównań trzeba przekazywać jawne
`std::basic_string_view`: dedukcja szablonu nie przyjmuje dotychczasowych `const char*`.

Runner jawnie ogranicza zakres, kończy się błędem kompilacji/wykonania zamiast
pomijać test, zachowuje binaria w wydrukowanym katalogu tymczasowym i nie usuwa
plików użytkownika. Na komputerze Intel wykonuje tylko wariant x86_64.

## 2. Konkretne miejsca wymagające portowania

Ścieżki w tabeli są względne do `plugin/capie/`. Zachować warstwy
`features -> engine -> core`, kompozycję w `dll`, dwa moduły i brak diagnostycznych
hooków oraz tekstu użytkownika w wydaniu release (ADR-0021).

| Obszar / pliki | Wymagana zmiana i dowód |
|---|---|
| `src/core/game_build.hpp`, `game_layout.hpp`, `signature_table.hpp`, `signatures.hpp`, `scanner.hpp`; wszystkie `src/features/**/*_pins.hpp` | Oddzielny profil dokładnego Mach-O: SHA-256, preferowana baza, adresy, offsety i sygnatury. Windows zachowuje swój profil. Każda wartość macOS wymaga instrukcji/callsite potwierdzającej semantykę; sam podobny ciąg bajtów nie wystarcza. Nie podstawiać zera ani Windows RVA za brak dowodu. |
| `src/core/pe_image.hpp`, `sha256.hpp`, `memory.hpp`, `hook_set.hpp` | Backend Mach-O z segmentami i ASLR slide zamiast PE; haszowanie bez bcrypt; bezpieczny odczyt przez macOS zamiast VirtualQuery/SEH; backend detourów zamiast MinHook. Wykrywanie adresu poza mapowaniem, przepełnienia i częściowego odczytu musi odmawiać działania. Propozycja do sprawdzenia: mach_vm_read_overwrite dla kopiowania pamięci, bez globalnego handlera SIGSEGV. |
| `src/core/log.hpp`, `trace_ring.hpp`; `src/features/editor/lifecycle.hpp`, `prefill.hpp`, `debug_observer.hpp` | Zastąpić Windows zegar monotoniczny, identyfikator wątku, SRWLOCK i I/O platformowymi odpowiednikami. Zachować limity, synchronizację i zakaz zapisu wprowadzanego tekstu w release. |
| `src/engine/msvc_string.hpp`, `game_calls.hpp`, `records.hpp`, `widgets.hpp`; `src/features/editor/trampolines.hpp`, `transport.hpp`; `src/features/country/*_hooks.hpp` | Adapter stringów libc++ zgodny z rzeczywistym kodem gry, rekordów i argumentów/zwrotów SysV. Usunięcie __fastcall nie dowodzi poprawnego ABI; ustalić hidden return pointers, ownership, lifetimes, konstrukcję/kopiowanie i sloty vtable. `game_calls` i `flag_layer_hooks` używają SEH do ochrony wywołań: sam bezpieczny odczyt nie zastępuje tej ochrony. |
| `src/dll/capie_core.cpp`, `capie_editor.cpp`; `src/launcher/*`, `src/proxy/*`, `src/tools/*`; `build.ps1`, `build_launcher.ps1` | Korzenie kompozycji dylib: inicjalizacja, eksporty, zależność editor/core, trwałość załadowania, pobranie głównego image i rozpoznanie konfliktów. Nowa ścieżka budowania i loader macOS; Windows proxy version.dll i zdalny LoadLibrary nie są przenośne. Windows skrypty i ich checki pozostają uruchamiane w Windows CI. |

Przed wyborem biblioteki hooków sprawdzić instrukcje przenoszone do trampoline,
RIP-relative operands, skoki, granice funkcji, atomowość aktywacji i rollback.
Testy muszą wymusić awarię na każdym etapie przygotowania, aktywacji i wycofania.
Nie włączać hooków częściowo i nie zwalniać trampoliny używanej przez trwający callback.

## 3. Build i testy offline do wdrożenia

1. Dodać oddzielny macOS build dla czystych testów oraz docelowych dylib x86_64.
   Nie budować z udawanym `windows.h` ani pustymi implementacjami MinHook.
   Istniejące testy czystej logiki uruchamiać najpierw; dopiero następnie testy adapterów.
2. Odseparować platformowe I/O od logiki tam, gdzie rzeczywiście blokuje testy:
   `playset_gate.hpp` ma już czyste parsery, lecz wciąga `config.hpp`, `win_handle.hpp`
   i `windows.h`; `public_keys_test.cpp`, `country_cache_test.cpp` i test statusu
   wciągają cały `capie_core.cpp`. Zachować te same przypadki i referencyjne wyniki.
3. Dodać macOS testy pamięci (null, unmapped, PROT_NONE, zakres przez granicę strony,
   overflow, błąd kopiowania), stringów libc++ (krótkie/długie, uszkodzone długości,
   terminator i embedded NUL), rekordów oraz fake-game testy wszystkich hooków.
   Testy z fałszywą pamięcią muszą korzystać z macOS layoutu, nie MSVC layoutu.
4. Wprowadzić niezależny test pliku Mach-O: dokładny hash, architektura, segment,
   jednoznaczna sygnatura w oczekiwanej lokalizacji i kontrola każdej pozycji mapy.
   Brak gry na dysku to jawne SKIP, nigdy PASS. Błędny hash/bajt oznacza brak aktywacji.
5. Przed uznaniem portu za gotowy uruchomić wszystkie dotychczasowe Windows testy,
   checki warstw, Python checki, oba warianty oraz macOS ABI/integrację x86_64.
   Ten audyt nie uruchamiał Windows builda i nie stanowi potwierdzenia braku regresji.

## 4. Loader i instalator: ograniczenia ADR

ADR-0004 i ADR-0030 wymagają przejścia przez Paradox Launcher, zachowania playsetu,
identyfikacji wyłącznie uruchomionej przez wrapper gry oraz przełącznika `CAPIE`.
Bramka czyta finalny `dlc_load.json`: missing/unreadable/invalid oznacza zwykłą grę.
Każdy moduł ponownie sprawdza hash gry i piny przed aktywacją. API 7 nie może być
ogłaszane przez pusty loader ani niekompletne hooki.

**Brak jeszcze dowiedzionej metody loadera spełniającej te wymagania na macOS.**
`DYLD_INSERT_LIBRARIES` przekazane do hardened `dowser` nie jest potwierdzoną ścieżką
propagacji do gry. Start samego `hoi4` z tą zmienną może służyć dopiero wyraźnie
opisanej ścieżce developerskiej, ale omija wymagany launcher i nie jest gotową
metodą instalacji zgodną z ADR. Nie zmieniać podpisu/entitlements dowser, nie wyłączać
SIP i nie modyfikować binariów gry jako domyślnego rozwiązania.

Kolejny dowód loadera: test na własnej małej aplikacji x86_64 i jej procesie potomnym,
który potwierdza dziedziczenie środowiska, moment inicjalizacji, bramkę playsetu,
tryb vanilla po błędzie oraz brak wpływu na obcy proces. Osobno trzeba sprawdzić
rzeczywisty łańcuch Steam → dowser → hoi4 i moment gotowości silnika. Własny fixture
nie dowodzi zachowania hardened launchera. Jeżeli jedyna wykonalna metoda omija
launcher, wymaga nowego ADR zamiast twierdzenia, że spełnia dotychczasowe decyzje.

W `rename-anything-macos/tools/install-macos.sh` istnieje tylko instalacja skryptów.
Nie dodawać automatycznego ładowania biblioteki do czasu powyższych dowodów.
Po ukończeniu loadera: osobny katalog CAPIE poza grą, instalacja dwóch bibliotek,
manifest/hash własnych plików, przywracanie poprzedniej konfiguracji, jawny uninstall
oraz instrukcje w obu README i docs. Instalator moda nie powinien sam włączać CAPIE
w playsecie ani zmieniać `dlc_load.json`.

Uwaga audytu: obecny instalator rozpoznaje własność deskryptora tylko po `name=`,
a `--copy`/`--uninstall` usuwa katalog `rename_anything_dev` niezależnie od manifestu.
Przed rozszerzeniem instalacji natywnej należy sprawdzić własność każdego usuwanego
artefaktu i testować kolizje w katalogu tymczasowym. W tym audycie niczego nie usuwano.

## 5. Dowody, hipotezy i akceptacja w grze

**Sprawdzone w tym audycie:** zależności Windows w wyżej wskazanych plikach,
wyniki trzech testów czystej logiki, blokady kompilacji dwóch kolejnych testów,
zasady ADR i zawartość obecnego instalatora. Nie potwierdzano ponownie adresów gry.

**Dowody binary RE:** odsyłać do `re-notes.md` i aktualnej mapy dowodów. Znane trzy
wejścia lokalizacji nie dają automatycznie pełnego pokrycia; lista inline lookupów
w starych notatkach ma rozbieżność między podaną liczbą a listą adresów. Trzeba
policzyć unikalne funkcje i udowodnić ścieżki `$KEY$`, CAPIE_STATUS/API/BUILD,
RN1 i danych krajów/fakcji. Wstrzyknięcie rekordów do tabeli lokalizacji jest tylko
hipotezą, dopóki nie znamy piszących, cache, przeładowania języka i lifetime poola.

**Propozycje, nie wyniki:** wybór detourów, mechanizm dostarczenia
dylib przez launcher oraz strategie pełnego pokrycia lokalizacji.

**Wymagane sprawdzenie w grze:** włączenie/wyłączenie CAPIE w playsecie, menu i logi,
edycja/Apply/Cancel/Enter dla API 1–2, konsola według faktycznego wariantu Windows,
nazwy kraju/przymiotnik/fakcja, flagi i portrety z brakującym DLC, utrwalenie po
save/restart/load, zmiana języka i cache, polityka hosta i uprawnień. Multiplayer,
hot join i Ironman pozostają osobnymi niewykonanymi testami; offline PASS nie
potwierdza ich zachowania. Niezgodny hash lub pin musi kończyć się zwykłą grą.

## 6. Warunek wydania: jeden release dla obu platform

Wymaganie użytkownika: jeden wspólny numer wersji i GitHub Release ma zawierać
zachowaną konfigurację Windows oraz konfigurację macOS. Gracz wybiera instrukcję
instalacji dla swojego systemu w obrębie tego samego wydania. Nie tworzyć osobnego
kanału ani rozbieżnych wersji funkcjonalnych dla macOS.

Zachować zgodność dotychczasowego pakietu Windows, jego nazw plików, manifestu
i instalatora. Do tego samego release'u dodać platformowy pakiet macOS z własną
konfiguracją, bibliotekami, loaderem, manifestem i instrukcją. Wspólny release
nie wymaga zmiany układu dotychczasowego ZIP Windows; oba zestawy muszą być
jednoznacznie dostępne na tej samej stronie wydania.

Warunki publikacji: testy obu platform, osobne hashe binariów gry i artefaktów,
sprawdzenie wyboru właściwego pakietu przez instalatory, osobne instrukcje Windows
oraz macOS i wspólne release notes ze stanem testów w grze. Nie publikować macOS
jako zgodnego API 7, dopóki implementacja i wymagane kontrole nie są kompletne.

## 7. Current implementation checkpoint (2026-10-16)

The inventory covers 164 Windows constants without inventing native equivalents:
45 proven, 112 static mappings, 5 requiring adapters, 2 replaced by explicit guard
policies, and no candidates. All 140 source pin roles are accounted for. The combined
export checks 218 static byte ranges; two initialized BSS values remain runtime
checks. This does not certify hook ABIs or runtime activation.

Shared localisation hook logic now builds on macOS with its Windows CRT branch
preserved. Twelve offline suites pass 1,155 checks per architecture (arm64 and x86_64
under Rosetta); the evidence checker/exporter and header generator have 23 passing Python tests.
The separate quiescent detour prototype is documented in `quiescent-detour.md`.

The next integration blocker is a proven safe activation point. Scripted-window
Tick at `0x100b44020`, called by the manager loop at `0x100b43f81`, is not evidence
of startup before worker threads: it operates on the active-window list and caches.
Hooking Tick first would itself require a safe initial patch. A GUI-thread callback
does not establish that localisation or other target pages are quiescent on all
threads. The current backend must not be installed into HOI4 on that assumption.

The earlier isolated launcher probe was inconclusive after three attempts.
A later test using installed Paradox 2026.12 and staged game settings successfully
ran the fixture child in enabled/disabled/invalid modes. A subsequent controlled
HOI4 launch loaded the read-only diagnostic and reached the main menu.
See `loader-research.md` and `read-only-diagnostic.md`. This does not prove OS
isolation, the complete Steam parent chain, or real-playset gating.
The production dylib pair, compliant launcher chain, native installer, Windows
regression build and in-game acceptance remain outstanding. Preserve one shared
Windows/macOS release once those requirements are met.

## 8. Shared name-layer integration checkpoint

Country/faction name protocol now compiles unchanged above a native macOS table
reader; see `variable-reader.md`. The full offline run passes 12 suites on both
architectures (2,310 assertions total), including 16 shared feature checks.
Windows memory and lookup branches retain their implementation; Windows build
regression has not run on this host. Apple inclusion of Windows game_layout.hpp
now fails explicitly, so further adapters must select their proven native layout.
`core/macos_game_layout.hpp` names the eight proven offsets currently consumed by
the integrated native variable reader. All Windows layout declarations were
compared against HEAD and remain identical; the inventory source digest was updated
for the added Apple compile-time rejection.

The external task-suspend experiment stopped before suspension because its own
Mach IPC transport did not deliver the task port (three failed arm64 attempts).
This is not evidence that task_suspend cannot work. See `suspend-fixture.md`;
no hook activation is authorized by that result and no security setting changed.

The shared memory/name-reader integration passed independent review. Static early-init
evidence is recorded in `early-init-evidence.md`: 45,052 export entries and 5,721
initializers do not establish exclusive access to the hook pages. No lifecycle
assumption was promoted to runtime readiness.

## 9. Flag-reader integration checkpoint

Ruling: continue the independently testable read-only feature adapters while safe
hook activation remains blocked. This does not authorize runtime patching.
Country flags now consume the native variable reader, and cosmetic tags use the
existing libc++ decoder at the proven country+0x1200 field. The Windows path is
retained. RED: Windows layout guard rejected the test; first implementation also
failed null-country rejection. GREEN: 32 shared feature checks pass on arm64
(ASan/UBSan) and x86_64/Rosetta. The added offset is documented in core.

The planned common Windows/macOS release remains unchanged. Native flag hooks,
portrait-reader integration, safe activation and Windows regression are unfinished.
