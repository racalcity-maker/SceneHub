import { useEffect, useRef } from "react";
import { useQueryClient } from "@tanstack/react-query";
import { useControllerStore } from "@/domains/controller";
import {
  GmVersions,
  gmVersionsChanged,
  gmVersionsEqual,
  gmVersionsSchema,
} from "@/domains/gm/model/gm_versions_types";
import { httpGetJson } from "@/platform/http/client";
import {
  gmInvalidationSchema,
  gmResyncRequiredSchema,
  type GmInvalidation,
  wsEnvelopeSchema,
} from "@/platform/ws/envelope";

const ROOM_RUNTIME_INVALIDATION_MIN_MS = 2500;

function buildWsUrl(baseUrl: string): string {
  const normalizedBase = baseUrl.replace(/\/+$/, "");

  if (normalizedBase.startsWith("https://")) {
    return normalizedBase.replace(/^https:\/\//, "wss://") + "/api/ws";
  }

  if (normalizedBase.startsWith("http://")) {
    return normalizedBase.replace(/^http:\/\//, "ws://") + "/api/ws";
  }

  return `ws://${normalizedBase}/api/ws`;
}

export function useGmVersionsWs() {
  const queryClient = useQueryClient();
  const activeController = useControllerStore((state) => state.activeController);
  const connectionStatus = useControllerStore((state) => state.connectionStatus);
  const setLastError = useControllerStore((state) => state.setLastError);
  const markSeen = useControllerStore((state) => state.markSeen);

  const reconnectTimerRef = useRef<number | null>(null);
  const socketRef = useRef<WebSocket | null>(null);
  const reconnectAttemptRef = useRef(0);
  const lastVersionsRef = useRef<GmVersions | null>(null);
  const versionsRequestRef = useRef<Promise<GmVersions> | null>(null);
  const versionsIgnoreUntilRef = useRef(0);
  const invalidationFlushTimerRef = useRef<number | null>(null);
  const runtimeInvalidationFlushTimerRef = useRef<number | null>(null);
  const lastRuntimeInvalidationAtRef = useRef(0);
  const pendingInvalidationsRef = useRef<Map<string, GmInvalidation>>(new Map());
  const pendingRuntimeInvalidationsRef = useRef<Map<string, GmInvalidation>>(new Map());

  useEffect(() => {
    const baseUrl = activeController?.baseUrl;

    const isReady =
      connectionStatus === "connected" ||
      connectionStatus === "authenticated" ||
      connectionStatus === "reconnecting";

    if (!baseUrl || !isReady || baseUrl.startsWith("mock://")) {
      return;
    }

    const controllerBaseUrl = baseUrl;

    let closedByEffect = false;

    async function loadVersionsSnapshot(): Promise<GmVersions> {
      if (!versionsRequestRef.current) {
        versionsRequestRef.current = httpGetJson<GmVersions>(
          controllerBaseUrl,
          "/api/gm/versions",
          gmVersionsSchema,
        ).finally(() => {
          versionsRequestRef.current = null;
        });
      }
      return versionsRequestRef.current;
    }

    function invalidateState() {
      queryClient.invalidateQueries({
        queryKey: ["controller", controllerBaseUrl, "gm", "state"],
      });
    }

    function invalidateRoomRuntime(roomId?: string) {
      if (roomId) {
        queryClient.invalidateQueries({
          queryKey: ["controller", controllerBaseUrl, "gm", "room-runtime", roomId],
        });
        return;
      }
      queryClient.invalidateQueries({
        queryKey: ["controller", controllerBaseUrl, "gm", "room-runtime"],
      });
    }

    function invalidateRoomProfiles(roomId?: string) {
      if (roomId) {
        queryClient.invalidateQueries({
          queryKey: ["controller", controllerBaseUrl, "gm", "room-profiles", roomId],
        });
        return;
      }
      queryClient.invalidateQueries({
        queryKey: ["controller", controllerBaseUrl, "gm", "room-profiles"],
      });
    }

    function invalidateRoomScenarios(roomId?: string) {
      if (roomId) {
        queryClient.invalidateQueries({
          queryKey: ["controller", controllerBaseUrl, "gm", "room-scenarios", roomId],
        });
        return;
      }
      queryClient.invalidateQueries({
        queryKey: ["controller", controllerBaseUrl, "gm", "room-scenarios"],
      });
    }

    function applyVersionsChanged(versions: GmVersions) {
      const previous = lastVersionsRef.current;

      if (previous && gmVersionsEqual(versions, previous)) {
        return;
      }
      lastVersionsRef.current = versions;

      if (!previous) {
        invalidateState();
        invalidateRoomRuntime();
        return;
      }

      if (gmVersionsChanged(versions, previous, ["rooms"])) {
        invalidateState();
      }

      if (gmVersionsChanged(versions, previous, ["devices", "ingest"])) {
        invalidateState();
      }

      if (gmVersionsChanged(versions, previous, ["scenarios"])) {
        invalidateRoomScenarios();
      }

      if (gmVersionsChanged(versions, previous, ["profiles"])) {
        invalidateRoomProfiles();
      }

      if (gmVersionsChanged(versions, previous, ["session", "runtime", "ingest"])) {
        invalidateState();
        invalidateRoomRuntime();
      }
    }

    function applyInvalidation(invalidation: GmInvalidation) {
      const slice = invalidation.slice;
      const targetId = invalidation.target_id || undefined;

      if (slice === "full.snapshot" || slice === "room.catalog") {
        invalidateState();
        invalidateRoomRuntime();
        invalidateRoomProfiles();
        invalidateRoomScenarios();
        return;
      }

      if (slice === "devices.catalog") {
        invalidateState();
        invalidateRoomScenarios();
        return;
      }

      if (slice === "devices.runtime") {
        invalidateState();
        invalidateRoomRuntime();
        return;
      }

      if (slice === "room.scenarios") {
        invalidateRoomScenarios(targetId);
        invalidateRoomRuntime(targetId);
        return;
      }

      if (slice === "room.profiles") {
        invalidateRoomProfiles(targetId);
        invalidateRoomRuntime(targetId);
        return;
      }

      if (slice === "room.runtime") {
        invalidateRoomRuntime(targetId);
        return;
      }

      if (slice === "system.summary") {
        invalidateState();
      }
    }

    function applyResyncRequired() {
      invalidateState();
      invalidateRoomRuntime();
      invalidateRoomProfiles();
      invalidateRoomScenarios();
    }

    function clearInvalidationFlushTimer() {
      if (invalidationFlushTimerRef.current !== null) {
        window.clearTimeout(invalidationFlushTimerRef.current);
        invalidationFlushTimerRef.current = null;
      }
    }

    function clearRuntimeInvalidationFlushTimer() {
      if (runtimeInvalidationFlushTimerRef.current !== null) {
        window.clearTimeout(runtimeInvalidationFlushTimerRef.current);
        runtimeInvalidationFlushTimerRef.current = null;
      }
    }

    function flushInvalidationMap(target: Map<string, GmInvalidation>) {
      const items = Array.from(target.values());
      target.clear();
      items.forEach(applyInvalidation);
    }

    function flushInvalidations() {
      clearInvalidationFlushTimer();
      flushInvalidationMap(pendingInvalidationsRef.current);
    }

    function flushRuntimeInvalidations() {
      clearRuntimeInvalidationFlushTimer();
      lastRuntimeInvalidationAtRef.current = Date.now();
      flushInvalidationMap(pendingRuntimeInvalidationsRef.current);
    }

    function queueRuntimeInvalidation(invalidation: GmInvalidation) {
      const key = `${invalidation.slice}:${invalidation.target_id || ""}`;
      pendingRuntimeInvalidationsRef.current.set(key, invalidation);
      if (runtimeInvalidationFlushTimerRef.current !== null) {
        return;
      }

      const elapsedMs = Date.now() - lastRuntimeInvalidationAtRef.current;
      const delayMs = Math.max(0, ROOM_RUNTIME_INVALIDATION_MIN_MS - elapsedMs);
      runtimeInvalidationFlushTimerRef.current = window.setTimeout(() => {
        flushRuntimeInvalidations();
      }, delayMs);
    }

    function queueInvalidation(invalidation: GmInvalidation) {
      if (invalidation.slice === "room.runtime") {
        queueRuntimeInvalidation(invalidation);
        return;
      }
      const key = `${invalidation.slice}:${invalidation.target_id || ""}`;
      pendingInvalidationsRef.current.set(key, invalidation);
      if (invalidationFlushTimerRef.current !== null) {
        return;
      }
      invalidationFlushTimerRef.current = window.setTimeout(() => {
        flushInvalidations();
      }, 50);
    }

    function clearReconnectTimer() {
      if (reconnectTimerRef.current !== null) {
        window.clearTimeout(reconnectTimerRef.current);
        reconnectTimerRef.current = null;
      }
    }

    function scheduleReconnect() {
      if (closedByEffect) {
        return;
      }

      const attempt = reconnectAttemptRef.current;
      const delayMs = Math.min(1000 * 2 ** attempt, 10000);
      reconnectAttemptRef.current = Math.min(attempt + 1, 5);

      clearReconnectTimer();
      reconnectTimerRef.current = window.setTimeout(() => {
        connect();
      }, delayMs);
    }

    function connect() {
      clearReconnectTimer();

      const wsUrl = buildWsUrl(controllerBaseUrl);
      const socket = new WebSocket(wsUrl);
      socketRef.current = socket;

      socket.onopen = () => {
        reconnectAttemptRef.current = 0;
        setLastError(null);
        markSeen(Date.now());

        socket.send(JSON.stringify({ type: "subscribe" }));
      };

      socket.onmessage = async (event) => {
        markSeen(Date.now());

        try {
          const parsedJson = JSON.parse(event.data);
          const envelope = wsEnvelopeSchema.parse(parsedJson);

          if (envelope.type === "connection.ready") {
            return;
          }

          if (envelope.type === "subscription.ready") {
            return;
          }

          if (envelope.type === "pong") {
            return;
          }
          if (envelope.type === "gm.invalidate") {
            const parsedInvalidation = gmInvalidationSchema.safeParse(envelope.payload);
            if (parsedInvalidation.success) {
              versionsIgnoreUntilRef.current = Date.now() + 500;
              queueInvalidation(parsedInvalidation.data);
            }
            return;
          }
          if (envelope.type === "gm.resync.required") {
            const parsedResync = gmResyncRequiredSchema.safeParse(envelope.payload);
            if (parsedResync.success) {
              versionsIgnoreUntilRef.current = Date.now() + 500;
              pendingRuntimeInvalidationsRef.current.clear();
              clearRuntimeInvalidationFlushTimer();
              pendingInvalidationsRef.current.clear();
              clearInvalidationFlushTimer();
              applyResyncRequired();
            }
            return;
          }
          if (envelope.type === "gm.versions.changed") {
            if (Date.now() < versionsIgnoreUntilRef.current) {
              return;
            }
            const parsedPayload = gmVersionsSchema.safeParse(envelope.payload);
            const versions = parsedPayload.success
              ? parsedPayload.data
              : await loadVersionsSnapshot();
            applyVersionsChanged(versions);
            return;
          }
        } catch (error) {
          const message = error instanceof Error ? error.message : "WebSocket message error";
          setLastError(message);
        }
      };

      socket.onerror = () => {
        setLastError("WebSocket connection error");
      };

      socket.onclose = () => {
        if (socketRef.current === socket) {
          socketRef.current = null;
        }

        if (!closedByEffect) {
          scheduleReconnect();
        }
      };
    }

    connect();

    return () => {
      closedByEffect = true;
      clearReconnectTimer();
      clearInvalidationFlushTimer();
      clearRuntimeInvalidationFlushTimer();
      pendingInvalidationsRef.current.clear();
      pendingRuntimeInvalidationsRef.current.clear();
      lastVersionsRef.current = null;
      versionsRequestRef.current = null;
      versionsIgnoreUntilRef.current = 0;
      lastRuntimeInvalidationAtRef.current = 0;

      if (socketRef.current) {
        socketRef.current.close();
        socketRef.current = null;
      }
    };
  }, [
    activeController?.baseUrl,
    connectionStatus,
    markSeen,
    queryClient,
    setLastError,
  ]);
}
