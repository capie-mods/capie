# Projekt eksperymentu lifecycle: zewnętrzne zatrzymanie własnego procesu

Status: eksperyment uruchomiono, ale zatrzymał się na uzyskaniu task portu.
Nie wykonano suspend/resume, zmian gry ani aktywacji hooków. Wyniki i limit trzech
prób opisuje [suspend-fixture.md](suspend-fixture.md). Poniższe kroki opisują
projekt eksperymentu, nie zaliczony test.
`runtime_ready=false`. Przeczytano `startup-fixture.md`, `quiescent-detour.md`
i kontrakt `core/macos_patch.hpp`.

Osobna, ograniczona hipoteza early-init ma zapisane
[dowody z dokładnego pliku gry i źródła Apple dyld](early-init-evidence.md).
Nie stanowi dowodu wyłączności stron ani gotowości do aktywacji.

## Konkretny mechanizm, który warto sprawdzić

Zewnętrzny kontroler zatrzymuje **całe zadanie**, nie kolejne wątki z enumeracji.
Oficjalny opis Apple `task_suspend` podaje: operacja czeka na zatrzymanie wszystkich
wątków zadania, a dodatni suspend count nie pozwala wykonywać się nowym wątkom.
To mocniejszy kontrakt niż snapshot plus `thread_suspend`.
Źródło: [Apple XNU task_suspend](https://github.com/apple-oss-distributions/xnu/blob/main/osfmk/man/task_suspend.html).
Bieżące źródło implementacji: [Apple XNU task.c](https://github.com/apple-oss-distributions/xnu/blob/main/osfmk/kern/task.c).
Źródło z gałęzi main nie jest dowodem identyczności kernela zainstalowanego systemu.

Nie wywołujemy `task_suspend(mach_task_self())` z patchera wewnątrz gry:
patcher zatrzymałby również siebie. Kontroler musi działać w innym procesie.
Uprawnienie do kontrolowania własnego fixture jest warunkiem eksperymentu;
odmowa kończy próbę, bez sudo, task_for_pid obchodzonego inną drogą ani zmian security.
Nie przenosi się tego automatycznie na HOI4, którego read-task request już odmówiono.

## Eksperyment A: bez zapisu kodu

1. Własny child x86_64 pod Rosettą i natywny arm64 uruchamiają workery oraz wątek
   stale tworzący i kończący krótkie workery. Współdzielony region zawiera tylko
   atomowe liczniki i identyfikator nonce sesji. Kontroler uzyskuje legalny task port
   do fixture przez jawny testowy mechanizm; dostęp należy sprawdzić osobno.
2. Kontroler wykonuje jeden `task_suspend`, sprawdza kod powrotu, a następnie
   obserwuje liczniki przez planowane 10 ms (odrzuca hold ponad 100 ms).
   Przed uruchomieniem pętli potrzebny jest niezależny watchdog ograniczający
   czas hold; sam pomiar po wznowieniu nie gwarantuje tego limitu. Wszystkie muszą pozostać stałe. Child nie
   udostępnia specjalnej ścieżki, która omija systemowy hold.
3. Kontroler wykonuje dokładnie jeden odpowiadający `task_resume`; liczniki muszą
   znowu rosnąć w ciągu 1 s. Powtórzyć 100 razy, także podczas tworzenia wątków.
   Rejestrować tylko statusy i liczniki; timeout kończy własny fixture.
4. Osobny wariant potwierdza, że źródłowy target i destination nie były wykonywane
   w zatrzymanym oknie. Należy sprawdzić dostępne stany wątków Rosetty; sam brak
   x86 RIP w zakresie nie dowodzi braku wykonania przetłumaczonego kodu.
5. Raport rozdziela dowód task suspension od quiescence potrzebnej do wznowienia
   po patchu. Eksperyment A niczego nie patchuje i nie daje zgody na instalację.

Kontroler nie może zależeć od malloc/dyld/locków w zatrzymanym child. Protokół
ma timeout i wyłączne właścicielstwo dokładnie jednego suspend count. Nie wolno
„resume aż ruszy”, bo można zdjąć hold należący do innego autora.

## Dlaczego to jeszcze nie jest gotowy lifecycle hookowania

Zatrzymanie wszystkich executorów na czas zapisu nie gwarantuje bezpiecznego
wznowienia. Wątek może być zatrzymany w środku zastępowanych 14 bajtów. Zmienione
bajty wymagają albo odmowy bez modyfikacji, albo osobno udowodnionego przeniesienia
stanu instrukcji; obecny backend nie ma takiego mechanizmu. Rosetta dodatkowo
wymaga dowodu mapowania stanu host/guest i invalidacji przetłumaczonego kodu.

`macos_patch::NativeMemory` jest polityką lokalnego procesu. Nie wolno użyć jej
na adresach child ani traktować lokalnego `sys_icache_invalidate` jako zdalnej
invalidacji cache Rosetty. Następny eksperyment z mutacją wymaga osobnej,
przejrzanej polityki remote-memory i jawnego dowodu cache/coherency; nie jest
częścią tego projektu. Uruchamianie pomocnika wewnątrz zatrzymanego child wymaga
jego wznowienia, co psuje gwarancję całego task hold.

Wariant rollback_failed/poisoned nie może po prostu wznowić procesu ani zgłosić
„gra działa bez CAPIE”. Obecny kontrakt fail-closed przy każdej awarii nadal nie
jest spełniony dla nieodwracalnej awarii modyfikacji kodu.

## Ocena innych dróg

Initializer DYLD lub wejście przed main nie dają wyłączności: istniejący fixture
ma worker uruchomiony przez wcześniejszą zależność. Liczba dwóch wątków pod
Rosettą nie identyfikuje ich i nie daje gwarancji. Interpose pthread_create nie
obejmuje wcześniejszych wątków ani wszystkich kernelowych ścieżek tworzenia.
Cooperative rendezvous działa dla własnych, kontrolowanych workerów, ale nie
obejmuje dowolnego kodu gry/Steam; brak obecnie dowodu takiego punktu w HOI4.

Wniosek projektowy: eksperyment A pozostaje niezaliczony z powodu transportu IPC;
nie jest uzasadnieniem ustawienia `caller_quiescent=true` w grze. Żaden opisany
mechanizm nie zamyka jeszcze lifecycle, stanu instrukcji, cache Rosetty i
nieodwracalnego rollbacku jednocześnie.
