import { HttpError } from "@/platform/http/http_error";
import {
  findMockProfile,
  findMockScenario,
  mockGmStateResponse,
  mockMetaResponse,
  mockSessionInfoResponse,
  mockRoomProfilesResponses,
  mockRoomRuntimeResponses,
  mockRoomScenariosResponses,
  recalculateMockSummary,
  setMockSessionInfoResponse,
  syncMockDashboardRoom,
} from "@/platform/http/mock/mock_store";

function applyMockRoomCommand(path: string, body?: unknown) {
  const queryIndex = path.indexOf("?");
  const pathname = queryIndex >= 0 ? path.slice(0, queryIndex) : path;
  const query = queryIndex >= 0 ? path.slice(queryIndex + 1) : "";
  const params = new URLSearchParams(query);

  switch (pathname) {
    case "/api/gm/room/profile/select": {
      const payload = (body ?? {}) as { room_id?: string; profile_id?: string };
      const targetRoomId = payload.room_id ?? params.get("room_id") ?? "";
      const runtimeTarget = mockRoomRuntimeResponses[targetRoomId];
      const selectedProfile = payload.profile_id
        ? findMockProfile(targetRoomId, payload.profile_id)
        : null;

      if (!runtimeTarget || !selectedProfile) {
        throw new HttpError(`No mock profile configured for ${targetRoomId}`, 404);
      }

      const selectedScenario =
        findMockScenario(targetRoomId, selectedProfile.scenario_id) ??
        findMockScenario(targetRoomId, runtimeTarget.selected_scenario_id);

      runtimeTarget.selected_profile_id = selectedProfile.id;
      runtimeTarget.selected_profile_name = selectedProfile.name;
      runtimeTarget.selected_profile_scenario_id = selectedProfile.scenario_id;
      runtimeTarget.selected_profile_duration_ms = selectedProfile.duration_ms;
      runtimeTarget.selected_scenario_id = selectedProfile.scenario_id;
      runtimeTarget.selected_scenario_name = selectedScenario?.name ?? selectedProfile.scenario_id;
      runtimeTarget.timer_duration_ms = selectedProfile.duration_ms;
      if (!runtimeTarget.session_present || runtimeTarget.session_state === "idle") {
        runtimeTarget.timer_remaining_ms = selectedProfile.duration_ms;
      }
      mockRoomProfilesResponses[targetRoomId].selected_profile_id = selectedProfile.id;
      syncMockDashboardRoom(targetRoomId);
      recalculateMockSummary();
      return;
    }
    case "/api/gm/room/scenario/select": {
      const payload = (body ?? {}) as { room_id?: string; scenario_id?: string };
      const targetRoomId = payload.room_id ?? params.get("room_id") ?? "";
      const runtimeTarget = mockRoomRuntimeResponses[targetRoomId];
      const selectedScenario = payload.scenario_id
        ? findMockScenario(targetRoomId, payload.scenario_id)
        : null;

      if (!runtimeTarget || !selectedScenario) {
        throw new HttpError(`No mock scenario configured for ${targetRoomId}`, 404);
      }

      runtimeTarget.selected_scenario_id = selectedScenario.id;
      runtimeTarget.selected_scenario_name = selectedScenario.name;
      syncMockDashboardRoom(targetRoomId);
      recalculateMockSummary();
      return;
    }
  }

  const roomId = params.get("room_id") ?? "";
  const runtime = mockRoomRuntimeResponses[roomId];

  if (!runtime) {
    throw new HttpError(`No mock room runtime configured for ${roomId || "unknown room"}`, 404);
  }

  switch (pathname) {
    case "/api/gm/room/game/start":
      runtime.session_present = true;
      runtime.session_state = "running";
      runtime.timer_state = "running";
      runtime.timer_duration_ms =
        runtime.timer_duration_ms || runtime.selected_profile_duration_ms || 3600000;
      runtime.timer_remaining_ms =
        runtime.timer_remaining_ms || runtime.timer_duration_ms || runtime.selected_profile_duration_ms;
      break;
    case "/api/gm/room/game/stop":
      runtime.session_present = true;
      runtime.session_state = "paused";
      runtime.timer_state = "paused";
      break;
    case "/api/gm/room/game/reset":
      runtime.session_present = false;
      runtime.session_state = "idle";
      runtime.timer_state = "idle";
      runtime.timer_remaining_ms = runtime.selected_profile_duration_ms || runtime.timer_duration_ms;
      runtime.scenario_runtime_state = "idle";
      runtime.scenario_current_step_index = 0;
      runtime.running_scenario_id = "";
      runtime.running_scenario_name = "";
      runtime.scenario_operator_message = "";
      runtime.scenario_wait_type = "none";
      break;
    case "/api/gm/room/scenario/start":
      runtime.running_scenario_id = runtime.selected_scenario_id;
      runtime.running_scenario_name = runtime.selected_scenario_name;
      runtime.scenario_runtime_state = "running";
      runtime.scenario_operator_message = "Scenario started";
      runtime.scenario_wait_type = "none";
      break;
    case "/api/gm/room/scenario/stop":
      runtime.scenario_runtime_state = "idle";
      runtime.scenario_operator_message = "";
      runtime.scenario_wait_type = "none";
      break;
    case "/api/gm/room/scenario/approve":
    case "/api/gm/room/scenario/next":
      runtime.scenario_current_step_index += 1;
      runtime.scenario_runtime_state = "running";
      runtime.scenario_operator_message = "Scenario advanced";
      runtime.scenario_wait_type = "none";
      runtime.scenario_wait_events = [];
      runtime.scenario_wait_event_count = 0;
      break;
    case "/api/gm/room/scenario/reset":
      runtime.scenario_runtime_state = "idle";
      runtime.scenario_current_step_index = 0;
      runtime.scenario_operator_message = "";
      runtime.scenario_wait_type = "none";
      runtime.scenario_wait_events = [];
      runtime.scenario_wait_event_count = 0;
      break;
    case "/api/gm/room/timer/start": {
      const durationMs = Number(params.get("duration_ms") ?? "0");
      runtime.timer_duration_ms = durationMs;
      runtime.timer_remaining_ms = durationMs;
      runtime.timer_state = "running";
      break;
    }
    case "/api/gm/room/timer/pause":
      runtime.timer_state = "paused";
      break;
    case "/api/gm/room/timer/resume":
      runtime.timer_state = "running";
      break;
    case "/api/gm/room/timer/reset": {
      const durationMs = Number(params.get("duration_ms") ?? String(runtime.timer_duration_ms));
      runtime.timer_duration_ms = durationMs;
      runtime.timer_remaining_ms = durationMs;
      runtime.timer_state = "idle";
      break;
    }
    case "/api/gm/room/timer/add": {
      const deltaMs = Number(params.get("delta_ms") ?? "0");
      runtime.timer_remaining_ms = Math.max(0, runtime.timer_remaining_ms + deltaMs);
      break;
    }
    case "/api/gm/room/hint/send": {
      const payload = (body ?? {}) as { message?: string };
      runtime.hint_active = true;
      runtime.hint_sent_count += 1;
      runtime.hint_message = payload.message ?? runtime.hint_message;
      break;
    }
    case "/api/gm/room/hint/clear":
      runtime.hint_active = false;
      runtime.hint_message = "";
      break;
    default:
      throw new HttpError(`No mock command configured for ${pathname}`, 400);
  }

  syncMockDashboardRoom(roomId);
  recalculateMockSummary();
}

