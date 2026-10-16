# Diagnostyczna dylib tylko do odczytu

Budowanie: `tools/build-macos-diagnostic.sh /absolute/new-private-output-directory`.
Test bez gry: `python3 tools/test-macos-diagnostic.py`.
Produkt: `capie_diagnostic.dylib`, wyłącznie x86_64. Nie jest to biblioteka CAPIE
API 7 ani gotowy loader. `nm -gU` na zbudowanym artefakcie nie wykazuje eksportów.

Ten dokument nie poleca uruchomienia gry. Biblioteka jest przeznaczona do osobno
przejrzanego, kontrolowanego uruchomienia przez wrapper developerski.
W ramach implementacji wykonano testy własnego fixture x86_64 oraz kontrolowaną
próbę z rzeczywistym HOI4 opisaną poniżej.

## Opt-in i raport

Constructor robi cokolwiek tylko po ustawieniu `CAPIE_DIAGNOSTIC_DIR` na
absolutną ścieżkę istniejącego prywatnego katalogu należącego do bieżącego UID,
bez uprawnień dla grupy i innych użytkowników. Odrzuca symlink jako końcowy
komponent katalogu. Tworzy nowy `capie-probe-<pid>.json` przez `openat`,
`O_EXCL` i `O_NOFOLLOW`, z uprawnieniami 0600. Nie nadpisuje istniejącego pliku.
Raport ma mniej niż 1024 bajty; błędy dostępu kończą diagnostykę bez aktywacji.
Błąd zapisu może pozostawić niepełny JSON, który nie jest pozytywnym wynikiem.

Raport nie zawiera ścieżki gry, argumentów procesu, danych uwierzytelnienia,
ciągów pobranych z gry ani danych użytkownika. Odczytywane tokeny są liczbowymi
identyfikatorami rejestru UI, nie sekretami.

## Kolejność kontroli

1. Dyld wskazuje plik i header obrazu głównego. Plik jest otwierany wyłącznie
   do odczytu, ma być zwykłym plikiem do 1 GiB; mapowanie jest read-only.
2. `core::macos::image::verify_game` sprawdza dokładny SHA-256 i strukturę Mach-O
   z rzeczywistym slide. Obcy plik kończy próbę przed odczytem pinów i tokenów.
3. Oczekiwany base+slide musi równać się adresowi main header z dyld. Header
   i wszystkie load commands (limit 128 KiB) są porównywane bajtowo z plikiem
   przez bezpieczny `ProcessMemory`. To kontrola tożsamości metadanych obrazu,
   nie hash całej pamięci procesu.
4. `macos_pins` porównuje trzy 14-bajtowe prefiksy `lookup_n`, `lookup_z`, `exists`
   oraz wygenerowany profil: 203 zakresy bajtów, 15 wskaźników uwzględniających
   slide i 22 dodatkowe kontrole celów rel32. Łącznie 240 kontroli obejmuje
   218 zakresów eksportu. Potem odczytuje dwa DWORD-y: `politics_tab` pod VA 0x1035af598 i `player_context`
   pod VA 0x1035ae4b8, oba po uwzględnieniu slide.

Kolejność tablic raportu: `lookup_prefix_matches` = lookup_n, lookup_z, exists;
`token_reads` = politics_tab, player_context. Pole `read=false` oznacza, że
wartość nie jest ważna. Wartości tokenów **nie są porównywane z oczekiwanymi**,
a pole `token_expected` zawsze brzmi `not_checked_after_init`: constructor może
wykonać się przed inicjalizacją rejestru gry. Nie ma późniejszego callbacku,
nowego wątku ani twierdzenia, że inicjalizacja dobiegła końca.

`static_subset_matches_not_activation` jest statusem starszego artefaktu
sprawdzonego w pierwszej próbie gry. Nowy build zwraca
`full_static_profile_matches_not_activation` wyłącznie po zgodności całego profilu.
`profile_checks` i `profile_matches` liczą kontrole; `profile_first_failure` jest
indeksem w kolejności bytes, pointers, relatives z wygenerowanego nagłówka albo -1.
Obcy obraz ma liczniki 0 i indeks -1. Nie jest to dowód stanu wątków, bezpieczeństwa
hooków, ABI, API, pokrycia lokalizacji ani zachowania funkcji gry.
Generator `re/generate_pin_header.py` weryfikuje hash gry, typowane manifesty
i zgodność 1:1 z eksportem. Relacja celu pojedynczego wpisu tablicy skoków jest
sprawdzana offline; runtime porównuje jego niezmienne bajty.
Biblioteka nie zapisuje do pamięci gry, nie zmienia ochrony stron, nie wstrzymuje
wątków, nie wywołuje funkcji gry i nie publikuje statusu „CAPIE loaded”.

