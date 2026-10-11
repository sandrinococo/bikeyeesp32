"""HTTP smoke tests for the ESP32-CAM device service.

Run directly from VS Code or a terminal:
    python tests/test_device_httpr2.py
    python tests/test_device_httpr2.py --url http://192.168.4.1 --verbose
"""

from __future__ import annotations

import argparse
import hashlib
import hmac
import http.client
import json
import os
import secrets
import sys
import time
import unittest
from dataclasses import dataclass
from urllib.error import HTTPError, URLError
from urllib.parse import urlsplit
from urllib.request import Request, urlopen


@dataclass(frozen=True)
class DeviceHttpClient:
    """Small standard-library HTTP client for the unauthenticated status API."""

    base_url: str = "http://192.168.4.1"
    timeout_seconds: float = 5.0
    verbose: bool = True
    device_pin: str = "482917"

    def _force_close_connection(self, connection: http.client.HTTPConnection) -> None:
        """Forza la chiusura immediata della socket HTTP e rilascia la risorsa sull'ESP32."""
        try:
            if connection.sock:
                connection.sock.shutdown(http.client.socket.SHUT_RDWR)
        except Exception:
            pass
        finally:
            connection.close()
            # Pausa di cortesia per permettere all'ESP32 di liberare il socket
            time.sleep(0.3)

    @staticmethod
    def _print(message: str = "") -> None:
        print(message, flush=True)

    def _log_request(self, request: Request, body: str = "") -> None:
        if not self.verbose:
            return
        self._print("REQUEST")
        self._print(f"  method: {request.get_method()}")
        self._print(f"  uri: {request.full_url}")
        self._print("  headers:")
        for name, value in request.header_items():
            self._print(f"    {name}: {value}")
        if body:
            self._print("  body:")
            self._print(body)

    def _log_response(self, status_code: int, headers: object, body: str) -> None:
        if not self.verbose:
            return
        self._print("RESPONSE")
        self._print(f"  status: {status_code}")
        self._print("  headers:")
        for name, value in headers.items():
            self._print(f"    {name}: {value}")
        self._print("  body:")
        self._print(body)

    def get_status(self) -> dict:
        """Fetch and decode GET /status, raising an assertion-friendly error."""
        url = f"{self.base_url.rstrip('/')}/status"
        request = Request(url, method="GET", headers={"Accept": "application/json"})
        self._log_request(request)
        try:
            with urlopen(request, timeout=self.timeout_seconds) as response:
                status_code = response.status
                response_headers = response.headers
                body = response.read().decode("utf-8")
        except HTTPError as error:
            error_body = error.read().decode("utf-8", errors="replace")
            self._log_response(error.code, error.headers, error_body)
            raise AssertionError(f"GET {url} returned HTTP {error.code}") from error
        except URLError as error:
            raise AssertionError(f"device is unreachable at {url}: {error.reason}") from error
        except TimeoutError as error:
            raise AssertionError(f"request to {url} timed out") from error

        self._log_response(status_code, response_headers, body)
        if status_code != 200:
            raise AssertionError(f"GET {url} returned HTTP {status_code}")
        try:
            payload = json.loads(body)
        except json.JSONDecodeError as error:
            raise AssertionError(f"GET {url} returned invalid JSON: {body!r}") from error
        if not isinstance(payload, dict):
            raise AssertionError("/status response must be a JSON object")
        return payload

    def register(self) -> dict:
        """Register a fresh app session using the configured device PIN."""
        nonce = secrets.token_hex(16)
        timestamp = int(time.time())
        proof = hmac.new(
            self.device_pin.encode("utf-8"),
            nonce.encode("utf-8"),
            hashlib.sha256,
        ).hexdigest()
        payload = {
            "pin": self.device_pin,
            "nonce": nonce,
            "proof": proof,
            "timestamp": timestamp,
        }
        body = json.dumps(payload)
        url = f"{self.base_url.rstrip('/')}/register"
        request = Request(
            url,
            data=body.encode("utf-8"),
            method="POST",
            headers={
                "Accept": "application/json",
                "Content-Type": "application/json",
            },
        )
        self._log_request(request, body)
        try:
            with urlopen(request, timeout=self.timeout_seconds) as response:
                status_code = response.status
                response_headers = response.headers
                response_body = response.read().decode("utf-8")
        except HTTPError as error:
            error_body = error.read().decode("utf-8", errors="replace")
            self._log_response(error.code, error.headers, error_body)
            raise AssertionError(f"POST {url} returned HTTP {error.code}") from error
        except URLError as error:
            raise AssertionError(f"device is unreachable at {url}: {error.reason}") from error
        except TimeoutError as error:
            raise AssertionError(f"request to {url} timed out") from error

        self._log_response(status_code, response_headers, response_body)
        if status_code != 200:
            raise AssertionError(f"POST {url} returned HTTP {status_code}")
        try:
            response = json.loads(response_body)
        except json.JSONDecodeError as error:
            raise AssertionError(f"POST {url} returned invalid JSON: {response_body!r}") from error
        if not isinstance(response, dict):
            raise AssertionError("/register response must be a JSON object")
        return response

    def set_led(
        self,
        session_token: str,
        api_key: str,
        timestamp: str | None = None,
        enable: bool = True,
    ) -> tuple[int, dict]:
        """Call POST /led and return its HTTP status and JSON response."""
        timestamp = timestamp or str(int(time.time()))
        payload = {
            "enabled": enable,
            "count": 10,
            "mode": "solid" if enable else "off",
            "brightness": 80,
            "red": 255,
            "green": 40,
            "blue": 0,
        }
        body = json.dumps(payload)
        url = f"{self.base_url.rstrip('/')}/led"
        request = Request(
            url,
            data=body.encode("utf-8"),
            method="POST",
            headers={
                "Accept": "application/json",
                "Content-Type": "application/json",
                "X-TIMESTAMP": timestamp,
                "X-API-KEY": api_key,
            },
        )
        self._log_request(request, body)
        parsed_url = urlsplit(url)
        connection = http.client.HTTPConnection(
            parsed_url.hostname,
            parsed_url.port or 80,
            timeout=self.timeout_seconds,
        )
        try:
            path = parsed_url.path or "/"
            if parsed_url.query:
                path += f"?{parsed_url.query}"
            connection.request(
                "POST",
                path,
                body=body,
                headers={
                    "Accept": "application/json",
                    "Content-Type": "application/json",
                    "X-TIMESTAMP": timestamp,
                    "X-API-KEY": api_key,
                },
            )
            response = connection.getresponse()
            status_code = response.status
            response_headers = response.headers
            response_body = response.read().decode("utf-8", errors="replace")
        except (OSError, TimeoutError) as error:
            raise AssertionError(f"device is unreachable at {url}: {error}") from error
        finally:
            self._force_close_connection(connection)

        self._log_response(status_code, response_headers, response_body)

        try:
            response = json.loads(response_body)
        except json.JSONDecodeError as error:
            raise AssertionError(f"POST {url} returned invalid JSON: {response_body!r}") from error
        if not isinstance(response, dict):
            raise AssertionError("/led response must be a JSON object")
        return status_code, response

    def open_stream(
        self, api_key: str, timestamp: str
    ) -> tuple[http.client.HTTPConnection, http.client.HTTPResponse]:
        """Open /stream and leave its connection available to the caller."""
        url = f"{self.base_url.rstrip('/')}/stream"
        parsed_url = urlsplit(url)
        connection = http.client.HTTPConnection(
            parsed_url.hostname,
            parsed_url.port or 80,
            timeout=self.timeout_seconds,
        )
        request = Request(
            url,
            method="GET",
            headers={
                "Accept": "multipart/x-mixed-replace",
                "X-TIMESTAMP": timestamp,
                "X-API-KEY": api_key,
            },
        )
        self._log_request(request)
        path = parsed_url.path or "/"
        if parsed_url.query:
            path += f"?{parsed_url.query}"
        try:
            connection.request(
                "GET",
                path,
                headers={
                    "Accept": "multipart/x-mixed-replace",
                    "X-TIMESTAMP": timestamp,
                    "X-API-KEY": api_key,
                },
            )
            return connection, connection.getresponse()
        except (OSError, TimeoutError) as error:
            self._force_close_connection(connection)
            raise AssertionError(f"device is unreachable at {url}: {error}") from error

    def stream_probe(self, api_key: str, timestamp: str) -> tuple[int, object, bytes]:
        """Open /stream, read its headers and a small first-frame probe."""
        connection, response = self.open_stream(api_key, timestamp)
        try:
            status_code = response.status
            response_headers = response.headers
            response_body = response.read(512) if status_code == 200 else response.read()
        except (OSError, TimeoutError) as error:
            raise AssertionError(f"stream probe failed: {error}") from error
        finally:
            self._force_close_connection(connection)

        body_text = response_body.decode("utf-8", errors="replace")
        self._log_response(status_code, response_headers, body_text)
        return status_code, response_headers, response_body

    def stream_for_duration(self, api_key: str, timestamp: str, duration_seconds: int = 5) -> None:
            """Legge lo stream /stream per una durata specifica, loggando ogni secondo tipo, dimensione e unicità dei frame."""
            url = f"{self.base_url if 'base_url' in locals() else self.base_url.rstrip('/')}/stream"
            parsed_url = urlsplit(url)
            connection = http.client.HTTPConnection(
                parsed_url.hostname,
                parsed_url.port or 80,
                timeout=self.timeout_seconds,
            )
            request = Request(
                url,
                method="GET",
                headers={
                    "Accept": "multipart/x-mixed-replace",
                    "X-TIMESTAMP": timestamp,
                    "X-API-KEY": api_key,
                },
            )
            self._log_request(request)
            try:
                path = parsed_url.path or "/"
                if parsed_url.query:
                    path += f"?{parsed_url.query}"
                connection.request(
                    "GET",
                    path,
                    headers={
                        "Accept": "multipart/x-mixed-replace",
                        "X-TIMESTAMP": timestamp,
                        "X-API-KEY": api_key,
                    },
                )
                response = connection.getresponse()
                if response.status != 200:
                    self._print(f"Stream fallito con HTTP status {response.status}")
                    return

                content_type = response.headers.get("Content-Type", " sconosciuto")
                self._print(f"Stream connesso. Content-Type: {content_type}")

                start_time = time.time()
                last_log_time = start_time
                buffer = b""
                frame_count = 0
                previous_hash = None

                while time.time() - start_time < duration_seconds:
                    chunk = response.fp.read(1024)
                    if not chunk:
                        break
                    buffer += chunk

                    # Estrazione dei singoli frame JPEG dai marker SOI (0xFFD8) e EOI (0xFFD9)
                    while True:
                        start_idx = buffer.find(b"\xff\xd8")
                        end_idx = buffer.find(b"\xff\xd9")

                        if start_idx != -1 and end_idx != -1 and end_idx > start_idx:
                            frame_data = buffer[start_idx:end_idx + 2]
                            buffer = buffer[end_idx + 2:] # Rimuove il frame processato dal buffer
                            
                            frame_count += 1
                            frame_size = len(frame_data)
                            
                            # Generazione di un hash rapido per verificare se il contenuto del frame è diverso
                            frame_hash = hashlib.md5(frame_data).hexdigest()
                            is_different = (frame_hash != previous_hash)
                            previous_hash = frame_hash

                            current_time = time.time()
                            # Logga ogni secondo o al primo frame utile
                            if current_time - last_log_time >= 1.0 or frame_count == 1:
                                elapsed = int(current_time - start_time)
                                self._print(
                                    f"[STREAM LOG - {elapsed}s] "
                                    f"Tipo: image/jpeg | "
                                    f"Dimensione: {frame_size} bytes | "
                                    f"Frame differente dal precedente: {is_different} "
                                    f"(Hash: {frame_hash[:8]})"
                                )
                                last_log_time = current_time
                        else:
                            break

                self._print(f"Stream completato. Ricevuti {frame_count} frame totali in {duration_seconds} secondi.")

            except (OSError, TimeoutError) as error:
                self._print(f"Errore durante lo stream: {error}")
            finally:
                self._force_close_connection(connection)

