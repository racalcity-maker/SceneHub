import type { GmRoomProfile, GmRoomRuntime } from "@/domains/gm";
import { formatDuration } from "@/domains/gm/lib/room_runtime_format";
import type { CommandFeedback } from "@/features/gm-room-control/model/useRoomCommands";
import { Badge } from "@/shared/ui/Badge";
import { Button } from "@/shared/ui/Button";
import { Panel } from "@/shared/ui/Panel";

interface GameControlPanelProps {
  runtime: GmRoomRuntime;
  profiles: GmRoomProfile[];
  selectedProfileId: string;
  selectedProfile: GmRoomProfile | null;
  selectedProfileScenarioName: string;
  commandFeedback: CommandFeedback;
  actionBusy: boolean;
  selectDisabled: boolean;
  canStartGame: boolean;
  canStopGame: boolean;
  canResetGame: boolean;
  onSelectProfile: (profileId: string) => void;
  onStartGame: () => void;
  onStopGame: () => void;
  onResetGame: () => void;
}

export function GameControlPanel({
  runtime,
  profiles,
  selectedProfileId,
  selectedProfile,
  selectedProfileScenarioName,
  commandFeedback,
  actionBusy,
  selectDisabled,
  canStartGame,
  canStopGame,
  canResetGame,
  onSelectProfile,
  onStartGame,
  onStopGame,
  onResetGame,
}: GameControlPanelProps) {
  return (
    <Panel title="Game Control" subtitle="Select game mode and control the room session">
      <div className="command-feedback-row compact">
        <Badge
          tone={
            commandFeedback.status === "success"
              ? "success"
              : commandFeedback.status === "error"
                ? "danger"
                : "muted"
          }
        >
          {commandFeedback.status}
        </Badge>
        <span className="command-feedback-text">
          {commandFeedback.message || "Runtime truth comes from controller snapshots and live events"}
        </span>
      </div>

      <label className="field-stack">
        <span>Game mode</span>
        <select
          className="toolbar-select control-select"
          value={selectedProfileId}
          onChange={(event) => {
            if (!event.target.value) {
              return;
            }
            onSelectProfile(event.target.value);
          }}
          disabled={selectDisabled}
        >
          <option value="">{profiles.length === 0 ? "No game modes" : "Select game mode"}</option>
          {profiles.map((profile) => (
            <option key={profile.id} value={profile.id} disabled={profile.valid === false}>
              {profile.name || profile.id} ({formatDuration(profile.duration_ms)}
              {profile.valid === false ? ", invalid" : ""})
            </option>
          ))}
        </select>
      </label>

      <div className="control-kv-grid">
        <div className="runtime-kv">
          <span>Mode</span>
          <strong>{selectedProfile?.name || runtime.selected_profile_name || "none"}</strong>
        </div>
        <div className="runtime-kv">
          <span>Scenario</span>
          <strong>{selectedProfileScenarioName}</strong>
        </div>
        <div className="runtime-kv">
          <span>Duration</span>
          <strong>
            {selectedProfile
              ? formatDuration(selectedProfile.duration_ms)
              : formatDuration(runtime.selected_profile_duration_ms)}
          </strong>
        </div>
        <div className="runtime-kv">
          <span>Hint pack</span>
          <strong>{selectedProfile?.hint_pack_id || "default"}</strong>
        </div>
      </div>

      <div className="command-button-row">
        <Button size="sm" onClick={onStartGame} disabled={actionBusy || !canStartGame}>
          Start Game
        </Button>
        <Button size="sm" tone="secondary" onClick={onStopGame} disabled={actionBusy || !canStopGame}>
          Stop Game
        </Button>
        <Button size="sm" tone="secondary" onClick={onResetGame} disabled={actionBusy || !canResetGame}>
          Reset Game
        </Button>
      </div>
    </Panel>
  );
}
