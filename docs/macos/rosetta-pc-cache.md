# Rosetta: guest PC i faktyczna widoczność zmiany kodu

**Wynik:** zwykłe `thread_get_state(x86_THREAD_STATE64)` nie dostarcza guest RIP.
Zdalny patch ma poprawne bajty i udany kernel cache flush, lecz wykonuje starą
translację. Pełna lokalna sekwencja patchowania w kontrolowanym child daje nowy wynik.
Nie uruchomiono gry; wszystkie adresy należały do własnych fixture.

## Stan instrukcji podczas task hold

`tests/macos_suspend/pc_probe.cpp` ma asm worker w znanym zakresie
`controlled_start..controlled_end`: `lock incq (%rdi); jmp controlled_start`.
Parent posiada legalnie przekazany task port i sprawdza PID własnego child.
Podczas 100 pełnych task holds pobiera stany wszystkich wątków.

W końcowym wariancie ze stałymi wątkami jest 300 odmów flavor4 (`x86_THREAD_STATE64`),
wszystkie KERN_INVALID_ARGUMENT(4), zero guest RIP. Flavor6 zwraca 300 sukcesów,
każdy z 68 natural_t. To rozmiar ARM_THREAD_STATE64 z SDK; x86_EXCEPTION_STATE64
również używa numeru6, ale ma inny rozmiar. Nie dekodowano host PC jako guest RIP.
Wstępny wariant z tworzeniem/kończeniem wątków miał dodatkowo 13 martwych portów;
nie wolno traktować takich błędów jako bezpiecznego PC poza targetem.

Wniosek dla gate: **brak dostępnego guest PC oznacza odmowę**, a nie „executor jest
poza prefiksem”. Udany task hold nie daje samodzielnie dowodu bezpiecznego wznowienia
po zmianie instrukcji. Nie wyklucza to innego API debug/translation-aware.

## Eksperyment cache z zaparkowanym callerem

Child tworzy prywatną anon stronę, wpisuje `mov eax,7; ret`, zamyka do RX i wykonuje
ją 10000 razy. Następnie publikuje ready i czeka poza tą stroną. Jest jedynym
kontrolowanym callerem tej funkcji. Parent zatrzymuje cały task i wykonuje remote
RW, `mach_vm_write` (`mov eax,42; ret`), odczyt kontrolny, cache attribute, RX.
Po sukcesie operacji wznawia child i pozwala na jedno wywołanie.

| Wariant | Readback | Wynik wywołania |
|---|---|---|
| remote `MATTR_CACHE / MATTR_VAL_CACHE_FLUSH` | 42 | **7** |
| jak wyżej, child dodatkowo lokalne `sys_icache_invalidate` | 42 | **7** |
| remote RW z `VM_PROT_COPY` | 42 | **7** |
| child pełna lokalna sekwencja RW\|COPY → write → icache → RX | 42 | **42** |

Wszystkie operacje kernela w tych wariantach zwracały KERN_SUCCESS.
Pierwsza próba remote bez readback także dała7; następna dodała readback,
żeby odróżnić niewidoczny zapis od nieodświeżonego wykonania. Nie próbujemy
ponownie tej samej hipotezy bez nowego mechanizmu.

Źródło [Apple vm_user.c](https://github.com/apple-oss-distributions/xnu/blob/main/osfmk/vm/vm_user.c)
potwierdza, że mach_vm_machine_attribute operuje na wskazanej mapie. Nie zawiera
obietnicy invalidacji translatora Rosetty. Wynik eksperymentu pokazuje, dlaczego
KERN_SUCCESS z tego API nie wystarcza jako dowód nowego wykonywanego kodu.

Istniejący `macos_patch_test.cpp` rzeczywiście wykonuje anon target przed install;
nie jest wyłącznie próbą zimnego kodu. Lokalny backend używa RW|COPY, lokalnego
mach_vm_write, sys_icache i odtworzenia praw. Nowy test powtarza całą sekwencję;
nie ustala jeszcze, który jej krok odpowiada za invalidację.

## Pliki i odtworzenie

Źródła w `plugin/capie/tests/macos_suspend/`: `pc_probe.cpp`, `remote_cache.cpp`,
`remote_cache_copy.cpp`, `local_reprotect.cpp`. Każdy jest samodzielnym fixture.
Kompilacja: `xcrun clang++ -std=c++20 -Wall -Wextra -Werror
-Wno-deprecated-declarations -arch x86_64 SOURCE -o OWN_TEMP_EXECUTABLE`.
`remote_cache` bez argumentu sprawdza remote, argument `local` dodaje tylko local
icache. `remote_cache_copy` dodaje remote COPY. `local_reprotect local` dodaje
pełną lokalną sekwencję. Wariant stale code celowo zwraca exit5, a nie PASS.
Wyniki liczbowe zapisano w `rosetta-pc-cache-results.json`.

## Następna granica

Designated local patcher może korzystać z działającej lokalnej sekwencji, ale
nie można go po prostu wznowić po task hold: cofnięcie hold dopuszcza pozostałe
executory, zaś zachowanie indywidualnych thread holds nie blokuje tworzenia nowych
wątków. Brak guest PC i potencjalna pozycja starej instrukcji nadal blokują grę.
Potrzebny jest osobny dowód early-init osiągalności target pages albo mechanizm
rendezvous/redirect z poprawnym wznowieniem wszystkich stanów. Ten test dostarcza
konkretnej różnicy cache, nie zastępuje takiego dowodu.
