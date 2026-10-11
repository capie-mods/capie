# Decyzje dystrybucji — 2026-10-11

Źródło: odpowiedzi użytkownika na pytania z
[distribution-options.txt](evidence/session-20261011/claude-distribution/distribution-options.txt).

1. **Model:** pliki moda przez Steam Workshop + natywny launcher z GitHub Releases.
2. **Sposób włączania DLL:** preferowany wpis launchera w opcjach uruchamiania Steam
   (`"…\RenameAnythingLauncher.exe" %command%`). Świadome włączenie rozszerzenia;
   usunięcie wpisu = zwykły HOI4 bez DLL. Drugi równorzędny tryb: skrót (np. na pulpicie) do launchera z tymi samymi
   parametrami; launcher może oferować przycisk „Utwórz skrót”. Oba tryby
   uruchamiają ten sam łańcuch Paradox Launcher → hoi4.exe.
   Wymaga testu runtime: `%command%` wskazuje Paradox Launcher (`dowser.exe`), więc
   launcher musi rozpoznać `hoi4.exe` utworzony przez ten Paradox Launcher (tylko
   potomek procesu uruchomionego przez launcher, nie dowolny istniejący proces).
3. **Kod publiczny, licencja MIT (wstępnie).** Umożliwia Certum Open Source / SignPath.
4. **Nieznana wersja gry:** launcher uruchamia grę bez DLL i pokazuje jasny komunikat.
5. **Tryb bez DLL:** decyzja root — skrypt wykrywa obecność natywnych capability;
   bez nich pola in-place są ukryte, a stany/miasta/postacie działają przez
   dotychczasowy edytor; nazwa kraju i pole tekstowe niedostępne z komunikatem.

MP: nadal niezweryfikowane; test z drugim graczem po zakończeniu SP.
6. **Skan antywirusowy przed wydaniem (obowiązkowy):** wszystkie publikowane binaria
   (launcher, DLL, injectory) przechodzą VirusTotal, a linki do wyników trafiają do
   release notes. Upload do VirusTotal upublicznia plik — wykonywany wyłącznie dla
   finalnych artefaktów wydania i za każdym razem po zgodzie użytkownika.
   Lokalnie, bez publikacji: Windows Defender `MpCmdRun -Scan -ScanType 3`.
   2026-10-11: country-capability, inline-editor-debug-bridge, inline-editor-render-bridge,
   launcher-candidate, caption-refresh-candidate — „found no threats” (Defender,
   skan custom; nie jest to wynik wielu silników ani heurystyk chmurowych VT).
7. **Zgoda użytkownika (2026-10-11):** upload finalnych artefaktów wydania do VirusTotal
   „jak przyjdzie na to pora”. Dotyczy wyłącznie plików wydania; każdy upload odnotować
   w release notes.
8. **Bootstrap CLI w PowerShell** (jedna linijka w terminalu), interaktywnie: (a) czy
   zainstalować launcher/DLL, (b) czy dodać launcher do opcji uruchamiania Steam —
   z jasną informacją, że gra z DLL nie jest deklarowana jako zgodna z MP, dopóki
   wpis nie zostanie cofnięty; plus tryb odinstalowania/cofnięcia.
