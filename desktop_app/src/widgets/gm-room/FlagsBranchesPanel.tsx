import type { GmRoomRuntime } from "@/domains/gm";
import { badgeToneForState, formatIdentifierLabel } from "@/domains/gm/lib/room_runtime_format";
import { Badge } from "@/shared/ui/Badge";
import { Panel } from "@/shared/ui/Panel";

interface FlagsBranchesPanelProps {
  runtime: GmRoomRuntime;
}

export function FlagsBranchesPanel({ runtime }: FlagsBranchesPanelProps) {
  return (
    <Panel title="Flags And Branches" subtitle="Scenario state machine visibility">
      <div className="runtime-section-stack">
        <div>
          <div className="runtime-section-title">Flags</div>
          {runtime.scenario_flags.length > 0 ? (
            <div className="runtime-badge-wrap">
              {runtime.scenario_flags.map((flag) => (
                <Badge key={flag.name} tone={flag.value ? "success" : "muted"}>
                  {flag.name}={flag.value ? "true" : "false"}
                </Badge>
              ))}
            </div>
          ) : (
            <div className="runtime-empty-line">No runtime flags</div>
          )}
        </div>

        <div>
          <div className="runtime-section-title">Branches</div>
          {runtime.scenario_branches.length > 0 ? (
            <div className="branch-list">
              {runtime.scenario_branches.map((branch) => (
                <div className="branch-card" key={branch.id}>
                  <div className="branch-card-head">
                    <strong>{branch.name || formatIdentifierLabel(branch.id)}</strong>
                    <Badge tone={badgeToneForState(branch.state)}>{branch.state}</Badge>
                  </div>
                  <div className="branch-card-meta">
                    <span>{formatIdentifierLabel(branch.id)}</span>
                    <span>step {branch.current_step_index}</span>
                    <span>{branch.wait_type || "none"}</span>
                  </div>
                </div>
              ))}
            </div>
          ) : (
            <div className="runtime-empty-line">No branch runtime</div>
          )}
        </div>
      </div>
    </Panel>
  );
}
