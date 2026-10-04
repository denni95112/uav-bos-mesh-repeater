# UAV-BOS Mesh-Repeater

LoRa-Repeater für die UAV-BOS-Tracker. Er sendet keine eigene Position und hat kein GPS.
Er leitet die Funkpakete der Tracker weiter, mit demselben Meshtastic-kompatiblen Protokoll
wie die Tracker-Firmware.

- Hardware: Heltec **WiFi LoRa 32 V2**, 868 MHz (ESP32, SX1276, OLED)
- Firmware: PlatformIO + Arduino (`src/`)

Die 433-MHz- und 915-MHz-Varianten des Boards passen nicht. Das Frequenzband ist fest EU 868.

## Betriebsarten

| Betriebsart | WLAN | LoRa |
|-------------|------|------|
| Repeater + WLAN | nur Config-AP, kein Router | leitet weiter |
| Repeater ohne WLAN | aus | leitet weiter |

Der Access Point heißt **`UAV-BOS-Repeater-XXXX`**. Die Seite ist `http://192.168.4.1`.
Beim ersten Start ist "Repeater + WLAN" aktiv, damit sich das Gerät einrichten lässt.

- **PRG kurz**: Betriebsart wechseln. Die Auswahl gilt 3 Sekunden nach dem letzten Druck.
- **PRG 3 Sekunden**: OLED an oder aus. Der Funk läuft weiter.

Aus "Repeater ohne WLAN" kommt man mit einem kurzen Tastendruck zurück zum Config-AP.

## Einrichtung

1. Repeater per USB an den PC (Datenkabel). In PlatformIO den Ordner öffnen und auf Upload klicken.
   Falls der Upload nicht startet: **PRG** halten, kurz **RST** drücken, **PRG** loslassen, erneut uploaden.
2. Mit dem WLAN `UAV-BOS-Repeater-XXXX` verbinden und `http://192.168.4.1` öffnen.
3. Dieselben LoRa-Werte eintragen wie bei den Trackern:
   - Modemprofil (Standard LongFast)
   - Frequenz-Slot
   - Hop-Limit
   - Sendeleistung (2–17 oder **20 dBm**; 18 und 19 werden zu 17. Der Tracker-SX1262 darf 22)
   - Weiterleitung: "Alle Pakete", "Nur UAV-BOS-Tracker" oder "Keine"
   - Sendezeit für fremde Pakete
   - **Mesh-Schlüssel**, identisch zu den Trackern
4. Optional ein AP-Passwort (mindestens 8 Zeichen, Benutzer `admin`).
5. Speichern. Das Gerät startet neu.

Ohne denselben Schlüssel erkennt der Repeater den UAV-BOS-Kanal nicht. "Alle Pakete" leitet
fremde Meshtastic-Pakete trotzdem weiter. "Nur UAV-BOS-Tracker" braucht den Schlüssel.

Der Repeater schickt alle 15 Minuten ein Hello mit Hop 0, damit die Tracker ihn als eigenen
Relay erkennen. Das Hello wird nicht weitergeflutet.

## Funk

Gleiche Vorgaben wie die Tracker-Firmware:

- EU_868, Sync-Word `0x2B`, Präambel 16
- Profile ShortFast, ShortSlow, MediumFast, MediumSlow, LongFast, LongModerate, LongSlow
- 250-kHz-Profile senden auf 869,525 MHz. 125-kHz-Profile nutzen Slot 1 (869,4625 MHz) oder Slot 2 (869,5875 MHz)
- 10 % Sendezeit im Band 869,4–869,65 MHz. Fremde Pakete stoppen früher, Tracker-Pakete haben Vorrang
- Kanalname `UAV-BOS`

**Schlüssel in der Firmware (optional):** `MESH_PSK_B64` in `.env` (Vorlage `.env.example`) oder als
Umgebungsvariable. Ein Gerät ohne gespeicherten Schlüssel übernimmt ihn einmalig. Der Wert steht
lesbar in der Binärdatei. Solche Builds nicht veröffentlichen.

## Bauen

```
pio run -e heltec_v2
pio run -e heltec_v2 -t upload
```
