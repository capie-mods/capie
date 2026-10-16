# Early-init: dowody i granice hipotezy

Status: analiza tylko do odczytu, 2026-10-16. Nie daje podstaw do ustawienia
`caller_quiescent=true`. Nie uruchomiono nowego fixture ani gry w ramach tej analizy.
Badany plik HOI4 1.19.3.0, x86_64, image base `0x100000000`, SHA-256:
`09419b087f8b4656c00df337bf838ff34c9b1d816b76cef5383ae6a4ac15b1fb`.

## Co wynika z dokładnego pliku gry

Analiza używa `docs/macos/re/macho.py`, pełnego zrzutu llvm-objdump oraz
`llvm-objdump --macho --exports-trie`. Adresy poniżej są nieslidowane.

| Obserwacja | Wynik | Granica dowodu |
| --- | --- | --- |
| Eksporty adresowe | 45052 wpisy | Brak eksportu nie oznacza braku wywołania po adresie |
| `lookup_n`, `lookup_z`, `exists` | `0x10288da90`, `0x10288d420`, `0x10288e4c0`; żaden nie jest eksportowany | Nie wyklucza wskaźników obliczanych w runtime |
| Literalne wskaźniki qword do tych trzech wejść | Po 0 trafień w całym pliku | Nie obejmuje adresowania względnego ani callbacków obliczanych |
| Bezpośrednie CALL/JMP do wejść w zrzucie | Odpowiednio 1, 9, 17; funkcje zawierające te instrukcje nie są eksportowane | To nie jest pełny graf wywołań |
| `__DATA,__mod_init_func` | VA `0x1032a2b78`, 45768 bajtów, 5721 wskaźników | Żaden adres wejścia initializera nie leży na stronach poniżej; wywołania pośrednie pozostają niezbadane |
| `LC_MAIN` | entryoff `0xb330` (45872), stacksize `0x400000` | VA wynikające z bazy: `0x10000b330`; nie analizowano tu treści main |

Operacja zmiany praw dotyczy całych stron, nie tylko trzech prefixów. Dla stron
4096 B użytych w dotychczasowym fixture x86_64/Rosetta:

| Zakres strony, koniec wyłączny | Przecinające go przedziały funkcji z LC_FUNCTION_STARTS |
| --- | --- |
| `0x10288d000..0x10288e000` | 11; pierwszy zaczyna się przed stroną, w `0x10288cc30` |
| `0x10288e000..0x10288f000` | 7; pierwszy zaczyna się przed stroną, w `0x10288dff0` |

Żaden początek tych funkcji nie występuje w eksportach. To nie dowodzi, że
zależność nie może dotrzeć do ich kodu pośrednio. Rozmiaru strony z fixture nie
wolno zamienić w niezweryfikowaną stałą dla dowolnego hosta.

Obraz ma `__DATA,__objc_classlist` długości 88 B. Nie zaobserwowano sekcji
`__objc_nlclslist` ani `__objc_nlcatlist`; nie wykonano pełnego audytu metod ObjC
i `+load`. W 24 bezpośrednich zależnościach są między innymi `libsteam_api.dylib`,
`libPDXSDK.dylib`, `libtbb.dylib`, libc++, libSystem, libobjc i frameworki Apple.
Nie przypięto hashami całego przechodniego łańcucha ani bibliotek overlay.

## Co mówi oficjalne źródło dyld

Źródło Apple odczytano 2026-10-16. Wersja repozytorium:
`fd8d0c4d52320ebf64db34f3cb280310d905c5ae` (commit z 2026-06-15).
Nie potwierdzono, że jest to dokładnie dyld zainstalowanego macOS.

[`runAllInitializersForMain`](https://github.com/apple-oss-distributions/dyld/blob/fd8d0c4d52320ebf64db34f3cb280310d905c5ae/dyld/DyldAPIs.cpp#L3752)
najpierw inicjalizuje libSystem i biblioteki poniżej niego, wraz z powiadomieniami
ObjC. Następnie uruchamia inserted dylibs przed głównym executable.
Nie jest to obietnica, że konstruktor inserted dylib jest pierwszym kodem procesu.

[`Loader::runInitializersBottomUp`](https://github.com/apple-oss-distributions/dyld/blob/fd8d0c4d52320ebf64db34f3cb280310d905c5ae/dyld/Loader.cpp#L2486)
uwzględnia zależności i upward links; powiadomienie inicjalizacji ObjC poprzedza
initializery C++ danego obrazu. Rekurencyjna inicjalizacja zależności i callbacks
nie są wyłącznością wykonania dla naszego konstruktora.

[`dyldMain.cpp`](https://github.com/apple-oss-distributions/dyld/blob/fd8d0c4d52320ebf64db34f3cb280310d905c5ae/dyld/dyldMain.cpp#L903)
wywołuje initializery przed przekazaniem sterowania do main. Ogólny kontrakt
inicjalizacji bibliotek opisują też
[Apple Dynamic Library Design Guidelines](https://developer.apple.com/library/archive/documentation/DeveloperTools/Conceptual/DynamicLibraries/100-Articles/DynamicLibraryDesignGuidelines.html).

## Czego nadal nie udowodniono

1. Brak eksportowanych targetów i brak wejść initializerów na stronach nie
   wykluczają pośredniej ścieżki zależność → callback → kod gry na tych stronach.
2. „Czysty łańcuch Steam” nie jest obecnie zamkniętym, przypiętym zbiorem obrazów
   i callbacków. Obce `DYLD_INSERT_LIBRARIES` oraz overlay mogą uruchomić workery
   wcześniej; sam zakaz dodatkowych zmiennych środowiska nie opisuje całego procesu.
3. Istniejący [startup fixture](startup-fixture.md) wykazał wcześniejszy worker
   zależności oraz dwa wątki w czystym wariancie Rosetta. Nie ustalono tożsamości
   dodatkowego wątku ani kontraktu translatora przy zmianie praw stron.
4. Snapshot liczby wątków, nawet równy 1, nie gwarantuje późniejszej wyłączności.
   Nie udowodniono też braku reentrancy przez dyld/ObjC ani przejścia bieżącego
   patchera lub jego helperów przez zmienianą stronę.

Najmniejszy własny model porządku inicjalizacji to executable + inserted dylib +
zależność z wcześniejszym initializerem/workerem. Taki negatywny przypadek już
istnieje w startup fixture. Kolejny podobny test nie zamknie dowodu dla gry.
Hipoteza early-init pozostaje otwarta; wymaga dowodu osiągalności całych stron
i zachowania translatora dla dokładnego łańcucha, zanim będzie mogła uzasadnić
produkcyjną instalację hooków.
