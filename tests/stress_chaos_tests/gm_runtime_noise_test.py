#!/usr/bin/env python3
"""GM runtime noise regression test over real HTTP + MQTT.

Scenario:
1. Log in to GM HTTP API.
2. Optionally select a scenario.
3. Reset/start room runtime.
4. Wait until the room enters a waiting state.
5. Publish many non-critical heartbeat/status updates over MQTT.
6. Verify the room is still waiting.
7. Publish the target device event.
8. Verify the scenario progresses and does not stay stuck in the wait.

This is the real-broker counterpart to the deterministic gm_session runtime
chaos tests. It exercises the "noisy traffic first, target event later" path.
"""

from __future__ import annotations

import argparse
import http.cookiejar
import json
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
from dataclasses import dataclass
from typing import Any

try:
    import paho.mqtt.client as mqtt
except ImportError as exc:  # pragma: no cover
    raise SystemExit("Missing dependency: paho-mqtt (pip install paho-mqtt)") from exc


GREEN = "\033[92m"
RED = "\033[91m"
YELLOW = "\033[93m"
CYAN = "\033[96m"
RESET = "\033[0m"


def ok(msg: str) -> None:
    print(f"{GREEN}OK{RESET} {msg}")


def info(msg: str) -> None:
    print(f"{CYAN}..{RESET} {msg}")


def warn(msg: str) -> None:
    print(f"{YELLOW}!!{RESET} {msg}")


def fail(msg: str) -> None:
    print(f"{RED}FAIL{RESET} {msg}")


def now_ms() -> int:
    return int(time.time() * 1000)


def make_boot_id() -> str:
    return uuid.uuid4().hex[:12]


def topic(node_id: str, suffix: str) -> str:
    return f"cp/v1/dev/{node_id}/{suffix}"


def ensure_client(client_id: str) -> mqtt.Client:
    if hasattr(mqtt, "CallbackAPIVersion"):
        return mqtt.Client(
            callback_api_version=mqtt.CallbackAPIVersion.VERSION1,
            client_id=client_id,
            protocol=mqtt.MQTTv311,
        )
    return mqtt.Client(client_id=client_id, protocol=mqtt.MQTTv311)


class HttpError(RuntimeError):
    pass


class AssertionFailure(RuntimeError):
    pass


class HttpSession:
    def __init__(self, host: str, port: int, verbose: bool = False) -> None:
        self.base_url = f"http://{host}:{port}" if port != 80 else f"http://{host}"
        self.origin = self.base_url
        self.verbose = verbose
        self._cookie_jar = http.cookiejar.CookieJar()
        self._opener = urllib.request.build_opener(
            urllib.request.HTTPCookieProcessor(self._cookie_jar)
        )

    def _request(
        self,
        method: str,
        path: str,
        *,
        body: dict[str, Any] | None = None,
        query: dict[str, Any] | None = None,
    ) -> tuple[int, str, dict[str, Any] | None]:
        url = self.base_url + path
        if query:
            url += "?" + urllib.parse.urlencode(query)

        data = None
        headers = {
            "Accept": "application/json",
            "Origin": self.origin,
            "Referer": self.origin + "/gm",
        }
        if body is not None:
            data = json.dumps(body, separators=(",", ":")).encode("utf-8")
            headers["Content-Type"] = "application/json"

        request = urllib.request.Request(url, data=data, headers=headers, method=method.upper())
        try:
            with self._opener.open(request, timeout=10.0) as response:
                raw = response.read().decode("utf-8", errors="replace")
                status = response.getcode()
        except urllib.error.HTTPError as exc:
            raw = exc.read().decode("utf-8", errors="replace")
            status = exc.code
        except urllib.error.URLError as exc:
            raise HttpError(f"{method} {url} failed: {exc}") from exc

        payload = None
        if raw:
            try:
                payload = json.loads(raw)
            except json.JSONDecodeError:
                payload = None

        if self.verbose:
            info(f"HTTP {method.upper()} {path} -> {status}")
        return status, raw, payload

    def login(self, username: str, password: str) -> None:
        status, raw, payload = self._request(
            "POST",
            "/api/auth/login",
            body={"username": username, "password": password},
        )
        if status != 200:
            raise HttpError(f"login failed: HTTP {status}: {raw}")
        role = payload.get("role") if isinstance(payload, dict) else None
        ok(f"HTTP login ok as {role or 'unknown'}")

    def get_runtime(self, room_id: str) -> dict[str, Any]:
        status, raw, payload = self._request(
            "GET",
            "/api/gm/room/runtime",
            query={"room_id": room_id, "detail": "detail"},
        )
        if status != 200 or not isinstance(payload, dict):
            raise HttpError(f"runtime read failed: HTTP {status}: {raw}")
        return payload

    def select_scenario(self, room_id: str, scenario_id: str) -> None:
        status, raw, _payload = self._request(
            "POST",
            "/api/gm/room/scenario/select",
            body={"room_id": room_id, "scenario_id": scenario_id},
        )
        if status != 200:
            raise HttpError(f"scenario select failed: HTTP {status}: {raw}")
        ok(f"selected scenario {scenario_id}")

    def post_room_action(self, path: str, room_id: str) -> None:
        status, raw, _payload = self._request("POST", path, query={"room_id": room_id})
        if status != 200:
            raise HttpError(f"{path} failed: HTTP {status}: {raw}")
        ok(f"{path} accepted for room {room_id}")


