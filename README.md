# GewinneFinalOllama

C++-Pipeline zum automatischen Abarbeiten von Gewinnspiel-Links, Download der Webseiten, Übergabe an Ollama (deepseek-r1:7b) und strukturierter Speicherung der Ergebnisse.

## Voraussetzungen
- Linux-Umgebung mit `curl` (für den Download) und installiertem Ollama inkl. Modell `deepseek-r1:7b`.
- Kompiler mit C++17-Unterstützung (`g++`, `clang++`, …).

## Projektstruktur
- `src/main.cpp` – Hauptprogramm mit den Unterprogrammen (Links laden, Webseiten holen, Prompts verarbeiten, Fortschritt halten).
- `links.txt` – Beispiel-Liste von Gewinnspiel-Links (eine Zeile pro Link). Pfad ist per Flag konfigurierbar.
- `prompts.txt` – Vorgabeprompt pro Zeile. Wird automatisch eingelesen; fällt auf interne Defaults zurück, falls leer.
- `result.txt` – Wird zur Laufzeit angelegt/befüllt und enthält die Ergebnisse pro Link im gewünschten Format.
- `progress.state` – Merkt sich den letzten bearbeiteten Link, damit ein Neustart an der richtigen Stelle fortsetzt.
- `current_page.json` – Temporäre JSON-Datei je Link mit Seitentext und Zwischenergebnissen (wird nach Abschluss des Links gelöscht).

## Bauen
```bash
g++ -std=c++17 src/main.cpp -o gewinnspiel
```

## Ausführen
Standardaufruf (verwendet die Dateien im Projektordner):
```bash
./gewinnspiel
```

Konfiguration über Flags (alle optional):
```bash
./gewinnspiel \
  --links /pfad/zu/deinen_links.txt \
  --prompts /pfad/zu/deinen_prompts.txt \
  --progress /pfad/zu/progress.state \
  --result /pfad/zu/result.txt \
  --temp-json /pfad/zu/current_page.json \
  --model deepseek-r1:7b \
  --poll-minutes 15
```

### Ablauf zur Laufzeit
1. **Links lesen & Fortschritt prüfen** – startet bei der nächsten offenen Zeile aus `progress.state`.
2. **Webseite downloaden** – HTML per `curl`, Skript/Style entfernt, Rest textualisiert.
3. **JSON schreiben** – Seitentext + bisherige Ergebnisse in `current_page.json`.
4. **Prompts sequenziell an Ollama** – jeder Prompt erhält den JSON-Kontext, Ausgaben werden auf dem Terminal in der Form
   `Prompt: …`, `OLLAMA denken: …`, `Ergebnis: …` gezeigt. Antworten werden in `result.txt` und in das JSON geschrieben.
5. **Fehlerfall** – wenn eine Seite nicht geladen werden kann: Hinweis im Terminal und Eintrag `Webseite nicht vorhanden` in `result.txt`.
6. **Bereinigung & Warten** – nach allen Prompts JSON löschen, Fortschritt speichern; wenn keine neuen Links vorhanden sind, wartet das Programm 15 Minuten (per Flag änderbar) und prüft erneut.

### Wichtige Hinweise
- `result.txt` und `progress.state` liegen im selben Ordner wie die verarbeiteten Dateien, können aber über Flags verlegt werden.
- Ollama wird mit `OLLAMA_NUM_GPU=1` aufgerufen, damit standardmäßig die GPU genutzt wird (sofern verfügbar).
- Die Prompts verlangen streng formatierte Antworten (z. B. `ADatum:<01.01.2025>`). Ollama erhält den Seitentext und alle bisherigen Ergebnisse als JSON-Kontext.

