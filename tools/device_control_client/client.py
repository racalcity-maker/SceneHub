#!/usr/bin/env python3
"""Reference MQTT device-control client for cp/v1 contract."""

from __future__ import annotations

import argparse
import json
import logging
import shlex
import threading
import time
import uuid
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, Optional

try:
    import paho.mqtt.client as mqtt
except ImportError as exc:  # pragma: no cover
    raise SystemExit("Missing dependency: paho-mqtt (pip install paho-mqtt)") from exc

from profiles import resolve_profile


LOG = logging.getLogger("device_control_client")

TOPIC_PREFIX = "cp/v1/dev"
DEFAULT_KEEPALIVE = 30
DEFAULT_HEARTBEAT_INTERVAL_MS = 2000
DEFAULT_STATUS_INTERVAL_MS = 5000
DEFAULT_DIAG_INTERVAL_MS = 0


def now_ms() -> int:
    return int(time.time() * 1000)


def make_boot_id() -> str:
    return uuid.uuid4().hex[:12]


def topic(device_id: str, suffix: str) -> str:
    return f"{TOPIC_PREFIX}/{device_id}/{suffix}"


def ensure_client(client_id: str) -> mqtt.Client:
    if hasattr(mqtt, "CallbackAPIVersion"):
        return mqtt.Client(
            callback_api_version=mqtt.CallbackAPIVersion.VERSION1,
            client_id=client_id,
            protocol=mqtt.MQTTv311,
        )
    return mqtt.Client(client_id=client_id, protocol=mqtt.MQTTv311)


@dataclass
class DeviceRuntime:
    boot_id: str
    started_at_monotonic: float
    online: bool
    last_error: str
    preset: str
    mode: str
    state: str
    health: str
    runtime_active: bool


