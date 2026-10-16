# Weryfikacja pliku Mach-O bez uruchamiania gry

`tools/test-macos-offline.sh` buduje testy na macOS i wykonuje je natywnie oraz,
na Apple Silicon, jako x86_64 przez Rosettę. Test `macos_image_test` domyślnie
czyta plik `hoi4` ze standardowego katalogu Steam. Inna lokalizacja:

```sh
HOI4_MACOS_BINARY='/pełna/ścieżka/hoi4' tools/test-macos-offline.sh
```

Brak domyślnego pliku oznacza jawne `SKIP actual game file`; testy syntetyczne
nadal działają. Nieistniejąca jawnie podana ścieżka oznacza błąd. Plik gry jest
otwierany wyłącznie do odczytu; test mutuje kopię w pamięci, aby wykazać odmowę
akceptacji zmienionego hasha. Niczego nie uruchamia ani nie zapisuje w grze.

Samodzielny `plugin/capie/src/core/macos_image.hpp` zawiera:

- parser thin little-endian x86_64 `MH_EXECUTE`, ograniczony nagłówek i load commands;
- kontrolę segmentów i sekcji: zakresy pliku/VM, nakładanie segmentów, zero-fill,
  __TEXT pod bazą `0x100000000`, kontrolę dodatniego i ujemnego ASLR slide;
- tłumaczenie niepustych zakresów pinów VA → file offset oraz VA → runtime address;
- SHA-256 przez systemowy CommonCrypto i `verify_game` wymagający dokładnego hasha
  `09419b087f8b4656c00df337bf838ff34c9b1d816b76cef5383ae6a4ac15b1fb`.

`parse` sprawdza tylko strukturę i nie identyfikuje wersji gry. `verify_game`
łączy oba sprawdzenia i zeruje wynik po błędzie. Testy obejmują zły format/CPU,
uszkodzone command sizes, zakresy i nakładanie segmentów, nieprawidłowe sekcje,
przepełnienie/underflow slide, wektor SHA-256 oraz rzeczywisty plik i jego mutację.

To fundament poza runtime CAPIE. Nie sprawdza zgodności pamięci uruchomionego procesu
z plikiem, nie ustala slide przez dyld, nie zastępuje sygnatur i pinów instrukcji,
nie instaluje hooków. Nie oznacza działającego portu ani wydania dla macOS.
Windows nie korzysta z nowego nagłówka; jego istniejący build nadal wymaga osobnej
weryfikacji na Windows przed wspólnym wydaniem.
