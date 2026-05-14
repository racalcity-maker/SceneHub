import { useQuery } from "@tanstack/react-query";
import { httpGetJson } from "@/platform/http/client";
import {
  ControllerMeta,
  controllerMetaSchema,
} from "@/domains/controller/model/controller_types";
import { useControllerStore } from "@/domains/controller/state/controller_store";

export function useActiveControllerMeta() {
  const activeController = useControllerStore((state) => state.activeController);

  return useQuery({
    queryKey: ["controller", activeController?.baseUrl ?? "none", "meta"],
    enabled: !!activeController?.baseUrl,
    retry: 1,
    queryFn: () =>
      httpGetJson<ControllerMeta>(activeController!.baseUrl, "/api/meta", controllerMetaSchema),
  });
}
