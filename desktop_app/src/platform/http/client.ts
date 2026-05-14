import { z } from "zod";
import { HttpError } from "@/platform/http/http_error";
import { readMockResponse, writeMockResponse } from "@/platform/http/mock/mock_transport";
import { tauriInvoke } from "@/platform/tauri/invoke";

interface TauriHttpPayload {
  status: number;
  contentType: string;
  body: string;
}

function buildControllerUrl(baseUrl: string, path: string): string {
  const trimmedBase = baseUrl.replace(/\/+$/, "");
  const normalizedPath = path.startsWith("/") ? path : `/${path}`;
  return `${trimmedBase}${normalizedPath}`;
}

function isTauriRuntimeAvailable(): boolean {
  return Boolean(window.__TAURI_INTERNALS__?.invoke);
}

async function tauriHttpGet(baseUrl: string, path: string): Promise<unknown> {
  const response = await tauriInvoke<TauriHttpPayload>("controller_http_get", {
    baseUrl,
    path,
  });

  if (response.status < 200 || response.status >= 300) {
    throw new HttpError(response.body || `HTTP ${response.status}`, response.status);
  }

  return response.contentType.includes("application/json")
    ? JSON.parse(response.body)
    : response.body;
}

async function tauriHttpPost(baseUrl: string, path: string, body?: unknown): Promise<unknown> {
  const response = await tauriInvoke<TauriHttpPayload>("controller_http_post", {
    baseUrl,
    path,
    body: body !== undefined ? JSON.stringify(body) : null,
  });

  if (response.status < 200 || response.status >= 300) {
    throw new HttpError(response.body || `HTTP ${response.status}`, response.status);
  }

  return response.contentType.includes("application/json")
    ? JSON.parse(response.body)
    : response.body;
}

export async function httpGetJsonRaw(baseUrl: string, path: string): Promise<unknown> {
  if (baseUrl.startsWith("mock://")) {
    return readMockResponse(path);
  }
  if (isTauriRuntimeAvailable()) {
    return tauriHttpGet(baseUrl, path);
  }

  const response = await fetch(buildControllerUrl(baseUrl, path), {
    credentials: "include",
  });

  if (!response.ok) {
    throw new HttpError(await response.text().catch(() => response.statusText), response.status);
  }

  return await response.json();
}

export async function httpPostJsonRaw(
  baseUrl: string,
  path: string,
  body?: unknown,
): Promise<unknown> {
  if (baseUrl.startsWith("mock://")) {
    return writeMockResponse(path, body);
  }
  if (isTauriRuntimeAvailable()) {
    return tauriHttpPost(baseUrl, path, body);
  }

  const response = await fetch(buildControllerUrl(baseUrl, path), {
    method: "POST",
    credentials: "include",
    headers: body !== undefined ? { "Content-Type": "application/json" } : undefined,
    body: body !== undefined ? JSON.stringify(body) : undefined,
  });

  if (!response.ok) {
    throw new HttpError(await response.text().catch(() => response.statusText), response.status);
  }

  const contentType = response.headers.get("content-type") || "";
  if (contentType.includes("application/json")) {
    return await response.json();
  }
  return await response.text();
}

export async function httpGetJson<T>(
  baseUrl: string,
  path: string,
  schema: z.ZodTypeAny,
): Promise<T> {
  const payload = await httpGetJsonRaw(baseUrl, path);
  return schema.parse(payload) as T;
}

export async function httpPostJson<T>(
  baseUrl: string,
  path: string,
  schema: z.ZodTypeAny,
  body?: unknown,
): Promise<T> {
  const payload = await httpPostJsonRaw(baseUrl, path, body);
  return schema.parse(payload) as T;
}
