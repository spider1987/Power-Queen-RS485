# Power Queen M2430N – ESP32-S3 RS485 Monitor

## Stand: Firmware 0.1.0

Minimale Diagnosefirmware: UART1, Modbus RTU 03/04, CRC16, HEX-Ausgabe,
Registerscanner, vorsichtige Baud/Slave-Diagnose und Statistik. FC06/FC10 sind
implementiert, aber standardmäßig **vor jeder Übertragung gesperrt**. Es gibt
keinen Schreibbefehl im Serial Monitor. Kein automatischer Busverkehr beim Boot.
Die zunächst geprüfte Basis wurde um WLAN, Webinterface, REST, RAM-Verlauf und
optionales MQTT ergänzt. Alle Scans starten ausdrücklich per Serial oder Web.
Keine erfundenen Register, Messwerte oder Ladephasen. Das Datenmodell ist vorbereitet;
unbekannte Messwerte sind NAN, ganzzahlige Zustände haben separate Gültigkeitsflags.

> **KEINE UNBEKANNTEN PINS DER REGLERBUCHSE MIT ESP32 ODER ADAPTER VERBINDEN.**
>
> **DO NOT CONNECT RS485 A/B DIRECTLY TO ESP32 GPIO.**

## Hardware und Verdrahtung

- ESP32-S3; das Buildprofil nimmt vorläufig DevKitC-1-N8 ohne PSRAM an.
  Boardtyp, Flashgröße und zugängliche Pins vor dem Flashen überprüfen.
- TTL↔RS485-Adapter mit automatischer Richtungsumschaltung und **3,3-V-TTL-Pegeln**.
  Ein mit 5 V versorgbarer Adapter ist nicht automatisch GPIO-kompatibel.
- ESP32 über USB, Adapter entsprechend dessen Datenblatt separat versorgen.
  Keine Versorgung aus der Reglerbuchse verwenden.

Am vorhandenen Regler ist laut Nutzer eine **RJ-Buchse mit sechs Pins** verbaut.
Die ursprüngliche achtpolige RJ45-Hypothese ist damit nicht anwendbar und wurde
verworfen. **Die Belegung aller sechs Kontakte ist unbekannt.** Insbesondere sind
GND, A/D+, B/D− und mögliche Versorgungspins noch nicht bestimmt. Keine Zuordnung
von einem ähnlichen Regler oder einem achtpoligen Stecker übernehmen.

Vor Anschluss Originalunterlagen bzw. eine belastbare Herstellerbelegung für
genau diese Gerätevariante beschaffen. Ein Foto der Buchse und ihrer Beschriftung
hilft bei der Identifikation, bestätigt allein aber keine elektrische Belegung.
Pinzählung immer mit dokumentierter Steckeransicht festlegen; nicht nach Kabelfarben
verdrahten. Verdrahtung spannungsfrei ändern. ESP32/Adapter bleiben separat versorgt.

| ESP32-S3 (Beispielkonfiguration) | Adapter TTL-Seite |
|---|---|
| GPIO17 TX | Eingang zum RS485-Sender |
| GPIO18 RX | Ausgang vom RS485-Empfänger |
| GND | GND |
| passende Versorgung | VCC laut Adapterdatenblatt |

**TXD/RXD-Beschriftungen an Adaptern sind nicht einheitlich.** Wenn dein Modul
TXD als *TTL-Ausgang* und RXD als *TTL-Eingang* bezeichnet, gilt ESP-TX→RXD und
ESP-RX←TXD wie in der Projektbeschreibung. Bei Transceiver-bezogener Beschriftung
gilt häufig ESP-TX→TXD/DI und ESP-RX←RXD/RO. Entscheidend sind Eingang/Ausgang
laut Datenblatt, nicht pauschales Kreuzen. ESP-RX niemals mit 5-V-TTL verbinden.
Lange Leitungen, Abschlusswiderstände, Biasing, Massepotentiale und galvanische
Trennung entsprechend dem Busaufbau berücksichtigen.

