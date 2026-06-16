#!/usr/bin/env python3
"""Stress a SceneHub broker with 20 MQTT clients emulating SceneHub Node v1."""

from __future__ import annotations

import argparse
import json
import logging
import queue
import random
import threading
import time
import uuid
from collections import OrderedDict
from dataclasses import dataclass, field
from typing import Any, Callable, Optional

from scenehub_contract import (
    CAPABILITIES,
    LED_EFFECTS,
    NORMAL_CASES,
    PROBLEM_CASES,
    CommandCase,
    build_manifest,
)

try:
    import paho.mqtt.client as mqtt
except ImportError as exc:
    raise SystemExit("Missing dependency: pip install paho-mqtt") from exc


LOG = logging.getLogger("scenehub_node_stress")
TOPIC_ROOT = "cp/v1/dev"
SUCCESS_STATUSES = {"done", "started", "accepted"}
MAX_DUPLICATES = 4
NODE_QUEUE_LEN = 4
NODE_RESULT_QUEUE_LEN = 8
NODE_ARGS_MAX = 1024
RESULT_PUBLISH_RETRIES = 4
RESULT_RETRY_DELAY = 0.1


def now_ms() -> int:
    return int(time.time() * 1000)


def topic(node_id: str, suffix: str) -> str:
    return f"{TOPIC_ROOT}/{node_id}/{suffix}"


def mqtt_message_topic(message: mqtt.MQTTMessage) -> str:
    try:
        return message.topic
    except UnicodeDecodeError:
        raw_topic = getattr(message, "_topic", b"")
        if isinstance(raw_topic, bytes):
            return raw_topic.decode("utf-8", errors="replace")
        return str(raw_topic)


def make_client(client_id: str) -> mqtt.Client:
    if hasattr(mqtt, "CallbackAPIVersion"):
        return mqtt.Client(
            callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
            client_id=client_id,
            protocol=mqtt.MQTTv311,
            clean_session=True,
            reconnect_on_failure=True,
        )
    return mqtt.Client(client_id=client_id, protocol=mqtt.MQTTv311, clean_session=True)


@dataclass
class ResultWaiter:
    event: threading.Event = field(default_factory=threading.Event)
    payloads: list[dict[str, Any]] = field(default_factory=list)


@dataclass
class NodeStats:
    commands_received: int = 0
    results_published: int = 0
    results_observed: int = 0
    input_events_published: int = 0
    rejected: int = 0
    busy: int = 0
    parse_errors: int = 0
    duplicate_hits: int = 0