## Wynik testu i ograniczenia

Cztery własne child launches przechodzą: bez opt-in, z poprawnym prywatnym
katalogiem, z katalogiem dostępnym dla innych oraz z symlinkiem katalogu.
Poprawny prywatny katalog otrzymuje bounded JSON 0600 ze statusem
`foreign_or_unreadable_executable`; identity i wszystkie read flags pozostają
false. Warianty niedozwolone nie tworzą raportu. Kompilacja x86_64 z
`-Wall -Wextra -Werror` przechodzi; wykonanie testów odbyło się pod Rosettą.

Ścieżka pozytywna z rzeczywistym HOI4 została wykonana 2026-10-16.
Constructor wykonuje I/O i hash całego pliku; nie zmierzono osobno jego kosztu. Odczyt pliku i snapshot obrazu nie są atomowe względem równoległych zmian pliku;
zgodny hash nie jest pełnym dowodem ABI. Czytelność tokenu nie dowodzi, że jego
wartość jest już zainicjalizowana. Nie obiecujemy bezpieczeństwa reentrancy
obcych initializerów ani odporności na równoległe skracanie mapowanego pliku.
Przed kontrolowanym uruchomieniem potrzebny jest niezależny review tej ścieżki
oraz konkretnej komendy wrappera; nie ma tu synchronizacji rozwiązującej lifecycle.

## Kontrolowana próba HOI4, 2026-10-16

Wynik w `read-only-diagnostic-results.json`: PID 62703, dokładny hash gry i
loaded identity zgodne, trzy prefiksy lookup zgodne. Raport 0600 ma status
`static_subset_matches_not_activation`. Oba DWORD-y były czytelne i równe 0
w constructorze; nie jest to ich walidacja po inicjalizacji. `lsof` potwierdził
mapowanie diagnostycznej dylib, `libsteam_api.dylib` i właściwy katalog roboczy gry.
Agent zaobserwował ekran Loading Databases, a następnie menu główne
Operation Postern v1.19.3.0.c01a (5632) (S); użytkownik potwierdził, że jest w grze. Brak testu kampanii, zmian nazw, hooków lub API 7.

Użyto zainstalowanego launchera Paradox 2026.12 z tymczasowymi ustawieniami,
developerskim child launcherem i osobnym plikiem opt-in `mod/capie.mod`.
To nie dowód prawidłowego odczytu rzeczywistego zestawu modów. Launcher
uruchomiono przez `open`; pojawił się komunikat błędu komunikacji ze Steam.
Nie uznajemy tego za test pełnego łańcucha Steam → launcher → gra.
Po uruchomieniu gry jej PPID wynosił 1, więc późniejszy odczyt `ps` nie dowodzi
oryginalnego rodzica. Dowód REA odczytu PID/start/ścieżki:
`ev_c823b71db355d1b3cd2f5ad90a81a9cc2a146dde49603be90ea3a02b02ad0069`.
Raport runtime i obserwacja `lsof` są osobnymi dowodami lokalnymi, nie zawartością
tego rekordu REA. Nie zmieniano plików binarnych ani ustawień launchera w katalogu gry.

Nowy profil 240 kontroli przeszedł niezależny review oraz test odmowy dla obcego
procesu. Wynik pierwszej próby powyżej dotyczy starszego artefaktu 3-pinowego;
nie należy przypisywać mu pokrycia nowego profilu.

## Próba pełnego profilu, 2026-10-16

Nowy artefakt o SHA-256
`6b60cca7ce0020dd1c175338af696bfff5a60826ff0fd588144f545f47e3e4d1`
załadował się przez developerski wrapper do HOI4 PID 66226. Raport:
`full-profile-diagnostic-results.json`, status
`full_static_profile_matches_not_activation`, **240/240**, pierwszy błąd -1.
Obejmuje 218 zakresów oraz 22 dodatkowe warunki celów rel32. Oba tokeny
pozostawały 0 w initializerze. Nie aktywowano hooków.
Pierwszy odczyt ps pokazał rodzica 65928 (sprawdzona sesja Paradox 2026.12);
kontrolny odczyt REA: `ev_27aaced39e7e0146d8d50361f510293d763b921a051989442b90b9f93a7ffeba`.
`lsof` potwierdził nową dylib, libsteam_api i poprawny cwd. Nadal nie jest to
test pełnego startu przez Steam ani rzeczywistego zestawu modów.

Po tej próbie agent potwierdził na ekranie menu główne HOI4
Operation Postern v1.19.3.0.c01a (5632) (S). Nie rozpoczęto kampanii.
