# Documentazione per sviluppatori

## Architettura

Il progetto e' un firmware Arduino per ESP32-CAM AI Thinker, gestito con PlatformIO. Il dispositivo avvia una rete Wi-Fi in modalita' access point, inizializza la camera e serve API HTTP sulla porta 80.

`src/main.cpp` compone i servizi, inizializza l'hardware e registra il routing:

- `GET /status` tramite `StatusService`;
- `POST /register` e autenticazione tramite `ConfigurationService`;
- `POST /led` tramite `LedService`;
- `GET /stream` tramite `StreamingService`.

Componenti principali:

- `ConfigurationService`: persistenza NVS con `Preferences`, registrazione, token di sessione e autenticazione HMAC-SHA256.
- `CommunicationUtils`: parsing JSON, risposte JSON, confronto a tempo costante, HMAC e generazione del token.
- `StatusService`: raccoglie telemetria di sistema, Wi-Fi, camera, batteria e LED per `GET /status`.
- `StreamingService`: inizializza il sensore OV2640 con il pinout AI Thinker e produce lo stream MJPEG.
- `LedService`: gestisce la striscia WS2812B/SP620, gli stati `off`, `solid` e `blink` e la persistenza delle impostazioni.

La configurazione centralizzata e' in `include/config.h`. Il manifest di build e dipendenze e' `platformio.ini`.

## Requisiti

- Visual Studio Code con estensione PlatformIO, oppure PlatformIO Core.
- Scheda ESP32-CAM AI Thinker.
- Adattatore USB-seriale per il caricamento.
- Alimentazione stabile a 5 V durante programmazione e uso della camera.

## Configurazione

Prima del build controllare `include/config.h`:

