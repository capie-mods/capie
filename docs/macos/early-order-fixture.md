# Minimalny first-inserted dylib: kolejność a osiągalność

Sprawdzono cztery własne przypadki, bez patchowania kodu, gry ani security.
Źródła: `plugin/capie/tests/macos_early_order/{child,first,later}.c`.
Wynik: `early-order-results.json`. Kompilacja C11 Wall/Wextra/Werror PASS.
Nie zmieniano wspólnych runnerów, engine, transportu ani core memory.

| Architektura | Zależności pierwszej inserted dylib | Calls do hidden target przed first ctor | Liczba wątków w first ctor |
|---|---|---:|---:|
| arm64 | tylko libSystem | 0 | 1 |
| x86_64/Rosetta | tylko libSystem | 0 | 2 |
| arm64 | libSystem + known later dylib | 1 | 1 |
| x86_64/Rosetta | libSystem + known later dylib | 1 | 2 |

W każdym wariancie first dylib stoi na pozycji zero DYLD_INSERT_LIBRARIES,
a later dylib na pozycji jeden. Bez zależności kolejność to first→later→executable
constructor→main. Z zależnością first→later dyld musi wykonać later wcześniej:
later→first→executable constructor→main. `otool -L` potwierdza, że minimalny
observer ma wyłącznie libSystem; nie linkuje libc++ ani frameworków GUI.

Later constructor pobiera wyeksportowany callback z executable. Callback wywołuje
hidden target, którego nie ma w `nm -g`. To konkretny kontrprzykład dla rozumowania:
„target nieeksportowany, executable initializers jeszcze nie działały, więc nikt
nie mógł wejść w target”. Sam hash znanej dependency nie eliminuje tej ścieżki.

## Zakres pozytywnego dowodu

W minimalnym wariancie ten kontrolowany kod nie wywołał targetu przed first ctor;
wszystkie własne ścieżki wywołania są widoczne w trzech małych plikach. Nie ma
własnego workera ani signal callbacku. Jest to dowód porządku i osiągalności
**dla zamkniętego fixture**, nie uniwersalna gwarancja quiescence dowolnego obrazu.
Rosetta nadal ma dodatkowy wątek. Snapshot calls=0 nie rezerwuje okna przyszłego
wykonania i nie ustanawia kontraktu translatora.

## Minimalny mechanizm potrzebny dla dokładnej gry

Minimalny bootstrap C zależny tylko od libSystem, pierwszy w inserted list,
może utworzyć węższy punkt audytu. Przed lokalnym patchem musi mieć dowód, że
wcześniejsze libSystem/sub-images/ObjC/Rosetta callbacks nie mogą wejść na całe
modyfikowane strony. Wymaga to zamkniętego, przypiętego zbioru wcześniejszego kodu
oraz analizy jego wiązań i callbacks do głównego obrazu; allowlista ścieżek lub
threadcount tego nie zastępuje. Bieżący launcher dopisuje CAPIE za Steam libraries,
więc nie realizuje nawet tej węższej hipotezy.

Włączenie pełnego C++ feature runtime należy odłożyć do momentu zakończenia
minimalnego bootstrapu: jego zależności nie mogą niejawnie rozszerzać wcześniejszego
closure. Nadal potrzebny jest osobny plan poisoned rollback i kontynuacji bez
CAPIE przy nieodwracalnej awarii zmiany praw. Nowy fixture nie zamyka tych dwóch
obowiązków i nie uzasadnia caller_quiescent=true w grze.

Odtworzenie ręczne: dla arm64 i x86_64 zbudować child z `clang -std=c11 -arch ARCH
-Wl,-export_dynamic`, later przez `-dynamiclib`, first przez `-dynamiclib`.
Wariant dependency dodaje `-DWITH_DEPENDENCY` i absolutną ścieżkę later przy linku.
Uruchomić wyłącznie własny child z listą `FIRST:LATER`; executablector i main
muszą wystąpić po obu dylib constructorach. Żadne ustawienie nie dotyczy launchera
gry ani globalnego środowiska.