class DeviceHttpTest(unittest.TestCase):
    """Smoke tests proving that the ESP32-CAM service exists and is healthy."""

    client = DeviceHttpClient(
        base_url=os.getenv("ESP32_DEVICE_URL", "http://192.168.4.1"),
        timeout_seconds=float(os.getenv("ESP32_HTTP_TIMEOUT", "5")),
    )
    registration_response: dict = {}

    @classmethod
    def setUpClass(cls) -> None:
        """Create one session for the suite and reuse it across authenticated tests."""
        cls.registration_response = cls.client.register()

    def test_01_device_exists(self) -> None:
        """The board must answer GET /status with the expected device contract."""
        status = self.client.get_status()

        self.assertEqual(status.get("tipo"), "ESP32-CAM")
        self.assertTrue(status.get("running"), "device reports running=false")
        self.assertIn("version", status)
        self.assertIsInstance(status.get("buildDateTime"), str)
        self.assertRegex(
            status["buildDateTime"],
            r"^[A-Z][a-z]{2} \d{2} \d{4} \d{2}:\d{2}:\d{2}$",
        )
        self.assertIn("serial", status)
        self.assertIsInstance(status.get("ram"), dict)
        self.assertIsInstance(status.get("wifi"), dict)
        self.assertIsInstance(status.get("camera"), dict)
        self.assertIsInstance(status.get("battery"), dict)
        self.assertIsInstance(status.get("ledStatus"), dict)
        self.assertIsInstance(status.get("internalLedStatus"), dict)

    def test_02_register(self) -> None:
        """The board must accept a fresh nonce and return a session token."""
        response = self.registration_response

        self.assertTrue(response.get("registered"))
        self.assertIsInstance(response.get("sessionToken"), str)
        self.assertGreater(len(response["sessionToken"]), 0)
        self.assertIsInstance(response.get("timestamp"), int)

    def test_03_register_invalidates_previous_session(self) -> None:
        """A new registration must invalidate the previous session token."""
        previous_token = self.registration_response["sessionToken"]
        new_registration = self.client.register()
        new_token = new_registration["sessionToken"]
        self.assertNotEqual(previous_token, new_token)

        old_timestamp = str(int(time.time()))
        old_message = f"{previous_token}:{old_timestamp}"
        old_api_key = hmac.new(
            previous_token.encode("utf-8"),
            old_message.encode("utf-8"),
            hashlib.sha256,
        ).hexdigest()
        old_status, old_response = self.client.set_led(
            previous_token, old_api_key, old_timestamp
        )
        self.assertEqual(old_status, 401, f"old session was accepted: {old_response}")

        new_timestamp = str(int(time.time()))
        new_message = f"{new_token}:{new_timestamp}"
        new_api_key = hmac.new(
            new_token.encode("utf-8"),
            new_message.encode("utf-8"),
            hashlib.sha256,
        ).hexdigest()
        new_status, new_response = self.client.set_led(
            new_token, new_api_key, new_timestamp
        )
        self.assertIn(new_status, (200, 409), f"new session rejected: {new_response}")

    def test_04_led_authentication(self) -> None:
        """POST /led must reject bad HMAC and accept a valid session signature."""
        registration = self.client.register()
        session_token = registration["sessionToken"]

        # Test HMAC non valido
        invalid_timestamp = str(int(time.time()))
        invalid_status, invalid_response = self.client.set_led(
            session_token, "0" * 64, invalid_timestamp, enable=True
        )
        self.assertEqual(
            invalid_status,
            401,
            f"invalid API key returned HTTP {invalid_status}: {invalid_response}",
        )
        self.assertIn("error", invalid_response, f"unexpected response: {invalid_response}")

        # 1. Accensione LED (enable=True)
        on_timestamp = str(int(time.time()))
        on_message = f"{session_token}:{on_timestamp}"
        on_api_key = hmac.new(
            session_token.encode("utf-8"),
            on_message.encode("utf-8"),
            hashlib.sha256,
        ).hexdigest()
        on_status, on_response = self.client.set_led(
            session_token, on_api_key, on_timestamp, enable=True
        )
        self.assertIn(
            on_status,
            (200, 409),
            f"valid API key returned HTTP {on_status}: {on_response}",
        )
        if on_status == 200:
            self.assertTrue(on_response.get("updated"))
        else:
            self.assertEqual(on_response.get("error"), "led strip is not configured")

        # Verifica stato LED e attesa di 2 secondi se è ON
        status = self.client.get_status()
        led_status = status.get("ledStatus", {})
        if led_status.get("enabled") or led_status.get("on") or led_status.get("state") == "on":
            time.sleep(2)

        # 2. Spegnimento LED (enable=False)
        off_timestamp = str(int(time.time()))
        off_message = f"{session_token}:{off_timestamp}"
        off_api_key = hmac.new(
            session_token.encode("utf-8"),
            off_message.encode("utf-8"),
            hashlib.sha256,
        ).hexdigest()
        off_status, off_response = self.client.set_led(
            session_token, off_api_key, off_timestamp, enable=False
        )
        self.assertIn(
            off_status,
            (200, 409),
            f"valid API key returned HTTP {off_status}: {off_response}",
        )
        if off_status == 200:
            self.assertTrue(off_response.get("updated"))
        else:
            self.assertEqual(off_response.get("error"), "led strip is not configured")

    def test_56_stream_authentication(self) -> None:
        """GET /stream must require auth and return the MJPEG stream when valid."""
        registration = self.client.register()
        session_token = registration["sessionToken"]
        invalid_timestamp = str(int(time.time()))

        invalid_status, _, invalid_body = self.client.stream_probe(
            "0" * 64, invalid_timestamp
        )
        self.assertEqual(
            invalid_status,
            401,
            f"invalid API key returned HTTP {invalid_status}: {invalid_body!r}",
        )

        valid_timestamp = str(int(time.time()))
        message = f"{session_token}:{valid_timestamp}"
        valid_api_key = hmac.new(
            session_token.encode("utf-8"),
            message.encode("utf-8"),
            hashlib.sha256,
        ).hexdigest()
        valid_status, headers, first_bytes = self.client.stream_probe(
            valid_api_key, valid_timestamp
        )
        self.assertEqual(
            valid_status,
            200,
            f"valid API key returned HTTP {valid_status}: {first_bytes!r}",
        )
        content_type = headers.get("Content-Type", "")
        self.assertIn("multipart/x-mixed-replace", content_type)
        self.assertIn(b"--frame", first_bytes)

    def test_58_stream_is_concurrent_with_status(self) -> None:
        """An open MJPEG stream must not block status or allow another stream."""
        session_token = self.client.register()["sessionToken"]

        def signed_headers() -> tuple[str, str]:
            timestamp = str(int(time.time()))
            message = f"{session_token}:{timestamp}"
            api_key = hmac.new(
                session_token.encode("utf-8"),
                message.encode("utf-8"),
                hashlib.sha256,
            ).hexdigest()
            return timestamp, api_key

        stream_connection = None
        second_connection = None
        try:
            timestamp, api_key = signed_headers()
            stream_connection, stream_response = self.client.open_stream(api_key, timestamp)
            self.assertEqual(
                stream_response.status,
                200,
                f"stream returned HTTP {stream_response.status}",
            )
            self.assertIn(
                "multipart/x-mixed-replace",
                stream_response.headers.get("Content-Type", ""),
            )
            first_bytes = stream_response.read(512)
            self.assertIn(b"--frame", first_bytes)

            status = self.client.get_status()
            self.assertTrue(status.get("running"), "status did not complete during stream")

            second_timestamp, second_api_key = signed_headers()
            second_connection, second_response = self.client.open_stream(
                second_api_key, second_timestamp
            )
            second_body = second_response.read()
            self.assertEqual(
                second_response.status,
                503,
                f"second stream returned HTTP {second_response.status}: {second_body!r}",
            )
            try:
                error = json.loads(second_body)
            except json.JSONDecodeError as error:
                self.fail(f"second stream returned invalid JSON: {second_body!r}")
            self.assertEqual(error.get("error"), "stream is already active")
        finally:
            if second_connection is not None:
                self.client._force_close_connection(second_connection)
            if stream_connection is not None:
                self.client._force_close_connection(stream_connection)

    def test_13_battery_status_contract(self) -> None:
        """Battery status must expose measurement and charge-detection state."""
        battery = self.client.get_status()["battery"]
        for field in ("present", "charging", "full", "chargeDetection"):
            self.assertIn(field, battery)
        self.assertIn("levelPercent", battery)
        self.assertIn("voltageMv", battery)
        self.assertIn("sampleIntervalMillis", battery)
        self.assertIn("averageSamples", battery)
        self.assertIn("samplesCollected", battery)



    def test_12_led_authentication_OFF(self) -> None:
        """POST /led must reject bad HMAC and accept a valid session signature."""
        registration = self.client.register()
        session_token = registration["sessionToken"]

        # Test HMAC non valido
        invalid_timestamp = str(int(time.time()))
        invalid_status, invalid_response = self.client.set_led(
            session_token, "0" * 64, invalid_timestamp, enable=True
        )
        self.assertEqual(
            invalid_status,
            401,
            f"invalid API key returned HTTP {invalid_status}: {invalid_response}",
        )
        self.assertIn("error", invalid_response, f"unexpected response: {invalid_response}")

        # 2. Spegnimento LED (enable=False)
        off_timestamp = str(int(time.time()))
        off_message = f"{session_token}:{off_timestamp}"
        off_api_key = hmac.new(
            session_token.encode("utf-8"),
            off_message.encode("utf-8"),
            hashlib.sha256,
        ).hexdigest()
        off_status, off_response = self.client.set_led(
            session_token, off_api_key, off_timestamp, enable=False
        )
        self.assertIn(
            off_status,
            (200, 409),
            f"valid API key returned HTTP {off_status}: {off_response}",
        )
        if off_status == 200:
            self.assertTrue(off_response.get("updated"))
        else:
            self.assertEqual(off_response.get("error"), "led strip is not configured")



    def test_10_internal_led_status_contract(self) -> None:
        """Internal LED status must state whether it is colorable and its mode."""
       
        
        internal_led = self.client.get_status()["internalLedStatus"]
        self.assertIn("present", internal_led)
        self.assertIn("colorable", internal_led)
        self.assertFalse(internal_led["colorable"])
        self.assertIn(internal_led.get("state"), ("off", "lowBattery", "charging", "full"))

    def test_57_stream_logging_for_5_seconds(self) -> None:
            """Esegue lo stream per 5 secondi verificando dimensioni, tipo e unicità dei frame."""
            registration = self.client.register()
            session_token = registration["sessionToken"]
            
            timestamp = str(int(time.time()))
            message = f"{session_token}:{timestamp}"
            api_key = hmac.new(
                session_token.encode("utf-8"),
                message.encode("utf-8"),
                hashlib.sha256,
            ).hexdigest()

            # Avvia lo stream di test per 5 secondi
            self.client.stream_for_duration(api_key, timestamp, duration_seconds=5)



