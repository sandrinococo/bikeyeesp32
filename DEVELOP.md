# Documentazione per sviluppatori

## Architettura

Il progetto e' un firmware Arduino per ESP32-CAM AI Thinker, gestito con PlatformIO. Il dispositivo avvia una rete Wi-Fi in modalita' access point, inizializza la camera e serve API HTTP asincrone con ESPAsyncWebServer sulla porta 80. Lo stream MJPEG e' una risposta a chunk; il callback attende una nuova sequenza di frame fino a un secondo e termina lo stream se la camera non produce frame. E' ammesso un solo stream attivo per volta: una nuova richiesta chiude e sostituisce quello precedente.

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

La configurazione centralizzata e' in `include/config.h`. Il manifest di build e dipendenze e' `platformio.ini`, che include ESPAsyncWebServer e AsyncTCP.

## Requisiti

- Visual Studio Code con estensione PlatformIO, oppure PlatformIO Core.
- Scheda ESP32-CAM AI Thinker.
- Adattatore USB-seriale per il caricamento.
- Alimentazione stabile a 5 V durante programmazione e uso della camera.
- PlatformIO e' installato in %USERPROFILE%\.platformio\penv\Scripts e per attivarlo occorre %USERPROFILE%\.platformio\penv\Scripts\activate

## Configurazione

Prima del build controllare `include/config.h`:

- `DEVICE_PIN`, identita' e credenziali dell'access point;
- parametri camera (`CAMERA_FRAME_SIZE`, qualita' JPEG e frame buffer);
- configurazione opzionale del partitore batteria;
- abilitazione e parametri della striscia LED.

Per abilitare una striscia WS2812B/SP620 impostare `LED_STRIP_ENABLED` a `1` e `LED_STRIP_COUNT` al numero reale di LED: sono soltanto valori predefiniti, perche' `POST /led` puo' cambiare a runtime sia l'abilitazione sia il numero di LED (massimo `LED_STRIP_MAX_COUNT`, 50) salvandoli in NVS. Se necessario usare un GPIO dati non usato dalla camera. Il valore predefinito e' GPIO 13.

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

Il test [tests/test_device_httpr2.py](tests/test_device_httpr2.py) usa la
libreria standard Python, mentre il test dello stream richiede anche OpenCV e
NumPy. Verifica che `GET /status` restituisca un JSON valido, che il dispositivo
risulti attivo e che siano presenti versione e data di build. Prima dell'intera
suite viene eseguito `connetti_esp32.bat ON`;
`connetti_esp32.bat OFF` viene eseguito alla fine della suite anche se uno o
piu' test falliscono. Ogni test crea e chiude la propria connessione HTTP.
`test_02_register_valid_and_invalid_proof` verifica che `/register` accetti
PIN, nonce e prova HMAC corretti e rifiuti una prova HMAC alterata con HTTP 401.
`test_03_led_start_stop_blink_stop` esegue nell'ordine start, stop, blink e stop;
prima di ciascuna richiesta `POST /led` registra una nuova sessione e firma gli
header di autenticazione con il relativo token.
`test_04_camera_accepts_valid_and_rejects_invalid_parameters` verifica una
configurazione ammessa, rifiuta una qualità JPEG fuori intervallo e ripristina
in ogni caso la configurazione iniziale letta da `/status`.
`test_05_stream_displays_mjpeg_frames_for_at_least_5_seconds` registra una
sessione, apre `/stream` e mostra i frame JPEG decodificati con OpenCV per
almeno cinque secondi; la console riporta il numero progressivo e le dimensioni
di ogni frame. Richiede una sessione grafica attiva e OpenCV:

```powershell
python -m pip install -r requirements-test.txt
```

Ogni test stampa in console inizio/fine e ogni richiesta HTTP con URI, header e
body, seguita da status, header e body della risposta. Alla fine viene stampato
il riepilogo con esito e conteggio dei test passati, falliti e saltati.

**Con il computer collegato alla rete Wi-Fi della scheda eseguire:**

```powershell
.venv\Scripts\activate.bat
```
```powershell
python tests/test_device_httpr2.py --verbose
```

Il test restituisce codice di uscita `0` se tutti i controlli passano. In
alternativa si possono impostare `ESP32_DEVICE_URL` e `ESP32_HTTP_TIMEOUT` come
variabili d'ambiente. Il test richiede che la scheda sia accesa e che il PC
abbia gia' effettuato la connessione al suo access point.

Il target definito in `platformio.ini` e' `esp32cam`, con framework Arduino. Il build usa ArduinoJson `^6.21.5` e Adafruit NeoPixel `^1.12.0`; PlatformIO scarica queste dipendenze automaticamente.

## Comportamento all'avvio

1. Inizializza seriale e archivio NVS.
2. Carica ed applica le impostazioni LED salvate.
3. Inizializza la camera; in caso di errore si ferma e stampa `Camera initialization failed` sulla seriale.
4. Avvia l'access point e il server HTTP asincrono sulla porta 80.
5. Il loop aggiorna il lampeggio LED; le richieste HTTP sono gestite dal server asincrono.

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
> pio device list 

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

Pulisce l'ultima build localmente
> pio run --target clean

Build e carica su ESP32

Su scheda programmatrice tieni premuto O01 (BOOT/FLASH) e premi RST , rilascia RST e rilascia O01 (BOOT/FLASH)
> pio run --target upload --upload-port COM4
Alla fine premi RST.

se COM4 e' la porta a cui e' connesso monitor serial deve essere chiuso.

monitor
> pio device monitor -p COM4

# ATTENZIONE

## Per la verifica del SW su scheda esp32 (tramite monitor) 

- la scheda esp32 e' collegata a speedupboost
- speedupboost collegato a batteria 
- batteria collegata ad alimentatore USB
- interuttore batteria acceso
- la scheda esp32 e' collegata a scheda programmatrice senza 5V
- scheda programmatrice collegata via USB a pc

## nel caso la scheda non risponda piu' per problemi di flash corrotta (monitor non mostra nulla neanche dopo RST)

E' possibile fare erase della memoria flash.

> pip install esptool==4.8.1

Su scheda programmatrice tieni premuto O01 (BOOT/FLASH) e premi RST , rilascia RST e rilascia O01 (BOOT/FLASH)
> python -m esptool --chip esp32 --port COM4 erase_flash
Alla fine premi RST.
