# Decyzje: rozszerzone API dla moderów — 2026-10-11

Odpowiedzi użytkownika na punkty badania architektury
(`docs/evidence/session-20261011/claude-api/`).

1. **Nowe słowa kluczowe vs protokół przez zmienne:** najlepiej oba warianty, z naciskiem
   na najstabilniejszą integrację z grą i bezbolesną aktualizację DLL po patchach gry.
   Każdy mod korzystający z API ma dołączać snippet, który pokazuje wyraźny wskaźnik
   (m.in. w menu głównym), gdy mod jest załadowany, a biblioteki DLL brak.
2. Tekst w API — pytanie badawcze, nie decyzja użytkownika.
3. Save po odinstalowaniu DLL: akceptowalne ryzyko, ale stan „brak DLL” musi być
   widoczny w grze (wskaźnik).
4. **MP:** DLL weryfikuje pozostałych graczy i odrzuca tych, którzy jej nie mają
   (lub mają niezgodną wersję).
5. Wersjonowanie API i bezpieczna degradacja dla modów innych autorów — zaakceptowane.
6. **Uprawnienia:** gracz zmienia nazwy wyłącznie swojego państwa. Wyjątki: host gry
   oraz overlord, gdy jego subjectem jest inny gracz.
7. Wskaźnik w menu głównym, możliwie nieinwazyjny — zaakceptowany.
8. Minimalna inwazyjność, walidacja danych z modów obcych autorów — zaakceptowane.

## Nazewnictwo (2026-10-11)

- **Rename Anything** — mod do zmiany nazw (Workshop, skrypty/GUI).
- **CAPIE — Cyprian's API Extender** — natywna biblioteka DLL rozszerzająca API
  modowania HOI4; Rename Anything jest jej pierwszym konsumentem.
- Obecne nazwy plików binarnych (`rename_anything_*.dll`, injectory, kontroler) są
  przypięte w manifestach, testach i review; zmiana nazw plików wymaga przebudowy,
  nowych hashy, manifestów i review — planowana jako osobny etap, nie ad hoc.

## Kolejność prac do pierwszego wydania (zaakceptowana 2026-10-11)

1. Integracja edytorów i liczników do `mod/` + test w grze (uzbrojona CAPIE/caption bridge,
   country DLL), w obecnym monorepo.
2. Zmiana nazw binariów na `capie_*.dll` jako osobny etap (rebuild, hashe, manifesty, review).
3. Wersjonowany kontrakt CAPIE ↔ mody (modele, pola, selektory, capability);
   Rename Anything przypina wersję CAPIE.
4. Podział na dwa repo z zachowaniem historii (`git filter-repo`): CAPIE (`plugin/`,
   launcher, bootstrap) i Rename Anything (`mod/`, `tools/`, `probes/`); dowody wg komponentu.
   Wspólny design system jako samodzielny katalog/repo współdzielone.
5. Przeniesienie do nowej organizacji GitHub (zakłada ją użytkownik; transfer po
   potwierdzeniu użytkownika).

## Decyzje do architektury CAPIE (2026-10-11, odpowiedzi na pytania z modding-api-architecture.md)

1. Kontrakt API = wariant B (protokół przez zmienne/scripted effects/cosmetic tag); wariant A
   (natywne słowa kluczowe) ewentualnie później jako dodatek.
2. MP na start = L0: blokada funkcji + widoczny baner do potwierdzenia CAPIE przez każdego
   gracza; odrzucanie graczy pozostaje tematem badawczym.
3. Skryptowe SDK CAPIE jako osobny przedmiot Workshop + osobne repo w organizacji GitHub.
4. Wskaźnik w menu głównym: „CAPIE załadowane / brak” wystarcza.
5. Rozszerzyć dozwolone znaki (cyrylica, akcenty, umlauty) — o ile HOI4 (fonty, parser,
   transport) je obsługuje; do zbadania.
6. Overlord → podmiot-gracz: najprostszy wariant (bez weta i bez dodatkowego UI).
7. Uprawnienia hosta przez regułę gry ustawianą w lobby — zaakceptowane.
8. Korpus starszych buildów: zaakceptowane podejście; domyślnie archiwizacja każdego nowego
   `hoi4.exe` od teraz (prywatnie, bez redystrybucji). Narzędzia wymagające danych logowania
   Steam użytkownik uruchamia wyłącznie sam.
9. CI na komputerze użytkownika (self-hosted, z zainstalowaną grą).
10. Po patchu gry mają działać wszystkie funkcje, a nie podzbiór; preferencja, by nie
    wydawać nowej wersji, gdy patch nie łamie używanych miejsc. **Decyzja: wariant (a)** —
    przy nieznanym SHA gry CAPIE uruchamia w runtime skaner sygnatur; wszystkie funkcje
    włącza tylko, gdy KAŻDY wzorzec (funkcje i offsety struktur w kodzie) jest jednoznaczny,
    a hookowany kod bajtowo identyczny z przetestowanym; inaczej CAPIE się nie włącza
    (wszystko albo nic) i gra działa bez niej z komunikatem. Na niezweryfikowanej wersji gry
    widoczny dyskretny znacznik. Domyślnie opcja wyłączona, włączana przez gracza
    (rekomendacja root; do zmiany na prośbę użytkownika). Zmienia decyzję dystrybucji nr 4
    tylko w trybie włączonym.
