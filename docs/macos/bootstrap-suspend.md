# Działający zewnętrzny hold przez jawny bootstrap endpoint

**100 cykli PASS arm64 oraz 100 cykli PASS x86_64/Rosetta**, wyłącznie własny
forkowany fixture. Nie uruchomiono gry, nie zmieniano jej pamięci/plików ani
ustawień security. Stary transport registered-port pozostaje zatrzymany.

## Dowód endpointu i praw

Nowy test `nonce.cpp` wysyła tylko zwykły nonce przez stary mechanizm: parent
nadal dostaje timeout mimo child send=0. Problem występuje bez task-port descriptor.
Nie można więc interpretować wcześniejszego timeoutu jako dowodu odmowy task control.

`bootstrap_nonce.cpp` rejestruje unikalną dla PID usługę we własnym bootstrap
namespace. Child pobiera send right przez `bootstrap_look_up`, a parent posiada
receive right utworzony przez siebie. Nonce dociera w obu architekturach. Po
zniszczeniu receive right lookup zwraca UNKNOWN_SERVICE(1102), co potwierdza cleanup.
Wstępna próba unregister(NULL) nie powiodła się; nie jest używana w końcowym kodzie.

`bootstrap_task.cpp` przenosi prawo do własnego task child w jawnym deskryptorze
Mach. Parent sprawdza `pid_for_task`: sukces i dokładnie PID niezebranego własnego
child. Test przeszedł na obu architekturach. Nie wywołuje `task_for_pid`, nie
potrzebuje ingerencji w security ani pozyskiwania praw do obcego procesu.
Nazwy portów są lokalne dla przestrzeni IPC: ich numery nie służą jako dowód
identyczności między procesami.

Źródła pierwotne:
[Apple ipc_tt.c](https://github.com/apple-oss-distributions/xnu/blob/main/osfmk/kern/ipc_tt.c)
pokazuje dziedziczenie registered rights; samo to nie opisuje całego fork wrappera.
[Apple Libc fork.c](https://github.com/apple-oss-distributions/Libc/blob/main/sys/fork.c)
i [Libsystem init.c](https://github.com/apple-oss-distributions/Libsystem/blob/main/init.c)
pokazują dodatkowe atfork handlers. Nie przypisano im konkretnej zmiany slotu bez pomiaru.
[Apple bootstrap.h](https://github.com/apple-oss-distributions/launchd/blob/main/liblaunch/bootstrap.h)
opisuje dynamiczne powiązanie nazwy z send right oraz deprecację register.
Wersje źródeł main nie są dowodem identyczności zainstalowanych bibliotek.

## Właściwy test hold

Kod: `plugin/capie/tests/macos_suspend/bootstrap_probe.cpp`.
Odtworzenie: `python3 tools/test-macos-bootstrap-suspend.py`.
Zapis pomiarów: `bootstrap-suspend-results.json`.

Po weryfikacji własnego PID parent wykonuje task_suspend, odczytuje współdzielony
atomowy licznik, czeka 10 ms, wymaga niezmienionego licznika i wykonuje dokładnie
jeden task_resume. Następnie wymaga ponownego postępu. Child ma gorący worker oraz
pętlę tworzącą/joinującą krótkie wątki. 100 powtórzeń na architekturę zakończyło
się sukcesem. Maksymalny zaobserwowany hold: arm64 16.972 ms, x86_64 18.011 ms.

Osobny watchdog kończy wyłącznie własny child po przekroczeniu deadline hold
100 ms albo limitu całego eksperymentu 20 s. Kontroler zawsze zbiera watchdog
przed zebraniem child, co zapobiega ponownemu użyciu PID przez watchdog po cleanup.
Nie jest to twarda gwarancja czasu rzeczywistego przy zagłodzeniu schedulerem;
zmierzone okna są poniżej 100 ms. Runner zwraca błąd, jeżeli brak PASS/100 cykli.

## Co to konkretnie odblokowuje, a czego nie

Udowodniono dostępny mechanizm: własny proces może dobrowolnie przekazać task
control do własnego helpera; helper potrafi zatrzymać i wznowić task, również
w wariancie Rosetta z równoczesnym tworzeniem wątków. Dlatego odmowa task_for_pid
nie przekreśla współpracującego sidecara inicjowanego przez loaded CAPIE.

Nie wdrożono jeszcze takiego protokołu w CAPIE ani w initializerze dyld. Produkcyjny
endpoint wymaga losowej nazwy/nonce i uwierzytelnienia audit trailera, powiązania
z dokładnym procesem/obrazem i cleanup przy awarii helpera. Przekazanie task prawa
jest istotnym uprawnieniem; PID-only jest wystarczający dla tego zamkniętego fixture,
nie dla dowolnych klientów usługi.

Hold nie rozwiązuje wznowienia PC wewnątrz zastępowanego prefiksu, Rosetta guest/host
state i invalidacji translacji, zdalnej polityki patchowania ani poisoned rollback.
Nie można więc jeszcze ustawić `caller_quiescent=true` dla hooków gry. Konkretnym
następnym zadaniem jest dowód bezpiecznego stanu target pages/PC oraz remote cache
protocol, wykorzystujący działający własny endpoint zamiast zgadywania liczby wątków.
