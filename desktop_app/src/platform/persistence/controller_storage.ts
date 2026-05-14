export interface StoredControllerRef {
  baseUrl: string;
  deviceId: string;
  deviceName: string;
  hostname?: string;
  lastKnownIp?: string;
}

const LAST_USED_KEY = "scenehub.desktop.controllers.last_used";
const RECENT_KEY = "scenehub.desktop.controllers.recent";
const MAX_RECENT_CONTROLLERS = 6;

function loadJson<T>(key: string): T | null {
  const raw = window.localStorage.getItem(key);

  if (!raw) {
    return null;
  }

  try {
    return JSON.parse(raw) as T;
  } catch {
    return null;
  }
}

export function loadLastUsedController(): StoredControllerRef | null {
  return loadJson<StoredControllerRef>(LAST_USED_KEY);
}

export function saveLastUsedController(controller: StoredControllerRef) {
  window.localStorage.setItem(LAST_USED_KEY, JSON.stringify(controller));
  rememberController(controller);
}

export function clearLastUsedController() {
  window.localStorage.removeItem(LAST_USED_KEY);
}

export function loadRecentControllers(): StoredControllerRef[] {
  return loadJson<StoredControllerRef[]>(RECENT_KEY) ?? [];
}

export function saveRecentControllers(controllers: StoredControllerRef[]) {
  window.localStorage.setItem(RECENT_KEY, JSON.stringify(controllers));
}

export function rememberController(controller: StoredControllerRef) {
  const recent = loadRecentControllers();
  const deduped = recent.filter((item) => item.baseUrl !== controller.baseUrl);
  const next = [controller, ...deduped].slice(0, MAX_RECENT_CONTROLLERS);
  saveRecentControllers(next);
}

export function forgetController(baseUrl: string) {
  const next = loadRecentControllers().filter((item) => item.baseUrl !== baseUrl);
  saveRecentControllers(next);
}
