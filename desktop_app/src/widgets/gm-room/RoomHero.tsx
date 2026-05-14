import type { GmRoomRuntime, GmRoomProfile, GmRoomScenario } from "@/domains/gm";
import {
  badgeToneForState,
  describeWaitTarget,
  formatDuration,
  formatIdentifierLabel,
} from "@/domains/gm/lib/room_runtime_format";
import { Badge } from "@/shared/ui/Badge";
import { Panel } from "@/shared/ui/Panel";

interface RoomHeroProps {
  roomTitle?: string;
  runtime: GmRoomRuntime;
  selectedProfile: GmRoomProfile | null;
  selectedScenario: GmRoomScenario | null;
}

export function RoomHero({ roomTitle, runtime, selectedProfile, selectedScenario }: RoomHeroProps) {
  const waitSummary = describeWaitTarget(runtime);

  return (
    <Panel
      title={roomTitle || formatIdentifierLabel(runtime.room_id)}
      subtitle={`${selectedProfile?.name || runtime.selected_profile_name || "No game mode"} / ${runtime.running_scenario_name || selectedScenario?.name || runtime.selected_scenario_name || "No scenario"}`}
    >
      <div className="room-control-hero">
        <div className="room-runtime-header">
          <Badge tone={badgeToneForState(runtime.session_state)}>{runtime.session_state}</Badge>
          <Badge tone={badgeToneForState(runtime.timer_state)}>{runtime.timer_state}</Badge>
          <Badge tone={badgeToneForState(runtime.scenario_runtime_state)}>{runtime.scenario_runtime_state}</Badge>
          <Badge tone={badgeToneForState(runtime.asset_prepare_state)}>
            assets {runtime.asset_prepare_state}
          </Badge>
        </div>
        <div className="room-hero-metrics">
          <div className="runtime-kv compact">
            <span>Timer</span>
            <strong>{formatDuration(runtime.timer_remaining_ms)}</strong>
          </div>
          <div className="runtime-kv compact">
            <span>Current step</span>
            <strong>{runtime.scenario_current_step_index}</strong>
          </div>
          <div className="runtime-kv compact">
            <span>Wait</span>
            <strong>{runtime.scenario_wait_type}</strong>
          </div>
          <div className="runtime-kv compact">
            <span>Target</span>
            <strong>{waitSummary}</strong>
          </div>
        </div>
      </div>
    </Panel>
  );
}