@dataclass
class RuntimeSnapshot:
    scenario_runtime_state: str
    scenario_wait_type: str
    scenario_current_step_text: str
    scenario_wait_summary: str
    scenario_last_error: str
    scenario_done_steps: int
    scenario_total_steps: int
    running_scenario_id: str

    @classmethod
    def from_payload(cls, payload: dict[str, Any]) -> "RuntimeSnapshot":
        return cls(
            scenario_runtime_state=str(payload.get("scenario_runtime_state") or ""),
            scenario_wait_type=str(payload.get("scenario_wait_type") or ""),
            scenario_current_step_text=str(payload.get("scenario_current_step_text") or ""),
            scenario_wait_summary=str(payload.get("scenario_wait_summary") or ""),
            scenario_last_error=str(payload.get("scenario_last_error") or ""),
            scenario_done_steps=int(payload.get("scenario_done_steps") or 0),
            scenario_total_steps=int(payload.get("scenario_total_steps") or 0),
            running_scenario_id=str(payload.get("running_scenario_id") or ""),
        )


class ContractPublisher:
    def __init__(self, mqtt_host: str, mqtt_port: int, client_id: str, node_id: str, verbose: bool = False) -> None:
        self.node_id = node_id
        self.verbose = verbose
        self.boot_id = make_boot_id()
        self.status_seq = 0
        self._connected = threading.Event()
        self._client = ensure_client(client_id)
        self._client.on_connect = self._on_connect
        self._client.connect(mqtt_host, mqtt_port, keepalive=30)
        self._client.loop_start()
        if not self._connected.wait(timeout=5.0):
            raise RuntimeError(f"MQTT connect timeout for {client_id}")

    def _on_connect(self, _client: mqtt.Client, _userdata: Any, _flags: dict[str, Any], rc: int) -> None:
        if rc == 0:
            self._connected.set()

    def close(self) -> None:
        try:
            self._client.disconnect()
        except Exception:
            pass
        self._client.loop_stop()

    def _publish(self, suffix: str, payload: dict[str, Any]) -> None:
        result = self._client.publish(
            topic(self.node_id, suffix),
            json.dumps(payload, ensure_ascii=False, separators=(",", ":")),
            qos=0,
            retain=False,
        )
        result.wait_for_publish(timeout=2.0)
        if self.verbose:
            info(f"MQTT {self.node_id}/{suffix} -> {payload}")

    def publish_heartbeat(self) -> None:
        self._publish(
            "heartbeat",
            {
                "ts_ms": now_ms(),
                "boot_id": self.boot_id,
                "uptime_ms": 0,
                "status_seq": self.status_seq,
            },
        )

    def publish_status(self, *, state: str = "noise", health: str = "ok", runtime_active: bool = False) -> None:
        self.status_seq += 1
        self._publish(
            "status",
            {
                "ts_ms": now_ms(),
                "boot_id": self.boot_id,
                "fw_version": "gm-runtime-noise-test",
                "mode": "normal",
                "state": state,
                "health": health,
                "capabilities": ["test"],
                "runtime": {"active": runtime_active},
            },
        )

    def publish_event(self, action: str, args: dict[str, Any]) -> None:
        self._publish(
            "event",
            {
                "ts_ms": now_ms(),
                "event": action,
                "args": args,
            },
        )


def wait_for_runtime(
    session: HttpSession,
    room_id: str,
    *,
    timeout_s: float,
    predicate,
    label: str,
) -> RuntimeSnapshot:
    deadline = time.time() + timeout_s
    last = None
    while time.time() < deadline:
        current = RuntimeSnapshot.from_payload(session.get_runtime(room_id))
        last = current
        if predicate(current):
            return current
        time.sleep(0.25)
    raise AssertionFailure(
        f"timeout waiting for {label}; last state={last.scenario_runtime_state if last else 'n/a'} "
        f"wait={last.scenario_wait_type if last else 'n/a'} "
        f"step={last.scenario_current_step_text if last else 'n/a'} "
        f"error={last.scenario_last_error if last else 'n/a'}"
    )