11. `versions.json`: w repo (podpisany) i dołączany do wydania.

## Korekty po analizie RE (2026-10-11, hoi4-re-findings.md)

- Decyzja 4 (odrzucanie graczy bez CAPIE) zastąpiona: skryptowy handshake wersji
  (`capie_ver` w każdej komendzie CAPIE, zmienna kraju) + blokada funkcji i widoczny
  baner/ostrzeżenie dla wszystkich. Bez natywnego wyrzucania graczy — zaakceptowane.
- Decyzja 6 (wyjątek dla hosta): brak triggera hosta w grze; wyjątek realizowany wyłącznie
  regułą gry ustawianą w lobby (decyzja 7) — zaakceptowane.
- Uprawnienia sprawdzane w samym efekcie skryptu (Execute nie weryfikuje nadawcy ani
  triggerów visible/enabled).

## Repozytoria (utworzone 2026-10-11, puste, PRIVATE, konto SirCypkowskyy)

- `capie` — DLL, launcher, bootstrap, dokumentacja CAPIE (`sites/capie`).
- `capie-script-sdk` — skryptowe SDK CAPIE (osobny przedmiot Workshop).
- `capie-design` — wspólny design system (`docs/design/`).
- `hoi4-rename-anything` — mod (bez zmian).
Podział kodu z historią (`git filter-repo`) na etapie 4 roadmapy; przeniesienie do
organizacji po zakończeniu, po potwierdzeniu użytkownika.

## Etap 2 — wymagania jakości kodu C++ (2026-10-11)

Zmiana nazw na `capie_*.dll` łączona z uporządkowaniem kodu natywnego: DRY, clean
architecture (rdzeń / funkcje / adaptery silnika), nazwane stałe zamiast magic numbers i
stringów, dokumentacja każdej funkcji/klasy/stałej (odpowiednik docstringów: komentarze
Doxygen z parametrami i wartościami zwracanymi), obszerny angielski opis na początku każdego
modułu (przepuszczony przez skill humanizer), clang-format + clang-tidy. Zachowanie musi
pozostać równoważne: wszystkie dotychczasowe testy offline, fingerprinty i mutation checki
przechodzą; potem niezależne review i pełny test w grze.

## Backlog funkcji CAPIE

- **Portret postaci w liście Postacie** (życzenie użytkownika 2026-10-11): skrypt HOI4 nie ma
  gettera portretu (`loc_objects_documentation.md`), więc wymaga natywnej funkcji lokalizacji
  CAPIE na postaci (np. `GetPortraitSmall` → nazwa sprite'a małego portretu, fallback
  `GFX_portrait_unknown`); mod pokaże portret 32–40 px przed ikoną roli (tooltip roli już jest),
  ukryty bez CAPIE. Po etapie 2, z osobnym RE, review i testem w grze.

## Decyzje etapu 2 (2026-10-12)

1. Dwie DLL: `capie_core.dll` (nazwy krajów, status, capability) i `capie_editor.dll`
   (edytory) — zaakceptowane.
2. Wariant release bez hooków diagnostycznych (4 hooki: Tick, Bind, Dispatch, ctor efektu);
   pełna diagnostyka tylko w wariancie debug — zaakceptowane, do potwierdzenia w grze.
3. clang-tidy/clang-format: użytkownik zgodził się, by root sam zainstalował komponent VS
   „C++ Clang tools for Windows” (zaplanowane na kolejną sesję).

## Decyzje roadmapy funkcji CAPIE (2026-10-12, po api-gap-analysis.md)

1. Kolejność (decyzja root): najpierw C1 publiczne pola tekstowe dla innych modów (+C8 prefill/fokus,
   C4 klucze statusu, C5 narzędzia tekstowe SDK, C3 alfabet łaciński) w v1.1 — najwyższa potrzeba
   i brak nowego RE; C6 portrety w v1.2 po osobnej sesji RE (RQ1–RQ3).
2. Funkcje wyłącznie wyświetlające (C2 odświeżanie widoków, C6 portrety, C7 nazwa gracza) mogą
   wyłączać się osobno, gdy ich piny przestaną pasować po patchu — wyjątek od zasady
   „wszystko albo nic”, która nadal obowiązuje dla funkcji zmieniających stan i transportu.
3. Rozszerzony alfabet łaciński w v1.1; cyrylica później (fonty, język gry).
4. Dokumentacja CAPIE otwarcie wymienia alternatywy (`hoi4_lua`, injector oparty na proxy DLL).
5. Profiler scripted GUI (C11) publiczny, wyłącznie w wariancie debug (decyzja root: odpowiada
   na zgłaszane przez modderów problemy wydajności; brak wpływu na release).