Alle UART-Pins und Einstellungen stehen in `include/config.h`. DE/RE ist mit
`RS485_DIRECTION_PIN = -1` deaktiviert. Später kann ein GPIO für zusammengeführtes
DE und /RE mit aktiv-HIGH-Sendersteuerung eingetragen werden.

## Installation, Build und Flash

PlatformIO IDE in VS Code oder PlatformIO Core installieren; Projektordner öffnen.
Das Profil verwendet Arduino und die festgelegte Espressif-Plattform 6.12.0.
PubSubClient 2.8 wird von PlatformIO automatisch installiert.

```sh
pio run
pio device list
pio run -t upload --upload-port COM12
pio run -t uploadfs --upload-port COM12
pio device monitor --port COM12 --baud 115200
```

COM12 ist nur ein Beispiel; tatsächlichen Port auswählen. USB CDC ist im Profil
aktiviert. Bei fehlendem Upload ggf. BOOT halten, RESET drücken, BOOT loslassen.
Nach Upload kann sich der USB-Port ändern. GPIO19/20 werden nicht für RS485 verwendet,
da diese auf S3 häufig USB dienen. Die gewählten GPIO17/18 trotzdem am Board prüfen.
Firmware und LittleFS-Webdateien jeweils hochladen. Fehlende Webdateien führen zu
einer erklärenden HTTP-503-Seite; das Dateisystem wird niemals automatisch formatiert.

## WLAN, Web und REST

`WIFI_SSID` und `WIFI_PASSWORD` in `include/config.h` setzen, bevorzugt stattdessen
in einer lokalen, von Git ignorierten `include/secrets.h` definieren:

```cpp
#pragma once
#define WIFI_SSID "Dein WLAN"
#define WIFI_PASSWORD "Dein Passwort"
```

Nach 30 Sekunden ohne WLAN startet `PowerQueen-Setup` unter `http://192.168.4.1`.
Standardmäßig ist der AP offen; bei Bedarf `WIFI_AP_PASSWORD` mit mindestens acht
Zeichen setzen. Die Station verbindet sich weiterhin mit dem konfigurierten WLAN;
der einmal gestartete AP bleibt bis zum Neustart aktiv. Zugangsdaten werden aktuell
im Build konfiguriert, es gibt noch keinen Browser-Konfigurationsassistenten.
Keine Zugangsdaten in Chat, Git oder MQTT eintragen.

- `/`: lokale responsive Übersicht und Verlauf, ohne CDN.
- `/debug`: FC03/04-Scanner, Start/Ende/Blockgröße, Stop, Diagnose, HEX und Statistik.
- `GET /api/status`: Messwerte (unbekannt = `null`), Kommunikationsstatus, Statistik.
- `GET /api/modbus/raw`: letzte Transaktion samt Einstellungen, TX/RX und CRC.
- `GET /api/modbus/stats`: Zähler.
- `GET /api/history`: chronologische RAM-Proben.
- `GET /api/modbus/control`: Token für die folgenden POST-Aufrufe.
- `POST /api/modbus/scan`: Formularfelder `start`, `end`, `function`, optional `count`.
- `POST /api/modbus/stop`, `POST /api/modbus/diagnose`.

POST-Aufrufe benötigen `X-Scan-Token`; das lokale UI setzt diesen automatisch.
Das Token verhindert fremde Browser-Formularaufrufe, ist aber kein Benutzerlogin.
Diese lokale HTTP-Diagnose nicht unverändert öffentlich ins Internet stellen.
Es gibt keine Web-API für FC06/FC10. Vollständige Scanprotokolle sind im Serial Monitor;
Web/REST zeigen die letzte Transaktion.

