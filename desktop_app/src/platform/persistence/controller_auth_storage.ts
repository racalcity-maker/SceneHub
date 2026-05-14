export interface StoredControllerCredentials {
  baseUrl: string;
  username: string;
  password: string;
}

const CREDENTIALS_KEY = "scenehub.desktop.controllers.credentials";

function loadAllCredentials(): Record<string, StoredControllerCredentials> {
  const raw = window.localStorage.getItem(CREDENTIALS_KEY);
  if (!raw) {
    return {};
  }

  try {
    const parsed = JSON.parse(raw) as Record<string, StoredControllerCredentials>;
    return parsed ?? {};
  } catch {
    return {};
  }
}

function saveAllCredentials(credentials: Record<string, StoredControllerCredentials>) {
  window.localStorage.setItem(CREDENTIALS_KEY, JSON.stringify(credentials));
}

export function loadControllerCredentials(baseUrl: string): StoredControllerCredentials | null {
  const credentials = loadAllCredentials();
  return credentials[baseUrl] ?? null;
}

export function saveControllerCredentials(entry: StoredControllerCredentials) {
  const credentials = loadAllCredentials();
  credentials[entry.baseUrl] = entry;
  saveAllCredentials(credentials);
}

export function clearControllerCredentials(baseUrl: string) {
  const credentials = loadAllCredentials();
  if (!credentials[baseUrl]) {
    return;
  }

  delete credentials[baseUrl];
  saveAllCredentials(credentials);
}
