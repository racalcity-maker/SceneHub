import { FormEvent, useEffect, useMemo, useState } from "react";
import { useControllerStore } from "@/domains/controller";
import { normalizeControllerBaseUrl } from "@/domains/controller";
import { useControllerDiscovery } from "@/features/controller-discovery/model/useControllerDiscovery";
import { Button } from "@/shared/ui/Button";

export function ControllerPicker() {
  const activeController = useControllerStore((state) => state.activeController);
  const recentControllers = useControllerStore((state) => state.recentControllers);
  const setActiveController = useControllerStore((state) => state.setActiveController);
  const disconnectActiveController = useControllerStore((state) => state.disconnectActiveController);
  const removeRecentController = useControllerStore((state) => state.removeRecentController);
  const discovery = useControllerDiscovery();
  const [baseUrl, setBaseUrl] = useState(activeController?.baseUrl ?? "");
  const [selectedRecentBaseUrl, setSelectedRecentBaseUrl] = useState("");
  const [selectedDiscoveredBaseUrl, setSelectedDiscoveredBaseUrl] = useState("");

  const placeholder = useMemo(() => "http://scenehub.local or http://192.168.1.50", []);
  const discoveryStatus = useMemo(() => {
    if (discovery.discovering) {
      return {
        tone: "muted" as const,
        text: "Scanning controllers...",
      };
    }

    if (discovery.discoveryError) {
      return {
        tone: "error" as const,
        text: discovery.discoveryError,
      };
    }

    if (!discovery.hasScanned) {
      return null;
    }

    if (discovery.discoveredControllers.length === 0) {
      return {
        tone: "muted" as const,
        text: "No controllers found on this network",
      };
    }

    return {
      tone: "success" as const,
      text:
        discovery.discoveredControllers.length === 1
          ? "Found 1 controller"
          : `Found ${discovery.discoveredControllers.length} controllers`,
    };
  }, [
    discovery.discovering,
    discovery.discoveryError,
    discovery.hasScanned,
    discovery.discoveredControllers.length,
  ]);

  useEffect(() => {
    if (activeController?.baseUrl) {
      setBaseUrl(activeController.baseUrl);
    }
  }, [activeController?.baseUrl]);

  function handleSubmit(event: FormEvent<HTMLFormElement>) {
    event.preventDefault();

    const normalized = normalizeControllerBaseUrl(baseUrl);
    if (!normalized) {
      return;
    }

    setActiveController({
      baseUrl: normalized,
    });
  }

  return (
    <form className="controller-picker" onSubmit={handleSubmit}>
      {recentControllers.length > 0 ? (
        <select
          className="toolbar-select"
          value={selectedRecentBaseUrl}
          onChange={(event) => {
            const nextBaseUrl = event.target.value;
            setSelectedRecentBaseUrl(nextBaseUrl);
            if (!nextBaseUrl) {
              return;
            }

            const nextController = recentControllers.find(
              (controller) => controller.baseUrl === nextBaseUrl,
            );
            if (!nextController) {
              return;
            }

            setBaseUrl(nextController.baseUrl);
            setActiveController({
              baseUrl: nextController.baseUrl,
              deviceId: nextController.deviceId || undefined,
              deviceName: nextController.deviceName || undefined,
              hostname: nextController.hostname,
            });
          }}
          aria-label="Recent controllers"
        >
          <option value="">Recent controllers</option>
          {recentControllers.map((controller) => (
            <option key={controller.baseUrl} value={controller.baseUrl}>
              {controller.deviceName || controller.baseUrl}
            </option>
          ))}
        </select>
      ) : null}
      <input
        className="toolbar-input"
        value={baseUrl}
        onChange={(event) => setBaseUrl(event.target.value)}
        placeholder={placeholder}
        aria-label="Controller address"
      />
      <Button type="submit" size="sm">
        Connect
      </Button>
      <Button type="button" size="sm" tone="secondary" onClick={() => void discovery.discover()} disabled={discovery.discovering}>
        {discovery.discovering ? "Scanning..." : "Scan"}
      </Button>
      <Button
        type="button"
        size="sm"
        tone="secondary"
        onClick={() =>
          setActiveController({
            baseUrl: "mock://scenehub",
            deviceName: "Mock SceneHub Controller",
          })
        }
      >
        Mock
      </Button>
      {activeController ? (
        <Button type="button" size="sm" tone="secondary" onClick={disconnectActiveController}>
          Disconnect
        </Button>
      ) : null}
      {selectedRecentBaseUrl ? (
        <Button
          type="button"
          size="sm"
          tone="secondary"
          onClick={() => {
            removeRecentController(selectedRecentBaseUrl);
            setSelectedRecentBaseUrl("");
          }}
        >
          Forget
        </Button>
      ) : null}
      {discovery.discoveredControllers.length > 0 ? (
        <select
          className="toolbar-select"
          value={selectedDiscoveredBaseUrl}
          onChange={(event) => {
            const nextBaseUrl = event.target.value;
            setSelectedDiscoveredBaseUrl(nextBaseUrl);
            if (!nextBaseUrl) {
              return;
            }

            const discovered = discovery.discoveredControllers.find(
              (controller) => controller.baseUrl === nextBaseUrl,
            );
            if (!discovered) {
              return;
            }

            setBaseUrl(discovered.baseUrl);
            discovery.activate(discovered);
          }}
          aria-label="Discovered controllers"
        >
          <option value="">Discovered controllers</option>
          {discovery.discoveredControllers.map((controller) => (
            <option key={controller.baseUrl} value={controller.baseUrl}>
              {controller.deviceName} ({controller.baseUrl})
            </option>
          ))}
        </select>
      ) : null}
      {discoveryStatus ? (
        <span
          className={
            discoveryStatus.tone === "error"
              ? "toolbar-status-text toolbar-status-text-error"
              : discoveryStatus.tone === "success"
                ? "toolbar-status-text toolbar-status-text-success"
                : "toolbar-status-text"
          }
        >
          {discoveryStatus.text}
        </span>
      ) : null}
    </form>
  );
}
