import { useControllerStore } from "@/domains/controller";

export function StatusBar() {
  const activeController = useControllerStore((state) => state.activeController);
  const connectionStatus = useControllerStore((state) => state.connectionStatus);
  const lastSeen = useControllerStore((state) => state.lastSeen);

  const lastSeenLabel = lastSeen ? new Date(lastSeen).toLocaleTimeString() : "never";

  return (
    <footer className="status-bar">
      <span>Runtime truth comes from controller snapshots and live events.</span>
      <span>
        {activeController?.deviceName || activeController?.baseUrl || "No controller"} |{" "}
        {connectionStatus} | last seen {lastSeenLabel}
      </span>
    </footer>
  );
}
