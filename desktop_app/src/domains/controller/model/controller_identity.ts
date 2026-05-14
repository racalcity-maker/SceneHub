import { ControllerMeta } from "@/domains/controller/model/controller_types";

export const SUPPORTED_CONTROLLER_PRODUCT_ID = "scenehub-controller";
export const SUPPORTED_CONTROLLER_API_VERSION = 1;

export interface ControllerCompatibility {
  supported: boolean;
  reason?: string;
}

export function normalizeControllerBaseUrl(input: string): string {
  const trimmed = input.trim();
  if (!trimmed) {
    return "";
  }

  if (trimmed.startsWith("mock://")) {
    return trimmed;
  }

  const withProtocol = /^[a-z]+:\/\//i.test(trimmed) ? trimmed : `http://${trimmed}`;

  try {
    const url = new URL(withProtocol);
    return url.origin;
  } catch {
    return withProtocol.replace(/\/+$/, "");
  }
}

export function evaluateControllerCompatibility(meta: ControllerMeta): ControllerCompatibility {
  if (meta.product_id && meta.product_id !== SUPPORTED_CONTROLLER_PRODUCT_ID) {
    return {
      supported: false,
      reason: `Unexpected controller product: ${meta.product_id}`,
    };
  }

  if (meta.api_version !== SUPPORTED_CONTROLLER_API_VERSION) {
    return {
      supported: false,
      reason: `Unsupported controller API version: ${meta.api_version}`,
    };
  }

  return { supported: true };
}
