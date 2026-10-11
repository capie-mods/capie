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