class EmulatedDevice:
    def __init__(self, broker_cfg: Dict[str, Any], global_defaults: Dict[str, Any], cfg: Dict[str, Any]) -> None:
        self.device_id = str(cfg["device_id"])
        self.client_id = str(cfg.get("client_id") or f"dcc-{self.device_id}")
        self.fw_version = str(cfg.get("fw_version") or "dcc-0.1.0")

        profile = resolve_profile(str(cfg.get("profile") or "generic"))
        self.profile = profile
        self.presets = profile.get("presets", {})
        initial_preset = str(cfg.get("preset") or "normal")
        initial_mode = str(cfg.get("mode") or profile.get("mode") or "normal")
        initial_state = str(cfg.get("state") or profile.get("state") or "idle")
        initial_health = str(cfg.get("health") or profile.get("health") or "ok")

        self.runtime = DeviceRuntime(
            boot_id=make_boot_id(),
            started_at_monotonic=time.monotonic(),
            online=True,
            last_error="",
            preset=initial_preset,
            mode=initial_mode,
            state=initial_state,
            health=initial_health,
            runtime_active=bool(cfg.get("runtime_active", profile.get("runtime_active", False))),
        )

        self.force_fault = bool(cfg.get("simulate_fault", False))
        self.force_degraded = bool(cfg.get("simulate_degraded", False))
        self.silent_mode = bool(cfg.get("silent_mode", False))
        self.response_delay_ms = int(cfg.get("response_delay_ms", 0))
        self.fake_reboot_ms = int(cfg.get("fake_reboot_ms", 3500))

        self.heartbeat_interval_ms = int(
            cfg.get("heartbeat_interval_ms", global_defaults.get("heartbeat_interval_ms", DEFAULT_HEARTBEAT_INTERVAL_MS))
        )
        self.status_interval_ms = int(
            cfg.get("status_interval_ms", global_defaults.get("status_interval_ms", DEFAULT_STATUS_INTERVAL_MS))
        )
        self.diag_interval_ms = int(cfg.get("diag_interval_ms", global_defaults.get("diag_interval_ms", DEFAULT_DIAG_INTERVAL_MS)))

        capabilities = cfg.get("capabilities")
        if isinstance(capabilities, list) and capabilities:
            self.capabilities = [str(item) for item in capabilities]
        else:
            self.capabilities = [str(item) for item in profile.get("capabilities", [])]

        self.quest_interface = self._normalize_quest_interface(
            cfg.get("quest_interface") or profile.get("quest_interface") or {}
        )
        if self.quest_interface and "describe_interface" not in self.capabilities:
            self.capabilities.append("describe_interface")
        self._quest_commands_by_topic = self._index_quest_commands(self.quest_interface)
        self._quest_events_by_id = {
            str(event.get("id")): event
            for event in self.quest_interface.get("events", [])
            if isinstance(event, dict) and event.get("id")
        }

        self._status_seq = 0
        self._connected = False
        self._stop_event = threading.Event()
        self._loop_thread = threading.Thread(target=self._run_loop, name=f"dcc-loop-{self.device_id}", daemon=True)
        self._state_lock = threading.RLock()

        self._mqtt = ensure_client(self.client_id)
        self._mqtt.on_connect = self._on_connect
        self._mqtt.on_disconnect = self._on_disconnect
        self._mqtt.on_message = self._on_message

        username = broker_cfg.get("username")
        password = broker_cfg.get("password")
        if username:
            self._mqtt.username_pw_set(username, password=password)

        self._host = str(broker_cfg.get("host", "127.0.0.1"))
        self._port = int(broker_cfg.get("port", 1883))
        self._keepalive = int(broker_cfg.get("keepalive", DEFAULT_KEEPALIVE))

    @staticmethod
    def _normalize_quest_interface(raw: Any) -> Dict[str, Any]:
        if not isinstance(raw, dict):
            return {}
        def normalize_params_schema(value: Any) -> list[Dict[str, Any]]:
            params: list[Dict[str, Any]] = []
            if not isinstance(value, list):
                return params
            for param in value:
                if not isinstance(param, dict):
                    continue
                key = str(param.get("key") or "").strip()
                if not key:
                    continue
                params.append(
                    {
                        "key": key,
                        "label": str(param.get("label") or key),
                        "type": str(param.get("type") or "text"),
                        "optional": bool(param.get("optional", False)),
                    }
                )
            return params

        commands = []
        for item in raw.get("commands", []):
            if not isinstance(item, dict):
                continue
            command_id = str(item.get("id") or "").strip()
            command_topic = str(item.get("topic") or "").strip()
            if not command_id or not command_topic:
                continue
            commands.append(
                {
                    "id": command_id,
                    "label": str(item.get("label") or command_id),
                    "kind": str(item.get("kind") or "mqtt_publish"),
                    "topic": command_topic,
                    "payload": str(item.get("payload") or ""),
                    "button_enabled": bool(item.get("button_enabled", True)),
                    "dangerous": bool(item.get("dangerous", False)),
                    "params_schema": normalize_params_schema(item.get("params_schema")),
                    "emit_event_id": str(item.get("emit_event_id") or "").strip(),
                    "emit_delay_ms": int(item.get("emit_delay_ms") or 0),
                }
            )

        events = []
        for item in raw.get("events", []):
            if not isinstance(item, dict):
                continue
            event_id = str(item.get("id") or "").strip()
            event_topic = str(item.get("topic") or "").strip()
            if not event_id or not event_topic:
                continue
            events.append(
                {
                    "id": event_id,
                    "label": str(item.get("label") or event_id),
                    "topic": event_topic,
                    "payload": str(item.get("payload") or ""),
                    "event_type": str(item.get("event_type") or event_id),
                }
            )

        if not commands and not events:
            return {}
        return {"version": int(raw.get("version") or 1), "commands": commands, "events": events}

    @staticmethod
    def _index_quest_commands(quest_interface: Dict[str, Any]) -> Dict[str, list[Dict[str, Any]]]:
        indexed: Dict[str, list[Dict[str, Any]]] = {}
        for command in quest_interface.get("commands", []):
            if not isinstance(command, dict):
                continue
            command_topic = str(command.get("topic") or "").strip()
            if not command_topic:
                continue
            indexed.setdefault(command_topic, []).append(command)
        return indexed

    def start(self) -> None:
        LOG.info("[%s] connecting to %s:%s", self.device_id, self._host, self._port)
        self._mqtt.connect(self._host, self._port, keepalive=self._keepalive)
        self._mqtt.loop_start()
        self._loop_thread.start()

    def stop(self) -> None:
        self._stop_event.set()
        if self._loop_thread.is_alive():
            self._loop_thread.join(timeout=2.0)
        self._mqtt.loop_stop()
        try:
            self._mqtt.disconnect()
        except Exception:
            pass

    def _subscribe_topics(self) -> None:
        for t in (
            topic(self.device_id, "control/command"),
            topic(self.device_id, "cmd"),
            topic("all", "control/command"),
            topic("all", "cmd"),
        ):
            self._mqtt.subscribe(t, qos=0)
        for t in self._quest_commands_by_topic:
            self._mqtt.subscribe(t, qos=0)
            LOG.info("[%s] subscribed quest command topic %s", self.device_id, t)

    def _on_connect(self, client: mqtt.Client, _userdata: Any, _flags: Dict[str, Any], rc: int) -> None:
        if rc != 0:
            LOG.error("[%s] connect failed rc=%s", self.device_id, rc)
            return
        self._connected = True
        self._subscribe_topics()
        LOG.info("[%s] connected and subscribed for commands", self.device_id)
        self.publish_status()
        self.publish_heartbeat()
        if self.force_fault:
            self.publish_diag(level="error", code="simulated_fault", message="Simulated fault is enabled.")
        elif self.force_degraded:
            self.publish_diag(level="warn", code="simulated_degraded", message="Simulated degraded mode is enabled.")

    def _on_disconnect(self, _client: mqtt.Client, _userdata: Any, rc: int) -> None:
        self._connected = False
        if not self._stop_event.is_set():
            LOG.warning("[%s] disconnected rc=%s", self.device_id, rc)

    def _on_message(self, _client: mqtt.Client, _userdata: Any, message: mqtt.MQTTMessage) -> None:
        try:
            payload_raw = message.payload.decode("utf-8")
        except Exception:
            LOG.warning("[%s] invalid command payload encoding", self.device_id)
            return

        if message.topic in self._quest_commands_by_topic:
            self._handle_quest_command_message(message.topic, payload_raw)
            return

        try:
            payload = json.loads(payload_raw)
        except json.JSONDecodeError:
            self.publish_result(
                request_id=f"invalid-{now_ms()}",
                command="unknown",
                status="error",
                error_code="invalid_request",
                message="Command payload is not valid JSON.",
            )
            return

        if not isinstance(payload, dict):
            self.publish_result(
                request_id=f"invalid-{now_ms()}",
                command="unknown",
                status="error",
                error_code="invalid_request",
                message="Command payload must be a JSON object.",
            )
            return

        threading.Thread(
            target=self._handle_command,
            kwargs={"payload": payload},
            daemon=True,
            name=f"dcc-cmd-{self.device_id}",
        ).start()

    def _effective_health(self) -> str:
        if self.force_fault:
            return "fault"
        if self.force_degraded:
            return "degraded"
        return self.runtime.health

    def _runtime_uptime_ms(self) -> int:
        if not self.runtime.online:
            return 0
        delta_s = max(0.0, time.monotonic() - self.runtime.started_at_monotonic)
        return int(delta_s * 1000)

    def _publish(self, suffix: str, data: Dict[str, Any], retain: bool = False) -> None:
        if self.silent_mode or not self.runtime.online or not self._connected:
            return
        body = json.dumps(data, ensure_ascii=False, separators=(",", ":"))
        self._mqtt.publish(topic(self.device_id, suffix), body, qos=0, retain=retain)

    def publish_heartbeat(self) -> None:
        with self._state_lock:
            payload = {
                "ts_ms": now_ms(),
                "boot_id": self.runtime.boot_id,
                "uptime_ms": self._runtime_uptime_ms(),
                "status_seq": self._status_seq,
            }
        self._publish("heartbeat", payload)

    def publish_status(self) -> None:
        with self._state_lock:
            self._status_seq += 1
            payload = {
                "ts_ms": now_ms(),
                "boot_id": self.runtime.boot_id,
                "fw_version": self.fw_version,
                "mode": self.runtime.mode,
                "state": self.runtime.state,
                "health": self._effective_health(),
                "capabilities": self.capabilities,
                "runtime": {"active": self.runtime.runtime_active},
            }
        self._publish("status", payload)

    def publish_diag(self, level: str, code: str, message: str, details: Optional[Dict[str, Any]] = None) -> None:
        payload: Dict[str, Any] = {
            "ts_ms": now_ms(),
            "level": level,
            "code": code,
            "message": message,
        }
        if details:
            payload["details"] = details
        self._publish("diag", payload)

    def publish_result(
        self,
        request_id: str,
        command: str,
        status: str,
        error_code: str = "",
        message: str = "",
        data: Optional[Dict[str, Any]] = None,
    ) -> None:
        payload = {
            "ts_ms": now_ms(),
            "request_id": request_id,
            "command": command,
            "status": status,
            "error_code": error_code,
            "message": message,
        }
        if data is not None:
            payload["data"] = data
        self._publish("result", payload)

    def _handle_command(self, payload: Dict[str, Any]) -> None:
        request_id = str(payload.get("request_id") or f"req-{now_ms()}")
        command = str(payload.get("command") or "").strip()
        args = payload.get("args")
        if not isinstance(args, dict):
            args = {}
        LOG.info("[%s] control command %s request_id=%s", self.device_id, command or "unknown", request_id)

        if self.response_delay_ms > 0:
            time.sleep(self.response_delay_ms / 1000.0)

        if command == "refresh_status":
            self.publish_status()
            self.publish_result(request_id, command, "ok")
            return

        if command == "reboot":
            self.publish_result(request_id, command, "accepted")
            self._fake_reboot()
            return

        if command == "reset_runtime":
            with self._state_lock:
                self.runtime.runtime_active = False
                self.runtime.state = "idle"
                self.runtime.last_error = ""
                self.force_fault = False
                self.force_degraded = False
            self.publish_status()
            self.publish_result(request_id, command, "ok")
            return

        if command == "apply_preset":
            preset_id = str(args.get("preset_id") or "").strip()
            if not preset_id:
                self.publish_result(request_id, command, "error", "invalid_request", "preset_id is required")
                return
            preset = self.presets.get(preset_id)
            if not preset:
                self.publish_result(request_id, command, "error", "not_supported", f"Preset '{preset_id}' not found")
                return
            with self._state_lock:
                self.runtime.preset = preset_id
                self.runtime.mode = str(preset.get("mode", self.runtime.mode))
                self.runtime.state = str(preset.get("state", self.runtime.state))
                self.runtime.health = str(preset.get("health", self.runtime.health))
                self.runtime.runtime_active = bool(preset.get("runtime_active", self.runtime.runtime_active))
            self.publish_status()
            self.publish_result(request_id, command, "ok")
            return

        if command == "describe_interface":
            if not self.quest_interface:
                LOG.warning("[%s] describe_interface requested but no quest_interface configured", self.device_id)
                self.publish_result(request_id, command, "error", "not_supported", "No quest interface configured")
                return
            LOG.info(
                "[%s] describe_interface returning %d command(s), %d event(s)",
                self.device_id,
                len(self.quest_interface.get("commands", [])),
                len(self.quest_interface.get("events", [])),
            )
            self.publish_result(
                request_id,
                command,
                "ok",
                data={"quest_interface": self.quest_interface},
            )
            return

        self.publish_result(request_id, command or "unknown", "error", "not_supported", "Unsupported command")

    def _handle_quest_command_message(self, command_topic: str, payload_raw: str) -> None:
        commands = self._quest_commands_by_topic.get(command_topic, [])
        matched = None
        for command in commands:
            expected_payload = str(command.get("payload") or "")
            if not expected_payload or expected_payload == payload_raw:
                matched = command
                break
        if not matched:
            LOG.info("[%s] ignored quest command topic=%s payload=%s", self.device_id, command_topic, payload_raw)
            return

        command_id = str(matched.get("id") or command_topic)
        LOG.info("[%s] quest command %s topic=%s payload=%s", self.device_id, command_id, command_topic, payload_raw)
        with self._state_lock:
            self.runtime.runtime_active = True
            self.runtime.state = f"quest:{command_id}"
        self.publish_status()

        emit_event_id = str(matched.get("emit_event_id") or "").strip()
        if emit_event_id:
            delay_ms = int(matched.get("emit_delay_ms") or 0)
            if delay_ms > 0:
                time.sleep(delay_ms / 1000.0)
            self.publish_quest_event(emit_event_id)

    def publish_quest_event(self, event_id: str, payload_override: Optional[str] = None) -> bool:
        event = self._quest_events_by_id.get(event_id)
        if not event:
            return False
        payload = str(event.get("payload") or "") if payload_override is None else str(payload_override)
        event_topic = str(event.get("topic") or "")
        if not event_topic:
            return False
        if self.silent_mode or not self.runtime.online or not self._connected:
            return False
        self._mqtt.publish(event_topic, payload, qos=0, retain=False)
        with self._state_lock:
            self.runtime.runtime_active = False
            self.runtime.state = f"event:{event_id}"
        self.publish_status()
        LOG.info("[%s] quest event %s topic=%s payload=%s", self.device_id, event_id, event_topic, payload)
        return True

    def _fake_reboot(self) -> None:
        with self._state_lock:
            self.runtime.online = False
            self.runtime.state = "rebooting"
        LOG.info("[%s] fake reboot started", self.device_id)

        time.sleep(max(0, self.fake_reboot_ms) / 1000.0)

        with self._state_lock:
            self.runtime.online = True
            self.runtime.boot_id = make_boot_id()
            self.runtime.started_at_monotonic = time.monotonic()
            self.runtime.state = "idle"
            self.runtime.last_error = ""
        LOG.info("[%s] fake reboot finished, boot_id=%s", self.device_id, self.runtime.boot_id)
        self.publish_status()
        self.publish_heartbeat()

    def _run_loop(self) -> None:
        next_hb = time.monotonic()
        next_status = time.monotonic()
        next_diag = time.monotonic()

        while not self._stop_event.is_set():
            now_monotonic = time.monotonic()
            if now_monotonic >= next_hb:
                self.publish_heartbeat()
                next_hb = now_monotonic + max(0.1, self.heartbeat_interval_ms / 1000.0)

            if now_monotonic >= next_status:
                self.publish_status()
                next_status = now_monotonic + max(0.2, self.status_interval_ms / 1000.0)

            if self.diag_interval_ms > 0 and now_monotonic >= next_diag:
                if self.force_fault:
                    self.publish_diag("error", "simulated_fault", "Periodic simulated fault heartbeat.")
                elif self.force_degraded:
                    self.publish_diag("warn", "simulated_degraded", "Periodic simulated degraded heartbeat.")
                next_diag = now_monotonic + max(0.5, self.diag_interval_ms / 1000.0)

            time.sleep(0.05)

    def snapshot(self) -> Dict[str, Any]:
        with self._state_lock:
            return {
                "device_id": self.device_id,
                "online": self.runtime.online,
                "mode": self.runtime.mode,
                "state": self.runtime.state,
                "health": self._effective_health(),
                "force_fault": self.force_fault,
                "force_degraded": self.force_degraded,
                "silent_mode": self.silent_mode,
                "response_delay_ms": self.response_delay_ms,
                "preset": self.runtime.preset,
                "boot_id": self.runtime.boot_id,
                "uptime_ms": self._runtime_uptime_ms(),
                "quest_commands": len(self.quest_interface.get("commands", [])),
                "quest_events": len(self.quest_interface.get("events", [])),
            }

    def set_fault(self, enabled: bool) -> None:
        with self._state_lock:
            self.force_fault = enabled
            if enabled:
                self.force_degraded = False
                self.runtime.last_error = "simulated_fault"
            elif self.runtime.last_error == "simulated_fault":
                self.runtime.last_error = ""
        self.publish_status()

    def set_degraded(self, enabled: bool) -> None:
        with self._state_lock:
            self.force_degraded = enabled
            if enabled:
                self.force_fault = False
                self.runtime.last_error = "simulated_degraded"
            elif self.runtime.last_error == "simulated_degraded":
                self.runtime.last_error = ""
        self.publish_status()

    def set_silent(self, enabled: bool) -> None:
        with self._state_lock:
            self.silent_mode = enabled
        if not enabled:
            self.publish_status()
            self.publish_heartbeat()

    def set_response_delay(self, delay_ms: int) -> None:
        with self._state_lock:
            self.response_delay_ms = max(0, delay_ms)

    def set_preset(self, preset_id: str) -> bool:
        preset = self.presets.get(preset_id)
        if not preset:
            return False
        with self._state_lock:
            self.runtime.preset = preset_id
            self.runtime.mode = str(preset.get("mode", self.runtime.mode))
            self.runtime.state = str(preset.get("state", self.runtime.state))
            self.runtime.health = str(preset.get("health", self.runtime.health))
            self.runtime.runtime_active = bool(preset.get("runtime_active", self.runtime.runtime_active))
        self.publish_status()
        return True

    def trigger_fake_reboot(self) -> None:
        threading.Thread(
            target=self._fake_reboot,
            name=f"dcc-reboot-{self.device_id}",
            daemon=True,
        ).start()

    def publish_now(self, kind: str) -> None:
        if kind in ("hb", "heartbeat"):
            self.publish_heartbeat()
            return
        if kind == "status":
            self.publish_status()
            return
        if kind == "diag":
            if self.force_fault:
                self.publish_diag("error", "simulated_fault", "Manual simulated fault diag.")
            elif self.force_degraded:
                self.publish_diag("warn", "simulated_degraded", "Manual simulated degraded diag.")
            else:
                self.publish_diag("info", "manual_diag", "Manual diagnostic heartbeat.")
            return
        raise ValueError(f"Unsupported publish kind: {kind}")


