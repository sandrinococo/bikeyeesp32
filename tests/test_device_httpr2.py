"""HTTP smoke tests for the ESP32-CAM device."""

from __future__ import annotations

import argparse
import hashlib
import hmac
import http.client
import json
import os
import secrets
import subprocess
import time
import unittest
from pathlib import Path
from urllib.parse import urlsplit


class DeviceHttpClient:
    """Own one HTTP connection for the lifetime of a single test."""

    def __init__(self, base_url: str, timeout_seconds: float) -> None:
        parsed_url = urlsplit(base_url)
        if parsed_url.scheme != "http" or parsed_url.hostname is None:
            raise ValueError(f"unsupported device URL: {base_url!r}")

        self.host = parsed_url.hostname
        self.port = parsed_url.port or 80
        self.base_path = parsed_url.path.rstrip("/")
        self.connection = http.client.HTTPConnection(
            self.host,
            self.port,
            timeout=timeout_seconds,
        )

    def connect(self) -> None:
        self.connection.connect()

    def close(self) -> None:
        self.connection.close()

    def _request_headers(self, headers: dict[str, str]) -> dict[str, str]:
        return {
            "Host": f"{self.host}:{self.port}",
            "Accept-Encoding": "identity",
            **headers,
        }

    @staticmethod
    def _log_request(
        method: str,
        uri: str,
        headers: dict[str, str],
        body: bytes = b"",
    ) -> None:
        print(f"\nHTTP REQUEST {method} {uri}", flush=True)
        print("Request headers:", flush=True)
        for name, value in headers.items():
            print(f"  {name}: {value}", flush=True)
        print("Request body:", flush=True)
        print(body.decode("utf-8", errors="replace") if body else "<empty>", flush=True)

    @staticmethod
    def _log_response(
        status: int,
        headers: http.client.HTTPMessage,
        body: bytes,
    ) -> None:
        print(f"HTTP RESPONSE {status}", flush=True)
        print("Response headers:", flush=True)
        for name, value in headers.items():
            print(f"  {name}: {value}", flush=True)
        print("Response body:", flush=True)
        print(body.decode("utf-8", errors="replace") if body else "<empty>", flush=True)

    def get_status(self) -> dict[str, object]:
        path = f"{self.base_path}/status"
        request_headers = self._request_headers({"Accept": "application/json"})
        uri = f"http://{self.host}:{self.port}{path}"
        self._log_request("GET", uri, request_headers)
        self.connection.request(
            "GET",
            path,
            headers=request_headers,
        )
        response = self.connection.getresponse()
        try:
            body = response.read()
            self._log_response(response.status, response.headers, body)
            if response.status != 200:
                raise AssertionError(
                    f"GET /status returned HTTP {response.status}: {body!r}"
                )
        finally:
            response.close()

        try:
            payload = json.loads(body)
        except json.JSONDecodeError as error:
            raise AssertionError(f"GET /status returned invalid JSON: {body!r}") from error
        if not isinstance(payload, dict):
            raise AssertionError("/status response must be a JSON object")
        return payload

    def post_json(
        self,
        path: str,
        payload: dict[str, object],
        headers: dict[str, str] | None = None,
    ) -> tuple[int, dict[str, object]]:
        body = json.dumps(payload).encode("utf-8")
        request_headers = self._request_headers({
            "Accept": "application/json",
            "Content-Type": "application/json",
        })
        if headers is not None:
            request_headers.update(headers)
        uri = f"http://{self.host}:{self.port}{self.base_path}{path}"
        self._log_request("POST", uri, request_headers, body)
        try:
            self.connection.request(
                "POST",
                f"{self.base_path}{path}",
                body=body,
                headers=request_headers,
            )
            response = self.connection.getresponse()
        except TimeoutError as error:
            self.connection.close()
            raise AssertionError(
                f"POST {path} timed out after waiting {self.timeout_seconds:g}s "
                "for the device response"
            ) from error
        try:
            status_code = response.status
            response_body = response.read()
            self._log_response(status_code, response.headers, response_body)
        finally:
            response.close()

        try:
            response_payload = json.loads(response_body)
        except json.JSONDecodeError as error:
            raise AssertionError(
                f"POST {path} returned invalid JSON: {response_body!r}"
            ) from error
        if not isinstance(response_payload, dict):
            raise AssertionError(f"POST {path} response must be a JSON object")
        return status_code, response_payload

    def get_stream(self, timestamp: str, api_key: str) -> http.client.HTTPResponse:
        path = f"{self.base_path}/stream"
        request_headers = self._request_headers({
            "Accept": "multipart/x-mixed-replace",
            "X-TIMESTAMP": timestamp,
            "X-API-KEY": api_key,
        })
        uri = f"http://{self.host}:{self.port}{path}"
        self._log_request("GET", uri, request_headers)
        self.connection.request("GET", path, headers=request_headers)
        response = self.connection.getresponse()
        if response.status == 200:
            response_body = b"<MJPEG stream; frames are displayed live>"
        else:
            response_body = response.read()
        self._log_response(response.status, response.headers, response_body)
        return response