`online` bezeichnet eine aktuelle CRC-korrekte Modbus-Antwort, einschließlich
Exceptions. `dataOnline` bezeichnet dekodierte Messdaten. Grün: Antwort jünger als
15 s; gelb: 15–60 s; rot: keine Antwort bzw. älter als 60 s. Ohne laufenden Scan
oder weitere Anfrage wird die letzte Verbindung also absichtlich veraltet.
Zeitstempel sind Millisekunden seit Boot, keine Uhrzeit. WLAN-Ausfall wird im UI
separat als nicht erreichbarer ESP32 angezeigt.

Der Ringbuffer speichert 361 Proben, alle zehn Sekunden, über etwa 60 Minuten.
Neustart löscht den RAM-Verlauf. Unbekannte oder veraltete Messwerte bleiben `null`
und erscheinen als Lücken. Ohne bestätigte Register gibt es keine Kurven.

## MQTT (optional, standardmäßig aus)

In `config.h` `MQTT_ENABLED`, Host, Port und gegebenenfalls Zugangsdaten setzen.
Topics unter `powerqueen/`: `status`, `battery/voltage`, `battery/current`,
`battery/power`, `pv/voltage`, `pv/current`, `pv/power`, `controller/temperature`,
`controller/state`, `controller/fault`, `energy/daily`, `energy/total`, `raw`.
Publikation alle zehn Sekunden mit Retain, Status `online`/`stale`/`offline`,
Last Will `offline`. Unbekannte/veraltete Messwerte werden als Literal `null`
publiziert, damit alte Retain-Werte nicht als aktuell stehen bleiben.
Keine MQTT-Subscriptions oder Schreibbefehle. Aktuell MQTT ohne TLS im lokalen Netz;
Wiederverbindungsversuche alle 15 s mit begrenztem Socket-Timeout.

## Kommunikation und Scanner

Startwerte: **9600 Baud, 8N1, Slave 1**; das ist eine Hypothese, kein Nachweis des
M2430N-Protokolls. Serial Monitor: 115200 Baud, Befehle mit Zeilenende.
Registeradressen sind die **rohen, nullbasierten 16-Bit-Adressen im RTU-Frame**,
keine 40001-Notation. Dezimal oder `0x`-Hex; FC als `3`/`4` oder `0x03`/`0x04`.

```text
help
read 0x0100 3
scan 0x0000 0x00ff 3 1
scan 0x0100 0x01ff 4 1
scan 0x1000 0x10ff 3 1
scan 0x3000 0x30ff 4 1
stop
stats
baud 9600
slave 1
diagnose
```

Ein Scan liest standardmäßig ein Register je Anfrage; optional letzter Parameter
1–4. Nach Abschluss einer Anfrage mindestens 200 ms Pause, Timeout 500 ms.
Bei Mehrfachanfragen betrifft eine Exception den gesamten Block; zur Eingrenzung
mit Blockgröße 1 wiederholen. `stop` beendet nach der laufenden Anfrage.
Ein großer Bereich dauert entsprechend lange; keine parallelen Busmaster betreiben.

`diagnose` sendet ausschließlich 80 einzelne Leseanfragen: 2400/4800/9600/19200,
IDs 1/2/10/16/247, FC03/04, Probeadressen 0x0000 und 0x0100. Diese Adressen sind
**keine bestätigten Messregister**. Maximal etwa eine Minute bei Timeouts.
Danach bzw. bei `stop` werden vorherige Baudrate und ID wiederhergestellt.
Eine CRC-korrekte Exception zeigt eine mögliche RTU-Verbindung; sie bestätigt
weder Modell noch Registermapping. Andere Baudraten, Parität oder IDs bleiben ungetestet.

CRC16: Start 0xFFFF, Polynom 0xA001, Low Byte zuerst. Beispiel:
`01 03 00 00 00 02 C4 0B`. Nur vollständig CRC-geprüfte Frames mit passender
Slave-ID, Funktion und Länge liefern Rohregister. Empfang endet nach t3.5 Busruhe;
Timeouts, Überläufe, falsche Frames und Exceptions werden getrennt gezählt.
UART-Initialisierung bedeutet nicht, dass ein Regler angeschlossen ist.

