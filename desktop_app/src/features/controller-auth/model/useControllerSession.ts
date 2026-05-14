import { useQuery } from "@tanstack/react-query";
import { useControllerStore } from "@/domains/controller";
import { sessionInfoSchema, type SessionInfo } from "@/domains/auth";
import { httpGetJson } from "@/platform/http/client";

export function useControllerSession() {
  const activeController = useControllerStore((state) => state.activeController);
  const connectionStatus = useControllerStore((state) => state.connectionStatus);

  return useQuery({
    queryKey: ["controller", activeController?.baseUrl ?? "none", "session"],
    enabled:
      !!activeController?.baseUrl &&
      connectionStatus !== "idle" &&
      connectionStatus !== "offline" &&
      connectionStatus !== "unsupported" &&
      connectionStatus !== "auth_required",
    retry: 1,
    queryFn: () =>
      httpGetJson<SessionInfo>(
        activeController!.baseUrl,
        "/api/session/info",
        sessionInfoSchema,
      ),
  });
}