- `DEVICE_PIN`, identita' e credenziali dell'access point;
- parametri camera (`CAMERA_FRAME_SIZE`, qualita' JPEG e frame buffer);
- configurazione opzionale del partitore batteria;
- abilitazione e parametri della striscia LED.

Per abilitare una striscia WS2812B/SP620 impostare `LED_STRIP_ENABLED` a `1`, `LED_STRIP_COUNT` al numero reale di LED e, se necessario, un GPIO dati non usato dalla camera. Il valore predefinito e' GPIO 13.

La striscia usa il protocollo NeoPixel a un filo, 800 kHz, ordine GRB. Collegare `DIN` al GPIO selezionato e condividere il GND con l'ESP32. Alimentare la striscia con un 5 V adeguato al numero di LED, non dal GPIO o dal pin 3.3 V dell'ESP32. Si raccomandano una resistenza da circa 330 ohm sul segnale e un condensatore da circa 1000 uF tra 5 V e GND vicino alla striscia.

Per la batteria, impostare `BATTERY_ADC_PIN` e il rapporto del partitore (`BATTERY_DIVIDER_RATIO`); il circuito deve mantenere l'ingresso ADC entro i limiti ammessi dalla scheda.

Per migliorare la stabilita' dell'access point e dello streaming HTTP, il
risparmio energetico Wi-Fi e' disabilitato quando
`WIFI_DISABLE_POWER_SAVE` vale `1`. Questo aumenta il consumo energetico; per
riattivarlo impostare il valore a `0` in `include/config.h`.

## Compilazione e caricamento

Da Visual Studio Code:

1. Aprire la cartella del progetto.
2. Usare `PlatformIO: Build`.
3. Mettere la ESP32-CAM in modalita' bootloader, secondo l'adattatore USB-seriale usato.
4. Usare `PlatformIO: Upload`.
5. Aprire `PlatformIO: Serial Monitor` a 115200 baud per leggere l'indirizzo dell'access point e gli eventuali errori della camera.

Da terminale, con PlatformIO Core installato:

```powershell
pio run
pio run --target upload
pio device monitor --baud 115200
```

## Test HTTP

Il test [tests/test_device_http.py](tests/test_device_http.py) usa solo la
libreria standard Python e verifica che la scheda risponda a `GET /status`.
Controlla anche il contratto JSON di batteria, LED interno e striscia LED.

Con il computer collegato alla rete Wi-Fi della scheda eseguire:

```powershell
.venv\Scripts\activate.bat
```

```powershell
python tests/test_device_http.py --verbose
```

Per usare un indirizzo o un timeout diversi:

```powershell
python tests/test_device_http.py --url http://192.168.4.1 --timeout 10
```

Il test `test_register` usa il PIN predefinito `482917`. Per un PIN diverso:

```powershell
python tests/test_device_http.py --pin 123456
```

In alternativa impostare `ESP32_DEVICE_PIN` nell'ambiente.

Il test `test_led_authentication` registra una sessione, invia una richiesta
`POST /led` con firma HMAC errata e verifica `401`, poi ripete la richiesta con
la firma corretta. Con la striscia disabilitata il secondo passaggio attende
`409 led strip is not configured`; con la striscia configurata attende `200`.

Il test `test_register_invalidates_previous_session` verifica che una nuova
registrazione sostituisca la sessione precedente: il vecchio token riceve
`401`, mentre il nuovo token continua a funzionare.

Il test `test_stream_authentication` verifica che `GET /stream` rifiuti una
firma non valida con `401` e che una firma valida restituisca `200`, content
type MJPEG e l'inizio del primo frame. Il test chiude la connessione dopo il
probe, perche' lo stream e' intenzionalmente continuo.

Il test restituisce codice di uscita `0` se tutti i controlli passano. In
alternativa si possono impostare `ESP32_DEVICE_URL` e `ESP32_HTTP_TIMEOUT` come
variabili d'ambiente. Il test richiede che la scheda sia accesa e che il PC
abbia gia' effettuato la connessione al suo access point.

Al termine viene stampato un report con una riga per ogni test (`OK` o
`FAILED`), un messaggio sintetico e il riepilogo complessivo, ad esempio:

```text
TEST REPORT
OK     test_device_exists: completed
OK     test_register: completed
OK     test_register_invalidates_previous_session: completed
OK     test_led_authentication: completed
OK     test_stream_authentication: completed
OK     test_battery_status_contract: completed
RESULT: OK | tests=7 | passed=7 | failed=0
```

Il target definito in `platformio.ini` e' `esp32cam`, con framework Arduino. Il build usa ArduinoJson `^6.21.5` e Adafruit NeoPixel `^1.12.0`; PlatformIO scarica queste dipendenze automaticamente.

## Comportamento all'avvio

1. Inizializza seriale e archivio NVS.
2. Carica ed applica le impostazioni LED salvate.
3. Inizializza la camera; in caso di errore si ferma e stampa `Camera initialization failed` sulla seriale.
4. Avvia l'access point e il server HTTP sulla porta 80.
5. Nel loop gestisce le richieste HTTP e aggiorna il lampeggio LED.

L'orologio del dispositivo viene allineato durante `POST /register`, usando il timestamp ricevuto. Il token e l'offset temporale sono persistiti in NVS.

## Sicurezza e limiti

Il PIN non viene inviato come segreto di rete: la registrazione verifica una prova HMAC. Le API protette usano un HMAC basato sul token di sessione e una finestra temporale di 5 secondi. L'access point usa WPA2.

Il firmware e' pensato per una rete locale affidabile. Per scenari esposti o con minacce fisiche, aggiungere TLS e una protezione hardware delle chiavi. La registrazione supporta una sola sessione: una nuova app registrata invalida il token precedente.


# IDE vscode

## installa plugin platformio 
Permette la comunicazione con device ESP32.

## configurazione iniziale
Collega ESP32 via USB al PC.


## installa su device 

Visualizza le porte a cui e' connesso ESP32
> pio device list -v

Ad esempio:
<code>
COM3
----
Hardware ID: PCI\VEN_8086&DEV_8C3D&SUBSYS_17E010CF&REV_04\3&11583659&0&B3
Description: Intel(R) Active Management Technology - SOL (COM3)

COM4
----
Hardware ID: USB VID:PID=0403:6001 SER=A5069RR4A
Description: USB Serial Port (COM4)
</code>

Recupera la COMx che ha PID=nnnn:pppp


> pio run --target upload --upload-port COM4

se COM4 e' la porta a cui e' connesso.