## Register verifizieren und Debugging

1. Verdrahtung, TTL-Pegel, Masse, Adapterrichtung und Boardpins prüfen.
2. Einzelne Anfrage starten und TX/RX inklusive CRC protokollieren.
3. Bei Timeout: Adapterdokumentation, A/B-Zuordnung, ID/Baud/Parität und weitere
   Busmaster prüfen. Kein RX kann auch eine falsche Richtung oder Pinbelegung sein.
4. Exception 01 = Funktion unbekannt, 02 = Adresse unbekannt, 03 = Wert ungültig,
   04 = Gerätefehler. Keine Schreibversuche zur Fehlerbehebung.
5. Rohwerte über mehrere Betriebspunkte mit dem LCD vergleichen. Skalierung,
   Vorzeichen, Wortreihenfolge und Bedeutung mit Originalunterlagen bestätigen.
6. Erst dann `include/registers.h` befüllen und Decoder ergänzen.
7. **Keine Ladeparameter verändern.** `MODBUS_WRITE_ENABLED` bleibt `false`.

CRC-korrekte Frames allein beweisen nicht, dass ein Register die vermutete Größe
enthält. Ein Adapter mit lokaler TX-Echofunktion muss entsprechend konfiguriert
werden; die Firmware akzeptiert Echo nicht als gültige Leseantwort. Leitungsrauschen
kann falsche/überlange Frames erzeugen. Die kooperative Zustandsmaschine begrenzt
Empfangsarbeit und Timeout, ohne endlose Warteschleifen; UART-TX-Drain ist begrenzt.

## Struktur und Prüfung

`modbus_protocol.*`: hardwareunabhängige CRC/Antwortprüfung;
`modbus_client.*`: UART und Transaktionszustand; `main.cpp`: Serial und Diagnose;
`powerqueen.*` und `registers.h`: Datenmodell und spätere bestätigte Dekodierung.
`webserver.*`: WLAN, LittleFS, REST und schreibfreies Scan-Interface;
`mqtt.*`: optionaler Publisher; `data/`: vollständig lokale Oberfläche.
Der eigene Web-Header heißt `webserver_api.h`: `webserver.h` würde unter Windows
mit dem Arduino-Bibliotheksheader `WebServer.h` kollidieren.
Registerdefinitionen können Messfelder über deren exakte Namen aus `PowerQueenData`
zuordnen. Der vorbereitete Decoder unterstützt nur einzelne unsigned 16-Bit-Werte;
Vorzeichen, Mehrwortwerte und Ladephasen erst nach Beleg erweitern. Es gibt derzeit
keine automatische Messregister-Abfrage, weil kein Register bestätigt ist.

Hosttests mit G++ (Linux/WSL, ohne Hardware):

```sh
mkdir -p .pio/host
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -Itests/host -Iinclude -Isrc tests/host/test_modbus.cpp \
  src/modbus_protocol.cpp src/modbus_client.cpp src/powerqueen.cpp -o .pio/host/test_modbus
.pio/host/test_modbus
```

Die Tests verwenden simulierten UART/Timer. Sie prüfen feste CRC-Vektoren,
Frameprüfung, Schreibsperre, Pausen, Timeouts, Exceptions, Überläufe sowie unbekannte
Messwerte und Ringbuffer. Ein Build oder Hosttest ersetzt keine Kommunikation am
echten Regler. WLAN/AP, Webdarstellung, MQTT und echte RS485-Kommunikation müssen
nach dem Flashen separat geprüft werden. Der HTTP-Server und MQTT-Verbindungsaufbau
sind synchron; Diagnosebetrieb zuerst mit deaktiviertem MQTT prüfen.

Referenzen: [Modbus-Spezifikationen](https://www.modbus.org/modbus-specifications),
[PlatformIO ESP32-S3-DevKitC-1](https://docs.platformio.org/en/latest/boards/espressif32/esp32-s3-devkitc-1.html).
