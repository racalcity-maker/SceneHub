import { useControllerStore } from "@/domains/controller";
import { Badge } from "@/shared/ui/Badge";
import { ErrorState } from "@/shared/ui/ErrorState";
import { LoadingState } from "@/shared/ui/LoadingState";

export function ControllerStatus() {
  const activeController = useControllerStore((state) => state.activeController);
  const controllerMeta = useControllerStore((state) => state.controllerMeta);
  const connectionStatus = useControllerStore((state) => state.connectionStatus);
  const lastError = useControllerStore((state) => state.lastError);

  if (!activeController?.baseUrl) {
    return <Badge tone="muted">No controller selected</Badge>;
  }

  if (connectionStatus === "connecting") {
    return <LoadingState label={`Connecting to ${activeController.baseUrl}...`} compact />;
  }

  if (connectionStatus === "auth_required") {
    return (
      <Badge tone="muted">login required</Badge>
    );
  }

  if (connectionStatus === "offline") {
    return (
      <ErrorState
        title="Controller unavailable"
        description={lastError ?? "No controller connection"}
        compact
      />
    );
  }

  if (!controllerMeta) {
    return <Badge tone="muted">No controller metadata</Badge>;
  }

  if (connectionStatus === "unsupported") {
    return (
      <ErrorState
        title="Unsupported controller"
        description={lastError ?? "Controller metadata is incompatible with this desktop build"}
        compact
      />
    );
  }

  return (
    <div className="toolbar-chip">
      <div className="toolbar-meta">
        <span>{controllerMeta.device_name || controllerMeta.device_id}</span>
        <span>{activeController.baseUrl}</span>
      </div>
      <Badge tone={connectionStatus === "authenticated" || connectionStatus === "connected" ? "success" : "muted"}>
        {connectionStatus}
      </Badge>
      <Badge tone={connectionStatus === "authenticated" || connectionStatus === "connected" ? "success" : "muted"}>
        API v{controllerMeta.api_version}
      </Badge>
      <Badge tone={connectionStatus === "authenticated" || connectionStatus === "connected" ? "success" : "muted"}>
        FW {controllerMeta.firmware_version}
      </Badge>
    </div>
  );
}
