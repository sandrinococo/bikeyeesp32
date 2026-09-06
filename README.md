# ESP32-CAM device service

Firmware PlatformIO per ESP32-CAM AI Thinker. Il dispositivo crea una rete Wi-Fi locale con SSID `ESP32-CAM-AI`, espone lo stato e lo stream MJPEG e supporta la registrazione di una sola app mobile.

## Struttura del firmware

- `main.cpp`: inizializzazione hardware, Wi-Fi e routing HTTP.
- `ConfigurationService`: registrazione, autenticazione HMAC e accesso alla NVS.
- `CommunicationUtils`: risposte JSON, parsing del body e primitive di hashing.
- `LedService`: pilotaggio WS2812B/SP620, lampeggio e persistenza delle impostazioni.
- `StatusService`: costruzione della risposta di `GET /status`.
- `StreamingService`: inizializzazione camera, stream MJPEG e misura FPS.

## Avvio

1. Installare l'estensione PlatformIO in VS Code.
2. Collegare la ESP32-CAM usando un adattatore USB-seriale.
3. Verificare `DEVICE_PIN` in `include/config.h`: questo e' il PIN stampato sul modulo.
4. Eseguire `PlatformIO: Build`, poi `PlatformIO: Upload`.

Dopo l'avvio, l'app mobile deve collegarsi alla rete Wi-Fi `ESP32-CAM-AI` con password `esp32cam-local`. L'IP AP predefinito e' `192.168.4.1`.

## API

### Stato e registrazione

`GET /status` non richiede autenticazione, per consentire discovery e registrazione. Risponde con:

```json
{
  "tipo":"ESP32-CAM", "version":"1.0",
  "deviceName":"ESP32-CAM-AI", "serial":"1234567890",
  "running":true, "message":"Device is running",
  "ram":{"freeBytes":123456,"totalBytes":327680},
  "wifi":{"mode":"accessPoint","signalDbm":-127,"connectedStations":0},
  "camera":{"resolution":"VGA","format":"JPEG","fps":0,
    "settings":{"brightness":0,"contrast":0,"saturation":0,
      "autoExposure":1,"exposureLevel":0}},
  "battery":{"present":false,"levelPercent":null,"voltageMv":null},
  "ledStatus":{"present":false,"configured":false,"gpio":-1,
    "count":0,"brightness":0,"mode":"off"}
}
```

`fps` e' il valore misurato durante lo stream MJPEG. In modalita' access point
`signalDbm` non rappresenta il segnale dell'AP verso il telefono: l'ESP32 non
puo' misurare direttamente quel valore; il client deve rilevarlo dal proprio
Wi-Fi.

## Batteria e striscia LED

La ESP32-CAM AI Thinker non include un sensore batteria o un driver per
strisce LED. Per una batteria con partitore resistivo, impostare in
`include/config.h` `BATTERY_ADC_PIN` e i limiti in millivolt. Il partitore deve
ridurre la tensione sotto il limite ADC della scheda.

La striscia WS2812B con controller SP620 e' compatibile con il protocollo
NeoPixel a un filo, 800 kHz e ordine colore GRB. In `include/config.h`
impostare `LED_STRIP_ENABLED` a `1` e `LED_STRIP_COUNT` al numero effettivo di
LED. Il GPIO dati predefinito e' `13`; cambiarlo solo se necessario e usare un
GPIO non occupato dalla camera.

I tre collegamenti sono `5V`, `GND` e `DIN`: il segnale va dal GPIO ESP32 al
`DIN` della striscia. La massa deve essere comune. La striscia non deve essere
alimentata dal pin 3.3V o dal GPIO dell'ESP32: per piu' di pochi LED usare un
alimentatore 5V dimensionato per la striscia, con massa collegata alla massa
ESP32. Aggiungere una resistenza da circa 330 ohm sul segnale dati e un
condensatore da circa 1000 uF tra 5V e GND vicino alla striscia.

La striscia viene pilotata con `Adafruit_NeoPixel`. Il comando autenticato
`POST /led` accetta, ad esempio:

```json
{"mode":"solid","brightness":80,"red":255,"green":40,"blue":0}
```

Per lampeggiare con un intervallo di un secondo:

```json
{"mode":"blink","intervalMillis":1000,"brightness":80,"red":255,"green":40,"blue":0}
```

Per spegnerla:

```json
{"mode":"off"}
```

I valori RGB e luminosita' sono compresi tra 0 e 255. `intervalMillis` e'
compreso tra 50 e 60000 millisecondi. Le impostazioni sono valide fino al
riavvio; `/status` restituisce sempre i valori attualmente applicati,
inclusi `intervalMillis` e `blinkOn`.

`POST /register` non richiede autenticazione. Body:

```json
{"pin":"482917","nonce":"random-app-nonce","proof":"hmac-hex","timestamp":1720000000}
```

`proof` e' `HMAC-SHA256(key=PIN, message=nonce)`, espresso in esadecimale minuscolo. Il `timestamp` Unix della richiesta viene associato alla sessione. La risposta contiene il token di sessione:

```json
{"registered":true,"sessionToken":"...","timestamp":1720000000}
```

Il token viene salvato in NVS. Una nuova registrazione sostituisce il token precedente.

### Autenticazione delle richieste

Tutte le richieste successive, incluso `GET /stream`, devono includere:

- `X-TIMESTAMP`: timestamp Unix in secondi;
- `X-API-KEY`: `HMAC-SHA256(key=sessionToken, message=sessionToken + ":" + timestamp)`, in esadecimale minuscolo.

Il timestamp deve differire dall'orologio del dispositivo di massimo 5 secondi. Un errore restituisce HTTP `401`.

`GET /stream` restituisce `multipart/x-mixed-replace` con frame JPEG. Il client puo' trasformare ogni parte JPEG nella propria lista di interi.

## Note di sicurezza

La registrazione e' protetta dal PIN tramite challenge-response: il PIN non viene trasmesso in chiaro. La rete AP usa WPA2 e il token viene persistito in NVS. Per un prodotto con minacce fisiche o traffico oltre la rete locale, aggiungere TLS o un secure element; il progetto e' predisposto per sostituire il trasporto Wi-Fi con BLE senza cambiare il contratto di autenticazione HTTP.