export async function readMockResponse(path: string): Promise<unknown> {
  if (path === "/api/meta") {
    return mockMetaResponse;
  }

  if (path === "/api/session/info") {
    return mockSessionInfoResponse;
  }

  if (path === "/api/gm/state") {
    return mockGmStateResponse;
  }

  if (path.startsWith("/api/gm/room/runtime")) {
    const queryIndex = path.indexOf("?");
    const query = queryIndex >= 0 ? path.slice(queryIndex + 1) : "";
    const roomId = new URLSearchParams(query).get("room_id") ?? "";
    const runtime = mockRoomRuntimeResponses[roomId];

    if (runtime) {
      return runtime;
    }

    throw new HttpError(`No mock room runtime configured for ${roomId || "unknown room"}`, 404);
  }

  if (path.startsWith("/api/gm/room/profiles")) {
    const queryIndex = path.indexOf("?");
    const query = queryIndex >= 0 ? path.slice(queryIndex + 1) : "";
    const roomId = new URLSearchParams(query).get("room_id") ?? "";
    const profiles = mockRoomProfilesResponses[roomId];

    if (profiles) {
      return profiles;
    }

    throw new HttpError(`No mock room profiles configured for ${roomId || "unknown room"}`, 404);
  }

  if (path.startsWith("/api/gm/room/scenarios")) {
    const queryIndex = path.indexOf("?");
    const query = queryIndex >= 0 ? path.slice(queryIndex + 1) : "";
    const roomId = new URLSearchParams(query).get("room_id") ?? "";
    const scenarios = mockRoomScenariosResponses[roomId];

    if (scenarios) {
      return scenarios;
    }

    throw new HttpError(`No mock room scenarios configured for ${roomId || "unknown room"}`, 404);
  }

  throw new HttpError(`No mock response configured for ${path}`);
}

export async function writeMockResponse(path: string, body?: unknown): Promise<unknown> {
  if (path === "/api/auth/login") {
    const payload = (body ?? {}) as { username?: string; password?: string };
    if (!payload.username || !payload.password) {
      throw new HttpError("missing fields", 400);
    }
    setMockSessionInfoResponse({
      role: payload.username === "operator" ? "user" : "admin",
      username: payload.username,
    });
    return { status: "ok", role: mockSessionInfoResponse.role };
  }

  if (path === "/api/auth/logout") {
    setMockSessionInfoResponse({
      role: "admin",
      username: "mock_admin",
    });
    return { status: "ok" };
  }

  if (path.startsWith("/api/gm/room/")) {
    applyMockRoomCommand(path, body);
    if (path === "/api/gm/room/profile/select") {
      const payload = (body ?? {}) as { room_id?: string; profile_id?: string };
      return {
        ok: true,
        room_id: payload.room_id ?? "",
        selected_profile_id: payload.profile_id ?? "",
      };
    }
    if (path === "/api/gm/room/scenario/select") {
      const payload = (body ?? {}) as { room_id?: string; scenario_id?: string };
      return {
        ok: true,
        room_id: payload.room_id ?? "",
        selected_scenario_id: payload.scenario_id ?? "",
      };
    }
    return { ok: true, accepted: true };
  }

  throw new HttpError(`No mock command configured for ${path}`);
}
