import { Link } from "react-router-dom";
import { useControllerStore } from "@/domains/controller";
import { useGmStateQuery } from "@/domains/gm";
import { Badge } from "@/shared/ui/Badge";
import { EmptyState } from "@/shared/ui/EmptyState";
import { ErrorState } from "@/shared/ui/ErrorState";
import { LoadingState } from "@/shared/ui/LoadingState";
import { Panel } from "@/shared/ui/Panel";

function formatDuration(ms: number): string {
  const totalSeconds = Math.max(0, Math.floor(ms / 1000));
  const minutes = Math.floor(totalSeconds / 60);
  const seconds = totalSeconds % 60;
  return `${String(minutes).padStart(2, "0")}:${String(seconds).padStart(2, "0")}`;
}

function roomTone(health: string): "success" | "danger" | "muted" {
  if (health === "fault") {
    return "danger";
  }
  if (health === "ok") {
    return "success";
  }
  return "muted";
}

export function GmDashboardPage() {
  const activeController = useControllerStore((state) => state.activeController);
  const connectionStatus = useControllerStore((state) => state.connectionStatus);
  const gmStateQuery = useGmStateQuery();

  const summary = gmStateQuery.data?.summary;
  const rooms = gmStateQuery.data?.rooms ?? [];

  return (
    <section className="page">
      <div>
        <div className="page-title">GM Dashboard</div>
        <div className="page-subtitle">All-rooms operational overview.</div>
      </div>

      {!activeController ? (
        <EmptyState
          title="No controller connected"
          description="Connect to a SceneHub Controller to load dashboard state."
        />
      ) : null}

      {activeController && connectionStatus === "unsupported" ? (
        <ErrorState
          title="Unsupported controller"
          description="This controller does not match the current desktop API contract."
        />
      ) : null}

      {activeController && gmStateQuery.isLoading ? (
        <LoadingState label="Loading GM dashboard state..." />
      ) : null}

      {activeController && gmStateQuery.isError ? (
        <ErrorState
          title="GM state unavailable"
          description={
            gmStateQuery.error instanceof Error
              ? gmStateQuery.error.message
              : "Dashboard state could not be loaded from the controller."
          }
        />
      ) : null}

      {summary ? (
        <div className="dashboard-summary-grid">
          <Panel title="Rooms">
            <div className="metric-value">{summary.rooms_total}</div>
            <div className="metric-label">{summary.active_sessions} active sessions</div>
          </Panel>
          <Panel title="Devices">
            <div className="metric-value">{summary.devices_total}</div>
            <div className="metric-label">Controller-visible devices</div>
          </Panel>
          <Panel title="Issues">
            <div className="metric-value">{summary.issues_total}</div>
            <div className="metric-label">Warnings and faults</div>
          </Panel>
          <Panel title="Hints">
            <div className="metric-value">{summary.active_hints}</div>
            <div className="metric-label">Active operator hints</div>
          </Panel>
        </div>
      ) : null}

      {summary ? (
        <Panel title="Controller Overview" subtitle={`Active profile: ${gmStateQuery.data?.active_profile || "n/a"}`}>
          <div className="dashboard-flags">
            <Badge tone={summary.has_fault ? "danger" : "muted"}>
              {summary.has_fault ? "fault present" : "no fault"}
            </Badge>
            <Badge tone={summary.has_degraded ? "danger" : "success"}>
              {summary.has_degraded ? "degraded rooms" : "healthy"}
            </Badge>
          </div>
        </Panel>
      ) : null}

      {rooms.length > 0 ? (
        <div className="room-card-grid">
          {rooms
            .slice()
            .sort((a, b) => a.sort_order - b.sort_order)
            .map((room) => (
              <Link
                key={room.room_id}
                to={`/gm/rooms/${room.room_id}`}
                className="room-card"
              >
                <div className="room-card-head">
                  <div>
                    <div className="room-card-title">{room.title}</div>
                    <div className="room-card-subtitle">{room.room_id}</div>
                  </div>
                  <Badge tone={roomTone(room.health)}>{room.health}</Badge>
                </div>

                <div className="room-card-metrics">
                  <div className="room-card-metric">
                    <span>Session</span>
                    <strong>{room.session_state}</strong>
                  </div>
                  <div className="room-card-metric">
                    <span>Timer</span>
                    <strong>{formatDuration(room.timer_remaining_ms)}</strong>
                  </div>
                  <div className="room-card-metric">
                    <span>Devices</span>
                    <strong>
                      {room.active_device_count}/{room.device_count}
                    </strong>
                  </div>
                  <div className="room-card-metric">
                    <span>Issues</span>
                    <strong>{room.issue_count}</strong>
                  </div>
                </div>

                <div className="room-card-runtime">
                  <div>
                    <span>Scenario</span>
                    <strong>{room.running_scenario_name || room.selected_scenario_name || "none"}</strong>
                  </div>
                  <div>
                    <span>Runtime</span>
                    <strong>{room.scenario_runtime_state}</strong>
                  </div>
                  <div>
                    <span>Step</span>
                    <strong>{room.scenario_current_step_index}</strong>
                  </div>
                </div>

                {room.hint_active || room.scenario_operator_message || room.scenario_last_error ? (
                  <div className="room-card-note">
                    {room.scenario_last_error ||
                      room.scenario_operator_message ||
                      room.hint_message ||
                      "Operator attention required"}
                  </div>
                ) : null}
              </Link>
            ))}
        </div>
      ) : null}
    </section>
  );
}
