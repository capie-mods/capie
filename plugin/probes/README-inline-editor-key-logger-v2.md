# Logger klawiszy v2 — osobny kandydat diagnostyczny

Bazuje na zamrożonym loggerze `7BAF2756…E0AFB904`, którego DLL ma hash
`349D118F…C8107A54`. Poprzednie źródła, outputy i aktywne assety pozostają
niezmienione. V2 nie był aktywowany w HOI4; wynik build/unit nie zalicza routingu
klawiszy, Enter, rename, fokusu ani multiplayer.

PID40560 wyczerpał poprzedni limit przez 32 odczyty zerowych eventów wrapper/TB
przed próbami klawiszy. Statusy `flags271=91↔81` zużyły osobny limit ticków.
Z takiego logu nie można stwierdzić, że zerowy event był odrzuconym Return.

V2 zachowuje dokładne osiem DWORD-ów, `wrapper+7C`, raw `copy_mask`, pełne flagi,
manager relations, lifecycle, pięć własnych nazw i typów. Każda para source
wrapper/TB oraz own field ma **trzy oddzielne saturating budżety**:

- `meaningful`: pełny odczyt ośmiu DWORD-ów i co najmniej jeden niezerowy; 32
  rekordy oraz dokładnie jeden marker `category=meaningful category_cap=32`.
- `empty_poll`: pełny odczyt i osiem zer; 2 rekordy + własny `category_cap=2`.
- `event_read_failed`: brak pełnego odczytu ośmiu DWORD-ów; 2 rekordy + własny cap.

Zera mogą być normalnym pollem; nie nazywamy ich nieudanym Return. Nieudany
odczyt samego `wrapper+7C` jest widoczny przez `wrapper_read=0` i `copy_mask`;
nie zmienia klasy poprawnie odczytanych DWORD-ów. Częściowy odczyt DWORD-ów
z niezerowym prefixem nadal jest `event_read_failed`, nie meaningful.
Puste odczyty i read failure nigdy nie zużywają budżetu meaningful. Każdy limit
ogranicza tylko log: obydwa oryginały nadal dostają każde wywołanie dokładnie raz.

Tick dedup porównuje `flags75&08`, `flags271&03`, maski udanego odczytu,
manager relations, lifecycle i View. Pozostałe bity nie powodują przejść ani
zużywania limitu64. Pierwszy faktyczny snapshot i każde rzeczywiste przejście
zachowują pełne raw flags obok `routing75/routing271`; nie przypisujemy
nieudokumentowanemu bitowi10 znaczenia „blink”. Bez heartbeat i engine-pointer cache.

Nadal 10 exact fingerprintów / 6 hooków. `wrapper_observed` jest identyczny:
prelog, następnie bezwarunkowy tail return oryginału z opaque pełnym uint64 RAX.
TB hook, jego Enter predicate/pending, transport35/69/70, prefill/render, dispatch,
capability i lifecycle są niezmienione. Nie dodano input synthesis ani Apply dispatch.

Build `build-inline-editor-key-logger-v2.ps1 -MinHookDir <minhook>` zapisuje tylko
`plugin/build/inline-editor-key-logger-v2`. Osobne nazwy:

- `rename_anything_inline_editor_key_logger_v2.dll`
- `inject_inline_editor_key_logger_v2.exe`
- `inline_key_logger_v2_test.exe`, `inline_editor_key_logger_v2.asm`
- runtime `inline_editor_key_logger_v2.log`

Własny C++ jest budowany `/std:c++20 /MT /W4 /WX`. Test używa rzeczywistych readera,
emitera, status observera i forwardera na własnej pamięci: 2000 pustych polli
i 2000 readfail na każdym source, potem Backspace/Return; osobne capy; wszystkie
osiem pozycji DWORD; guard-page partial reads; foreign/wrong primary/TB/definition;
manager i routing transitions; ignorowane raw flag toggles; full RAX i lock-free
original once. Zachowuje 7931 bazowych checks rzeczywistego buildera.
Jedyny DLL load test następuje we własnym disposable test EXE: wrong-host init10,
retry33 przed pin/hookami. Nie jest to injection ani aktywacja w grze.

