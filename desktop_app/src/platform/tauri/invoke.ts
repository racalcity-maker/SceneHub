declare global {
  interface Window {
    __TAURI_INTERNALS__?: {
      invoke: <T>(command: string, args?: Record<string, unknown>) => Promise<T>;
    };
  }
}

export async function tauriInvoke<T>(
  command: string,
  args?: Record<string, unknown>,
): Promise<T> {
  const invoke = window.__TAURI_INTERNALS__?.invoke;
  if (!invoke) {
    throw new Error("Tauri runtime unavailable");
  }

  return invoke<T>(command, args);
}

export {};
