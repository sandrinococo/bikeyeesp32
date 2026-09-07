# ESP32-CAM device service

Firmware per ESP32-CAM AI Thinker che crea una rete Wi-Fi locale e offre a una app mobile:

- stato e informazioni del dispositivo;
- registrazione di una sola app alla volta;
- streaming video MJPEG autenticato;
- controllo di una striscia LED opzionale.

## Attivazione e connessione

Dopo aver caricato e avviato il firmware, collegare il telefono alla rete Wi-Fi generata dal dispositivo:

- SSID: `ESP32-CAM-AI`
- password: `esp32cam-local`
- indirizzo del dispositivo: `192.168.4.1`

La rete ospita un solo client Wi-Fi alla volta. Prima di usare gli endpoint autenticati, l'app deve registrarsi con `POST /register`.

## Endpoint API

La base URL e' `http://192.168.4.1`.

### `GET /status`

Non richiede autenticazione. Usarlo per individuare il dispositivo e leggerne lo stato prima della registrazione.

```http
GET /status HTTP/1.1
Host: 192.168.4.1
```

La risposta contiene identita' del dispositivo, memoria, stato Wi-Fi, impostazioni e FPS della camera, batteria e stato LED. Esempio:

```json
{
  "tipo": "ESP32-CAM",
  "version": "1.0",
  "deviceName": "ESP32-CAM-AI",
  "serial": "1234567890",
  "running": true,
  "wifi": {"mode": "accessPoint", "connectedStations": 1},
  "camera": {"resolution": "VGA", "format": "JPEG", "fps": 0},
  "battery": {"present": false, "levelPercent": null, "voltageMv": null},
  "ledStatus": {"present": false, "configured": false, "mode": "off"}
}
```

In modalita' access point, `wifi.signalDbm` non misura il segnale percepito dal telefono; l'app deve ricavarlo dalla propria connessione Wi-Fi.

### `POST /register`

Non richiede autenticazione. Registra l'app e restituisce il token necessario per le richieste successive. Una nuova registrazione sostituisce quella precedente.

```http
POST /register HTTP/1.1
Host: 192.168.4.1
Content-Type: application/json

{"pin":"482917","nonce":"random-app-nonce-lungo-almeno-16-caratteri","proof":"hmac-hex","timestamp":1720000000}
```

Campi richiesti:

- `pin`: PIN del dispositivo.
- `nonce`: stringa casuale lunga almeno 16 caratteri.
- `proof`: `HMAC-SHA256(key=PIN, message=nonce)`, in esadecimale minuscolo.
- `timestamp`: orario Unix in secondi, da usare anche per sincronizzare la sessione.

Risposta:

```json
{"registered":true,"sessionToken":"...","timestamp":1720000000}
```

Conservare `sessionToken` nell'archivio sicuro dell'app.

### Autenticazione

`POST /led` e `GET /stream` richiedono questi header:

```http
X-TIMESTAMP: <timestamp Unix in secondi>
X-API-KEY: <hmac-hex>
```

Calcolare `X-API-KEY` come `HMAC-SHA256(key=sessionToken, message=sessionToken + ":" + timestamp)`, in esadecimale minuscolo. Il timestamp deve differire dall'orologio del dispositivo di non piu' di 5 secondi; in caso contrario, o con credenziali non valide, il dispositivo risponde con HTTP `401`.

### `GET /stream`

Richiede autenticazione. Restituisce uno stream `multipart/x-mixed-replace` composto da frame JPEG.

```http
GET /stream HTTP/1.1
Host: 192.168.4.1
X-TIMESTAMP: 1720000001
X-API-KEY: <hmac-hex>
```

Il client deve leggere le parti delimitate da `frame` e decodificare il contenuto `image/jpeg` di ogni parte. Il valore `camera.fps` di `GET /status` viene aggiornato durante lo stream.

### `POST /led`

Richiede autenticazione e una striscia LED configurata. In caso contrario risponde con HTTP `409`.

```http
POST /led HTTP/1.1
Host: 192.168.4.1
Content-Type: application/json
X-TIMESTAMP: 1720000001
X-API-KEY: <hmac-hex>

{"mode":"solid","brightness":80,"red":255,"green":40,"blue":0}
```

Campi:

- `mode`: `off`, `solid` oppure `blink`.
- `brightness`, `red`, `green`, `blue`: valori interi da 0 a 255.
- `intervalMillis`: obbligatorio per regolare il lampeggio, da 50 a 60000 millisecondi; se omesso mantiene il valore corrente.

Esempio di lampeggio:

```json
{"mode":"blink","intervalMillis":1000,"brightness":80,"red":255,"green":40,"blue":0}
```

Per spegnere la striscia:

```json
{"mode":"off"}
```

La risposta conferma la configurazione applicata. Le impostazioni LED vengono mantenute anche dopo il riavvio e sono disponibili in `ledStatus` tramite `GET /status`.

## Errori

Le risposte di errore sono JSON e usano principalmente questi codici HTTP:

- `400`: body JSON o parametri non validi;
- `401`: dispositivo non registrato, header mancanti, timestamp scaduto o firma non valida;
- `404`: endpoint inesistente;
- `409`: striscia LED non configurata.
