# Initializer DYLD nie dowodzi bezczynności kodu

Uruchomienie: `python3 tools/test-macos-startup.py`.
Skrypt tworzy własny child i bibliotekę marker ładowaną przez
`DYLD_INSERT_LIBRARIES`. Nie uruchamia gry ani launchera, nie zmienia kodu,
nie zatrzymuje wątków. Istniejący test sześciu przypadków DYLD pozostaje bez zmian.

Pomiar 2026-10-16 na macOS 27.0.1/arm64: 12 uruchomień, po trzy na wariant.
Każdy wariant mierzy `task_threads` w initializerze marker oraz w `main` child.
Surowy wynik: [startup-fixture-results.json](startup-fixture-results.json).

| Proces | Wcześniejszy celowy worker | Initializer | main | Powtórzenia |
| --- | --- | --- | --- | --- |
| arm64 | nie | 1 | 1 | 3/3 |
| arm64 | tak | 2 | 2 | 3/3 |
| x86_64 / Rosetta (`translated=1`) | nie | 2 | 2 | 3/3 |
| x86_64 / Rosetta (`translated=1`) | tak | 3 | 3 | 3/3 |

Nie ustalono tożsamości dodatkowego wątku w czystym procesie Rosetta.
Nie można z tego pomiaru wyprowadzić, że ten wątek jest bezpieczny, ignorowalny,
zawsze występuje albo nigdy nie wykona kodu objętego patchem.
Obserwacje dotyczą dwóch punktów czasowych, nie całego życia procesu.

Wariant prior-worker wymusza zależność linkera marker→worker.dylib.
Initializer zależności uruchamia pthread i czeka na potwierdzenie jego gotowości,
a worker pozostaje aktywny do końca własnego procesu. Marker sprawdza gotowość
zależności przed pomiarem. Jest to kontrolowany kontrprzykład dla założenia,
że initializer wstrzykniętej biblioteki musi poprzedzać wszystkie worker threads.
Nie jest to pomiar bibliotek Steam ani rzeczywistej kolejności bibliotek w grze.

## Ocena minimalnego gate

Warunek `task_threads == 1` może być **warunkiem odmowy**, ale nie jest tu
wystarczającą gwarancją pozwalającą instalować hooki. W mierzonym czystym x86_64
pod Rosettą odrzuca wszystkie trzy uruchomienia już w initializerze.
Zastąpienie go warunkiem `<=2`, filtrowanie nieznanego wątku lub założenie,
że „to tylko Rosetta”, byłoby nieudowodnionym obejściem.

Snapshot nie rezerwuje wyłącznego prawa wykonania, nie zapobiega kolejnemu
utworzeniu wątku i nie dowodzi zachowania helperów wywołanych po pomiarze.
Do safe-early-patch potrzebne byłoby niezależne potwierdzenie całego okna:
żaden wykonawca nie może wejść na target page, również przez callback/sygnał,
a bieżący kod patchera i jego helpery muszą leżeć poza nią. Bez tego nie wolno
przekazać `caller_quiescent=true` na podstawie samego wyniku enumeracji.

Kod initializerów działa podczas inicjalizacji bibliotek. Ten fixture nie
ustala kontraktu reentrancy dyld ani bezpieczeństwa dodatkowych wywołań loadera,
alokatorów, callbacków czy oczekiwania na obce blokady w tym miejscu. Dodanie
loader callbacks lub synchronizacji bez takiego dowodu może wprowadzić nowe
wyścigi albo deadlock. Zewnętrzne biblioteki wstrzykiwane przez Steam lub inne
oprogramowanie mogą mieć własne initializery i worker threads; wariant kontrolny
pokazuje samą możliwość, nie dowodzi konkretnego zachowania Steam.

**Decyzja:** nie aktywować hooków gry z takiego gate. Potrzebny pozostaje
potwierdzony lifecycle/rendezvous obejmujący wszystkie wykonawców target page
albo inny mechanizm z równie konkretnym dowodem. UI tick i samo wykonanie
initializera DYLD nie dostarczają tego dowodu. Nie powstała tu implementacja
thread-suspend ani synchronizacji produkcyjnej.