class VirtualSceneHubNode:
    def __init__(
        self,
        index: int,
        host: str,
        port: int,
        username: str,
        password: str,
        heartbeat_interval: float,
        command_delay_ms: int,
        command_qos: int,
        log_results: bool,
        log_payload_bytes: int,
        subscribe_results: bool,
        drop_results_rate: float,
        delay_results_ms: int,
        disconnect_during_command_rate: float,
    ) -> None:
        self.index = index
        self.node_id = f"scenehubnode_{index}"
        self.node_name = f"SceneHubNode {index}"
        self.client_id = f"dcc-scenehubnode-{index}"
        self.host = host
        self.port = port
        self.username = username
        self.password = password
        self.heartbeat_interval = heartbeat_interval
        self.command_delay_ms = command_delay_ms
        self.command_qos = command_qos
        self.log_results = log_results
        self.log_payload_bytes = log_payload_bytes
        self.subscribe_results = subscribe_results
        self.drop_results_rate = drop_results_rate
        self.delay_results_ms = delay_results_ms
        self.disconnect_during_command_rate = disconnect_during_command_rate
        self.command_publisher: Optional[Callable[[str, str, int], bool]] = None
        self.fault_rng = random.Random(1000 + index)
        self.expected_subscriptions = 2 if subscribe_results else 1
        self.boot_id = uuid.uuid4().hex[:12]
        self.started = time.monotonic()
        self.status_seq = 0
        self.manifest = build_manifest(self.node_id, self.node_name)
        self.stats = NodeStats()
        self.connected = threading.Event()
        self.subscriptions_ready = threading.Event()
        self.subscription_failures: list[str] = []
        self.subscription_count = 0
        self.log_incoming = threading.Event()
        self.stop_event = threading.Event()
        self.command_queue: queue.Queue[dict[str, Any]] = queue.Queue(maxsize=NODE_QUEUE_LEN)
        self.result_queue: queue.Queue[dict[str, Any]] = queue.Queue(maxsize=NODE_RESULT_QUEUE_LEN)
        self.waiters: dict[str, ResultWaiter] = {}
        self.waiters_lock = threading.Lock()
        self.publish_ack_lock = threading.Lock()
        self.pending_result_publishes: dict[int, dict[str, Any]] = {}
        self.duplicate_cache: OrderedDict[str, tuple[str, dict[str, Any]]] = OrderedDict()
        self.execution_counts: dict[str, int] = {}
        self.state_lock = threading.Lock()
        self.relays = [False] * 4
        self.mosfets = [0] * 4
        self.outputs = [False] * 4
        self.leds = [{"mode": "off"} for _ in range(2)]

        self.client = self._new_client()
        self.worker = threading.Thread(target=self._command_worker, daemon=True)
        self.telemetry = threading.Thread(target=self._telemetry_worker, daemon=True)

    def rotate_boot_id(self) -> None:
        self.boot_id = uuid.uuid4().hex[:12]
        self.started = time.monotonic()
        self.status_seq = 0

    def _new_client(self) -> mqtt.Client:
        client = make_client(self.client_id)
        if self.username:
            client.username_pw_set(self.username, password=self.password)
        client.on_connect = self._on_connect
        client.on_disconnect = self._on_disconnect
        client.on_subscribe = self._on_subscribe
        client.on_publish = self._on_publish
        client.on_message = self._on_message
        client.reconnect_delay_set(min_delay=1, max_delay=3)
        return client

    def start(self) -> None:
        self.client.connect_async(self.host, self.port, keepalive=30)
        self.client.loop_start()
        self.worker.start()
        self.telemetry.start()

    def disconnect_transport(self) -> None:
        self.connected.clear()
        self.subscriptions_ready.clear()
        try:
            self.client.disconnect()
        except Exception as exc:
            LOG.warning("[%s] disconnect failed: %s", self.node_id, exc)
        try:
            self.client.loop_stop()
        except Exception as exc:
            LOG.warning("[%s] loop stop failed: %s", self.node_id, exc)

    def reconnect_transport(self) -> None:
        self.connected.clear()
        self.subscriptions_ready.clear()
        self.subscription_count = 0
        self.subscription_failures.clear()
        self.client = self._new_client()
        try:
            self.client.connect_async(self.host, self.port, keepalive=30)
            self.client.loop_start()
        except Exception as exc:
            LOG.warning("[%s] reconnect failed: %s", self.node_id, exc)

    def stop(self) -> None:
        self.stop_event.set()
        if self.worker.is_alive():
            self.worker.join(timeout=2)
        if self.telemetry.is_alive():
            self.telemetry.join(timeout=2)
        try:
            self.client.disconnect()
        except Exception:
            pass
        self.client.loop_stop()

    def _on_connect(self, client: mqtt.Client, _userdata: Any, _flags: Any, reason_code: Any, _properties: Any = None) -> None:
        rc = getattr(reason_code, "value", reason_code)
        if rc != 0:
            LOG.error("[%s] connection rejected rc=%s", self.node_id, rc)
            return
        self.subscription_count = 0
        self.subscription_failures.clear()
        self.subscriptions_ready.clear()
        client.subscribe(topic(self.node_id, "control/command"), qos=1)
        if self.subscribe_results:
            client.subscribe(topic(self.node_id, "result"), qos=1)
        self.connected.set()
        self.publish_heartbeat()
        self.publish_status()

    def _on_disconnect(self, _client: mqtt.Client, _userdata: Any, *args: Any) -> None:
        self.connected.clear()
        self.subscriptions_ready.clear()

    def _on_subscribe(
        self,
        _client: mqtt.Client,
        _userdata: Any,
        _mid: int,
        reason_codes: Any,
        _properties: Any = None,
    ) -> None:
        codes = reason_codes if isinstance(reason_codes, list) else [reason_codes]
        for code in codes:
            value = getattr(code, "value", code)
            if value == 0x80 or value >= 128:
                self.subscription_failures.append(str(code))
            else:
                self.subscription_count += 1
        if self.subscription_count >= self.expected_subscriptions or self.subscription_failures:
            self.subscriptions_ready.set()

    def _on_publish(self, _client: mqtt.Client, _userdata: Any, mid: int, *args: Any) -> None:
        with self.publish_ack_lock:
            payload = self.pending_result_publishes.pop(mid, None)
        if payload is not None:
            self._observe_result_payload(payload)

    def _on_message(self, _client: mqtt.Client, _userdata: Any, message: mqtt.MQTTMessage) -> None:
        msg_topic = mqtt_message_topic(message)
        should_log = (
            self.log_incoming.is_set()
            and (
                msg_topic.endswith("/control/command")
                or (self.log_results and msg_topic.endswith("/result"))
            )
        )
        if should_log:
            self.log_incoming_message(message, msg_topic)
        if msg_topic.endswith("/result"):
            self._observe_result(message.payload)
            return
        if not msg_topic.endswith("/control/command"):
            return
        self.stats.commands_received += 1
        try:
            payload = json.loads(message.payload.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            self.stats.parse_errors += 1
            self._publish_result("", "", "rejected", "invalid_request")
            return
        if not isinstance(payload, dict):
            self.stats.parse_errors += 1
            self._publish_result("", "", "rejected", "invalid_request")
            return
        request_id = payload.get("request_id")
        command = payload.get("command")
        args = payload.get("args", {})
        if (
            not isinstance(request_id, str) or not request_id
            or len(request_id) >= 48
            or not isinstance(command, str) or not command
            or len(command) >= 64
            or not isinstance(args, dict)
            or len(json.dumps(args, separators=(",", ":"))) >= NODE_ARGS_MAX
        ):
            self.stats.parse_errors += 1
            self._publish_result(
                request_id if isinstance(request_id, str) else "",
                command if isinstance(command, str) else "",
                "rejected",
                "invalid_request",
            )
            return
        item = {"request_id": request_id, "command": command, "args": args}
        try:
            self.command_queue.put_nowait(item)
        except queue.Full:
            self.stats.busy += 1
            payload = self._make_result_payload(request_id, command, "rejected", "busy")
            if self._defer_result_payload(payload):
                self.stats.rejected += 1

    def log_incoming_message(self, message: mqtt.MQTTMessage, msg_topic: str) -> None:
        try:
            body = message.payload.decode("utf-8")
        except UnicodeDecodeError:
            body = repr(message.payload)
        total = len(body)
        if self.log_payload_bytes >= 0 and total > self.log_payload_bytes:
            body = body[:self.log_payload_bytes] + f"... <truncated {total - self.log_payload_bytes} chars>"
        LOG.info(
            "RX %s qos=%d topic=%s payload=%s",
            self.node_id,
            message.qos,
            msg_topic,
            body,
        )

    def _observe_result(self, raw: bytes) -> None:
        try:
            payload = json.loads(raw.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            return
        self._observe_result_payload(payload)

    def _observe_result_payload(self, payload: dict[str, Any]) -> None:
        request_id = payload.get("request_id")
        if not isinstance(request_id, str):
            return
        self.stats.results_observed += 1
        with self.waiters_lock:
            waiter = self.waiters.get(request_id)
            if waiter:
                waiter.payloads.append(payload)
                waiter.event.set()

    def _command_worker(self) -> None:
        while not self.stop_event.is_set():
            self._drain_deferred_results()
            try:
                item = self.command_queue.get(timeout=0.2)
            except queue.Empty:
                continue
            if self.command_delay_ms:
                time.sleep(self.command_delay_ms / 1000.0)
            self._process_command(item)
            self.command_queue.task_done()
            self._drain_deferred_results()

    def _telemetry_worker(self) -> None:
        while not self.stop_event.wait(self.heartbeat_interval):
            if self.connected.is_set():
                self.publish_heartbeat()

    def _publish(self, suffix: str, payload: dict[str, Any], qos: int = 0) -> bool:
        body = json.dumps(payload, separators=(",", ":"), ensure_ascii=True)
        info = self.client.publish(topic(self.node_id, suffix), body, qos=qos, retain=False)
        return info.rc == mqtt.MQTT_ERR_SUCCESS

    def publish_heartbeat(self) -> None:
        self._publish("heartbeat", {
            "ts_ms": now_ms(),
            "boot_id": self.boot_id,
            "uptime_ms": int((time.monotonic() - self.started) * 1000),
            "status_seq": self.status_seq,
        })

    def publish_status(self) -> None:
        self.status_seq += 1
        self._publish("status", {
            "ts_ms": now_ms(),
            "boot_id": self.boot_id,
            "fw_version": "0.1.0-stress",
            "mode": "normal",
            "state": "idle",
            "health": "ok",
            "capabilities": CAPABILITIES,
            "runtime": {"active": False},
            "status_seq": self.status_seq,
        })

    def publish_input_event(self, channel: int, value: int) -> bool:
        if channel < 1 or channel > 4 or value not in (0, 1):
            return False
        published = self._publish("event", {
            "event": "input.changed",
            "args": {"channel": channel, "value": value},
            "ts_ms": now_ms(),
        })
        if published:
            self.stats.input_events_published += 1
        return published

    def _make_result_payload(
        self,
        request_id: str,
        command: str,
        status: str,
        error_code: str = "",
        data: Optional[dict[str, Any]] = None,
    ) -> dict[str, Any]:
        payload: dict[str, Any] = {
            "request_id": request_id,
            "command": command,
            "status": status,
            "ts_ms": now_ms(),
        }
        if error_code:
            payload["error"] = {"code": error_code}
        if data is not None:
            payload["data"] = data
        return payload

    def _publish_result(
        self,
        request_id: str,
        command: str,
        status: str,
        error_code: str = "",
        data: Optional[dict[str, Any]] = None,
        cache: bool = False,
    ) -> dict[str, Any]:
        payload = self._make_result_payload(request_id, command, status, error_code, data)
        if self.drop_results_rate > 0 and self.fault_rng.random() < self.drop_results_rate:
            LOG.debug("[%s] dropping result %s by fault injection", self.node_id, request_id)
            return payload
        if self.delay_results_ms > 0:
            time.sleep(self.delay_results_ms / 1000.0)
        if not self._publish_result_payload_with_retry(payload):
            return payload
        if status == "rejected":
            self.stats.rejected += 1
        if cache and request_id:
            self.duplicate_cache[request_id] = (command, dict(payload))
            self.duplicate_cache.move_to_end(request_id)
            while len(self.duplicate_cache) > MAX_DUPLICATES:
                self.duplicate_cache.popitem(last=False)
        return payload

    def _publish_result_payload(self, payload: dict[str, Any]) -> bool:
        info = self.client.publish(
            topic(self.node_id, "result"),
            json.dumps(payload, separators=(",", ":"), ensure_ascii=True),
            qos=1,
            retain=False,
        )
        if info.rc != mqtt.MQTT_ERR_SUCCESS:
            return False
        with self.publish_ack_lock:
            self.pending_result_publishes[info.mid] = payload
        self.stats.results_published += 1
        return True

    def _publish_result_payload_with_retry(self, payload: dict[str, Any]) -> bool:
        for attempt in range(RESULT_PUBLISH_RETRIES):
            if self._publish_result_payload(payload):
                return True
            if attempt + 1 < RESULT_PUBLISH_RETRIES:
                time.sleep(RESULT_RETRY_DELAY)
        return False

    def _defer_result_payload(self, payload: dict[str, Any]) -> bool:
        try:
            self.result_queue.put_nowait(payload)
            return True
        except queue.Full:
            LOG.warning("[%s] result queue full; dropping result %s", self.node_id, payload.get("request_id"))
            return False

    def _drain_deferred_results(self) -> None:
        while not self.stop_event.is_set():
            try:
                payload = self.result_queue.get_nowait()
            except queue.Empty:
                return
            self._publish_result_payload_with_retry(payload)
            self.result_queue.task_done()

    @staticmethod
    def _channel(args: dict[str, Any], maximum: int) -> tuple[Optional[int], str]:
        value = args.get("channel")
        if not isinstance(value, int):
            return None, "missing_channel"
        if value < 1 or value > maximum:
            return None, "invalid_channel"
        return value, ""

    @staticmethod
    def _pwm(args: dict[str, Any], key: str, required: bool = True, default: int = 255) -> tuple[Optional[int], str]:
        value = args.get(key, default)
        if required and key not in args:
            return None, f"missing_{key}"
        if not isinstance(value, int) or value < 0 or value > 255:
            return None, "invalid_args"
        return value, ""

    @staticmethod
    def _color(args: dict[str, Any]) -> bool:
        value = args.get("color")
        if not isinstance(value, str) or len(value) not in (7, 9) or not value.startswith("#"):
            return False
        try:
            int(value[1:], 16)
        except ValueError:
            return False
        return True

    def _reject(self, request_id: str, command: str, code: str) -> None:
        self._publish_result(request_id, command, "rejected", code, cache=True)

    def _process_command(self, item: dict[str, Any]) -> None:
        request_id = item["request_id"]
        command = item["command"]
        args = item["args"]
        cached = self.duplicate_cache.get(request_id)
        if cached:
            self.stats.duplicate_hits += 1
            old_command, old_result = cached
            if old_command != command:
                self._publish_result(request_id, command, "rejected", "invalid_request")
            else:
                replay = dict(old_result)
                replay["ts_ms"] = now_ms()
                self._publish_result_payload(replay)
            return

        self.execution_counts[request_id] = self.execution_counts.get(request_id, 0) + 1
        data: Optional[dict[str, Any]] = None
        status = "started"

        if command == "describe_interface":
            status = "done"
            data = {"device_description": self.manifest}
        elif command == "node.get_status":
            status = "done"
            data = {
                "hardware": {
                    "relays": 4,
                    "mosfets": 4,
                    "universal_inputs": 4,
                    "universal_outputs": 4,
                    "led_strips": 2,
                }
            }
        elif command == "node.identify":
            status = "done"
        elif command in {"relay.all_off", "mosfet.all_off", "io.all_off", "node.all_off"}:
            with self.state_lock:
                if command in {"relay.all_off", "node.all_off"}:
                    self.relays = [False] * 4
                if command in {"mosfet.all_off", "node.all_off"}:
                    self.mosfets = [0] * 4
                if command in {"io.all_off", "node.all_off"}:
                    self.outputs = [False] * 4
                if command == "node.all_off":
                    self.leds = [{"mode": "off"} for _ in range(2)]
        elif command in {"relay.set", "io.set"}:
            channel, error = self._channel(args, 4)
            if error:
                self._reject(request_id, command, error)
                return
            on_value = args.get("on")
            if not isinstance(on_value, (bool, int)):
                self._reject(request_id, command, "missing_on")
                return
            target = self.relays if command == "relay.set" else self.outputs
            with self.state_lock:
                target[channel - 1] = bool(on_value)
        elif command == "relay.pulse":
            channel, error = self._channel(args, 4)
            duration = args.get("duration_ms")
            if error:
                self._reject(request_id, command, error)
                return
            if not isinstance(duration, int) or duration <= 0:
                self._reject(request_id, command, "missing_duration_ms")
                return
        elif command.startswith("mosfet."):
            channel, error = self._channel(args, 4)
            if error:
                self._reject(request_id, command, error)
                return
            effect = command.split(".", 1)[1]
            if effect == "set":
                value, error = self._pwm(args, "value")
            elif effect == "fade":
                value, error = self._pwm(args, "target", required=False)
                duration = args.get("duration_ms", 500)
                if not isinstance(duration, int) or duration < 0 or duration > 60000:
                    error = "invalid_args"
            elif effect == "pulse":
                value, error = self._pwm(args, "value", required=False)
                duration = args.get("duration_ms", 300)
                if not isinstance(duration, int) or duration <= 0 or duration > 60000:
                    error = "invalid_args"
            elif effect in {"blink", "breathe"}:
                value, error = self._pwm(args, "value", required=False)
            elif effect == "effect":
                alias = args.get("effect")
                if alias not in {"set", "pulse", "blink", "fade", "fade_in", "fade_out", "breathe"}:
                    self._reject(request_id, command, "invalid_args")
                    return
                value, error = self._pwm(args, "value", required=False)
                status = "done"
            else:
                self._reject(request_id, command, "not_supported")
                return
            if error:
                self._reject(request_id, command, error)
                return
            with self.state_lock:
                self.mosfets[channel - 1] = value
        elif command.startswith("led."):
            channel, error = self._channel(args, 2)
            if error:
                self._reject(request_id, command, error)
                return
            mode = command.split(".", 1)[1]
            if mode in {"solid", "blink", "breathe"} and not self._color(args):
                self._reject(request_id, command, "invalid_args")
                return
            if mode == "effect" and args.get("effect") not in LED_EFFECTS:
                self._reject(request_id, command, "invalid_args")
                return
            if mode not in {"off", "solid", "blink", "breathe", "effect"}:
                self._reject(request_id, command, "not_supported")
                return
            if mode in {"off", "solid"}:
                status = "done"
            with self.state_lock:
                self.leds[channel - 1] = {"mode": mode, **args}
        else:
            self._reject(request_id, command, "not_supported")
            return

        if (
            self.disconnect_during_command_rate > 0
            and self.fault_rng.random() < self.disconnect_during_command_rate
        ):
            LOG.warning("[%s] disconnecting during command %s by fault injection", self.node_id, command)
            self.disconnect_transport()
            return

        self._publish_result(request_id, command, status, data=data, cache=True)
        if status in SUCCESS_STATUSES:
            self.publish_status()

    def send_command(
        self,
        command: str,
        args: dict[str, Any],
        request_id: Optional[str] = None,
        qos: Optional[int] = None,
    ) -> str:
        request_id = request_id or f"stress-{self.index}-{uuid.uuid4().hex[:12]}"
        with self.waiters_lock:
            self.waiters[request_id] = ResultWaiter()
        payload = {
            "request_id": request_id,
            "command": command,
            "args": args,
            "ts_ms": now_ms(),
        }
        body = json.dumps(payload, separators=(",", ":"))
        publish_qos = self.command_qos if qos is None else qos
        command_topic = topic(self.node_id, "control/command")
        if self.command_publisher:
            self.command_publisher(command_topic, body, publish_qos)
        else:
            self.client.publish(command_topic, body, qos=publish_qos, retain=False)
        return request_id

    def send_raw(self, payload: str, qos: Optional[int] = None) -> None:
        publish_qos = self.command_qos if qos is None else qos
        command_topic = topic(self.node_id, "control/command")
        if self.command_publisher:
            self.command_publisher(command_topic, payload, publish_qos)
        else:
            self.client.publish(command_topic, payload, qos=publish_qos, retain=False)

    def publish_raw_result(self, payload: str, qos: int = 1) -> bool:
        info = self.client.publish(
            topic(self.node_id, "result"),
            payload,
            qos=qos,
            retain=False,
        )
        return info.rc == mqtt.MQTT_ERR_SUCCESS

    def wait_result(self, request_id: str, timeout: float) -> Optional[dict[str, Any]]:
        with self.waiters_lock:
            waiter = self.waiters.get(request_id)
        if not waiter:
            return None
        if not waiter.event.wait(timeout):
            with self.waiters_lock:
                self.waiters.pop(request_id, None)
            return None
        with self.waiters_lock:
            payload = waiter.payloads[-1] if waiter.payloads else None
            self.waiters.pop(request_id, None)
        return payload


class StressRunner:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.controller_client: Optional[mqtt.Client] = None
        self.controller_connected = threading.Event()
        self.nodes = [
            VirtualSceneHubNode(
                i,
                args.host,
                args.port,
                args.username,
                args.password,
                args.heartbeat_interval,
                args.command_delay_ms,
                args.command_qos,
                args.log_results,
                args.log_payload_bytes,
                args.subscribe_results,
                args.drop_results_rate,
                args.delay_results_ms,
                args.disconnect_during_command_rate,
            )
            for i in range(1, args.nodes + 1)
        ]
        self.checks = 0
        self.failures: list[str] = []
        if self.args.command_publisher == "controller":
            self.controller_client = self._new_controller_client()
            for node in self.nodes:
                node.command_publisher = self.publish_command

    def _new_controller_client(self) -> mqtt.Client:
        client = make_client(f"dcc-ctl-{uuid.uuid4().hex[:8]}")
        if self.args.username:
            client.username_pw_set(self.args.username, password=self.args.password)

        def on_connect(
            _client: mqtt.Client,
            _userdata: Any,
            _flags: Any,
            reason_code: Any,
            _properties: Any = None,
        ) -> None:
            rc = getattr(reason_code, "value", reason_code)
            if rc == 0:
                self.controller_connected.set()
            else:
                LOG.error("controller publisher rejected rc=%s", rc)

        def on_disconnect(_client: mqtt.Client, _userdata: Any, *args: Any) -> None:
            self.controller_connected.clear()

        client.on_connect = on_connect
        client.on_disconnect = on_disconnect
        client.reconnect_delay_set(min_delay=1, max_delay=3)
        return client

    def publish_command(self, command_topic: str, payload: str, qos: int) -> bool:
        if not self.controller_client:
            return False
        info = self.controller_client.publish(command_topic, payload, qos=qos, retain=False)
        return info.rc == mqtt.MQTT_ERR_SUCCESS

    def check(self, condition: bool, message: str) -> None:
        self.checks += 1
        if not condition:
            self.failures.append(message)
            LOG.error("FAIL: %s", message)

    @staticmethod
    def node_debug_snapshot(node: VirtualSceneHubNode) -> dict[str, Any]:
        with node.publish_ack_lock:
            pending_pubacks = len(node.pending_result_publishes)
        return {
            "commands": node.stats.commands_received,
            "published": node.stats.results_published,
            "observed": node.stats.results_observed,
            "rejected": node.stats.rejected,
            "busy": node.stats.busy,
            "duplicates": node.stats.duplicate_hits,
            "command_q": node.command_queue.qsize(),
            "result_q": node.result_queue.qsize(),
            "pending_pubacks": pending_pubacks,
            "connected": node.connected.is_set(),
        }

    def log_result_diagnostics(
        self,
        label: str,
        results: list[tuple[VirtualSceneHubNode, str, Optional[dict[str, Any]]]],
        baseline: dict[VirtualSceneHubNode, dict[str, Any]],
        expected_statuses: set[str],
    ) -> None:
        by_node: dict[VirtualSceneHubNode, list[str]] = {}
        for node, request_id, result in results:
            status = result.get("status") if result else "timeout"
            if status not in expected_statuses:
                by_node.setdefault(node, []).append(f"{request_id}:{status}")
        for node, entries in by_node.items():
            before = baseline.get(node, {})
            after = self.node_debug_snapshot(node)
            examples = ", ".join(entries[:2])
            if len(entries) > 2:
                examples += f", ... +{len(entries) - 2}"
            LOG.warning(
                "%s diagnostics %s missing=%d examples=%s delta(commands=%s published=%s observed=%s "
                "busy=%s rejected=%s duplicates=%s) queues(command=%s result=%s pending_pubacks=%s) "
                "connected=%s",
                label,
                node.node_id,
                len(entries),
                examples,
                after["commands"] - before.get("commands", after["commands"]),
                after["published"] - before.get("published", after["published"]),
                after["observed"] - before.get("observed", after["observed"]),
                after["busy"] - before.get("busy", after["busy"]),
                after["rejected"] - before.get("rejected", after["rejected"]),
                after["duplicates"] - before.get("duplicates", after["duplicates"]),
                after["command_q"],
                after["result_q"],
                after["pending_pubacks"],
                after["connected"],
            )

    def start(self) -> None:
        if self.controller_client:
            LOG.info("Connecting stress controller publisher")
            self.controller_client.connect_async(self.args.host, self.args.port, keepalive=30)
            self.controller_client.loop_start()
            if not self.controller_connected.wait(self.args.connect_timeout):
                self.check(False, "controller publisher did not connect")
                return
        LOG.info("Connecting %d emulated SceneHub nodes", len(self.nodes))
        for node in self.nodes:
            node.start()
        deadline = time.monotonic() + self.args.connect_timeout
        for node in self.nodes:
            remaining = max(0.0, deadline - time.monotonic())
            self.check(node.connected.wait(remaining), f"{node.node_id} did not connect")
        for node in self.nodes:
            remaining = max(0.0, deadline - time.monotonic())
            ready = node.subscriptions_ready.wait(remaining)
            self.check(ready, f"{node.node_id} did not receive SUBACK")
            self.check(
                not node.subscription_failures,
                f"{node.node_id} subscription rejected: {node.subscription_failures}",
            )
        connected = sum(node.connected.is_set() for node in self.nodes)
        ready = sum(node.subscriptions_ready.is_set() and not node.subscription_failures for node in self.nodes)
        LOG.info("Connected %d/%d nodes; subscriptions ready %d/%d", connected, len(self.nodes), ready, len(self.nodes))

    def stop(self) -> None:
        for node in self.nodes:
            node.stop()
        if self.controller_client:
            try:
                self.controller_client.disconnect()
            except Exception:
                pass
            self.controller_client.loop_stop()

    def find_nodes(self, selector: str) -> list[VirtualSceneHubNode]:
        normalized = selector.strip().lower()
        if normalized == "all":
            return list(self.nodes)
        if normalized.startswith("scenehubnode_"):
            normalized = normalized.removeprefix("scenehubnode_")
        try:
            index = int(normalized)
        except ValueError:
            return []
        return [node for node in self.nodes if node.index == index]

    @staticmethod
    def parse_input_value(raw: str) -> Optional[int]:
        normalized = raw.strip().lower()
        if normalized in {"1", "on", "true", "high"}:
            return 1
        if normalized in {"0", "off", "false", "low"}:
            return 0
        return None

    def publish_input_to_nodes(
        self,
        nodes: list[VirtualSceneHubNode],
        channel: int,
        value: int,
    ) -> int:
        published = 0
        for node in nodes:
            if node.connected.is_set() and node.publish_input_event(channel, value):
                published += 1
        return published

    def wait_nodes_ready(self, nodes: list[VirtualSceneHubNode], label: str) -> None:
        deadline = time.monotonic() + self.args.connect_timeout
        for node in nodes:
            remaining = max(0.0, deadline - time.monotonic())
            self.check(node.connected.wait(remaining), f"{label}: {node.node_id} did not reconnect")
        for node in nodes:
            remaining = max(0.0, deadline - time.monotonic())
            ready = node.subscriptions_ready.wait(remaining)
            self.check(ready, f"{label}: {node.node_id} did not receive SUBACK after reconnect")
            self.check(
                not node.subscription_failures,
                f"{label}: {node.node_id} subscription rejected after reconnect: {node.subscription_failures}",
            )

    def check_status_for_nodes(self, nodes: list[VirtualSceneHubNode], label: str) -> None:
        requests = [(node, node.send_command("node.get_status", {})) for node in nodes]
        deadline = time.monotonic() + self.args.result_timeout
        for node, request_id in requests:
            result = node.wait_result(request_id, max(0.0, deadline - time.monotonic()))
            self.check(
                result is not None and result.get("status") == "done",
                f"{label}: {node.node_id} did not return status",
            )

    def validate_manifest_result(
        self,
        node: VirtualSceneHubNode,
        result: Optional[dict[str, Any]],
        label: str,
    ) -> None:
        data = result.get("data") if result else None
        manifest = data.get("device_description") if isinstance(data, dict) else None
        self.check(
            isinstance(manifest, dict),
            f"{label}: {node.node_id} missing device_description",
        )
        if not isinstance(manifest, dict):
            return
        self.check(
            manifest.get("manifest_version") == 2,
            f"{label}: {node.node_id} manifest_version={manifest.get('manifest_version')}",
        )
        self.check(
            manifest.get("format") == "compact_resources",
            f"{label}: {node.node_id} format={manifest.get('format')}",
        )
        self.check(
            manifest.get("capability_contract") == "scenehub.node.compact.v1",
            f"{label}: {node.node_id} capability_contract={manifest.get('capability_contract')}",
        )
        resources = manifest.get("resources")
        self.check(isinstance(resources, dict), f"{label}: {node.node_id} resources missing")
        if not isinstance(resources, dict):
            return
        expected_counts = {
            "relays": 4,
            "mosfets": 4,
            "inputs": 4,
            "outputs": 4,
            "led_strips": 2,
        }
        for key, expected in expected_counts.items():
            value = resources.get(key)
            actual = len(value) if isinstance(value, list) else None
            self.check(
                actual == expected,
                f"{label}: {node.node_id} resources.{key} count={actual}",
            )

    @staticmethod
    def print_interactive_help() -> None:
        print(
            "\nInteractive commands:\n"
            "  input <node|all> <channel 1-4> <0|1>\n"
            "  pulse <node|all> <channel 1-4> [duration_ms]\n"
            "  inputs <node|all> <in1> <in2> <in3> <in4>\n"
            "  status\n"
            "  help\n"
            "  quit\n"
        )

    def interactive_phase(self) -> None:
        for node in self.nodes:
            node.log_incoming.set()
        LOG.info("Interactive mode: clients remain connected; type 'help' or 'quit'")
        self.print_interactive_help()

        while True:
            try:
                line = input("scenehub-nodes> ").strip()
            except (EOFError, KeyboardInterrupt):
                print()
                return
            if not line:
                continue
            parts = line.split()
            command = parts[0].lower()
            if command in {"quit", "exit", "q"}:
                return
            if command in {"help", "?"}:
                self.print_interactive_help()
                continue
            if command == "status":
                connected = [node.node_id for node in self.nodes if node.connected.is_set()]
                print(f"Connected {len(connected)}/{len(self.nodes)}: {', '.join(connected)}")
                continue
            if command == "input" and len(parts) == 4:
                nodes = self.find_nodes(parts[1])
                value = self.parse_input_value(parts[3])
                try:
                    channel = int(parts[2])
                except ValueError:
                    channel = 0
                if not nodes or channel not in range(1, 5) or value is None:
                    print("Usage: input <node|all> <channel 1-4> <0|1>")
                    continue
                sent = self.publish_input_to_nodes(nodes, channel, value)
                print(f"Published input.changed to {sent}/{len(nodes)} nodes")
                continue
            if command == "pulse" and len(parts) in {3, 4}:
                nodes = self.find_nodes(parts[1])
                try:
                    channel = int(parts[2])
                    duration_ms = int(parts[3]) if len(parts) == 4 else 100
                except ValueError:
                    channel = 0
                    duration_ms = 0
                if not nodes or channel not in range(1, 5) or duration_ms < 0:
                    print("Usage: pulse <node|all> <channel 1-4> [duration_ms]")
                    continue
                high = self.publish_input_to_nodes(nodes, channel, 1)
                time.sleep(duration_ms / 1000.0)
                low = self.publish_input_to_nodes(nodes, channel, 0)
                print(f"Published pulse to {min(high, low)}/{len(nodes)} nodes")
                continue
            if command == "inputs" and len(parts) == 6:
                nodes = self.find_nodes(parts[1])
                values = [self.parse_input_value(value) for value in parts[2:]]
                if not nodes or any(value is None for value in values):
                    print("Usage: inputs <node|all> <in1> <in2> <in3> <in4>")
                    continue
                sent = 0
                for channel, value in enumerate(values, start=1):
                    sent += self.publish_input_to_nodes(nodes, channel, int(value))
                print(f"Published {sent}/{len(nodes) * 4} input.changed events")
                continue
            print("Unknown command. Type 'help'.")

    def run_case_across_nodes(self, case: CommandCase, label: str) -> None:
        LOG.debug("%s: %s", label, case.command)
        requests = [(node, node.send_command(case.command, dict(case.args))) for node in self.nodes]
        deadline = time.monotonic() + self.args.result_timeout
        results: list[tuple[VirtualSceneHubNode, str, Optional[dict[str, Any]]]] = []
        for node, request_id in requests:
            result = node.wait_result(request_id, max(0.0, deadline - time.monotonic()))
            results.append((node, request_id, result))

        timed_out = [node.node_id for node, _, result in results if result is None]
        if timed_out:
            self.check(
                False,
                f"{label}: {case.command} timed out on {len(timed_out)}/{len(self.nodes)} nodes: "
                f"{', '.join(timed_out)}",
            )

        for node, _, result in results:
            if not result:
                continue
            status = result.get("status")
            error = (result.get("error") or {}).get("code")
            self.check(
                status in case.expected_statuses,
                f"{label}: {node.node_id} {case.command} returned status={status}",
            )
            if case.expected_errors:
                self.check(
                    error in case.expected_errors,
                    f"{label}: {node.node_id} {case.command} returned error={error}",
                )

    def normal_phase(self) -> None:
        LOG.info("Phase 1: valid command traffic")
        for round_index in range(1, self.args.rounds + 1):
            cases = list(NORMAL_CASES)
            random.Random(1000 + round_index).shuffle(cases)
            for case in cases:
                self.run_case_across_nodes(case, f"normal round {round_index}")
            for node in self.nodes:
                for channel in range(1, 5):
                    node.publish_input_event(channel, (channel + round_index) % 2)
            LOG.info("Completed normal round %d/%d", round_index, self.args.rounds)

    def input_scenario_phase(self) -> None:
        LOG.info("Phase 2: input event scenario traffic")
        patterns = [
            [0, 0, 0, 0],
            [1, 0, 0, 0],
            [1, 1, 0, 0],
            [0, 1, 1, 0],
            [0, 0, 1, 1],
            [0, 0, 0, 0],
        ]
        expected = len(self.nodes) * len(patterns) * 4
        published = 0
        for pattern in patterns:
            for channel, value in enumerate(pattern, start=1):
                published += self.publish_input_to_nodes(self.nodes, channel, value)
            time.sleep(self.args.input_step_delay)
        LOG.info("Input scenario complete: published %d/%d events", published, expected)
        self.check(
            published == expected,
            f"input scenario: published {published}/{expected} input events",
        )

    def reconnect_phase(self) -> None:
        LOG.info("Phase 3: reconnect waves")
        wave_size = min(self.args.reconnect_wave_size, len(self.nodes))
        for start_index in range(0, len(self.nodes), wave_size):
            wave = self.nodes[start_index:start_index + wave_size]
            LOG.info(
                "reconnect wave: %s",
                ", ".join(node.node_id for node in wave),
            )
            for node in wave:
                node.disconnect_transport()
            time.sleep(self.args.reconnect_offline_delay)
            for node in wave:
                node.reconnect_transport()
            self.wait_nodes_ready(wave, "reconnect wave")
            self.check_status_for_nodes(wave, "reconnect wave")
        LOG.info("Reconnect waves complete")

    def reconnect_churn_phase(self) -> None:
        LOG.info("Phase 4: random reconnect churn")
        count = min(self.args.churn_nodes, len(self.nodes))
        if count <= 0:
            return
        rng = random.Random(self.args.random_seed)
        targets = rng.sample(self.nodes, count)
        rotated: list[str] = []
        LOG.info("reconnect churn: %s", ", ".join(node.node_id for node in targets))
        for node in targets:
            node.disconnect_transport()
        time.sleep(self.args.churn_offline_delay)
        for node in targets:
            if rng.random() < self.args.churn_boot_id_rate:
                node.rotate_boot_id()
                rotated.append(node.node_id)
            node.reconnect_transport()
        self.wait_nodes_ready(targets, "reconnect churn")
        for node in targets:
            node.publish_heartbeat()
            node.publish_status()
        self.check_status_for_nodes(self.nodes, "reconnect churn")
        LOG.info(
            "Reconnect churn complete: targets=%s boot_id_rotated=%s",
            ",".join(node.node_id for node in targets),
            ",".join(rotated) if rotated else "none",
        )

    def duplicate_client_phase(self) -> None:
        LOG.info("Phase 5: duplicate client_id replacement")
        count = min(self.args.duplicate_client_count, len(self.nodes))
        targets = self.nodes[:count]
        duplicates: list[tuple[VirtualSceneHubNode, mqtt.Client]] = []

        for node in targets:
            connected = threading.Event()
            duplicate = make_client(node.client_id)
            if self.args.username:
                duplicate.username_pw_set(self.args.username, password=self.args.password)

            def on_connect(
                _client: mqtt.Client,
                _userdata: Any,
                _flags: Any,
                reason_code: Any,
                _properties: Any = None,
                event: threading.Event = connected,
                node_id: str = node.node_id,
            ) -> None:
                rc = getattr(reason_code, "value", reason_code)
                if rc == 0:
                    LOG.debug("duplicate client connected for %s", node_id)
                    event.set()
                else:
                    LOG.error("duplicate client rejected for %s rc=%s", node_id, rc)

            duplicate.on_connect = on_connect
            duplicate.connect_async(self.args.host, self.args.port, keepalive=30)
            duplicate.loop_start()
            self.check(connected.wait(self.args.connect_timeout), f"duplicate client: {node.node_id} did not connect")
            duplicates.append((node, duplicate))

        time.sleep(self.args.duplicate_hold)
        for _, duplicate in duplicates:
            try:
                duplicate.disconnect()
            except Exception:
                pass
            duplicate.loop_stop()

        self.wait_nodes_ready(targets, "duplicate client")
        self.check_status_for_nodes(targets, "duplicate client")

    def describe_recovery_phase(self) -> None:
        LOG.info("Phase 6: simultaneous describe_interface and retry recovery")
        pending = list(self.nodes)
        recovered = 0
        manifest_results: dict[VirtualSceneHubNode, dict[str, Any]] = {}

        requests = [
            (node, node.send_command("describe_interface", {}))
            for node in pending
        ]
        deadline = time.monotonic() + self.args.result_timeout
        pending = []
        for node, request_id in requests:
            result = node.wait_result(request_id, max(0.0, deadline - time.monotonic()))
            if not result or result.get("status") != "done":
                pending.append(node)
            else:
                manifest_results[node] = result

        if pending:
            LOG.warning(
                "Initial describe_interface burst missed %d/%d nodes; retrying sequentially",
                len(pending),
                len(self.nodes),
            )

        for retry_index in range(1, self.args.interface_retries + 1):
            if not pending:
                break
            retry_pending: list[VirtualSceneHubNode] = []
            for node in pending:
                if self.args.interface_retry_delay:
                    time.sleep(self.args.interface_retry_delay)
                request_id = node.send_command("describe_interface", {})
                result = node.wait_result(request_id, self.args.result_timeout)
                if result and result.get("status") == "done":
                    recovered += 1
                    manifest_results[node] = result
                else:
                    retry_pending.append(node)
            pending = retry_pending
            LOG.info(
                "describe_interface retry %d/%d: remaining %d",
                retry_index,
                self.args.interface_retries,
                len(pending),
            )

        self.check(
            not pending,
            "describe_interface recovery failed for: "
            + ", ".join(node.node_id for node in pending),
        )
        for node in self.nodes:
            self.validate_manifest_result(node, manifest_results.get(node), "describe_interface")
            self.check(
                node.connected.is_set(),
                f"describe_interface recovery: {node.node_id} disconnected",
            )
        LOG.info(
            "describe_interface recovery complete: initial %d/%d, recovered %d",
            len(self.nodes) - recovered - len(pending),
            len(self.nodes),
            recovered,
        )

    def duplicate_phase(self) -> None:
        LOG.info("Phase 7: duplicate request_id and conflicting duplicate")
        originals: list[tuple[VirtualSceneHubNode, str]] = []
        for node in self.nodes:
            request_id = f"duplicate-{node.index}-{uuid.uuid4().hex[:8]}"
            node.send_command("relay.pulse", {"channel": 1, "duration_ms": 100}, request_id)
            result = node.wait_result(request_id, self.args.result_timeout)
            self.check(result is not None, f"duplicate original: {node.node_id} timed out")
            originals.append((node, request_id))

        for node, request_id in originals:
            node.send_command("relay.pulse", {"channel": 1, "duration_ms": 100}, request_id)
            replay = node.wait_result(request_id, self.args.result_timeout)
            self.check(replay is not None, f"duplicate replay: {node.node_id} timed out")
            self.check(
                node.execution_counts.get(request_id) == 1,
                f"duplicate replay: {node.node_id} executed side effect more than once",
            )

        for node, request_id in originals:
            node.send_command("relay.set", {"channel": 1, "on": True}, request_id)
            conflict = node.wait_result(request_id, self.args.result_timeout)
            error = ((conflict or {}).get("error") or {}).get("code")
            self.check(
                conflict is not None and conflict.get("status") == "rejected" and error == "invalid_request",
                f"conflicting duplicate: {node.node_id} was not rejected",
            )

    def problem_phase(self) -> None:
        LOG.info("Phase 8: invalid and boundary commands")
        for case in PROBLEM_CASES:
            self.run_case_across_nodes(case, "problem")

        requests = []
        large_value = "x" * 1400
        for node in self.nodes:
            request_id = node.send_command("node.get_status", {"padding": large_value})
            requests.append((node, request_id))
            node.send_raw('{"request_id":"broken","command":')
            node.send_raw("[]")
        for node, request_id in requests:
            result = node.wait_result(request_id, self.args.result_timeout)
            error = ((result or {}).get("error") or {}).get("code")
            self.check(
                result is not None and result.get("status") == "rejected" and error == "invalid_request",
                f"oversize args: {node.node_id} did not reject the command",
            )
        time.sleep(0.5)

    def result_corruption_phase(self) -> None:
        LOG.info("Phase 9: corrupted result traffic")
        targets = self.nodes[:min(5, len(self.nodes))]
        published = 0
        for node in targets:
            payloads = [
                '{"request_id":"corrupt-json","status":',
                json.dumps({
                    "request_id": f"foreign-{node.index}-{uuid.uuid4().hex[:8]}",
                    "command": "relay.set",
                    "status": "done",
                    "ts_ms": now_ms(),
                }, separators=(",", ":")),
                json.dumps({
                    "request_id": f"missing-command-{node.index}-{uuid.uuid4().hex[:8]}",
                    "status": "done",
                    "ts_ms": now_ms(),
                }, separators=(",", ":")),
                json.dumps({
                    "request_id": f"bad-status-{node.index}-{uuid.uuid4().hex[:8]}",
                    "command": "node.get_status",
                    "status": "teleported",
                    "ts_ms": now_ms(),
                }, separators=(",", ":")),
                json.dumps({
                    "request_id": f"oversize-result-{node.index}-{uuid.uuid4().hex[:8]}",
                    "command": "node.get_status",
                    "status": "done",
                    "data": {"padding": "x" * 1400},
                    "ts_ms": now_ms(),
                }, separators=(",", ":")),
            ]
            for payload in payloads:
                if node.publish_raw_result(payload, qos=1):
                    published += 1
        LOG.info("Corrupted result traffic published %d packets", published)
        time.sleep(0.5)
        self.check_status_for_nodes(self.nodes, "result corruption")

    def burst_phase(self) -> None:
        LOG.info("Phase 10: queue pressure burst")
        all_requests: list[tuple[VirtualSceneHubNode, str]] = []
        for node in self.nodes:
            for burst_index in range(self.args.burst):
                request_id = node.send_command(
                    "relay.set",
                    {"channel": burst_index % 4 + 1, "on": bool(burst_index % 2)},
                )
                all_requests.append((node, request_id))
        completed = 0
        deadline = time.monotonic() + self.args.result_timeout
        for node, request_id in all_requests:
            result = node.wait_result(request_id, max(0.0, deadline - time.monotonic()))
            if result and result.get("status") in {"started", "rejected"}:
                completed += 1
        LOG.info(
            "Queue pressure burst complete: observed %d/%d results",
            completed,
            len(all_requests),
        )
        self.check(
            completed == len(all_requests),
            f"burst: observed {completed}/{len(all_requests)} results",
        )

    def slow_client_phase(self) -> None:
        LOG.info("Phase 11: slow node isolation")
        count = min(self.args.slow_clients, len(self.nodes))
        if count <= 0:
            return
        slow_nodes = self.nodes[-count:]
        fast_nodes = self.nodes[:-count]
        original_delays = {node: node.command_delay_ms for node in slow_nodes}
        for node in slow_nodes:
            node.command_delay_ms = self.args.slow_command_delay_ms
        try:
            baseline = {node: self.node_debug_snapshot(node) for node in self.nodes}
            all_requests: list[tuple[VirtualSceneHubNode, str]] = []
            for node in self.nodes:
                for index in range(self.args.slow_burst):
                    request_id = node.send_command(
                        "relay.set",
                        {"channel": index % 4 + 1, "on": bool(index % 2)},
                    )
                    all_requests.append((node, request_id))

            fast_done = 0
            fast_deadline = time.monotonic() + self.args.result_timeout
            fast_results: list[tuple[VirtualSceneHubNode, str, Optional[dict[str, Any]]]] = []
            for node, request_id in [(node, rid) for node, rid in all_requests if node in fast_nodes]:
                result = node.wait_result(request_id, max(0.0, fast_deadline - time.monotonic()))
                fast_results.append((node, request_id, result))
                if result and result.get("status") in {"started", "rejected"}:
                    fast_done += 1
            expected_fast = len(fast_nodes) * self.args.slow_burst
            if fast_done != expected_fast:
                self.log_result_diagnostics(
                    "slow isolation fast",
                    fast_results,
                    baseline,
                    {"started", "rejected"},
                )
            self.check(
                fast_done == expected_fast,
                f"slow isolation: fast nodes observed {fast_done}/{expected_fast} results",
            )

            slow_done = 0
            slow_timeout = self.args.result_timeout + (self.args.slow_command_delay_ms / 1000.0) * self.args.slow_burst
            slow_deadline = time.monotonic() + slow_timeout
            slow_results: list[tuple[VirtualSceneHubNode, str, Optional[dict[str, Any]]]] = []
            for node, request_id in [(node, rid) for node, rid in all_requests if node in slow_nodes]:
                result = node.wait_result(request_id, max(0.0, slow_deadline - time.monotonic()))
                slow_results.append((node, request_id, result))
                if result and result.get("status") in {"started", "rejected"}:
                    slow_done += 1
            expected_slow = len(slow_nodes) * self.args.slow_burst
            if slow_done != expected_slow:
                self.log_result_diagnostics(
                    "slow isolation slow",
                    slow_results,
                    baseline,
                    {"started", "rejected"},
                )
            self.check(
                slow_done == expected_slow,
                f"slow isolation: slow nodes observed {slow_done}/{expected_slow} results",
            )
        finally:
            for node, delay in original_delays.items():
                node.command_delay_ms = delay

    def soak_phase(self) -> None:
        if self.args.soak_seconds <= 0:
            return
        LOG.info("Phase 12: soak traffic for %.1f seconds", self.args.soak_seconds)
        deadline = time.monotonic() + self.args.soak_seconds
        next_input = time.monotonic()
        next_command = time.monotonic()
        rng = random.Random(4242)
        while time.monotonic() < deadline:
            now = time.monotonic()
            if now >= next_input:
                node = rng.choice(self.nodes)
                channel = rng.randint(1, 4)
                value = rng.randint(0, 1)
                self.publish_input_to_nodes([node], channel, value)
                next_input = now + self.args.soak_input_interval
            if now >= next_command:
                node = rng.choice(self.nodes)
                request_id = node.send_command("node.get_status", {})
                result = node.wait_result(request_id, self.args.result_timeout)
                self.check(
                    result is not None and result.get("status") == "done",
                    f"soak: {node.node_id} did not return status",
                )
                next_command = now + self.args.soak_command_interval
            time.sleep(0.05)

    def health_phase(self) -> None:
        LOG.info("Phase 13: post-stress health probe")
        case = CommandCase("node.get_status", {}, {"done"})
        self.run_case_across_nodes(case, "health")
        for node in self.nodes:
            self.check(node.connected.is_set(), f"{node.node_id} disconnected during stress")

    def qos1_probe_phase(self) -> None:
        LOG.info("Optional phase: external QoS 1 command delivery probe")
        requests = [
            (node, node.send_command("node.get_status", {}, qos=1))
            for node in self.nodes
        ]
        for node, request_id in requests:
            result = node.wait_result(request_id, self.args.result_timeout)
            self.check(
                result is not None and result.get("status") == "done",
                f"qos1 probe: {node.node_id} did not return a result",
            )

    def summary(self) -> int:
        commands = sum(node.stats.commands_received for node in self.nodes)
        published = sum(node.stats.results_published for node in self.nodes)
        observed = sum(node.stats.results_observed for node in self.nodes)
        input_events = sum(node.stats.input_events_published for node in self.nodes)
        rejected = sum(node.stats.rejected for node in self.nodes)
        busy = sum(node.stats.busy for node in self.nodes)
        duplicates = sum(node.stats.duplicate_hits for node in self.nodes)
        print("\nSceneHub Node stress summary")
        print(f"  nodes:             {len(self.nodes)}")
        print(f"  commands received: {commands}")
        print(f"  results published: {published}")
        print(f"  results observed:  {observed}")
        print(f"  input events:      {input_events}")
        print(f"  rejected:          {rejected}")
        print(f"  queue busy:        {busy}")
        print(f"  duplicate hits:    {duplicates}")
        print(f"  checks:            {self.checks}")
        print(f"  failures:          {len(self.failures)}")
        if self.failures:
            print("\nFailures:")
            for failure in self.failures[:50]:
                print(f"  - {failure}")
            if len(self.failures) > 50:
                print(f"  - ... and {len(self.failures) - 50} more")
            return 1
        print("  result:            PASS")
        return 0

    def run(self) -> int:
        try:
            self.start()
            if self.failures:
                return self.summary()
            self.normal_phase()
            self.input_scenario_phase()
            self.reconnect_phase()
            self.reconnect_churn_phase()
            self.duplicate_client_phase()
            self.describe_recovery_phase()
            self.duplicate_phase()
            self.problem_phase()
            self.result_corruption_phase()
            self.burst_phase()
            self.slow_client_phase()
            self.soak_phase()
            self.health_phase()
            if self.args.qos1_probe:
                self.qos1_probe_phase()
            result = self.summary()
            if not self.args.no_hold:
                self.interactive_phase()
            return result
        finally:
            self.stop()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Emulate SceneHub Node v1 clients and stress the SceneHub MQTT broker."
    )
    parser.add_argument("--host", required=True, help="SceneHub MQTT broker IP/host")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--username", default="")
    parser.add_argument("--password", default="")
    parser.add_argument("--nodes", type=int, default=20)
    parser.add_argument("--rounds", type=int, default=1)
    parser.add_argument("--burst", type=int, default=8, help="Commands per node in queue pressure phase")
    parser.add_argument("--heartbeat-interval", type=float, default=2.0)
    parser.add_argument("--command-delay-ms", type=int, default=10)
    parser.add_argument("--input-step-delay", type=float, default=0.05)
    parser.add_argument("--reconnect-wave-size", type=int, default=5)
    parser.add_argument("--reconnect-offline-delay", type=float, default=0.5)
    parser.add_argument("--churn-nodes", type=int, default=5)
    parser.add_argument("--churn-offline-delay", type=float, default=1.0)
    parser.add_argument("--churn-boot-id-rate", type=float, default=0.5)
    parser.add_argument("--duplicate-client-count", type=int, default=3)
    parser.add_argument("--duplicate-hold", type=float, default=0.5)
    parser.add_argument("--slow-clients", type=int, default=2)
    parser.add_argument("--slow-command-delay-ms", type=int, default=250)
    parser.add_argument("--slow-burst", type=int, default=8)
    parser.add_argument("--drop-results-rate", type=float, default=0.0)
    parser.add_argument("--delay-results-ms", type=int, default=0)
    parser.add_argument("--disconnect-during-command-rate", type=float, default=0.0)
    parser.add_argument("--random-seed", type=int, default=4242)
    parser.add_argument("--soak-seconds", type=float, default=0.0)
    parser.add_argument("--soak-input-interval", type=float, default=1.0)
    parser.add_argument("--soak-command-interval", type=float, default=5.0)
    parser.add_argument(
        "--command-qos",
        type=int,
        choices=[0, 1],
        default=1,
        help="MQTT QoS used for all emulated controller commands",
    )
    parser.add_argument(
        "--command-publisher",
        choices=["controller", "self"],
        default="self",
        help=(
            "MQTT client used to publish control commands. 'self' works with the current "
            "broker ACL because every virtual node publishes to its own topic. 'controller' "
            "uses one extra client and requires broker ACL rules that allow it to publish "
            "to node command topics."
        ),
    )
    parser.add_argument("--connect-timeout", type=float, default=20.0)
    parser.add_argument("--result-timeout", type=float, default=8.0)
    parser.add_argument(
        "--interface-retries",
        type=int,
        default=2,
        help="Sequential retries for describe_interface responses missed during the initial burst",
    )
    parser.add_argument(
        "--interface-retry-delay",
        type=float,
        default=0.25,
        help="Delay in seconds before each sequential describe_interface retry",
    )
    parser.add_argument(
        "--qos1-probe",
        action="store_true",
        help="Run a final QoS 1 command probe that may expose broker PUBACK handling issues",
    )
    parser.add_argument(
        "--no-hold",
        action="store_true",
        help="Exit after stress phases instead of keeping clients connected interactively",
    )
    parser.add_argument(
        "--log-results",
        action="store_true",
        help="In hold mode, also log result messages when --subscribe-results is enabled.",
    )
    parser.add_argument(
        "--subscribe-results",
        action="store_true",
        help="Also subscribe each emulated node to its own /result topic; this is heavier than real node behavior.",
    )
    parser.add_argument(
        "--log-payload-bytes",
        type=int,
        default=240,
        help="Maximum characters of each interactive RX payload to log; use -1 for full payloads",
    )
    parser.add_argument("--log-level", choices=["DEBUG", "INFO", "WARNING", "ERROR"], default="INFO")
    args = parser.parse_args()
    if args.nodes < 1:
        parser.error("--nodes must be at least 1")
    if args.rounds < 1:
        parser.error("--rounds must be at least 1")
    if args.burst < 1:
        parser.error("--burst must be at least 1")
    if args.input_step_delay < 0:
        parser.error("--input-step-delay must not be negative")
    if args.reconnect_wave_size < 1:
        parser.error("--reconnect-wave-size must be at least 1")
    if args.reconnect_offline_delay < 0:
        parser.error("--reconnect-offline-delay must not be negative")
    if args.churn_nodes < 0:
        parser.error("--churn-nodes must not be negative")
    if args.churn_offline_delay < 0:
        parser.error("--churn-offline-delay must not be negative")
    if args.churn_boot_id_rate < 0 or args.churn_boot_id_rate > 1:
        parser.error("--churn-boot-id-rate must be between 0 and 1")
    if args.duplicate_client_count < 0:
        parser.error("--duplicate-client-count must not be negative")
    if args.duplicate_hold < 0:
        parser.error("--duplicate-hold must not be negative")
    if args.slow_clients < 0:
        parser.error("--slow-clients must not be negative")
    if args.slow_command_delay_ms < 0:
        parser.error("--slow-command-delay-ms must not be negative")
    if args.slow_burst < 1:
        parser.error("--slow-burst must be at least 1")
    if args.drop_results_rate < 0 or args.drop_results_rate > 1:
        parser.error("--drop-results-rate must be between 0 and 1")
    if args.delay_results_ms < 0:
        parser.error("--delay-results-ms must not be negative")
    if args.disconnect_during_command_rate < 0 or args.disconnect_during_command_rate > 1:
        parser.error("--disconnect-during-command-rate must be between 0 and 1")
    if args.soak_seconds < 0:
        parser.error("--soak-seconds must not be negative")
    if args.soak_input_interval <= 0:
        parser.error("--soak-input-interval must be greater than 0")
    if args.soak_command_interval <= 0:
        parser.error("--soak-command-interval must be greater than 0")
    if args.interface_retries < 0:
        parser.error("--interface-retries must not be negative")
    if args.interface_retry_delay < 0:
        parser.error("--interface-retry-delay must not be negative")
    if args.log_payload_bytes < -1:
        parser.error("--log-payload-bytes must be -1 or greater")
    return args


def main() -> int:
    args = parse_args()
    logging.basicConfig(
        level=getattr(logging, args.log_level),
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )
    return StressRunner(args).run()


if __name__ == "__main__":
    raise SystemExit(main())
