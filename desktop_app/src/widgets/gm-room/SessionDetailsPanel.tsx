import type { GmRoomRuntime } from "@/domains/gm";
import { formatDuration } from "@/domains/gm/lib/room_runtime_format";
import { Button } from "@/shared/ui/Button";
import { Panel } from "@/shared/ui/Panel";

interface SessionDetailsPanelProps {
  runtime: GmRoomRuntime;
  actionBusy: boolean;
  hintMessage: string;
  onHintMessageChange: (value: string) => void;
  onSendHint: () => void;
  onClearHint: () => void;
}

export function SessionDetailsPanel({
  runtime,
  actionBusy,
  hintMessage,
  onHintMessageChange,
  onSendHint,
  onClearHint,
}: SessionDetailsPanelProps) {
  return (
    <Panel title="Session Details" subtitle="Timer, hint state and operator message">
      <div className="runtime-kv-grid">
        <div className="runtime-kv">
          <span>Session present</span>
          <strong>{runtime.session_present ? "yes" : "no"}</strong>
        </div>
        <div className="runtime-kv">
          <span>Session state</span>
          <strong>{runtime.session_state}</strong>
        </div>
        <div className="runtime-kv">
          <span>Timer</span>
          <strong>{formatDuration(runtime.timer_remaining_ms)}</strong>
        </div>
        <div className="runtime-kv">
          <span>Profile duration</span>
          <strong>{formatDuration(runtime.selected_profile_duration_ms)}</strong>
        </div>
        <div className="runtime-kv">
          <span>Hint status</span>
          <strong>{runtime.hint_active ? "active" : "idle"}</strong>
        </div>
        <div className="runtime-kv">
          <span>Hints sent</span>
          <strong>{runtime.hint_sent_count}</strong>
        </div>
      </div>

      {runtime.hint_message ? <div className="runtime-note">{runtime.hint_message}</div> : null}

      <div className="runtime-section-title">Operator hint</div>
      <div className="command-input-row">
        <input
          className="toolbar-input command-input command-input-wide"
          value={hintMessage}
          onChange={(event) => onHintMessageChange(event.target.value)}
          placeholder="Hint message"
        />
        <Button size="sm" onClick={onSendHint} disabled={actionBusy}>
          Send Hint
        </Button>
        <Button size="sm" tone="secondary" onClick={onClearHint} disabled={actionBusy}>
          Clear Hint
        </Button>
      </div>
    </Panel>
  );
}