def parse_json_object(text: str) -> dict[str, Any]:
    try:
        value = json.loads(text)
    except json.JSONDecodeError as exc:
        raise argparse.ArgumentTypeError(f"invalid JSON: {exc}") from exc
    if not isinstance(value, dict):
        raise argparse.ArgumentTypeError("JSON value must be an object")
    return value


def publish_noise(
    publishers: list[ContractPublisher],
    count: int,
    noise_pattern: str,
    delay_ms: int,
) -> None:
    for index in range(count):
        publisher = publishers[index % len(publishers)]
        if noise_pattern in ("heartbeat", "mixed"):
            publisher.publish_heartbeat()
        if noise_pattern in ("status", "mixed"):
            publisher.publish_status(state=f"noise_{index}")
        if delay_ms > 0:
            time.sleep(delay_ms / 1000.0)


def assert_still_waiting(before: RuntimeSnapshot, after: RuntimeSnapshot, expected_wait_type: str) -> None:
    if after.scenario_runtime_state != "waiting":
        raise AssertionFailure(
            f"room stopped waiting during noise: state={after.scenario_runtime_state} error={after.scenario_last_error}"
        )
    if expected_wait_type and after.scenario_wait_type != expected_wait_type:
        raise AssertionFailure(
            f"wait type changed during noise: expected={expected_wait_type} actual={after.scenario_wait_type}"
        )
    if after.scenario_done_steps != before.scenario_done_steps:
        raise AssertionFailure(
            f"done_steps changed during noise: before={before.scenario_done_steps} after={after.scenario_done_steps}"
        )


def runtime_progressed(before: RuntimeSnapshot, after: RuntimeSnapshot) -> bool:
    if after.scenario_runtime_state == "error":
        return False
    if after.scenario_done_steps > before.scenario_done_steps:
        return True
    if before.scenario_runtime_state == "waiting" and after.scenario_runtime_state != "waiting":
        return True
    if after.scenario_current_step_text and after.scenario_current_step_text != before.scenario_current_step_text:
        return True
    return False