V2 RNInitialize jawnie odrzuca stary keylogger, render, focus i wcześniejsze
bridge warianty. Stary keylogger nie zna v2 basename: guard jest asymetryczny,
więc **zakaz co-load w obu kolejnościach; świeży proces z jednym wariantem**.
Nie nadpisywać ani reloadować załadowanej DLL. Root zatwierdza runtime osobno.

W następnym runtime sprawdzić osobno nowe `category_cap`: empty cap nie wyklucza
diagnostyki klawiszy, meaningful cap ją ogranicza. Niezerowy event nie jest sam
w sobie dowodem naciśnięcia klawisza — zapisujemy dane, nie zgadujemy ich producer.

## Następna infrastruktura debugowania — plan, bez implementacji w v2

Użytkownik chce więcej widoczności po kliknięciu. V2 kończy wyłącznie poprawkę
budżetów i statusów; poniższe wymaga osobnego źródła, review i procesu QA.

1. Utworzyć exact manifest siedmiu własnych SG modeli oraz ich rzeczywistych
   Open/Cancel/Apply/QA-guards selectorów ze źródeł fixture. To osobny observer
   whitelist, bez poszerzania Apply whitelist lub logowania obcych selectorów.
   Każdy record: monotonic_ms, thread, sequence, model/control/stage i stabilny
   refusal enum. Rozróżnić view/type/context, epoch/readiness, pole/snapshot,
   codec, capability, ctor origin/count oraz zaobserwowany script error; nie
   wywnioskować wykonania skryptu lub sukcesu z samego dispatch/ctor.
2. Dodać bounded rolling ring meaningful records per own field/source, aby cap
   pliku nie usuwał najnowszej diagnostyki. Ring przechowuje tylko skopiowane
   skalary, bez engine pointerów. Wymaga spójnego atomic sequence snapshotu,
   osobnego testu concurrent reader/writer i jawnego lost/overwrite count.
3. Kandydat export `RNRequestDebugSnapshot(mask)` może wyłącznie ustawić atomowe
   flagi żądania; nie odczytuje GUI, nie wywołuje native i nie czeka na UI thread.
   Proponowane osobne bity: own routing/status, copied key ring, on-demand own
   UTF32 field snapshots. Nie są exportem ani requestflags zaimplementowanymi w v2.
4. Bezpieczny punkt wykonania to istniejący `tick_observed`: po `original_tick`
   i dotychczasowym observe, dopiero przy świeżo rozpoznanym własnym modelu.
   Żądania muszą być per model, aby pierwszy unrelated/hidden tick nie zgubił
   requestu dla innych pól. Consume i ograniczony TTL trzeba zaprojektować
   jawnie. Po fresh View/model/root/type/epoch/generation guard można skopiować
   wyłącznie własne nazwane pola; relookup po odczycie, żadnych borrowed cache.
   Foreign/invalid/hidden daje stage refusal, nie wymuszony focus/SetText/dispatch.
5. Snapshot tekstu wyłącznie na żądanie, z dotychczasowym limitem128 codepoints;
   obejmuje max pięć własnych fields, żadnego tekstu obcych kontrolek. Następny
   controller zbiera plik/ring, bez callbacku z własnym pointerem do engine.
   Testy: repeated request/TTL/model-order, foreign reads0, lock-free native
   forwarding, race-safe ring i brak side effects na target/buffer/Apply.

Wynik i finalne hashe:
[monorepo archive: docs/evidence/session-20261011/day2-inline-editor-key-logger-v2-candidate.json](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/session-20261011/day2-inline-editor-key-logger-v2-candidate.json).
Actual MSVC forwarding ASM:
[monorepo archive: docs/evidence/session-20261011/day2-inline-editor-key-logger-v2-forward-asm.txt](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/session-20261011/day2-inline-editor-key-logger-v2-forward-asm.txt).