class DeviceHttpTest(unittest.TestCase):
    """Tests the device HTTP API using a separate connection for each test."""

    base_url = os.getenv("ESP32_DEVICE_URL", "http://192.168.4.1")
    timeout_seconds = float(os.getenv("ESP32_HTTP_TIMEOUT", "5"))
    device_pin = os.getenv("ESP32_DEVICE_PIN", "482917")

    @staticmethod
    def _run_wifi_action(action: str) -> None:
        script = Path(__file__).resolve().parents[1] / "connetti_esp32.bat"
        subprocess.run(
            ["cmd.exe", "/c", str(script), action],
            cwd=script.parent,
            check=True,
        )

    def setUp(self) -> None:
        print(f"\n===== START {self.id()} =====", flush=True)
        self.client = DeviceHttpClient(self.base_url, self.timeout_seconds)
        self.addCleanup(self.client.close)
        self.client.connect()

    def tearDown(self) -> None:
        print(f"===== END {self.id()} =====", flush=True)

    def _wait_for_camera_capture(self, phase: str) -> dict[str, object]:
        deadline = time.monotonic() + self.timeout_seconds
        last_camera: dict[str, object] = {}
        while True:
            status = self.client.get_status()
            camera = status.get("camera")
            self.assertIsInstance(camera, dict, "/status camera must be a JSON object")
            last_camera = camera
            if (
                camera.get("ready") is True
                and camera.get("reconfiguring") is False
                and camera.get("hasCapturedFrame") is True
            ):
                return camera
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                self.fail(
                    f"camera did not capture a frame {phase} within "
                    f"{self.timeout_seconds:g}s; last camera status: {last_camera}"
                )
            time.sleep(min(0.1, remaining))

    def test_01_device_exists(self) -> None:
        """GET /status must return valid device information."""
        status = self.client.get_status()

        self.assertIs(status.get("running"), True, "/status reports device not running")
        version = status.get("version")
        build_date_time = status.get("buildDateTime")
        self.assertIsInstance(version, str)
        self.assertTrue(version)
        self.assertIsInstance(build_date_time, str)
        self.assertTrue(build_date_time)

        print(f"Versione firmware: {version}", flush=True)
        print(f"Data build: {build_date_time}", flush=True)

    def test_02_register_valid_and_invalid_proof(self) -> None:
        """POST /register accepts a correct proof and rejects an incorrect one."""
        nonce = secrets.token_hex(16)
        timestamp = int(time.time())
        correct_proof = hmac.new(
            self.device_pin.encode("utf-8"),
            nonce.encode("utf-8"),
            hashlib.sha256,
        ).hexdigest()

        valid_status, valid_response = self.client.post_json(
            "/register",
            {
                "pin": self.device_pin,
                "nonce": nonce,
                "proof": correct_proof,
                "timestamp": timestamp,
            },
        )
        self.assertEqual(
            valid_status,
            200,
            f"valid registration returned HTTP {valid_status}: {valid_response}",
        )
        self.assertIs(valid_response.get("registered"), True)
        session_token = valid_response.get("sessionToken")
        self.assertIsInstance(session_token, str)
        self.assertTrue(session_token)
        self.assertEqual(valid_response.get("timestamp"), timestamp)

        invalid_proof = ("1" if correct_proof[0] == "0" else "0") + correct_proof[1:]
        invalid_status, invalid_response = self.client.post_json(
            "/register",
            {
                "pin": self.device_pin,
                "nonce": secrets.token_hex(16),
                "proof": invalid_proof,
                "timestamp": int(time.time()),
            },
        )
        self.assertEqual(
            invalid_status,
            401,
            f"invalid registration proof returned HTTP {invalid_status}: {invalid_response}",
        )
        self.assertEqual(invalid_response.get("error"), "invalid registration proof")

    def test_03_led_start_stop_blink_stop(self) -> None:
        """Register before each authenticated LED start, stop, blink, and stop request."""
        actions = (
            ("start", True, "solid"),
            ("stop", False, "off"),
            ("blink", True, "blink"),
            ("stop", False, "off"),
        )

        for action, enabled, mode in actions:
            nonce = secrets.token_hex(16)
            register_timestamp = int(time.time())
            proof = hmac.new(
                self.device_pin.encode("utf-8"),
                nonce.encode("utf-8"),
                hashlib.sha256,
            ).hexdigest()
            register_status, register_response = self.client.post_json(
                "/register",
                {
                    "pin": self.device_pin,
                    "nonce": nonce,
                    "proof": proof,
                    "timestamp": register_timestamp,
                },
            )
            self.assertEqual(
                register_status,
                200,
                f"registration before LED {action} returned HTTP "
                f"{register_status}: {register_response}",
            )
            self.assertIs(register_response.get("registered"), True)
            session_token = register_response.get("sessionToken")
            self.assertIsInstance(session_token, str)
            self.assertTrue(session_token)

            request_timestamp = str(int(time.time()))
            api_key = hmac.new(
                session_token.encode("utf-8"),
                f"{session_token}:{request_timestamp}".encode("utf-8"),
                hashlib.sha256,
            ).hexdigest()
            led_status, led_response = self.client.post_json(
                "/led",
                {
                    "enabled": enabled,
                    "count": 10,
                    "mode": mode,
                    "brightness": 80,
                    "intervalMillis": 500,
                    "red": 255,
                    "green": 40,
                    "blue": 0,
                },
                headers={
                    "X-TIMESTAMP": request_timestamp,
                    "X-API-KEY": api_key,
                },
            )
            self.assertEqual(
                led_status,
                200,
                f"LED {action} returned HTTP {led_status}: {led_response}",
            )
            self.assertIs(led_response.get("updated"), True)
            self.assertIs(led_response.get("enabled"), enabled)
            self.assertEqual(led_response.get("mode"), mode)

    def test_04_camera_accepts_valid_and_rejects_invalid_parameters(self) -> None:
        """Check valid and invalid camera settings, then restore the initial settings."""
        camera = self.client.get_status().get("camera")
        self.assertIsInstance(camera, dict, "/status camera must be a JSON object")
        defaults = {
            "resolution": camera.get("resolution"),
            "framebuffers": camera.get("framebuffers"),
            "jpegquality": camera.get("jpegquality"),
        }
        self.assertIn(defaults["resolution"], ("QQVGA", "QVGA", "VGA", "SVGA", "XGA", "SXGA", "UXGA"))
        self.assertIsInstance(defaults["framebuffers"], int)
        self.assertIn(defaults["framebuffers"], range(1, 5))
        self.assertIsInstance(defaults["jpegquality"], int)
        self.assertIn(defaults["jpegquality"], range(12, 21))

        valid_quality = 19 if defaults["jpegquality"] != 19 else 20
        valid_settings = {
            **defaults,
            "jpegquality": valid_quality,
        }

        def configure_camera(settings: dict[str, object]) -> tuple[int, dict[str, object]]:
            nonce = secrets.token_hex(16)
            register_timestamp = int(time.time())
            proof = hmac.new(
                self.device_pin.encode("utf-8"),
                nonce.encode("utf-8"),
                hashlib.sha256,
            ).hexdigest()
            register_status, register_response = self.client.post_json(
                "/register",
                {
                    "pin": self.device_pin,
                    "nonce": nonce,
                    "proof": proof,
                    "timestamp": register_timestamp,
                },
            )
            self.assertEqual(
                register_status,
                200,
                f"registration before camera request returned HTTP "
                f"{register_status}: {register_response}",
            )
            session_token = register_response.get("sessionToken")
            self.assertIsInstance(session_token, str)
            self.assertTrue(session_token)

            request_timestamp = str(int(time.time()))
            api_key = hmac.new(
                session_token.encode("utf-8"),
                f"{session_token}:{request_timestamp}".encode("utf-8"),
                hashlib.sha256,
            ).hexdigest()
            return self.client.post_json(
                "/camera",
                settings,
                headers={
                    "X-TIMESTAMP": request_timestamp,
                    "X-API-KEY": api_key,
                },
            )

        try:
            valid_status, valid_response = configure_camera(valid_settings)
            self.assertEqual(
                valid_status,
                200,
                f"valid camera settings returned HTTP "
                f"{valid_status}: {valid_response}",
            )
            self.assertIs(valid_response.get("configured"), True)
            self.assertEqual(valid_response.get("camera"), valid_settings)
            self._wait_for_camera_capture("after applying valid settings")

            invalid_settings = {
                **valid_settings,
                "jpegquality": 21,
            }
            invalid_status, invalid_response = configure_camera(invalid_settings)
            self.assertEqual(
                invalid_status,
                400,
                f"invalid camera settings returned HTTP "
                f"{invalid_status}: {invalid_response}",
            )
            self.assertEqual(
                invalid_response.get("error"),
                "framebuffers must be 1..4 and jpegquality 12..20",
            )
        finally:
            restore_status, restore_response = configure_camera(defaults)
            self.assertEqual(
                restore_status,
                200,
                f"failed to restore initial camera settings: {restore_response}",
            )
            self.assertEqual(restore_response.get("camera"), defaults)
            self._wait_for_camera_capture("after restoring initial settings")

    def test_05_stream_displays_mjpeg_frames_for_at_least_5_seconds(self) -> None:
        """Register and display decoded MJPEG frames in an OpenCV window for five seconds."""
        try:
            import cv2
            import numpy as np
        except ImportError as error:
            self.fail(
                "test 5 requires OpenCV and NumPy; install them with "
                "'python -m pip install -r requirements-test.txt': "
                f"{error}"
            )

        nonce = secrets.token_hex(16)
        register_timestamp = int(time.time())
        proof = hmac.new(
            self.device_pin.encode("utf-8"),
            nonce.encode("utf-8"),
            hashlib.sha256,
        ).hexdigest()
        register_status, register_response = self.client.post_json(
            "/register",
            {
                "pin": self.device_pin,
                "nonce": nonce,
                "proof": proof,
                "timestamp": register_timestamp,
            },
        )
        self.assertEqual(
            register_status,
            200,
            f"registration before stream returned HTTP "
            f"{register_status}: {register_response}",
        )
        self.assertIs(register_response.get("registered"), True)
        session_token = register_response.get("sessionToken")
        self.assertIsInstance(session_token, str)
        self.assertTrue(session_token)

        minimum_duration = 5.0
        window_name = "ESP32-CAM MJPEG stream"
        response: http.client.HTTPResponse | None = None
        frame_count = 0

        try:
            self._wait_for_camera_capture("before starting the stream")
            request_timestamp = str(int(time.time()))
            api_key = hmac.new(
                session_token.encode("utf-8"),
                f"{session_token}:{request_timestamp}".encode("utf-8"),
                hashlib.sha256,
            ).hexdigest()
            response = self.client.get_stream(request_timestamp, api_key)
            self.assertEqual(
                response.status,
                200,
                f"GET /stream returned HTTP {response.status}",
            )
            content_type = response.headers.get("Content-Type", "")
            self.assertIn(
                "multipart/x-mixed-replace",
                content_type.lower(),
                f"unexpected stream Content-Type: {content_type!r}",
            )
            started_at = time.monotonic()
            print(
                f"Stream MJPEG attivo per almeno {minimum_duration:.0f} secondi; "
                "premere Ctrl+C per interrompere.",
                flush=True,
            )

            frame_buffer = bytearray()
            deadline = started_at + minimum_duration
            while time.monotonic() < deadline:
                try:
                    chunk = response.read1(4096)
                except TimeoutError as error:
                    elapsed = time.monotonic() - started_at
                    self.fail(
                        f"MJPEG stream stalled while reading: no response data "
                        f"within {self.timeout_seconds:g}s; elapsed={elapsed:.2f}s, "
                        f"decoded_frames={frame_count}: {error}"
                    )
                if not chunk:
                    break
                frame_buffer.extend(chunk)

                while True:
                    start_index = frame_buffer.find(b"\xff\xd8")
                    if start_index < 0:
                        if len(frame_buffer) > 1:
                            del frame_buffer[:-1]
                        break
                    if start_index:
                        del frame_buffer[:start_index]
                    end_index = frame_buffer.find(b"\xff\xd9", 2)
                    if end_index < 0:
                        break

                    jpeg_frame = bytes(frame_buffer[:end_index + 2])
                    del frame_buffer[:end_index + 2]
                    image = cv2.imdecode(
                        np.frombuffer(jpeg_frame, dtype=np.uint8),
                        cv2.IMREAD_COLOR,
                    )
                    self.assertIsNotNone(image, "received frame is not a valid JPEG")
                    cv2.imshow(window_name, image)
                    cv2.waitKey(1)
                    frame_count += 1
                    elapsed = time.monotonic() - started_at
                    height, width = image.shape[:2]
                    print(
                        f"Frame {frame_count:04d} | {elapsed:.2f}s | "
                        f"{width}x{height}",
                        flush=True,
                    )

            elapsed = time.monotonic() - started_at
            self.assertGreaterEqual(
                elapsed,
                minimum_duration,
                f"stream ended after {elapsed:.2f}s; expected at least "
                f"{minimum_duration:.0f}s",
            )
            self.assertGreater(frame_count, 0, "stream delivered no valid JPEG frames")
            print(
                f"Stream terminato: {frame_count} frame in {elapsed:.2f}s.",
                flush=True,
            )
        finally:
            try:
                if response is not None:
                    response.close()
            finally:
                self.client.close()
                cv2.destroyAllWindows()


