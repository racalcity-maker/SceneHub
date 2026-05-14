import { useEffect } from "react";
import {
  evaluateControllerCompatibility,
  useActiveControllerMeta,
  useControllerStore,
} from "@/domains/controller";
import { useControllerSession } from "@/features/controller-auth/model/useControllerSession";
import { HttpError } from "@/platform/http/http_error";

export function useControllerConnectionManager() {
  const activeController = useControllerStore((state) => state.activeController);
  const markSeen = useControllerStore((state) => state.markSeen);
  const reportMetaLoading = useControllerStore((state) => state.reportMetaLoading);
  const reportMetaSuccess = useControllerStore((state) => state.reportMetaSuccess);
  const reportMetaAuthRequired = useControllerStore((state) => state.reportMetaAuthRequired);
  const reportMetaUnavailable = useControllerStore((state) => state.reportMetaUnavailable);
  const reportUnsupported = useControllerStore((state) => state.reportUnsupported);
  const reportSessionAuthenticated = useControllerStore((state) => state.reportSessionAuthenticated);
  const reportSessionAuthRequired = useControllerStore((state) => state.reportSessionAuthRequired);

  const metaQuery = useActiveControllerMeta();
  const sessionQuery = useControllerSession();

  useEffect(() => {
    if (!activeController?.baseUrl) {
      return;
    }

    if (metaQuery.isLoading) {
      reportMetaLoading();
      return;
    }

    if (metaQuery.isError) {
      if (metaQuery.error instanceof HttpError && [401, 403].includes(metaQuery.error.status ?? 0)) {
        reportMetaAuthRequired("Controller requires login before metadata can be read");
        return;
      }

      reportMetaUnavailable(metaQuery.error instanceof Error ? metaQuery.error.message : "Meta load failed");
      return;
    }

    if (metaQuery.data) {
      const compatibility = evaluateControllerCompatibility(metaQuery.data);
      if (!compatibility.supported) {
        reportUnsupported(compatibility.reason ?? "Unsupported controller");
        return;
      }

      reportMetaSuccess(metaQuery.data);
      markSeen(Date.now());
    }
  }, [
    activeController?.baseUrl,
    markSeen,
    metaQuery.data,
    metaQuery.error,
    metaQuery.isError,
    metaQuery.isLoading,
    reportMetaAuthRequired,
    reportMetaLoading,
    reportMetaSuccess,
    reportMetaUnavailable,
    reportUnsupported,
  ]);

  useEffect(() => {
    if (!activeController?.baseUrl) {
      return;
    }

    if (sessionQuery.isError) {
      if (sessionQuery.error instanceof HttpError && sessionQuery.error.status === 401) {
        reportSessionAuthRequired("Controller requires login");
      }
      return;
    }

    if (sessionQuery.data) {
      reportSessionAuthenticated(sessionQuery.data);
    }
  }, [
    activeController?.baseUrl,
    reportSessionAuthenticated,
    reportSessionAuthRequired,
    sessionQuery.data,
    sessionQuery.error,
    sessionQuery.isError,
  ]);

  return null;
}
