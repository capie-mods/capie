# Rozstrzygnięcie dwóch proponowanych gates

Status: analiza kodu i wcześniejszych pomiarów; nie aktywowano hooków gry.
Nie twierdzimy, że bezpieczny lifecycle jest niemożliwy. Poniższe **konkretne
proponowane warunki są niewystarczające** przy obecnym zakresie dowodów.

## Handoff task hold → indywidualne thread holds → local patcher

Oficjalne źródło [Apple task.c, task_release_locked](https://github.com/apple-oss-distributions/xnu/blob/main/osfmk/kern/task.c)
zmniejsza task suspend count. Gdy osiąga zero, woła workq_proc_resumed i zwalnia
zadaniowy hold wątków. [Apple thread.c, thread_create_waiting_internal](https://github.com/apple-oss-distributions/xnu/blob/main/osfmk/kern/thread.c)
nie zakłada nowemu workqueue thread indywidualnego user hold. Hold zadaniowy
jest dokładany wyłącznie wtedy, gdy task suspend count jest dodatni.

Wniosek z kodu: indywidualne holds uzyskane dla wcześniejszej listy pozostają
na tamtych wątkach, lecz nie są regułą dla nowych workqueue threads. Po task_resume
patcher oraz nowy worker mogą wykonać się równocześnie. Ponowna enumeracja jest
kolejnym snapshotem; nie zamyka tego okna. Sama flaga zakazująca pthread_create
nie obejmuje kernel workqueue. To wystarcza do odrzucenia argumentu „wszystkie
pozostałe wątki mają hold, więc tylko patcher może się wykonać”.

Dodatkowo zatrzymany wcześniej guest mógł znajdować się wewnątrz prefiksu.
[Pomiar PC](rosetta-pc-cache.md) nie uzyskał guest RIP dla żadnego z 300 stanów.
Nawet gdyby uniemożliwić tworzenie nowych workerów, ta druga przesłanka pozostaje
niepotwierdzona. Nie zastępujemy jej host PC ani rozpoznaniem nazwy wątku Rosetty.

## CAPIE jako pierwszy inserted initializer

[Apple runAllInitializersForMain](https://github.com/apple-oss-distributions/dyld/blob/fd8d0c4d52320ebf64db34f3cb280310d905c5ae/dyld/DyldAPIs.cpp#L3752)
wykonuje libSystem i obrazy belowLibSystem, wraz z powiadomieniami ObjC, przed
inserted dylibs. Następnie przechodzi inserted images w kolejności, a main
executable inicjalizuje dopiero po nich. Inicjalizacja każdej inserted library
obejmuje jej zależności i upward links.

Ta kolejność daje **węższą hipotezę do audytu** niż zwykłe „przed main”:
CAPIE musi być pierwszym inserted image, nie mieć zależności prowadzących do gry,
a wszystkie wcześniejsze initializery/callbacks muszą należeć do zamkniętego
zbioru znanych obrazów. Trzeba wykluczyć runtime callbacki do target pages
oraz kod patchera/helperów na tych stronach. Nie trzeba dowodzić braku wszystkich
wątków, ale trzeba dowieść braku ich osiągalności do modyfikowanych stron.

**Obecny child launcher nie spełnia pierwszej przesłanki:**
`macos_child_policy.hpp::libraries` zachowuje foreign/Steam entries i dopisuje
CAPIE na końcu. Znany, dozwolony overlay nadal może zatem inicjalizować się przed
CAPIE. Sama allowlista jego hasha nie dowodzi braku wcześniej utworzonego workera
lub callbacku. Nie zmieniono tej polityki w ramach analizy.

Read-only kontrola dokładnego Mach-O gry: jedyna sekcja pasująca do
`interpose|init|objc_nl` to `__DATA,__mod_init_func`, VA0x1032a2b78, rozmiar0xb2c8.
Brak sekcji interpose/nonlazy ObjC nie dowodzi pełnej nieosiągalności. Pozostałe
ograniczenia źródłowe i 5721 initializerów są opisane w [early-init-evidence.md](early-init-evidence.md).

## Minimalny rzeczywisty zakres dalszej pracy

1. Zaprojektować osobny tryb first-inserted z odmową innych inserted libraries,
   override paths i flat namespace; ustalić jawnie konsekwencje dla Steam overlay.
   To propozycja, a nie obecnie autoryzowana zmiana loadera w tym dokumencie.
2. Przypiąć faktyczny dyld/shared-cache/Rosetta i pełen dependency closure patchera,
   a następnie audytować ich wcześniejsze callbacks/bindings do głównego obrazu.
   Aktualny dowód kolejności ze źródła dyld nie potwierdza jego wersji na hoście.
3. Dopiero po zamknięciu reachability użyć lokalnej sekwencji, której widoczność
   translacji sprawdzono. Remote cache attribute ani lokalny icache-only nie
   spełniły pomiaru (bajty42, wykonanie7).
4. Osobno rozwiązać containment przy poisoned rollback. „Wszystkie piny pasują”
   ani first-inserted nie gwarantują, że każda przyszła awaria ochrony/zapisu da
   sprawny kod i bezpieczną kontynuację bez CAPIE.

Nie ma obecnie poprawnej podstawy do zamiany runtime_ready na true. Konkretny
wynik tego audytu to odrzucenie thread-handoff jako samodzielnej gwarancji oraz
wykazanie, że bieżąca polityka append-CAPIE nie realizuje hipotezy first-inserted.