def setUpModule() -> None:
    """Connect to the ESP32 Wi-Fi once before running this module's test suite."""
    print("\n===== CONNECT ESP32 WI-FI =====", flush=True)
    DeviceHttpTest._run_wifi_action("ON")


def tearDownModule() -> None:
    """Disconnect from the ESP32 Wi-Fi once after all module tests finish."""
    print("\n===== DISCONNECT ESP32 WI-FI =====", flush=True)
    DeviceHttpTest._run_wifi_action("OFF")


class SummaryTestResult(unittest.TextTestResult):
    """Keep test outcomes for the final console summary."""

    def __init__(self, stream, descriptions, verbosity):
        super().__init__(stream, descriptions, verbosity)
        self.outcomes: list[tuple[str, str, str]] = []

    @staticmethod
    def _name(test: unittest.case.TestCase) -> str:
        return test.id().rsplit(".", 1)[-1]

    def addSuccess(self, test: unittest.case.TestCase) -> None:
        super().addSuccess(test)
        self.outcomes.append((self._name(test), "PASS", ""))

    def addFailure(self, test: unittest.case.TestCase, err) -> None:
        super().addFailure(test, err)
        self.outcomes.append((self._name(test), "FAIL", self._exc_info_to_string(err, test)))

    def addError(self, test: unittest.case.TestCase, err) -> None:
        super().addError(test, err)
        self.outcomes.append((self._name(test), "ERROR", self._exc_info_to_string(err, test)))

    def addSkip(self, test: unittest.case.TestCase, reason: str) -> None:
        super().addSkip(test, reason)
        self.outcomes.append((self._name(test), "SKIP", reason))


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
        help="device PIN used to create the valid registration proof",
    )
    parser.add_argument("--verbose", action="store_true", help="show individual test names")
    args = parser.parse_args()

    DeviceHttpTest.base_url = args.url
    DeviceHttpTest.timeout_seconds = args.timeout
    DeviceHttpTest.device_pin = args.pin
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(DeviceHttpTest)
    runner = unittest.TextTestRunner(
        verbosity=2 if args.verbose else 1,
        resultclass=SummaryTestResult,
    )
    result = runner.run(suite)
    print("\n===== TEST SUMMARY =====", flush=True)
    for test_name, outcome, details in result.outcomes:
        print(f"{outcome:5} {test_name}" + (f": {details}" if details else ""), flush=True)
    passed = sum(outcome == "PASS" for _, outcome, _ in result.outcomes)
    failed = sum(outcome in ("FAIL", "ERROR") for _, outcome, _ in result.outcomes)
    skipped = sum(outcome == "SKIP" for _, outcome, _ in result.outcomes)
    print(
        f"RESULT: {'PASS' if result.wasSuccessful() else 'FAIL'} | "
        f"tests={result.testsRun} | passed={passed} | failed={failed} | skipped={skipped}",
        flush=True,
    )
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())