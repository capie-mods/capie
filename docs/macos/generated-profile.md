# Typowana tabela diagnostyczna

Generowanie: `python3 docs/macos/re/generate_pin_header.py`.
Testy offline: `tools/test-macos-profile.sh`.
Wynik: `plugin/capie/src/core/macos_profile.generated.hpp`, namespace
`capie::core::macos::profile`, tablice `bytes`, `pointers`, `relatives`
i odpowiadające im `*_ids`; wymagany SHA w `target_sha256`.

Generator korzysta z czterech manifestów źródłowych i istniejącego `Exporter`:
walidacji zakresów, świeżych bajtów, slotów, targetów, companions i referencji.
Przed generowaniem sprawdza pełny SHA binarium. Wygenerowane rekordy muszą
odpowiadać dokładnie 1:1 wszystkim 218 zakresom z `port-pin-evidence.json`.
Typ nie wynika z sufiksu identyfikatora: exporter pointerów wybiera go na podstawie
pól źródłowego rekordu; literal i jump-table mają jawny rodzaj danych.
Nieznany kind, konflikt, brak referencji lub rozbieżność eksportu przerywa pracę.

| Tablica | Liczba | Sposób kontroli runtime |
| --- | --- | --- |
| `bytes` | 203 | 199 zakresów kodu + 4 zakresy danych, dokładne bajty |
| `pointers` | 15 | Wartość wskaźnika równa target VA + slide |
| `relatives` | 22 | Signed rel32 + jawny koniec instrukcji, sprawdzony target |

Cztery zakresy danych to trzy cstrings i jeden wpis względnej tablicy skoków.
Wpis jump-table leży w sekcji `__TEXT,__text`, lecz pozostaje semantycznie danymi:
`BytesPin.code=false`. Relacja `table_base + signed entry == target` jest sprawdzana
przez istniejący exporter offline. Nie otrzymuje błędnego `RelativePin`, który
wymaga końca instrukcji zamiast bazy tabeli. Dokładne niezmienne bajty wpisu są
kontrolowane runtime, ale nie dodano osobnego runtime checkera tej relacji.

22 relacje obejmują 21 jawnych rekordów i skok thunka konstruktora. Nie są
kolejnymi zakresami dodawanymi do liczby 218, tylko dodatkowymi warunkami ich
interpretacji. Tokeny inicjalizowane w BSS pozostają wyłączone z tablic statycznych.
Wskaźniki nie są porównywane z surowym little-endian qword z pliku: runtime
uwzględnia actual slide obrazu. Tablica nie dowodzi ABI, lifecycle ani quiescence.

Test-first: pierwsze uruchomienie nowych testów zakończyło się brakiem modułu
generatora. Po implementacji 17 testów Python (istniejący exporter + 5 nowych
przypadków) przechodzi. Nowy runtime self-check ma 57 sprawdzeń na arm64 i x86_64:
15 wskaźników × zgodny slide / nieprzesunięty disk value / niedostępny odczyt,
cztery dane read-only przyjęte jako dane i odrzucone jako kod, liczby i zgodność
rozmiarów tablic ID. Osobno runner uruchamia istniejące 48 sprawdzeń silnika pinów.
Są to syntetyczne odczyty własnego procesu testowego; nie wykonują kodu gry.

Generator nie zmienia diagnostycznej dylib już załadowanej w grze. Integracja
wymaga osobnej kompilacji, kontroli hasha nowego artefaktu i oddzielnego uruchomienia.