def _parse_switch(token: str, current: bool) -> bool:
    value = token.strip().lower()
    if value in ("on", "1", "true", "yes"):
        return True
    if value in ("off", "0", "false", "no"):
        return False
    if value in ("toggle",):
        return not current
    raise ValueError("Expected on/off/toggle")


def _resolve_targets(devices_map: Dict[str, EmulatedDevice], target: str) -> list[EmulatedDevice]:
    if target == "all":
        return list(devices_map.values())
    device = devices_map.get(target)
    return [device] if device else []


def _print_help() -> None:
    print(
        "\nConsole commands:\n"
        "  help\n"
        "  list\n"
        "  show <device_id|all>\n"
        "  fault <device_id|all> <on|off|toggle>\n"
        "  degraded <device_id|all> <on|off|toggle>\n"
        "  silent <device_id|all> <on|off|toggle>\n"
        "  delay <device_id|all> <ms>\n"
        "  reboot <device_id|all>\n"
        "  preset <device_id|all> <preset_id>\n"
        "  publish <device_id|all> <hb|status|diag>\n"
        "  event <device_id|all> <event_id> [payload]\n"
        "  quit / exit\n"
    )


def _run_console(devices: list[EmulatedDevice]) -> None:
    devices_map = {dev.device_id: dev for dev in devices}
    _print_help()
    while True:
        try:
            line = input("dcc> ").strip()
        except EOFError:
            print()
            break
        except KeyboardInterrupt:
            print()
            break

        if not line:
            continue
        try:
            parts = shlex.split(line)
        except ValueError as err:
            print(f"parse error: {err}")
            continue
        cmd = parts[0].lower()
        if cmd in ("quit", "exit"):
            break
        if cmd == "help":
            _print_help()
            continue
        if cmd == "list":
            for dev in devices:
                snap = dev.snapshot()
                print(
                    f"{snap['device_id']}: health={snap['health']} "
                    f"silent={snap['silent_mode']} delay={snap['response_delay_ms']}ms "
                    f"preset={snap['preset']}"
                )
            continue
        if cmd == "show" and len(parts) >= 2:
            targets = _resolve_targets(devices_map, parts[1])
            if not targets:
                print("unknown target")
                continue
            for dev in targets:
                print(json.dumps(dev.snapshot(), ensure_ascii=False, indent=2))
            continue
        if cmd in ("fault", "degraded", "silent") and len(parts) >= 3:
            targets = _resolve_targets(devices_map, parts[1])
            if not targets:
                print("unknown target")
                continue
            for dev in targets:
                snap = dev.snapshot()
                current = bool(snap["force_fault"] if cmd == "fault" else snap["force_degraded"] if cmd == "degraded" else snap["silent_mode"])
                try:
                    enabled = _parse_switch(parts[2], current)
                except ValueError as err:
                    print(str(err))
                    break
                if cmd == "fault":
                    dev.set_fault(enabled)
                elif cmd == "degraded":
                    dev.set_degraded(enabled)
                else:
                    dev.set_silent(enabled)
                print(f"{dev.device_id}: {cmd}={enabled}")
            continue
        if cmd == "delay" and len(parts) >= 3:
            targets = _resolve_targets(devices_map, parts[1])
            if not targets:
                print("unknown target")
                continue
            try:
                value = int(parts[2])
            except ValueError:
                print("delay must be integer milliseconds")
                continue
            for dev in targets:
                dev.set_response_delay(value)
                print(f"{dev.device_id}: delay={max(0, value)}ms")
            continue
        if cmd == "reboot" and len(parts) >= 2:
            targets = _resolve_targets(devices_map, parts[1])
            if not targets:
                print("unknown target")
                continue
            for dev in targets:
                dev.trigger_fake_reboot()
                print(f"{dev.device_id}: reboot triggered")
            continue
        if cmd == "preset" and len(parts) >= 3:
            targets = _resolve_targets(devices_map, parts[1])
            if not targets:
                print("unknown target")
                continue
            preset_id = parts[2]
            for dev in targets:
                ok = dev.set_preset(preset_id)
                if ok:
                    print(f"{dev.device_id}: preset={preset_id}")
                else:
                    print(f"{dev.device_id}: preset '{preset_id}' not found")
            continue
        if cmd == "publish" and len(parts) >= 3:
            targets = _resolve_targets(devices_map, parts[1])
            if not targets:
                print("unknown target")
                continue
            for dev in targets:
                try:
                    dev.publish_now(parts[2].lower())
                    print(f"{dev.device_id}: published {parts[2]}")
                except ValueError as err:
                    print(str(err))
                    break
            continue
        if cmd == "event" and len(parts) >= 3:
            targets = _resolve_targets(devices_map, parts[1])
            if not targets:
                print("unknown target")
                continue
            payload_override = parts[3] if len(parts) >= 4 else None
            for dev in targets:
                ok = dev.publish_quest_event(parts[2], payload_override)
                if ok:
                    print(f"{dev.device_id}: quest event {parts[2]} published")
                else:
                    print(f"{dev.device_id}: quest event '{parts[2]}' not found or not published")
            continue

        print("unknown command; type 'help'")