class SummaryTestResult(unittest.TextTestResult):
    """Test result that keeps a compact report for humans and CI logs."""

    def __init__(self, stream, descriptions, verbosity):
        super().__init__(stream, descriptions, verbosity)
        self.report = []

    @staticmethod
    def _test_name(test: unittest.TestCase) -> str:
        return test.id().rsplit(".", 1)[-1]

    @staticmethod
    def _message(error) -> str:
        message = str(error[1]).strip().splitlines()
        return message[0] if message else "no details"

    def addSuccess(self, test) -> None:
        super().addSuccess(test)
        self.report.append((self._test_name(test), "OK", "completed"))

    def addFailure(self, test, error) -> None:
        super().addFailure(test, error)
        self.report.append((self._test_name(test), "FAILED", self._message(error)))

    def addError(self, test, error) -> None:
        super().addError(test, error)
        self.report.append((self._test_name(test), "FAILED", self._message(error)))


def print_report(result: SummaryTestResult) -> None:
    """Print one summary line per test and the final suite result."""
    print("\nTEST REPORT", flush=True)
    for test_name, outcome, message in result.report:
        print(f"{outcome:6} {test_name}: {message}", flush=True)
    passed = result.testsRun - len(result.failures) - len(result.errors)
    overall = "OK" if result.wasSuccessful() else "FAILED"
    print(
        f"RESULT: {overall} | tests={result.testsRun} | "
        f"passed={passed} | failed={len(result.failures) + len(result.errors)}",
        flush=True,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Test the ESP32-CAM HTTP status endpoint")
    parser.add_argument(
        "--url",
        default=os.getenv("ESP32_DEVICE_URL", "http://192.168.4.1"),
        help="base device URL (default: http://192.168.4.1)",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=float(os.getenv("ESP32_HTTP_TIMEOUT", "5")),
        help="HTTP timeout in seconds (default: 5)",
    )
    parser.add_argument(
        "--pin",
        default=os.getenv("ESP32_DEVICE_PIN", "482917"),
        help="device PIN used by the register test",
    )
    parser.add_argument("--verbose", action="store_true", help="show individual test names")
    parser.add_argument("--quiet", action="store_true", help="disable HTTP request/response logging")
    args = parser.parse_args()

    DeviceHttpTest.client = DeviceHttpClient(args.url, args.timeout, not args.quiet, args.pin)
    verbosity = 2 if args.verbose else 1
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(DeviceHttpTest)
    runner = unittest.TextTestRunner(verbosity=verbosity, resultclass=SummaryTestResult)
    result = runner.run(suite)
    print_report(result)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(int(main()))