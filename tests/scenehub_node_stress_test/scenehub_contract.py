"""SceneHub Node v1 contract used by the stress emulator."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Optional


CAPABILITIES = [
    "heartbeat", "status", "describe_interface", "node.identify",
    "node.get_status", "relay.set", "relay.pulse", "relay.all_off",
    "mosfet.set", "mosfet.fade", "mosfet.pulse", "mosfet.blink",
    "mosfet.breathe", "mosfet.all_off", "mosfet.effect", "io.set",
    "io.all_off", "node.all_off", "led.off", "led.solid", "led.blink",
    "led.breathe", "led.effect", "input.changed",
]
LED_EFFECTS = [
    "rainbow", "rainbow_cycle", "color_wipe", "scanner", "theater_chase",
    "strobe", "pulse", "fade_in_out", "twinkle", "twinkle_random",
    "sparkle", "glitter", "comet", "larson", "running_lights",
    "fire_flicker", "chase_dual", "chase_single", "bounce", "breath_wave",
]


def policy(result_required: bool = False, confirm: bool = False) -> dict[str, Any]:
    return {
        "manual_allowed": True,
        "scenario_allowed": True,
        "requires_confirmation": confirm,
        "result_required": result_required,
        "timeout_ms": 3000,
        "danger_level": "normal",
    }


def command_template(
    command: str,
    target: str,
    schema: str,
    default_args: Optional[dict[str, Any]] = None,
    result_required: bool = False,
    confirm: bool = False,
) -> dict[str, Any]:
    item: dict[str, Any] = {
        "id": command,
        "label": command,
        "target": target,
        "command": command,
        "args_schema_ref": schema,
        "policy": policy(result_required, confirm),
    }
    if default_args is not None:
        item["default_args"] = default_args
    return item


def build_manifest(node_id: str, node_name: str) -> dict[str, Any]:
    resources = {
        "relays": [{"channel": i, "label": f"Relay {i}"} for i in range(1, 5)],
        "mosfets": [{"channel": i, "label": f"MOSFET {i}"} for i in range(1, 5)],
        "inputs": [
            {"channel": i, "label": f"Input {i}", "event": "input.changed"}
            for i in range(1, 5)
        ],
        "outputs": [{"channel": i, "label": f"Output {i}"} for i in range(1, 5)],
        "led_strips": [
            {
                "strip": i,
                "pixels": 30,
                "chipset": "ws2812",
                "color_order": "grb",
                "rgbw": False,
                "label": f"LED Strip {i}",
            }
            for i in range(1, 3)
        ],
    }
    templates = [
        command_template("relay.set", "relays", "output_set", {"on": True}),
        command_template("relay.pulse", "relays", "pulse", {"duration_ms": 300}, True),
        command_template("relay.all_off", "relays", "none"),
        command_template("mosfet.set", "mosfets", "mosfet_set", {"value": 255}),
        command_template("mosfet.fade", "mosfets", "mosfet_fade", {"target": 255}),
        command_template("mosfet.pulse", "mosfets", "mosfet_pulse", {"value": 255}),
        command_template("mosfet.blink", "mosfets", "mosfet_blink", {"value": 255}),
        command_template("mosfet.breathe", "mosfets", "mosfet_breathe", {}),
        command_template("mosfet.all_off", "mosfets", "none"),
        command_template(
            "mosfet.effect", "mosfets", "mosfet_effect",
            {"effect": "set", "value": 255},
        ),
        command_template("io.set", "outputs", "output_set", {"on": True}),
        command_template("io.all_off", "outputs", "none"),
        command_template("node.all_off", "device", "none", confirm=True),
        command_template("led.off", "led_strips", "led_strip_only"),
        command_template("led.solid", "led_strips", "led_solid", {"color": "#ffffff"}),
        command_template(
            "led.blink", "led_strips", "led_blink",
            {"color": "#ffffff", "times": 1},
        ),
        command_template(
            "led.breathe", "led_strips", "led_breathe", {"color": "#ffffff"},
        ),
        command_template(
            "led.effect", "led_strips", "led_effect", {"effect": "rainbow"},
        ),
    ]
    schemas = {
        "output_set": [
            {"key": "channel", "type": "resource_channel"},
            {"key": "on", "type": "checkbox"},
        ],
        "pulse": [
            {"key": "channel", "type": "resource_channel"},
            {"key": "duration_ms", "type": "number"},
        ],
        "mosfet_set": [
            {"key": "channel", "type": "resource_channel"},
            {"key": "value", "type": "number"},
        ],
        "mosfet_fade": [
            {"key": "channel", "type": "resource_channel"},
            {"key": "target", "type": "number", "optional": True},
        ],
        "mosfet_pulse": [
            {"key": "channel", "type": "resource_channel"},
            {"key": "value", "type": "number", "optional": True},
        ],
        "mosfet_blink": [
            {"key": "channel", "type": "resource_channel"},
            {"key": "value", "type": "number", "optional": True},
        ],
        "mosfet_breathe": [{"key": "channel", "type": "resource_channel"}],
        "mosfet_effect": [
            {"key": "channel", "type": "resource_channel"},
            {
                "key": "effect",
                "type": "select",
                "options": ["set", "pulse", "blink", "fade", "fade_in", "fade_out", "breathe"],
            },
        ],
        "led_strip_only": [{"key": "channel", "type": "resource_channel"}],
        "led_solid": [
            {"key": "channel", "type": "resource_channel"},
            {"key": "color", "type": "text"},
        ],
        "led_blink": [
            {"key": "channel", "type": "resource_channel"},
            {"key": "color", "type": "text"},
            {"key": "times", "type": "number"},
        ],
        "led_breathe": [
            {"key": "channel", "type": "resource_channel"},
            {"key": "color", "type": "text"},
        ],
        "led_effect": [
            {"key": "channel", "type": "resource_channel"},
            {"key": "effect", "type": "select", "options": LED_EFFECTS},
        ],
        "input_event": [
            {"key": "channel", "type": "resource_channel"},
            {"key": "value", "type": "number", "optional": True},
        ],
        "none": [],
    }
    return {
        "manifest_version": 2,
        "format": "compact_resources",
        "node_kind": "scenehub_node",
        "capability_contract": "scenehub.node.compact.v1",
        "device": {"id": node_id, "name": node_name, "kind": "scenehub_node"},
        "resources": resources,
        "command_templates": templates,
        "event_templates": [{
            "id": "input.changed",
            "label": "Input changed",
            "source": "inputs",
            "event": "input.changed",
            "args_schema_ref": "input_event",
        }],
        "schemas": schemas,
    }


@dataclass(frozen=True)
class CommandCase:
    command: str
    args: dict[str, Any]
    expected_statuses: set[str]
    expected_errors: set[str] = field(default_factory=set)


NORMAL_CASES = [
    CommandCase("node.get_status", {}, {"done"}),
    CommandCase("node.identify", {}, {"done"}),
    CommandCase("relay.set", {"channel": 1, "on": True}, {"started"}),
    CommandCase("relay.pulse", {"channel": 4, "duration_ms": 300}, {"started"}),
    CommandCase("relay.all_off", {}, {"started"}),
    CommandCase("mosfet.set", {"channel": 1, "value": 255}, {"started"}),
    CommandCase("mosfet.fade", {"channel": 2, "target": 64, "duration_ms": 500}, {"started"}),
    CommandCase("mosfet.pulse", {"channel": 3, "value": 128, "duration_ms": 300}, {"started"}),
    CommandCase("mosfet.blink", {"channel": 4, "value": 200}, {"started"}),
    CommandCase("mosfet.breathe", {"channel": 1}, {"started"}),
    CommandCase("mosfet.effect", {"channel": 2, "effect": "fade_out", "duration_ms": 250}, {"done"}),
    CommandCase("io.set", {"channel": 4, "on": True}, {"started"}),
    CommandCase("led.off", {"channel": 1}, {"done"}),
    CommandCase("led.solid", {"channel": 2, "color": "#ff0080"}, {"done"}),
    CommandCase("led.blink", {"channel": 1, "color": "#00ff00", "times": 2}, {"started"}),
    CommandCase("led.breathe", {"channel": 2, "color": "#0000ff"}, {"started"}),
    CommandCase("led.effect", {"channel": 1, "effect": "rainbow"}, {"started"}),
    CommandCase("node.all_off", {}, {"started"}),
]

PROBLEM_CASES = [
    CommandCase("unknown.command", {}, {"rejected"}, {"not_supported"}),
    CommandCase("relay.set", {"channel": 1}, {"rejected"}, {"missing_on"}),
    CommandCase("relay.set", {"channel": 0, "on": True}, {"rejected"}, {"invalid_channel"}),
    CommandCase("relay.set", {"channel": 5, "on": True}, {"rejected"}, {"invalid_channel"}),
    CommandCase("relay.pulse", {"channel": 1, "duration_ms": 0}, {"rejected"}, {"missing_duration_ms"}),
    CommandCase("mosfet.set", {"channel": 1, "value": -1}, {"rejected"}, {"invalid_args"}),
    CommandCase("mosfet.set", {"channel": 1, "value": 256}, {"rejected"}, {"invalid_args"}),
    CommandCase("mosfet.fade", {"channel": 1, "duration_ms": 60001}, {"rejected"}, {"invalid_args"}),
    CommandCase("mosfet.pulse", {"channel": 1, "duration_ms": 60001}, {"rejected"}, {"invalid_args"}),
    CommandCase("mosfet.effect", {"channel": 1, "effect": "explode"}, {"rejected"}, {"invalid_args"}),
    CommandCase("io.set", {"channel": 1, "on": "true"}, {"rejected"}, {"missing_on"}),
    CommandCase("led.solid", {"channel": 1, "color": "red"}, {"rejected"}, {"invalid_args"}),
    CommandCase("led.effect", {"channel": 3, "effect": "rainbow"}, {"rejected"}, {"invalid_channel"}),
    CommandCase("led.effect", {"channel": 1, "effect": "invalid"}, {"rejected"}, {"invalid_args"}),
]
