import { create } from "zustand";
import type { SessionInfo } from "@/domains/auth";
import type { ControllerMeta } from "@/domains/controller/model/controller_types";
import {
  clearLastUsedController,
  forgetController,
  loadLastUsedController,
  loadRecentControllers,
  saveLastUsedController,
  StoredControllerRef,
} from "@/platform/persistence/controller_storage";

export type ConnectionStatus =
  | "idle"
  | "connecting"
  | "connected"
  | "auth_required"
  | "authenticated"
  | "reconnecting"
  | "offline"
  | "stale"
  | "unsupported";

export interface ActiveControllerState {
  baseUrl: string;
  deviceId?: string;
  deviceName?: string;
  hostname?: string;
}

interface ControllerStoreState {
  activeController: ActiveControllerState | null;
  controllerMeta: ControllerMeta | null;
  sessionInfo: SessionInfo | null;
  recentControllers: StoredControllerRef[];
  connectionStatus: ConnectionStatus;
  lastError: string | null;
  lastSeen: number | null;
  setActiveController: (controller: ActiveControllerState | null) => void;
  disconnectActiveController: () => void;
  patchActiveController: (patch: Partial<ActiveControllerState>) => void;
  removeRecentController: (baseUrl: string) => void;
  setConnectionStatus: (status: ConnectionStatus) => void;
  setLastError: (message: string | null) => void;
  markSeen: (timestamp: number) => void;
  reportMetaLoading: () => void;
  reportMetaSuccess: (meta: ControllerMeta) => void;
  reportMetaAuthRequired: (message: string) => void;
  reportMetaUnavailable: (message: string) => void;
  reportUnsupported: (message: string) => void;
  reportSessionAuthenticated: (session: SessionInfo) => void;
  reportSessionAuthRequired: (message: string) => void;
  reportLoginStarted: () => void;
  reportLoginSucceeded: () => void;
  reportLoginFailed: (message: string, authRequired?: boolean) => void;
  reportLogoutSucceeded: () => void;
}

function restoreActiveController(): ActiveControllerState | null {
  const stored = loadLastUsedController();

  if (!stored?.baseUrl) {
    return null;
  }

  return {
    baseUrl: stored.baseUrl,
    deviceId: stored.deviceId,
    deviceName: stored.deviceName,
    hostname: stored.hostname,
  };
}

export const useControllerStore = create<ControllerStoreState>((set) => ({
  activeController: restoreActiveController(),
  controllerMeta: null,
  sessionInfo: null,
  recentControllers: loadRecentControllers(),
  connectionStatus: "idle",
  lastError: null,
  lastSeen: null,
  setActiveController: (controller) => {
    if (controller) {
      const stored: StoredControllerRef = {
        baseUrl: controller.baseUrl,
        deviceId: controller.deviceId ?? "",
        deviceName: controller.deviceName ?? controller.baseUrl,
        hostname: controller.hostname,
      };
      saveLastUsedController(stored);
    } else {
      clearLastUsedController();
    }

    set({
      activeController: controller,
      controllerMeta: null,
      sessionInfo: null,
      recentControllers: loadRecentControllers(),
      connectionStatus: controller ? "connecting" : "idle",
      lastError: null,
      lastSeen: null,
    });
  },
  disconnectActiveController: () => {
    clearLastUsedController();
    set({
      activeController: null,
      controllerMeta: null,
      sessionInfo: null,
      connectionStatus: "idle",
      lastError: null,
      lastSeen: null,
    });
  },
  patchActiveController: (patch) =>
    set((state) => {
      if (!state.activeController) {
        return state;
      }

      const changed = Object.entries(patch).some(([key, value]) => {
        const currentValue =
          state.activeController?.[key as keyof ActiveControllerState];
        return currentValue !== value;
      });

      if (!changed) {
        return state;
      }

      const nextController = {
        ...state.activeController,
        ...patch,
      };

      const stored: StoredControllerRef = {
        baseUrl: nextController.baseUrl,
        deviceId: nextController.deviceId ?? "",
        deviceName: nextController.deviceName ?? nextController.baseUrl,
        hostname: nextController.hostname,
      };
      saveLastUsedController(stored);

      return {
        ...state,
        activeController: nextController,
        recentControllers: loadRecentControllers(),
      };
    }),
  removeRecentController: (baseUrl) =>
    set((state) => {
      forgetController(baseUrl);

      const activeMatches = state.activeController?.baseUrl === baseUrl;
      if (activeMatches) {
        clearLastUsedController();
      }

      return {
        ...state,
        activeController: activeMatches ? null : state.activeController,
        controllerMeta: activeMatches ? null : state.controllerMeta,
        sessionInfo: activeMatches ? null : state.sessionInfo,
        recentControllers: loadRecentControllers(),
        connectionStatus: activeMatches ? "idle" : state.connectionStatus,
      };
    }),
  setConnectionStatus: (status) => set({ connectionStatus: status }),
  setLastError: (message) => set({ lastError: message }),
  markSeen: (timestamp) => set({ lastSeen: timestamp }),
  reportMetaLoading: () =>
    set((state) => ({
      ...state,
      connectionStatus: state.activeController ? "connecting" : "idle",
      lastError: null,
      sessionInfo: null,
    })),
  reportMetaSuccess: (meta) =>
    set((state) => {
      if (!state.activeController) {
        return state;
      }

      const nextController = {
        ...state.activeController,
        deviceId: meta.device_id,
        deviceName: meta.device_name,
        hostname: meta.hostname,
      };
      const stored: StoredControllerRef = {
        baseUrl: nextController.baseUrl,
        deviceId: nextController.deviceId ?? "",
        deviceName: nextController.deviceName ?? nextController.baseUrl,
        hostname: nextController.hostname,
      };
      saveLastUsedController(stored);

      return {
        ...state,
        activeController: nextController,
        controllerMeta: meta,
        recentControllers: loadRecentControllers(),
        connectionStatus: "connected",
        lastError: null,
      };
    }),
  reportMetaAuthRequired: (message) =>
    set((state) => ({
      ...state,
      connectionStatus: "auth_required",
      lastError: message,
      sessionInfo: null,
    })),
  reportMetaUnavailable: (message) =>
    set((state) => ({
      ...state,
      connectionStatus: "offline",
      lastError: message,
      sessionInfo: null,
    })),
  reportUnsupported: (message) =>
    set((state) => ({
      ...state,
      connectionStatus: "unsupported",
      lastError: message,
      sessionInfo: null,
    })),
  reportSessionAuthenticated: (session) =>
    set((state) => ({
      ...state,
      sessionInfo: session,
      connectionStatus: "authenticated",
      lastError: null,
    })),
  reportSessionAuthRequired: (message) =>
    set((state) => ({
      ...state,
      sessionInfo: null,
      connectionStatus: "auth_required",
      lastError: message,
    })),
  reportLoginStarted: () =>
    set((state) => ({
      ...state,
      connectionStatus: "connecting",
      lastError: null,
      sessionInfo: null,
    })),
  reportLoginSucceeded: () =>
    set((state) => ({
      ...state,
      connectionStatus: "connected",
      lastError: null,
      sessionInfo: null,
    })),
  reportLoginFailed: (message, authRequired = false) =>
    set((state) => ({
      ...state,
      connectionStatus: authRequired ? "auth_required" : "offline",
      lastError: message,
      sessionInfo: null,
    })),
  reportLogoutSucceeded: () =>
    set((state) => ({
      ...state,
      sessionInfo: null,
      connectionStatus: "auth_required",
      lastError: null,
    })),
}));
