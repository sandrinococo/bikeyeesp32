@echo off
setlocal enabledelayedexpansion

:: ---------------------------------------------------------
:: Controllo dei parametri da riga di comando (ON | OFF)
:: ---------------------------------------------------------
set "ACTION=%~1"

if /I "%ACTION%"=="ON" goto :MODE_ON
if /I "%ACTION%"=="OFF" goto :MODE_OFF

echo.
echo USO: %~nx0 [ON^|OFF]
echo   ON  - Cerca e connette a una rete Wi-Fi che inizia per "ESP32"
echo   OFF - Disconnette l'ESP32 se attualmente connesso
exit /b 1

:: =========================================================
:: MODALITÀ OFF: DISCONNESSIONE
:: =========================================================
:MODE_OFF
echo Controllo della connessione attuale...

:: Verifica se siamo connessi a una rete ESP32
netsh wlan show interfaces | findstr /C:"SSID" | findstr /B /C:"    SSID" | findstr /C:"ESP32" >nul
if errorlevel 1 (
    :: Controllo alternativo sulla riga del profilo
    netsh wlan show interfaces | findstr /C:"Profile" | findstr /C:"ESP32" >nul
    if errorlevel 1 (
        echo [INFO] Nessuna connessione attiva a reti ESP32.
        exit /b 0
    )
)

echo Disconnessione dalla rete ESP32 in corso...
netsh wlan disconnect >nul
if not errorlevel 1 (
    echo [SUCCESSO] Disconnessione effettuata.
    exit /b 0
) else (
    echo [ERRORE] Impossibile disconnettere la rete.
    exit /b 1
)

:: =========================================================
:: MODALITÀ ON: RICERCA E CONNESSIONE
:: =========================================================
:MODE_ON
echo Ricerca reti Wi-Fi con SSID che iniziano per "ESP32"...

:: 1. Scansiona le reti disponibili e le salva in un file temporaneo
netsh wlan show networks mode=bssid > "%TEMP%\wifi_list.txt"

set "TARGET_SSID="

:: 2. Cerca la rete ESP32 nel file temporaneo
for /f "tokens=*" %%A in ('type "%TEMP%\wifi_list.txt"') do (
    set "line=%%A"
    
    echo !line! | findstr /R "^SSID [0-9]* :" >nul
    if not errorlevel 1 (
        for /f "tokens=2* delims=:" %%B in ("!line!") do (
            set "found_ssid=%%B"
            set "found_ssid=!found_ssid:~1!"
            
            echo !found_ssid! | findstr /B "ESP32" >nul
            if not errorlevel 1 (
                set "TARGET_SSID=!found_ssid!"
                goto :found
            )
        )
    )
)

if exist "%TEMP%\wifi_list.txt" del "%TEMP%\wifi_list.txt"
echo [ERRORE] Nessuna rete Wi-Fi trovata con SSID iniziale "ESP32".
exit /b 1

:found
if exist "%TEMP%\wifi_list.txt" del "%TEMP%\wifi_list.txt"

echo [SUCCESSO] Trovata la rete: "!TARGET_SSID!"
echo Richiesta di connessione in corso...

:: 3. Avvia la connessione
netsh wlan connect name="!TARGET_SSID!" >nul

:: 4. Verifica dell'avvenuta connessione (fino a 10 tentativi)
echo In attesa di conferma connessione...
set /a attempts=0

:check_loop
timeout /t 1 /nobreak >nul
set /a attempts+=1

netsh wlan show interfaces | findstr /C:"!TARGET_SSID!" >nul
if not errorlevel 1 (
    echo [OK] Connessione a "!TARGET_SSID!" stabilita con successo!
    exit /b 0
)

if !attempts! LSS 10 goto :check_loop

echo [ERRORE] Impossibile confermare la connessione a "!TARGET_SSID!" entro il tempo limite.
exit /b 1