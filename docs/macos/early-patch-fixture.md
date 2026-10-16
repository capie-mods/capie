# Działający minimalny bootstrap C przed initializerami

Własny x86_64 fixture pod Rosettą: **4 scenariusze PASS**.
Nie ładuje HOI4 i nie zawiera jego adresów ani profilu. Nie zmienia plików
wykonywalnych; zmieniana jest prywatna strona pamięci własnego child.

Kod: `plugin/capie/tests/macos_early_patch/`.
Budowanie: `python3 plugin/capie/tests/macos_early_patch/build.py` wypisuje folder.
Dla folderu F uruchomić własny F/child z DYLD_INSERT_LIBRARIES=F/bootstrap.dylib:F/later.dylib.
Builder generuje profile.h z SHA obu własnych obrazów, adresem symbolu owned_target
oraz pełnym nagłówkiem/loadcommands child. Nie przyjmuje ścieżki gry.

| Gate | Późniejszy initializer | main przed/po arm |
|---|---|---|
| dokładny obraz i kolejność | installed=1, oryginalne7 | 7 → 42 |
| odwrócona kolejność inserted | installed=0 | 7 → 7 |
| nieznane dodatkowe inserted | installed=0 | 7 → 7 |
| błędny oczekiwany SHA child | installed=0 | 7 → 7 |

Pełny wynik: `early-patch-results.json`. Pole installed jest odczytywane przez
późniejszy initializer, więc dowód kolejności nie polega na kolejności buforowanych
wydruków printf. Podmiana istnieje już przed tym initializerem, lecz forwarduje
przez trampolinę oryginalną funkcję. Dopiero jawne arm w main przełącza wynik42.

`otool -L` potwierdza jedyną zależność bootstrapu: `/usr/lib/libSystem.B.dylib`.
To C11, bez static C++ constructors, libc++ lub frameworków GUI. Konstruktor
sprawdza dokładną listę/order inserted, odmawia selected override env, sprawdza
SHA child/later, loaded header/loadcommands i dokładny 14B prefix.
W fixture prefiks to14NOP. Local RW|COPY → write → icache → RX działa pod Rosettą.

## Ważna poprawka identyfikacji obrazu

W tym trybie dyld image0 jest inserted dylib, image1 later dylib, a executable
jest index2. Nie można utożsamiać indeksu0 z głównym obrazem. Read-only identity.c
potwierdziło filetype6,6,2. Bootstrap wyszukuje **dokładnie jeden MH_EXECUTE**
i używa jego indeksu do path/header/slide. Początkowy gate bezpiecznie odmówił
na SHA wskutek błędnego założenia index0; nie wykonywał wtedy żadnego zapisu.
Po trzech odmowach najpierw ustalono przyczynę oddzielnym odczytem identity,
a dopiero potem zmieniono założenie.

## Granice, których nie wolno przenieść jako gotowy produkcyjny kod

To zamknięty fixture. Znane later nie jest zależnością bootstrapu; callbacki
własnego procesu są jawne i nie uruchamiają workerów przed patchem. Nie jest to
jeszcze audyt wcześniejszego closure libSystem/ObjC/Rosetta na dokładnym hoście
gry ani gwarancja odporności na arbitralny preload. Produkcyjny gate musi
uwzględnić pełne dozwolone obrazy i mechanizmy override, nie tylko te testowe zmienne.

Fixture kończy własny child przez _exit80..82 po niepotwierdzonej mutacji/ochronie;
nie nazywa tego fallbackiem. To **nie spełnia jeszcze produkcyjnego kontraktu
„gra kontynuuje bez CAPIE” przy poisoned rollback**. Brak także produkcyjnego
zarządzania lifetime trampoliny/unwind. Root otrzymał działający model do
projektu transactional bootstrap, nie zgodę na aktywację HOI4.
