import { useEffect, useMemo, useState } from "react";
import type { GmRoomRuntime, GmRoomScenario } from "@/domains/gm";
import { formatDuration } from "@/domains/gm/lib/room_runtime_format";
import { Button } from "@/shared/ui/Button";
import { Panel } from "@/shared/ui/Panel";

interface RuntimePanelProps {
  runtime: GmRoomRuntime;
  selectedScenario: GmRoomScenario | null;
  waitSummary: string;
  actionBusy: boolean;
  canApproveWait: boolean;
  canSkipWait: boolean;
  canPauseTimer: boolean;
  canResumeTimer: boolean;
  canAdjustTimer: boolean;
  timerStartMinutes: string;
  onTimerStartMinutesChange: (value: string) => void;
  onApprove: () => void;
  onSkipWait: () => void;
  onPause: () => void;
  onResume: () => void;
  onPlusMinute: () => void;
  onMinusMinute: () => void;
  onStartTimer: () => void;
  onResetTimer: () => void;
}

function isTimerRunning(runtime: GmRoomRuntime): boolean {
  return String(runtime.timer_state || "").toLowerCase() === "running";
}

function isTimeWaitActive(runtime: GmRoomRuntime): boolean {
  return (
    String(runtime.scenario_runtime_state || "").toLowerCase() === "waiting" &&
    String(runtime.scenario_wait_type || "").toLowerCase() === "time" &&
    runtime.scenario_wait_until_ms > 0
  );
}

export function RuntimePanel({
  runtime,
  selectedScenario,
  waitSummary,
  actionBusy,
  canApproveWait,
  canSkipWait,
  canPauseTimer,
  canResumeTimer,
  canAdjustTimer,
  timerStartMinutes,
  onTimerStartMinutesChange,
  onApprove,
  onSkipWait,
  onPause,
  onResume,
  onPlusMinute,
  onMinusMinute,
  onStartTimer,
  onResetTimer,
}: RuntimePanelProps) {

  const [clientNowMs, setClientNowMs] = useState(() => Date.now());

  const snapshotClock = useMemo(
    () => ({
      clientReceivedAtMs: Date.now(),
      runtimeNowMs: runtime.runtime_now_ms || 0,
      timerRemainingMs: runtime.timer_remaining_ms || 0,
    }),
    [
      runtime.runtime_now_ms,
      runtime.timer_remaining_ms,
      runtime.timer_state,
      runtime.scenario_wait_until_ms,
      runtime.scenario_wait_type,
      runtime.scenario_runtime_state,
    ],
  );

  const shouldTick = isTimerRunning(runtime) || isTimeWaitActive(runtime);

  useEffect(() => {
    if (!shouldTick) {
      return;
    }

    const id = window.setInterval(() => {
      setClientNowMs(Date.now());
    }, 500);

    return () => window.clearInterval(id);
  }, [shouldTick]);

  const elapsedClientMs = Math.max(0, clientNowMs - snapshotClock.clientReceivedAtMs);

  const liveRuntimeNowMs =
    snapshotClock.runtimeNowMs > 0
      ? snapshotClock.runtimeNowMs + elapsedClientMs
      : 0;

  const liveTimerRemainingMs = isTimerRunning(runtime)
    ? Math.max(0, snapshotClock.timerRemainingMs - elapsedClientMs)
    : Math.max(0, runtime.timer_remaining_ms || 0);

  const liveWaitRemainingMs =
    isTimeWaitActive(runtime) && liveRuntimeNowMs > 0
      ? Math.max(0, runtime.scenario_wait_until_ms - liveRuntimeNowMs)
      : 0;

  const liveWaitSummary = isTimeWaitActive(runtime)
    ? `time left ${formatDuration(liveWaitRemainingMs)}`
    : waitSummary;
  return (
    <Panel title="Runtime" subtitle="Current step, waits and operator controls">
      <div className="runtime-kv-grid">
        <div className="runtime-kv">
          <span>Scenario</span>
          <strong>
            {runtime.running_scenario_name || selectedScenario?.name || runtime.selected_scenario_name || "none"}
          </strong>
        </div>

        <div className="runtime-kv">
          <span>Runtime</span>
          <strong>{runtime.scenario_runtime_state}</strong>
        </div>

        <div className="runtime-kv">
          <span>Game time</span>
          <strong className="runtime-time-value">{formatDuration(liveTimerRemainingMs)}</strong>
        </div>

        <div className="runtime-kv">
          <span>Step</span>
          <strong className="runtime-step-value">{runtime.scenario_current_step_index}</strong>
        </div>

        <div className="runtime-kv runtime-kv-wide">
          <span>Waiting</span>
          <strong className={isTimeWaitActive(runtime) ? "runtime-wait-countdown" : undefined}>
            {liveWaitSummary}
          </strong>
        </div>
      </div>

      {runtime.scenario_wait_operator_prompt ? (
        <div className="runtime-note">{runtime.scenario_wait_operator_prompt}</div>
      ) : null}
      {runtime.scenario_operator_message ? <div className="runtime-note">{runtime.scenario_operator_message}</div> : null}
      {runtime.scenario_last_error ? <div className="runtime-note error">{runtime.scenario_last_error}</div> : null}

      <div className="command-button-row">
        <Button size="sm" onClick={onApprove} disabled={actionBusy || !canApproveWait}>
          {runtime.scenario_wait_operator_label || "Continue"}
        </Button>
        <Button size="sm" tone="secondary" onClick={onSkipWait} disabled={actionBusy || !canSkipWait}>
          {runtime.scenario_wait_type === "time"
            ? "Skip Time"
            : runtime.scenario_wait_operator_skip_label || "Skip Wait"}
        </Button>
        <Button size="sm" tone="secondary" onClick={onPause} disabled={actionBusy || !canPauseTimer}>
          Pause
        </Button>
        <Button size="sm" tone="secondary" onClick={onResume} disabled={actionBusy || !canResumeTimer}>
          Resume
        </Button>
        <Button size="sm" tone="secondary" onClick={onPlusMinute} disabled={actionBusy || !canAdjustTimer}>
          +1 Min
        </Button>
        <Button size="sm" tone="secondary" onClick={onMinusMinute} disabled={actionBusy || !canAdjustTimer}>
          -1 Min
        </Button>
      </div>

      <details className="surface-details">
        <summary>Manual timer start</summary>
        <div className="command-input-row">
          <input
            className="toolbar-input command-input"
            value={timerStartMinutes}
            onChange={(event) => onTimerStartMinutesChange(event.target.value)}
            placeholder="Minutes"
          />
          <Button size="sm" onClick={onStartTimer} disabled={actionBusy}>
            Start Timer
          </Button>
          <Button size="sm" tone="secondary" onClick={onResetTimer} disabled={actionBusy}>
            Reset Timer
          </Button>
        </div>
      </details>
    </Panel>
  );
}