def load_config(path: Path) -> Dict[str, Any]:
    raw = path.read_text(encoding="utf-8")
    config = json.loads(raw)
    if not isinstance(config, dict):
        raise ValueError("Config root must be an object.")
    if "devices" not in config or not isinstance(config["devices"], list) or not config["devices"]:
        raise ValueError("Config must contain non-empty 'devices' list.")
    return config


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Reference client for device control MQTT contract.")
    parser.add_argument(
        "--config",
        default=str(Path(__file__).with_name("config_example.json")),
        help="Path to config JSON.",
    )
    parser.add_argument("--device", action="append", help="Run only selected device_id (repeatable).")
    parser.add_argument(
        "--log-level",
        default="INFO",
        choices=["DEBUG", "INFO", "WARNING", "ERROR"],
        help="Logging verbosity.",
    )
    parser.add_argument(
        "--no-console",
        action="store_true",
        help="Disable interactive console and run as background sender only.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    logging.basicConfig(
        level=getattr(logging, args.log_level),
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )

    cfg = load_config(Path(args.config))
    broker_cfg = cfg.get("broker") or {}
    defaults = cfg.get("defaults") or {}
    selected = set(args.device or [])

    devices: list[EmulatedDevice] = []
    for dev_cfg in cfg["devices"]:
        if not isinstance(dev_cfg, dict):
            continue
        device_id = str(dev_cfg.get("device_id") or "")
        if not device_id:
            continue
        if selected and device_id not in selected:
            continue
        devices.append(EmulatedDevice(broker_cfg, defaults, dev_cfg))

    if not devices:
        LOG.error("No devices selected to run.")
        return 2

    LOG.info("Starting %d emulated device(s)", len(devices))
    for dev in devices:
        dev.start()

    try:
        if args.no_console:
            while True:
                time.sleep(1.0)
        else:
            _run_console(devices)
    except KeyboardInterrupt:
        LOG.info("Stopping client...")
    finally:
        for dev in devices:
            dev.stop()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
