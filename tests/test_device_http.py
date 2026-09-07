"""HTTP smoke tests for the ESP32-CAM device service.

Run directly from VS Code or a terminal:
    python tests/test_device_http.py
    python tests/test_device_http.py --url http://192.168.4.1 --verbose
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
        self, session_token: str, api_key: str, timestamp: str | None = None
    ) -> tuple[int, dict]:
        """Call POST /led and return its HTTP status and JSON response."""
        timestamp = timestamp or str(int(time.time()))
        payload = {"mode": "off"}
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
            connection.close()

        self._log_response(status_code, response_headers, response_body)

        try:
            response = json.loads(response_body)
        except json.JSONDecodeError as error:
            raise AssertionError(f"POST {url} returned invalid JSON: {response_body!r}") from error
        if not isinstance(response, dict):
            raise AssertionError("/led response must be a JSON object")
        return status_code, response

    def stream_probe(self, api_key: str, timestamp: str) -> tuple[int, object, bytes]:
        """Open /stream, read its headers and a small first-frame probe."""
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
        try:
            path = parsed_url.path or "/"
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
            status_code = response.status
            response_headers = response.headers
            response_body = response.read(512) if status_code == 200 else response.read()
        except (OSError, TimeoutError) as error:
            raise AssertionError(f"device is unreachable at {url}: {error}") from error
        finally:
            connection.close()

        body_text = response_body.decode("utf-8", errors="replace")
        self._log_response(status_code, response_headers, body_text)
        return status_code, response_headers, response_body


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

    def test_device_exists(self) -> None:
        """The board must answer GET /status with the expected device contract."""
        status = self.client.get_status()

        self.assertEqual(status.get("tipo"), "ESP32-CAM")
        self.assertTrue(status.get("running"), "device reports running=false")
        self.assertIn("version", status)
        self.assertIn("serial", status)
        self.assertIsInstance(status.get("ram"), dict)
        self.assertIsInstance(status.get("wifi"), dict)
        self.assertIsInstance(status.get("camera"), dict)
        self.assertIsInstance(status.get("battery"), dict)
        self.assertIsInstance(status.get("ledStatus"), dict)
        self.assertIsInstance(status.get("internalLedStatus"), dict)

    def test_register(self) -> None:
        """The board must accept a fresh nonce and return a session token."""
        response = self.registration_response

        self.assertTrue(response.get("registered"))
        self.assertIsInstance(response.get("sessionToken"), str)
        self.assertGreater(len(response["sessionToken"]), 0)
        self.assertIsInstance(response.get("timestamp"), int)

    def test_register_invalidates_previous_session(self) -> None:
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

    def test_led_authentication(self) -> None:
        """POST /led must reject bad HMAC and accept a valid session signature."""
        # Synchronize the device clock immediately before testing authenticated calls.
        registration = self.client.register()
        session_token = registration["sessionToken"]
        invalid_timestamp = str(int(time.time()))

        invalid_status, invalid_response = self.client.set_led(
            session_token, "0" * 64, invalid_timestamp
        )
        self.assertEqual(
            invalid_status,
            401,
            f"invalid API key returned HTTP {invalid_status}: {invalid_response}",
        )
        self.assertIn("error", invalid_response, f"unexpected response: {invalid_response}")

        valid_timestamp = str(int(time.time()))
        message = f"{session_token}:{valid_timestamp}"
        valid_api_key = hmac.new(
            session_token.encode("utf-8"),
            message.encode("utf-8"),
            hashlib.sha256,
        ).hexdigest()
        valid_status, valid_response = self.client.set_led(
            session_token, valid_api_key, valid_timestamp
        )
        self.assertIn(
            valid_status,
            (200, 409),
            f"valid API key returned HTTP {valid_status}: {valid_response}",
        )
        if valid_status == 200:
            self.assertTrue(valid_response.get("updated"))
        else:
            self.assertEqual(valid_response.get("error"), "led strip is not configured")

    def test_stream_authentication(self) -> None:
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

    def test_battery_status_contract(self) -> None:
        """Battery status must expose measurement and charge-detection state."""
        battery = self.client.get_status()["battery"]
        for field in ("present", "charging", "full", "chargeDetection"):
            self.assertIn(field, battery)
        self.assertIn("levelPercent", battery)
        self.assertIn("voltageMv", battery)
        self.assertIn("sampleIntervalMillis", battery)
        self.assertIn("averageSamples", battery)
        self.assertIn("samplesCollected", battery)

    def test_internal_led_status_contract(self) -> None:
        """Internal LED status must state whether it is colorable and its mode."""
        internal_led = self.client.get_status()["internalLedStatus"]
        self.assertIn("present", internal_led)
        self.assertIn("colorable", internal_led)
        self.assertFalse(internal_led["colorable"])
        self.assertIn(internal_led.get("state"), ("off", "lowBattery", "charging", "full"))


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
