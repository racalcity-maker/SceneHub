import { z } from "zod";
import { httpPostJson } from "@/platform/http/client";

export const gmRoomCommandResultSchema = z
  .object({
    ok: z.boolean(),
    accepted: z.boolean().optional(),
    room_id: z.string().optional(),
    action_id: z.string().optional(),
  })
  .passthrough();

export type GmRoomCommandResult = z.infer<typeof gmRoomCommandResultSchema>;

function encodedRoomPath(roomId: string, path: string): string {
  return `${path}?room_id=${encodeURIComponent(roomId)}`;
}

export function startGame(baseUrl: string, roomId: string) {
  return httpPostJson(baseUrl, encodedRoomPath(roomId, "/api/gm/room/game/start"), gmRoomCommandResultSchema);
}

export function stopGame(baseUrl: string, roomId: string) {
  return httpPostJson(baseUrl, encodedRoomPath(roomId, "/api/gm/room/game/stop"), gmRoomCommandResultSchema);
}

export function resetGame(baseUrl: string, roomId: string) {
  return httpPostJson(baseUrl, encodedRoomPath(roomId, "/api/gm/room/game/reset"), gmRoomCommandResultSchema);
}

export function approveScenarioWait(baseUrl: string, roomId: string) {
  return httpPostJson(
    baseUrl,
    encodedRoomPath(roomId, "/api/gm/room/scenario/approve"),
    gmRoomCommandResultSchema,
  );
}

export function nextScenarioStep(baseUrl: string, roomId: string, branchId?: string) {
  const path = branchId
    ? `${encodedRoomPath(roomId, "/api/gm/room/scenario/next")}&branch_id=${encodeURIComponent(branchId)}`
    : encodedRoomPath(roomId, "/api/gm/room/scenario/next");

  return httpPostJson(baseUrl, path, gmRoomCommandResultSchema);
}

export function pauseTimer(baseUrl: string, roomId: string) {
  return httpPostJson(baseUrl, encodedRoomPath(roomId, "/api/gm/room/timer/pause"), gmRoomCommandResultSchema);
}

export function resumeTimer(baseUrl: string, roomId: string) {
  return httpPostJson(baseUrl, encodedRoomPath(roomId, "/api/gm/room/timer/resume"), gmRoomCommandResultSchema);
}

export function addTimerMs(baseUrl: string, roomId: string, deltaMs: number) {
  return httpPostJson(
    baseUrl,
    `${encodedRoomPath(roomId, "/api/gm/room/timer/add")}&delta_ms=${deltaMs}`,
    gmRoomCommandResultSchema,
  );
}

export function startTimer(baseUrl: string, roomId: string, durationMs: number) {
  return httpPostJson(
    baseUrl,
    `${encodedRoomPath(roomId, "/api/gm/room/timer/start")}&duration_ms=${durationMs}`,
    gmRoomCommandResultSchema,
  );
}

export function resetTimer(baseUrl: string, roomId: string, durationMs: number) {
  return httpPostJson(
    baseUrl,
    `${encodedRoomPath(roomId, "/api/gm/room/timer/reset")}&duration_ms=${durationMs}`,
    gmRoomCommandResultSchema,
  );
}

export function sendHint(baseUrl: string, roomId: string, message: string) {
  return httpPostJson(baseUrl, "/api/gm/room/hint/send", gmRoomCommandResultSchema, {
    room_id: roomId,
    message,
  });
}

export function clearHint(baseUrl: string, roomId: string) {
  return httpPostJson(baseUrl, encodedRoomPath(roomId, "/api/gm/room/hint/clear"), gmRoomCommandResultSchema);
}
