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
