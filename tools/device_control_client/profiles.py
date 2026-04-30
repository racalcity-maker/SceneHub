"""Profile templates for the device control reference client."""

from __future__ import annotations

from copy import deepcopy
from typing import Any, Dict


MANDATORY_CAPABILITIES = ["heartbeat", "status", "diag", "refresh_status"]
OPTIONAL_CAPABILITIES = ["reboot", "reset_runtime", "apply_preset"]


PROFILE_TEMPLATES: Dict[str, Dict[str, Any]] = {
    "generic": {
        "mode": "normal",
        "state": "idle",
        "health": "ok",
        "runtime_active": False,
        "capabilities": MANDATORY_CAPABILITIES + OPTIONAL_CAPABILITIES,
        "presets": {
            "normal": {"mode": "normal", "state": "idle", "health": "ok"},
            "maintenance": {"mode": "maintenance", "state": "idle", "health": "degraded"},
        },
    },
    "sensor_beam": {
        "mode": "armed",
        "state": "watching",
        "health": "ok",
        "runtime_active": True,
        "capabilities": MANDATORY_CAPABILITIES + ["reboot", "reset_runtime"],
        "presets": {
            "armed": {"mode": "armed", "state": "watching", "health": "ok"},
            "service": {"mode": "service", "state": "idle", "health": "degraded"},
        },
    },
    "relay_node": {
        "mode": "normal",
        "state": "ready",
        "health": "ok",
        "runtime_active": False,
        "capabilities": MANDATORY_CAPABILITIES + OPTIONAL_CAPABILITIES,
        "presets": {
            "normal": {"mode": "normal", "state": "ready", "health": "ok"},
            "blocked": {"mode": "normal", "state": "blocked", "health": "fault"},
        },
    },
}


def list_profiles() -> list[str]:
    return sorted(PROFILE_TEMPLATES.keys())


def resolve_profile(profile_name: str) -> Dict[str, Any]:
    """Return a deep-copied profile; fallback to generic."""
    profile = PROFILE_TEMPLATES.get(profile_name) or PROFILE_TEMPLATES["generic"]
    return deepcopy(profile)
