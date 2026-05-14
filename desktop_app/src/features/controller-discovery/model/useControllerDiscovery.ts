import { useState } from "react";
import { useControllerStore } from "@/domains/controller";
import { tauriInvoke } from "@/platform/tauri/invoke";

export interface DiscoveredController {
  baseUrl: string;
  deviceId: string;
  deviceName: string;
  hostname?: string;
  firmwareVersion: string;
  apiVersion: number;
  source: string;
}

export function useControllerDiscovery() {
  const setActiveController = useControllerStore((state) => state.setActiveController);
  const [discoveredControllers, setDiscoveredControllers] = useState<DiscoveredController[]>([]);
  const [discovering, setDiscovering] = useState(false);
  const [discoveryError, setDiscoveryError] = useState<string | null>(null);
  const [hasScanned, setHasScanned] = useState(false);

  async function discover() {
    setDiscovering(true);
    setDiscoveryError(null);
    try {
      const controllers = await tauriInvoke<DiscoveredController[]>("discover_controllers");
      setDiscoveredControllers(controllers);
      setHasScanned(true);
    } catch (error) {
      setDiscoveryError(error instanceof Error ? error.message : "Discovery failed");
      setHasScanned(true);
    } finally {
      setDiscovering(false);
    }
  }

  function activate(controller: DiscoveredController) {
    setActiveController({
      baseUrl: controller.baseUrl,
      deviceId: controller.deviceId,
      deviceName: controller.deviceName,
      hostname: controller.hostname,
    });
  }

  return {
    discoveredControllers,
    discovering,
    discoveryError,
    hasScanned,
    discover,
    activate,
  };
}
