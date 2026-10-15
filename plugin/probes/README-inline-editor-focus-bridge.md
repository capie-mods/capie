# Osobny kandydat jednorazowego fokusu i zaznaczenia

Ten wariant jest **nieaktywny**. Nie zmienia zamrożonego
`inline_editor_render_bridge.cpp` SHA256
`104F5C572496D610E5A395129BC2CCAA57B162B61BD88F0FF88B82AD99219517`
ani jego DLL, fixture, moda lub konfiguracji gry. Kompilacja i testy offline
nie zaliczają działania fokusu, zaznaczenia ani zastąpienia tekstu w grze.

Po jednorazowym prefill oraz render refresh **wszystkich pól modelu** kandydat
próbuje skupić pierwsze pole: lidera, krótkiej nazwy partii lub nazwy kraju.
Korzysta z potwierdzonych natywnych metod:

- `225E320(manager, edit)` — przełączenie aktywnego widgetu;
- `22F2410(edit+80, 0, copied_UTF32_length)` — zaznaczenie całego tekstu;
- `22F4D80(edit)` — odświeżenie po zaznaczeniu, zgodnie z native callerem.

Przed fokusem, po nim/przed zaznaczeniem, po zaznaczeniu/przed refresh oraz po
refresh ponownie rozwiązuje pełny View/root/model/epoch/generation, dokładnie
nazwane pola i ich typ, widoczność, native disabled bit oraz niezmieniony tekst
obu pól. Porównuje tożsamości ze wskaźnikami **lokalnymi dla tego wywołania**.
Żaden pożyczony widget ani manager nie jest przechowywany między tickami.
Żaden lock nie obejmuje natywnych wywołań. Callback zmieniający kontekst kończy
próbę przed następną fazą. Funkcje Enter, transportu, dispatch i lifecycle są
bajtowo zgodne ze źródłem render bridge.

Polityka arbitrażu jest celowo ścisła: przed fokusem świeży manager z globalnego
`3453230` musi mieć **null w +158 i +230**, a wszystkie nasze pola muszą mieć
wyłączony bit01 w `edit+271`. Nie odbieramy fokusu obcemu widgetowi, receiverowi
ani już aktywnemu własnemu polu. Przycisk Open pozostawiony przez silnik w +158
także zablokuje próbę; nie zakładamy, że blur takiego przycisku jest nieszkodliwy.
To jawna granica pierwszego eksperymentu, do rozstrzygnięcia w QA. Po natywnym
fokusie wymagamy tego samego aktualnego managera, `+158==primary_edit`,
`+230==primary_edit+1A0`, bit01 ustawionego tylko w pierwszym polu.

Długość bieżącej nazwy to **1..128 punktów UTF32**, zgodnie z istniejącym
snapshot/prefill; cały zakres mieści się w natywnym `int`. Bieżący tekst spoza
alfabetu nowego wejścia także może być zaznaczony. Nie uzależniamy fokusu od
`encode_field`. Ograniczenie nowego Apply do 32 znaków pozostaje bez zmian.
Nie ma obcinania, podmiany znaków, syntetycznej klawiatury ani bezpośrednich
zapisów caret/selection/dirty/focus w strukturach gry.

Odrzucenie którejkolwiek fazy pozostawia bieżące opening bez readiness;
istniejący `attempted` blokuje ponowienie w tej samej epoce. Konieczne jest
Cancel/Open. Nie dodajemy pętli przejmującej fokus ani ponownego zaznaczania
draftu przy powrocie karty. `autofocus-selection-calls-returned` oznacza jedynie
powrót wywołań i poprawny odczyt tekstu/fokusu; nie jest testem pikseli lub
zastąpienia tekstu. Dotychczasowe `prefill-readback-pass` występuje wcześniej
i nie deklaruje sukcesu nowej fazy.

Build: `build-inline-editor-focus-bridge.ps1 -MinHookDir <minhook>`.
Własne C++: `/std:c++20 /MT /W4 /WX`; MinHook C zachowuje oddzielną politykę.
Wyłącznie nowy output `plugin/build/inline-editor-focus-bridge`:
`rename_anything_inline_editor_focus_bridge.dll`,
`inject_inline_editor_focus_bridge.exe`, `inline_focus_transport_test.exe`.
Nie nadpisujemy render B20 ani jego outputu. Injector używa nowego dokładnego
basenamu; RNInitialize odrzuca stary inline editor/render bridge i wcześniejsze
proby oraz inny HMODULE o swoim basenamie. Dokładny hash EXE i 11 unikalnych
fingerprintów `.text` poprzedza pięć dotychczasowych hooków. Metody fokusu
i zaznaczenia są wywołaniami, nie nowymi hookami. Nie ładować równolegle wariantów.

Test zawiera dotychczasowy rzeczywisty builder payload oraz rzeczywistą nową
orkiestrację na własnych fake obiektach. Sprawdza kolejność, pełny zakres UTF32,
modele/pierwsze pole, brak locków nad callbackami, arbitraż i odrzucenie zmian
epoch/generation/root/tekstu drugiego pola/receivera/widgetu/managera w każdej
fazie. Stub callbacków nie jest dowodem ABI ani działania natywnych funkcji.

Przed aktywacją wymagane niezależne review i zakończenie render QA. Pierwszy
runtime: Open bez klikania pola, jeden znak zastępuje całą nazwę, drugie pole
nie odbiera fokusu, Cancel bez rename, draft zachowany po powrocie karty.
Menu/load, czas wywołania na UI thread oraz reentrancy natywnych observerów
pozostają do testów. Enter ma osobny etap; ten wariant go nie zmienia.

Primary dowody ABI i granic:
[monorepo archive: docs/evidence/session-20261011/day2-native-focus-selection-proposal.txt](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/session-20261011/day2-native-focus-selection-proposal.txt)
oraz `day2-native-focus-selection-raw.txt`. Końcowe hashe i wyniki offline:
[monorepo archive: docs/evidence/session-20261011/day2-inline-editor-focus-candidate.json](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/session-20261011/day2-inline-editor-focus-candidate.json).
