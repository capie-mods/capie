# Osobny logger producenta klawiszy naszych pól

**Nieaktywny kandydat diagnostyczny**, bazujący wyłącznie na render bridge
`104F5C572496D610E5A395129BC2CCAA57B162B61BD88F0FF88B82AD99219517`.
Nie zawiera nowej logiki fokusu, zaznaczania ani Enter→Apply. Nie zmienia
moda, fixture, konfiguracji, istniejących buildów lub działającej gry.

Dodaje obserwację **przed** oryginałem `22F4580(CEditBox*, wrapper*)` oraz przed
dotychczasowym Enter predicate w hooku TB `22F1030`. Wrapper zawsze kończy się
`return original_wrapper(edit,event)`: dokładnie jedno przekazanie, bez SEH,
locka, retry, normalizacji bool lub odczytów po powrocie. Pełne RAX jest opaque
`uint64_t`; nie przypisujemy mu semantyki bool. Wygenerowany ASM zachowuje RCX/RDX
i kończy wrapper przez `rex_jmp rax` do oryginału. Dotychczasowy TB hook nadal
przekazuje oryginał raz i zachowuje pełny wynik.

Przed odczytem zdarzenia wymagany jest dokładny własny primary vtable2B4A940,
TB2B4ABD0, definition2B54050 oraz jeden z pięciu nazwanych fieldów bridge.
Obce nazwy i błędne typy przechodzą do oryginału bez logowania payloadu.
**Nie wymagamy** widoczności, aktywnego receivera lub readiness do rekordu
naszego pola: fałszywe guardy mają być widoczne w diagnostyce.

Nowy rekord `RN_KEYLOG` kopiuje tylko osiem DWORD-ów (decimal/hex), wrapper+7C,
flag75/271, maski powodzenia odczytu, thread/sequence oraz skalary lifecycle.
Manager+158 i +230 są logowane wyłącznie jako relacje bool do edit/edit+1A0.
Osobno pokazuje key0==13, modifier2==0, field7==0, wrapper type1, mask02 disabled,
mask08 visible, mask01 active i powodzenie odczytu managera. Nie kopiuje tekstu,
nazw obcych pól, adresów lub wskaźników do loga/persistent state. Istniejące
prefill/Enter diagnostic baseline nadal mają swoje wcześniejsze logi tekstowe;
nie są częścią nowego producenta diagnostycznego.

`RN_KEYTICK` po oryginalnym Tick i istniejącym observe_tick świeżo rozwiązuje
własny View/model/root oraz dokładnie nazwane pola. Loguje pierwszą obserwację
i zmianę skalarów, w tym epoch/generation, fokus, hidden/invalid view. Dla
invalid View zapisuje tylko rozpoznany własny model; nie dereferencjonuje root.
Reset bind jest widoczny przez istniejący log oraz zmianę lifecycle generation.
Nie ma heartbeat ani pętli zrzucającej tekst co klatkę. Nowe history zawierają
tylko skalary; żaden borrowed widget/root/manager nie jest zachowywany.

Limit na cały proces: 32 rekordy klawiszy + jeden marker cap dla każdej pary
source(wrapper/TB)/own field. Saturating CAS zapobiega wrap/repeat. Status tick
ma 64 przejścia + jeden marker cap na field/model. Po cap oryginały nadal są
przekazywane dokładnie raz; żaden limit nie blokuje gry ani klawisza.

10 dokładnych unikalnych fingerprintów pinned EXE, sześć hooków (pięć
dotychczasowych + wrapper22F4580). Pełny 41-byte FP wrappera pochodzi z primary
raw `day2-enter-producer-raw.txt` SHA256
`47F5AAB2CB225CC9D412DADD3CF0CB151306ADF1D2BF20ECA6D02E947E1DD9A6`.
Prefill, lifecycle, pending Enter, dispatch/payload/capability, pięć pól oraz
ich nazwy pozostają zgodne z render bridge. Tick wrapper ma wyłącznie dodane
observe; TB hook wyłącznie prefix przed predicate. Brak nowych native setterów,
focus requests, consumed writes, dispatch, observera lub input synthesis.

Build `build-inline-editor-key-logger.ps1 -MinHookDir <minhook>` tworzy tylko
`plugin/build/inline-editor-key-logger`. Własny C++ `/std:c++20 /MT /W4 /WX`;
zachowuje osobny MinHook C. Nowe outputy: DLL
`rename_anything_inline_editor_key_logger.dll`, injector
`inject_inline_editor_key_logger.exe`, `inline_key_logger_test.exe`, ASM
`inline_editor_key_logger.asm` i osobny runtime `inline_editor_key_logger.log`.
Test używa rzeczywistego readera/forwardera na własnych fake obiektach; sprawdza
all8DWORD, guard-page read failure, foreign/type rejection, opaque64 RAX,
original once/no locks, cap po powtórzeniach oraz świeże tick transitions.
Zachowuje rzeczywisty builder testów 35/69/70. Negatywny test ładuje DLL tylko
we własnym disposable test EXE: host hash odrzuca init kodem10 przed hookami,
ponowienie zwraca33. Nie jest to aktywacja w HOI4.

RNInitialize odrzuca stare inline editor/render, focus i wcześniejsze native
proby oraz inny HMODULE o własnym basenamie. Guard jest **asymetryczny**:
zamrożony stary moduł nie zna nowego logger basenamu. Nie wolno próbować
odwrotnego późniejszego co-load. Wyłącznie jeden wariant w świeżym procesie;
nie podmieniać/reloadować załadowanej DLL. Root decyduje o oddzielnej aktywacji.

Pierwszy runtime ma porównać zwykły znak/Backspace z Return po kliknięciu pola.
Wrapper bez TB przy false routing gate wskazuje konkretny etap routingu;
TB z false predicate/focus/readiness pokazuje odrzucony warunek. Brak obu
rekordów nie dowodzi problemu SDL/OS. Logger nie zalicza Enter→Apply, rename,
target safety, menu/load, multiplayer ani działania native focus/select-all.

Kontrakt/propozycja: [monorepo archive: docs/evidence/session-20261011/day2-enter-producer-logger-proposal.txt](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/session-20261011/day2-enter-producer-logger-proposal.txt).
Wyniki, hashe i granice:
[monorepo archive: docs/evidence/session-20261011/day2-inline-editor-key-logger-candidate.json](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/session-20261011/day2-inline-editor-key-logger-candidate.json).
Zachowany compiler ASM forward:
[monorepo archive: docs/evidence/session-20261011/day2-inline-editor-key-logger-forward-asm.txt](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/session-20261011/day2-inline-editor-key-logger-forward-asm.txt).
