# Eksperyment A: zatrzymany na transporcie IPC

**Nie uzyskano task portu i nie wykonano żadnego suspend/resume.**
Wynik nie dowodzi quiescence ani gotowości hooków. Gra nie była uruchamiana,
zatrzymywana ani odczytywana przez ten eksperyment; kod wykonuje operacje tylko
na własnych forkowanych children. Nie zmieniano ustawień bezpieczeństwa.

Kod: `plugin/capie/tests/macos_suspend/probe.cpp`.
Runner odtworzenia: `python3 tools/test-macos-suspend.py` (nie był ponownie
uruchamiany po osiągnięciu limitu prób; równoważne build/run wykonano ręcznie).
Wyniki: `suspend-fixture-results.json`.

Pierwsza próba arm64: `task_for_pid` dla własnego child zwróciło KERN_FAILURE(5).
Dwie następne próby jawnego przesłania task portu przez zarejestrowany Mach port
zakończyły się timeoutem odbioru. W ostatniej próbie child potwierdził udane
wysłanie wiadomości, ale parent jej nie otrzymał. x86_64 pod Rosettą dał identyczny
wynik obu prób transportu. Kompilacja obu architektur z Wall/Wextra/Werror PASS.

**Wątpliwy jest transport endpointu przez mach_ports_register/fork**, a nie
udowodniona odmowa security dla port transferu. Po trzech nieudanych próbach
arm64 zatrzymano pracę nad wariantem zgodnie z regułą. Nie wolno dopisywać
PASS dla 100 cykli ani nazywać timeoutu odmową kernela bez dalszego dowodu.

Fixture ma osobny watchdog kończący tylko własny child po 20 sekundach.
Cleanup zabija i zbiera watchdog przed zebraniem child, aby PID child nie był
ponownie przydzielony w oknie działania watchdoga. Docelowa pętla prosi o hold
10 ms i odrzuca wynik przekraczający 100 ms, ale nie została wykonana. Nie jest
to twarda gwarancja maksymalnego zatrzymania 100 ms przy zawieszeniu kontrolera;
przed uruchomieniem tej pętli wymagany jest dodatkowy watchdog deadline hold.

Następny krok to oddzielny test IPC wysyłający zwykły nonce zamiast task portu,
z ustaleniem rzeczywistej tożsamości endpointu po fork. Dopiero działający
transport uzasadnia powrót do eksperymentu task suspension. Żaden nowy transport
ani obejście uprawnień nie zostały tu wypróbowane.
