# Fixture: DYLD a hardened parent

**Własny test potwierdza, że samo ustawienie DYLD przed hardened parent nie
wystarcza do załadowania biblioteki w jego potomku.** Nie testowano tutaj gry,
Steama ani Paradox Launchera.

Odtworzenie (około 3 sekund na tym komputerze):

```sh
python3 tools/test-macos-dyld.py
```

Skrypt buduje w prywatnym katalogu tymczasowym dwa własne procesy i bibliotekę
z jednym symbolem. Podpisuje ad-hoc tylko własny parent z `--options runtime`,
weryfikuje podpis, następnie wykonuje poniższe scenariusze. Nie zmienia entitlements,
SIP, ustawień systemu ani zainstalowanych aplikacji. Zachowuje artefakty i JSON
wyników w wypisanym katalogu. Dylib to fixture bez API CAPIE i bez hooków.

Wynik 2026-10-16: wszystkie 6 scenariuszy PASS, po trzy dla arm64 i x86_64/Rosetta.

| Scenariusz | Parent | Child |
|---|---|---|
| Bezpośredni start własnego child z DYLD_INSERT_LIBRARIES | nie dotyczy | symbol biblioteki obecny, zmienna obecna |
| DYLD_INSERT_LIBRARIES przekazane hardened parent; zwykłe execv child | symbol nieobecny, zmienna usunięta | symbol nieobecny, zmienna nieobecna |
| Hardened parent sam ustawia DYLD_INSERT_LIBRARIES przed execv child | na wejściu symbol nieobecny, zmienna usunięta | symbol obecny, zmienna obecna |

„Symbol obecny” sprawdzany jest przez `dlsym(RTLD_DEFAULT, "capie_fixture_loaded")`;
„zmienna obecna” przez `getenv`. Skrypt wymaga dokładnych wyników i kodu wyjścia 0.
Nie traktuje samego wpisu w środowisku jako dowodu załadowania biblioteki.

## Znaczenie dla portu

Dla takiego łańcucha dyld musi dostać zmienną w środowisku *bezpośredniego startu
procesu docelowego*, już po granicy hardened runtime. Ustawienie jej wyłącznie
przez wrapper stojący przed hardened launcherem nie wystarcza. Fixture potwierdza
możliwość, gdy kod parenta współpracuje i jawnie ustawia środowisko dziecka.

Nie dowodzi, że obecny dowser udostępnia taki punkt konfiguracji. Nie dowodzi też,
że dowser tworzy hoi4 przez execv: rzeczywisty launcher może korzystać z innego
procesu, LaunchServices lub Steam. Nadal potrzebny jest dowód obsługi launchera,
finalnego playsetu i wskazania wyłącznie właściwej gry zgodnie z ADR-0004/0030.
Bezpośredni start gry przez wrapper nie jest automatycznie zgodną z ADR metodą
instalacji. Nie zmieniano istniejącego launchera ani pliku gry.