def run(args: argparse.Namespace) -> int:
    session = HttpSession(args.host, args.http_port, verbose=args.verbose)
    session.login(args.username, args.password)

    if args.scenario_id:
        session.select_scenario(args.room_id, args.scenario_id)

    reset_path = "/api/gm/room/scenario/reset" if args.start_mode == "scenario" else "/api/gm/room/game/reset"
    start_path = "/api/gm/room/scenario/start" if args.start_mode == "scenario" else "/api/gm/room/game/start"

    if args.reset_first:
        session.post_room_action(reset_path, args.room_id)
        time.sleep(0.3)
    if args.start_mode != "none":
        session.post_room_action(start_path, args.room_id)

    target = ContractPublisher(
        args.mqtt_host,
        args.mqtt_port,
        client_id=f"gm-noise-target-{uuid.uuid4().hex[:8]}",
        node_id=args.target_node_id,
        verbose=args.verbose,
    )
    noise_publishers: list[ContractPublisher] = []
    try:
        target.publish_heartbeat()
        target.publish_status(state="ready")

        for index in range(max(0, args.noise_clients)):
            node_id = f"{args.noise_node_prefix}_{index + 1:02d}"
            publisher = ContractPublisher(
                args.mqtt_host,
                args.mqtt_port,
                client_id=f"gm-noise-{index + 1:02d}-{uuid.uuid4().hex[:6]}",
                node_id=node_id,
                verbose=args.verbose,
            )
            publisher.publish_heartbeat()
            publisher.publish_status(state="ready")
            noise_publishers.append(publisher)

        wait_snapshot = wait_for_runtime(
            session,
            args.room_id,
            timeout_s=args.wait_timeout_s,
            label="room waiting state",
            predicate=lambda snap: snap.scenario_runtime_state == "waiting"
            and (not args.expected_wait_type or snap.scenario_wait_type == args.expected_wait_type)
            and (not args.expected_step_substring or args.expected_step_substring in snap.scenario_current_step_text),
        )
        ok(
            "room entered waiting state: "
            f"wait={wait_snapshot.scenario_wait_type} step='{wait_snapshot.scenario_current_step_text}'"
        )

        active_noise_publishers = noise_publishers or [target]
        info(
            f"publishing {args.noise_count} {args.noise_pattern} noise update(s) "
            f"via {len(active_noise_publishers)} MQTT client(s)"
        )
        publish_noise(active_noise_publishers, args.noise_count, args.noise_pattern, args.noise_delay_ms)

        post_noise = RuntimeSnapshot.from_payload(session.get_runtime(args.room_id))
        assert_still_waiting(wait_snapshot, post_noise, args.expected_wait_type)
        ok("room still waiting after noise flood")

        info(f"publishing target event {args.target_action} on {args.target_node_id}")
        target.publish_event(args.target_action, args.target_args)

        progressed = wait_for_runtime(
            session,
            args.room_id,
            timeout_s=args.progress_timeout_s,
            label="scenario progress after target event",
            predicate=lambda snap: runtime_progressed(wait_snapshot, snap),
        )
        if progressed.scenario_runtime_state == "error":
            raise AssertionFailure(
                f"scenario entered error after target event: {progressed.scenario_last_error}"
            )
        ok(
            "scenario progressed after target event: "
            f"state={progressed.scenario_runtime_state} done={progressed.scenario_done_steps}/{progressed.scenario_total_steps} "
            f"step='{progressed.scenario_current_step_text}'"
        )
        return 0
    finally:
        for publisher in noise_publishers:
            publisher.close()
        target.close()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Stress GM runtime with heartbeat/status noise before the target event."
    )
    parser.add_argument("--host", required=True, help="SceneHub controller host for HTTP. Example: 192.168.1.50")
    parser.add_argument("--http-port", type=int, default=80, help="HTTP port. Default: 80")
    parser.add_argument("--mqtt-host", help="MQTT broker host. Default: same as --host")
    parser.add_argument("--mqtt-port", type=int, default=1883, help="MQTT port. Default: 1883")
    parser.add_argument("--username", required=True, help="GM username")
    parser.add_argument("--password", required=True, help="GM password")
    parser.add_argument("--room-id", required=True, help="Room id")
    parser.add_argument("--scenario-id", help="Optional scenario id to select before start")
    parser.add_argument(
        "--start-mode",
        choices=("scenario", "game", "none"),
        default="scenario",
        help="How to start runtime after reset. Default: scenario",
    )
    parser.add_argument(
        "--reset-first",
        action="store_true",
        default=False,
        help="Reset room runtime before start",
    )
    parser.add_argument(
        "--target-node-id",
        required=True,
        help="MQTT topic namespace id for the waited device. Usually quest_device.client_id / physical node id.",
    )
    parser.add_argument(
        "--target-action",
        required=True,
        help="Event action string published on cp/v1/dev/<node>/event, for example input.pressed",
    )
    parser.add_argument(
        "--target-args",
        type=parse_json_object,
        default={},
        help='Event args JSON object. Example: {"channel":2}',
    )
    parser.add_argument(
        "--expected-wait-type",
        default="event",
        help="Expected room scenario wait type while paused. Default: event",
    )
    parser.add_argument(
        "--expected-step-substring",
        help="Optional substring that must appear in scenario_current_step_text while waiting",
    )
    parser.add_argument("--noise-count", type=int, default=100, help="Number of noisy heartbeat/status rounds. Default: 100")
    parser.add_argument("--noise-clients", type=int, default=4, help="Number of extra MQTT clients for noise. Default: 4")
    parser.add_argument(
        "--noise-pattern",
        choices=("heartbeat", "status", "mixed"),
        default="mixed",
        help="Type of noise traffic. Default: mixed",
    )
    parser.add_argument(
        "--noise-node-prefix",
        default="gm_noise",
        help="Prefix for generated noise node ids. Default: gm_noise",
    )
    parser.add_argument(
        "--noise-delay-ms",
        type=int,
        default=0,
        help="Optional delay between noise rounds. Default: 0",
    )
    parser.add_argument(
        "--wait-timeout-s",
        type=float,
        default=20.0,
        help="How long to wait for room to enter waiting state. Default: 20",
    )
    parser.add_argument(
        "--progress-timeout-s",
        type=float,
        default=10.0,
        help="How long to wait for progress after target event. Default: 10",
    )
    parser.add_argument("--verbose", action="store_true", help="Verbose HTTP/MQTT logging")
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    if not args.mqtt_host:
        args.mqtt_host = args.host
    try:
        return run(args)
    except (HttpError, AssertionFailure, RuntimeError) as exc:
        fail(str(exc))
        return 1
    except KeyboardInterrupt:
        warn("interrupted")
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
