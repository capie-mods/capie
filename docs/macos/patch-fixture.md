# Próba zapisu do własnego __TEXT pod Rosettą

Uruchomienie: `python3 tools/test-macos-patch.py` z katalogu repozytorium.
Skrypt buduje wyłącznie własny program x86_64 w prywatnym katalogu tymczasowym.
Nie uruchamia HOI4 ani launchera, nie zmienia plików gry, uprawnień systemu ani zależności CAPIE.
Surowy pomiar: [patch-fixture-results.json](patch-fixture-results.json).

Pomiar 2026-10-16: system raportuje macOS 27.0.1, host arm64;
każdy proces fixture raportuje `sysctl.proc_translated=1`, stronę 4096 bajtów.
Nie wykonano tego eksperymentu na fizycznym procesorze Intel.

| Własny program | __TEXT: mach_vm_protect RW + COPY | __TEXT: mprotect RW | Anonymous RX: obie metody |
| --- | --- | --- | --- |
| unsigned | zapis i odtworzenie działają | EACCES, errno 13 | działają |
| ad-hoc | zapis i odtworzenie działają | EACCES, errno 13 | działają |
| ad-hoc + hardened runtime | zapis i odtworzenie działają | EACCES, errno 13 | działają |

W dziewięciu udanych próbach wywołanie funkcji zwraca kolejno 7, 42 po zmianie,
a następnie 7 po odtworzeniu oryginalnych sześciu bajtów. W trzech odmowach
`mprotect` program kończy próbę przed zapisem z kodem 10. Pozostałe kończą kodem 0.
Nie było sygnałów ani awarii procesu. SHA-256 plików wykonywalnych przed i po próbie
pozostało identyczne we wszystkich 12 przypadkach.

## Uprawnienia i odtwarzanie

Dla `__TEXT` początkowe current/max protection wynoszą RX/RX (`5/5`).
`mach_vm_protect(task_self, page, size, false, READ|WRITE|COPY)` zwraca
`KERN_SUCCESS` i ustanawia bieżące RW (`3`), bez prawa wykonania w oknie zapisu.
**Maksymalne uprawnienia zmieniają się na RWX (`7`)** wskutek COPY.
Po zapisie osobna operacja przywraca bieżące RX, następuje
`sys_icache_invalidate`, dopiero potem wywołanie zmienionej funkcji.
Ta sama sekwencja odtwarza pierwotne bajty. Końcowe
`mach_vm_protect(..., true, READ|EXECUTE)` przywraca również maksimum RX;
`mach_vm_region` potwierdza ponownie `5/5`.
Dla anonimowej kontroli początkowe maksimum wynosi RWX; nie zmniejszamy go.

Funkcja assembly jest izolowana do własnego obszaru kodu; kod wykonujący zmianę
nie leży na modyfikowanej stronie. Linker ostrzega, że alignment sekcji `0x4000`
został ograniczony do `0x1000`, zgodnego z raportowaną stroną 4096 bajtów;
wywołanie testowej funkcji jest oddzielone paddingiem od kodu próby.

`codesign -d --verbose=4` potwierdza brak podpisu dla unsigned,
`flags=0x2(adhoc)` dla ad-hoc i `flags=0x10002(adhoc,runtime)` dla hardened.
Nie dodano entitlementów. Wynik dla własnego ad-hoc hardened procesu nie dowodzi
identycznej polityki dla podpisanych cudzych aplikacji ani natywnego arm64.

## Co ten wynik pozwala projektować

Mechanizm zapisu do prywatnej strony wykonywalnej x86_64 jest wykonalny w tej
konfiguracji, łącznie z odtworzeniem bajtów oraz bieżących i maksymalnych uprawnień.
To dowód mechanizmu ochrony pamięci, **nie gotowy backend hooków**.
Fixture zmienia tylko immediate pojedynczej instrukcji; nie bada skoków, relokacji
RIP-relative, trampolin, unwind, podpisów metod, współbieżności ani zachowania gry.
Przyszły backend musi osobno rozwiązać zatrzymanie innych wykonawców strony,
pełne instrukcje zastępowanego prefiksu, ochronę wszystkich stron, błędy częściowej
transakcji i rollback. Usunięcie EXEC podczas zapisu wymaga szczególnej uwagi,
ponieważ przypadkowy inny wykonawca tej samej strony nie może wówczas kontynuować.
Nie dodano funchook ani żadnego hooka do runtime CAPIE.
