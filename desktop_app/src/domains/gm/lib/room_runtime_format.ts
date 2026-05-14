export function formatDuration(ms: number): string {
  const totalSeconds = Math.max(0, Math.floor(ms / 1000));
  const hours = Math.floor(totalSeconds / 3600);
  const minutes = Math.floor((totalSeconds % 3600) / 60);
  const seconds = totalSeconds % 60;

  if (hours > 0) {
    return `${String(hours).padStart(2, "0")}:${String(minutes).padStart(2, "0")}:${String(seconds).padStart(2, "0")}`;
  }

  return `${String(minutes).padStart(2, "0")}:${String(seconds).padStart(2, "0")}`;
}

export function formatIdentifierLabel(value: string): string {
  if (!value) {
    return "none";
  }

  return value
    .replace(/[_:]+/g, " ")
    .replace(/\s+/g, " ")
    .trim();
}

export function badgeToneForState(value: string): "success" | "danger" | "muted" {
  if (value === "running" || value === "ready" || value === "done") {
    return "success";
  }
  if (value === "fault" || value === "error") {
    return "danger";
  }
  return "muted";
}

export function describeWaitTarget(runtime: {
  scenario_wait_type: string;
  scenario_wait_events: Array<{ event_type: string; source_id: string }>;
  scenario_wait_source_id: string;
  scenario_wait_event_type: string;
  scenario_wait_flags: Array<{ name: string; value: boolean }>;
  scenario_wait_operator_prompt: string;
  scenario_wait_until_ms: number;
}) {
  if (runtime.scenario_wait_type === "event") {
    if (runtime.scenario_wait_events.length > 0) {
      return runtime.scenario_wait_events
        .map((event) =>
          `${formatIdentifierLabel(event.source_id || "device")} / ${formatIdentifierLabel(event.event_type || "event")}`,
        )
        .join(", ");
    }

    return `${formatIdentifierLabel(runtime.scenario_wait_source_id || "device")} / ${formatIdentifierLabel(runtime.scenario_wait_event_type || "event")}`;
  }

  if (runtime.scenario_wait_type === "flags") {
    if (runtime.scenario_wait_flags.length === 0) {
      return "runtime flags";
    }

    return runtime.scenario_wait_flags
      .map((flag) => `${formatIdentifierLabel(flag.name)}=${flag.value ? "true" : "false"}`)
      .join(", ");
  }

  if (runtime.scenario_wait_type === "operator") {
    return runtime.scenario_wait_operator_prompt || "operator approval";
  }

  if (runtime.scenario_wait_type === "time") {
    return runtime.scenario_wait_until_ms > 0 ? `until ${runtime.scenario_wait_until_ms}` : "time wait";
  }

  return runtime.scenario_wait_type || "none";
}
