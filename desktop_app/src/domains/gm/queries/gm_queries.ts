import { useQuery } from "@tanstack/react-query";
import { useControllerStore } from "@/domains/controller";
import { httpGetJson } from "@/platform/http/client";
import { GmState, gmStateSchema } from "@/domains/gm/model/gm_state_types";
import {
  GmRoomRuntime,
  gmRoomRuntimeSchema,
} from "@/domains/gm/model/gm_room_runtime_types";
import {
  GmRoomProfiles,
  gmRoomProfilesSchema,
} from "@/domains/gm/model/gm_room_profile_types";
import {
  GmRoomScenarios,
  gmRoomScenariosSchema,
} from "@/domains/gm/model/gm_room_scenario_types";

export function useGmStateQuery() {
  const activeController = useControllerStore((state) => state.activeController);
  const connectionStatus = useControllerStore((state) => state.connectionStatus);
  const isReady =
    connectionStatus === "connected" || connectionStatus === "authenticated";

  return useQuery({
    queryKey: ["controller", activeController?.baseUrl ?? "none", "gm", "state"],
    enabled: !!activeController?.baseUrl && isReady,
    retry: 1,
    queryFn: () =>
      httpGetJson<GmState>(activeController!.baseUrl, "/api/gm/state", gmStateSchema),
  });
}

export function useGmRoomRuntimeQuery(roomId?: string) {
  const activeController = useControllerStore((state) => state.activeController);
  const connectionStatus = useControllerStore((state) => state.connectionStatus);
  const isReady =
    connectionStatus === "connected" || connectionStatus === "authenticated";

  return useQuery({
    queryKey: ["controller", activeController?.baseUrl ?? "none", "gm", "room-runtime", roomId ?? "none"],
    enabled: !!activeController?.baseUrl && !!roomId && isReady,
    retry: 1,
    queryFn: () =>
      httpGetJson<GmRoomRuntime>(
        activeController!.baseUrl,
        `/api/gm/room/runtime?room_id=${encodeURIComponent(roomId!)}`,
        gmRoomRuntimeSchema,
      ),
  });
}

export function useGmRoomProfilesQuery(roomId?: string) {
  const activeController = useControllerStore((state) => state.activeController);
  const connectionStatus = useControllerStore((state) => state.connectionStatus);
  const isReady =
    connectionStatus === "connected" || connectionStatus === "authenticated";

  return useQuery({
    queryKey: ["controller", activeController?.baseUrl ?? "none", "gm", "room-profiles", roomId ?? "none"],
    enabled: !!activeController?.baseUrl && !!roomId && isReady,
    retry: 1,
    queryFn: () =>
      httpGetJson<GmRoomProfiles>(
        activeController!.baseUrl,
        `/api/gm/room/profiles?room_id=${encodeURIComponent(roomId!)}`,
        gmRoomProfilesSchema,
      ),
  });
}

export function useGmRoomScenariosQuery(roomId?: string) {
  const activeController = useControllerStore((state) => state.activeController);
  const connectionStatus = useControllerStore((state) => state.connectionStatus);
  const isReady =
    connectionStatus === "connected" || connectionStatus === "authenticated";

  return useQuery({
    queryKey: ["controller", activeController?.baseUrl ?? "none", "gm", "room-scenarios", roomId ?? "none"],
    enabled: !!activeController?.baseUrl && !!roomId && isReady,
    retry: 1,
    queryFn: () =>
      httpGetJson<GmRoomScenarios>(
        activeController!.baseUrl,
        `/api/gm/room/scenarios?room_id=${encodeURIComponent(roomId!)}`,
        gmRoomScenariosSchema,
      ),
  });
}
