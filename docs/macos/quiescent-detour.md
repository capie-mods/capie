# Wąski backend detour dla bezczynnego kodu x86_64

Uruchomienie: `tools/test-macos-detour.sh`.
Wynik 2026-10-16: **92 sprawdzenia PASS**, x86_64 pod Rosettą, własny proces testowy.
Kod: `plugin/capie/src/core/macos_patch.hpp`; test: `plugin/capie/tests/macos_patch_test.cpp`.
Backend nie jest podłączony do gry, loadera ani inicjalizacji CAPIE.

## Obowiązkowy warunek wywołania

`install(..., caller_quiescent=true)` oraz `remove(..., caller_quiescent=true)`
wymagają wcześniejszej gwarancji wywołującego, że żaden inny wykonawca nie wejdzie
na modyfikowaną stronę, do destination ani do trampoliny przez cały czas operacji.
Dotyczy to również innych funkcji na stronie, sygnałów/callbacków, zmian ochrony,
zapisu i odmapowania. Flaga dokumentuje przekazaną gwarancję; jej nie sprawdza.
Warunek obejmuje również bieżący wątek: backend, jego polityka i wszystkie
wywołane funkcje pomocnicze muszą wykonywać się poza modyfikowaną stroną.
Samo umieszczenie destination na innej stronie nie sprawdza tego warunku.
Fałszywa deklaracja jest naruszeniem kontraktu, nie obsługiwanym scenariuszem.

Nie jest to backend ogólny. Zapis skoku obejmuje 14 bajtów i nie jest atomowy.
Strona traci EXEC na czas zapisu. Sama zgodność SHA i bajtów ani synchronizacja
wyłącznie wywołań CAPIE nie zatrzymują pozostałego kodu gry. Potrzebny będzie
rendezvous w znanym bezpiecznym punkcie lub osobno zweryfikowany mechanizm
zatrzymania i wznowienia wszystkich odpowiednich wykonawców. Proste wyliczenie
wątków i `thread_suspend` nie jest tu implementowane: nie udowodniono eliminacji
wyścigu tworzenia nowych wątków, bezpiecznej pozycji instruction pointer ani
braku blokad trzymanych przez zatrzymane wątki. Ten prototyp nie uzasadnia
aktywacji hooków przez opóźniony wątek loadera.

## Ograniczenia prefiksu

Przyjmowany jest prefiks 14–32 bajtów, mieszczący się w jednej stronie RX.
Każdy bajt musi być zgodny z oczekiwanym prefiksem, a wszystkie instrukcje muszą
należeć do jawnej krótkiej listy: pojedynczy NOP, push/pop rejestru, MOV immediate
do 32-bitowego rejestru, 64-bitowy MOV rejestr→rejestr, MOVABS REX 48/49 B8..BF imm64 i SUB RSP immediate.
Nieznane instrukcje, inne postacie MOVABS, RIP-relative memory, CALL, JMP i RET są odrzucane.
Nie ma ogólnego dekodera x86 ani mechanizmu relokacji.

Trampolina kopiuje pełne instrukcje i dodaje `FF 25 00000000 + address64`
do kontynuacji za prefiksem. Źródło dostaje taki skok do replacement i wypełnienie
NOP. Skok nie zużywa rejestru. Nie rejestrujemy informacji unwind: wyjątki i stack unwinding przez trampolinę
są zabronione. Profilowanie i przejścia z obcymi ABI nie zostały sprawdzone. Działanie natywne na procesorze Intel pozostaje niesprawdzone.

## Transakcja i błędy

Najpierw sprawdzane są granice, bieżące RX, różne strony source/destination,
rozpoznany prefiks i bajty. Trampolina powstaje RW, a przed modyfikacją źródła
jest zamykana do RX. Źródło przechodzi do prywatnego RW przez VM_PROT_COPY;
ponowna kontrola bajtów poprzedza zapis. Po zapisie następują flush instrukcji,
odtworzenie bieżącej i maksymalnej ochrony oraz odczyt kontrolny.

Błąd po częściowym zapisie uruchamia odtworzenie oryginalnych bajtów i ochrony.
`rolled_back` oznacza potwierdzone odtworzenie. `rollback_failed` pozostawia
`State::poisoned` i zatrzymuje alokację trampoliny; **nie gwarantuje sprawnego kodu,
odtworzonych uprawnień ani bezpiecznej kontynuacji procesu**. Wywołujący musi mieć
osobny plan powstrzymania dalszego wykonania. Ta granica jest powodem, dla którego
backend nie spełnia jeszcze całego kontraktu fail-closed uruchamiania gry.

`remove` porównuje wszystkie bajty z zainstalowanym skokiem. Przy cudzej zmianie
zwraca `mismatch` i niczego nie nadpisuje. Uchwyt nie kopiuje własności ani nie
usuwa hooka z destruktora; udane jawne remove zwalnia trampolinę dopiero po
odtworzeniu źródła. Testy błędów polityki symulują częściowy zapis oraz odmowę
potwierdzenia ochrony; nie zmuszają kernela do rzeczywistej nieodwracalnej awarii.

## Sprawdzone

- __TEXT i anonymous RX: replacement zwraca 42, trampolina oryginalne 7,
  remove odtwarza 7, oryginalne bajty i ochronę strony.
- Quiescence=false, mismatch, rozmiar, RIP-relative/CALL, zerowy adres,
  destination na stronie źródła oraz przekroczenie strony: odmowa.
- Błąd alokacji/zmiany ochrony, częściowy zapis, przejściowy błąd ochrony,
  trwała odmowa potwierdzenia rollbacku: rozróżnione stany i zachowana własność.
- Zmieniony przez obcego autora zainstalowany skok: remove odmawia nadpisania.

Dekoder MOVABS ma 16 dodatnich prób (oba prefiksy REX × 8 rejestrów) oraz
16 odmów dla obciętej 9-bajtowej instrukcji. Pierwsze 14 bajtów `lookup_n`,
`lookup_z` i `exists` to pełne `push rbp; mov rbp,rsp; movabs reg,imm64`.
Runner porównuje testowe bajty z `core-pins.json` i, jeśli plik gry istnieje,
z plikiem po sprawdzeniu SHA-256. Nie ładuje go ani nie wykonuje jego kodu.
Brak domyślnego pliku jest jawnie raportowany jako SKIP; błędna jawna ścieżka
`HOI4_MACOS_BINARY` przerywa sprawdzenie. To dowód relokowalności prefiksu,
nie ABI, lifecycle, quiescence ani kompletnego pokrycia lokalizacji.
