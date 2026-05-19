import { useEffect, useMemo, useState } from "react";
import type {
  GmRoomRuntime,
  GmRoomScenario,
  GmRoomScenarioBranch,
  GmRoomScenarioStep,
  GmRoomScenarioVariant,
} from "@/domains/gm";
import { formatDuration, formatIdentifierLabel } from "@/domains/gm/lib/room_runtime_format";
import { Badge } from "@/shared/ui/Badge";
import { Button } from "@/shared/ui/Button";
import { Panel } from "@/shared/ui/Panel";
import { ScenarioStepTypeIcon } from "@/widgets/gm-room/ScenarioStepTypeIcon";

interface ScenarioSnapshotPanelProps {
  runtime: GmRoomRuntime;
  selectedScenario: GmRoomScenario | null;
  actionBusy: boolean;
  onNextBranch: (branchId: string) => void;
}

function displayStepLabel(step: GmRoomScenarioStep, index: number): string {
  return step.label || (step.id ? formatIdentifierLabel(step.id) : `Step ${index + 1}`);
}

function displayBranchLabel(branch: GmRoomScenarioBranch, index: number): string {
  return branch.name || (branch.id ? formatIdentifierLabel(branch.id) : `Branch ${index + 1}`);
}

function displayVariantLabel(variant: GmRoomScenarioVariant, index: number): string {
  return variant.label || (variant.id ? formatIdentifierLabel(variant.id) : `Variant ${index + 1}`);
}

function stepParamsObject(step: GmRoomScenarioStep): Record<string, unknown> {
  return step.params && typeof step.params === "object" && !Array.isArray(step.params)
    ? (step.params as Record<string, unknown>)
    : {};
}

function compactText(value: string, maxLength: number): string {
  if (value.length <= maxLength) {
    return value;
  }
  return `${value.slice(0, Math.max(0, maxLength - 1))}…`;
}

function audioBaseName(path: string): string {
  if (!path) {
    return "";
  }
  const parts = String(path).split("/").filter(Boolean);
  return parts.length > 0 ? parts[parts.length - 1] : path;
}

function buildScenarioAudioLabel(step: GmRoomScenarioStep): string | null {
  const params = stepParamsObject(step);
  const command = String(step.command_id || "").toLowerCase();
  const channel = String(params.channel || "effect").toLowerCase();
  const file = compactText(audioBaseName(String(params.file || "")), 28);

  if (command === "play") {
    const kind = channel === "background" || channel === "bg" || channel === "music" ? "bg" : "sfx";
    return file ? `Play ${kind}: ${file}` : `Play ${kind}`;
  }
  if (command === "stop") {
    return `Stop audio${params.channel ? `: ${params.channel}` : ""}`;
  }
  if (command === "pause") {
    return "Pause audio";
  }
  if (command === "resume") {
    return "Resume audio";
  }
  if (command === "set_volume") {
    return `Set volume${params.volume !== undefined ? `: ${params.volume}` : ""}`;
  }

  return `Audio: ${formatIdentifierLabel(step.command_id || "command")}`;
}

function buildSemanticStepLabel(step: GmRoomScenarioStep): string | null {
  const type = String(step.type || "").trim().toLowerCase();

  if (type === "device_command") {
    if (String(step.device_id || "") === "system_audio") {
      return buildScenarioAudioLabel(step);
    }
    if (step.device_id || step.command_id) {
      return `${formatIdentifierLabel(step.device_id || "device")} - ${formatIdentifierLabel(
        step.command_id || "command",
      )}`;
    }
  }

  if (type === "device_command_group") {
    return `Command group${step.command_count > 0 ? ` (${step.command_count})` : ""}`;
  }

  if (type === "wait_device_event") {
    const device = formatIdentifierLabel(step.device_id || "device");
    const event = formatIdentifierLabel(step.event_id || step.source_id || "event");
    return `Wait ${device} - ${event}`;
  }

  if (type === "wait_time") {
    return `Wait ${Math.max(1, Math.round((Number(step.duration_ms) || 1000) / 1000))} sec`;
  }

  if (type === "operator_approval") {
    return step.operator_prompt || step.operator_approve_label || "Operator approval";
  }

  if (type === "show_operator_message") {
    return step.operator_message || "Show operator message";
  }

  if (type === "set_flag") {
    return `Set flag: ${formatIdentifierLabel(step.flag_name || "flag")}`;
  }

  if (type === "wait_flags") {
    return `Wait flags${step.flag_count > 0 ? ` (${step.flag_count})` : ""}`;
  }

  if (type === "wait_any_device_event") {
    return `Wait any event${step.event_count > 0 ? ` (${step.event_count})` : ""}`;
  }

  if (type === "wait_all_device_events") {
    return `Wait all events${step.event_count > 0 ? ` (${step.event_count})` : ""}`;
  }

  if (type === "end_game") {
    return "End game";
  }

  return null;
}

type RuntimeBranch = GmRoomRuntime["scenario_branches"][number];

type ScenarioStepVisualState = "done" | "current" | "waiting" | "error" | "future";

function getBranchStepVisualState(
  branchRuntime: RuntimeBranch | undefined,
  index: number,
  globalIndex: number,
): ScenarioStepVisualState {
  if (!branchRuntime) {
    return "future";
  }

  const explicitStep = Array.isArray(branchRuntime.steps)
    ? branchRuntime.steps.find((step) => Number(step.index) === index)
    : undefined;
  const explicitState = String(explicitStep?.state || "").toLowerCase();

  if (explicitState === "done") {
    return "done";
  }
  if (explicitState === "current") {
    return "current";
  }
  if (explicitState === "waiting") {
    return "waiting";
  }
  if (explicitState === "error") {
    return "error";
  }

  const failedStepIndex = Number(branchRuntime.failed_step_index);
  if (Number.isFinite(failedStepIndex) && (failedStepIndex === index || failedStepIndex === globalIndex)) {
    return "error";
  }

  const doneSteps = Math.max(0, Number(branchRuntime.done_steps ?? branchRuntime.completed_step_count) || 0);
  if (String(branchRuntime.state || "").toLowerCase() === "done" || index < doneSteps) {
    return "done";
  }

  const currentLocalIndex = Number(branchRuntime.current_step_local_index);
  const currentGlobalIndex = Number(branchRuntime.current_step_index);
  const isCurrent =
    (Number.isFinite(currentLocalIndex) && currentLocalIndex === index) ||
    (Number.isFinite(currentGlobalIndex) && currentGlobalIndex === globalIndex);

  if (isCurrent) {
    const branchState = String(branchRuntime.state || "").toLowerCase();
    if (branchState === "error") {
      return "error";
    }
    if (branchState === "waiting") {
      return "waiting";
    }
    if (branchState === "running") {
      return "current";
    }
  }

  return "future";
}

function getScenarioStepStatusLabel(state: ScenarioStepVisualState): string {
  if (state === "done") {
    return "done";
  }

  if (state === "current") {
    return "current";
  }

  if (state === "waiting") {
    return "waiting";
  }

  if (state === "error") {
    return "error";
  }

  return "";
}

function canNextRuntimeBranch(branchRuntime: GmRoomRuntime["scenario_branches"][number] | undefined): boolean {
  if (!branchRuntime) {
    return false;
  }

  const state = String(branchRuntime.state || "").toLowerCase();

  return state === "waiting" || state === "running";
}

function getBranchWaitRemainingMs(branchRuntime: RuntimeBranch | undefined, liveRuntimeNowMs: number): number | null {
  if (!branchRuntime || String(branchRuntime.wait_type || "").toLowerCase() !== "time") {
    return null;
  }

  if (!branchRuntime.wait_until_ms || !liveRuntimeNowMs) {
    return null;
  }

  return Math.max(0, branchRuntime.wait_until_ms - liveRuntimeNowMs);
}

function renderCompactStepList(
  steps: GmRoomScenarioStep[],
  keyPrefix: string,
  branchRuntime?: RuntimeBranch,
  liveRuntimeNowMs = 0,
  globalStartIndex = 0,
) {
  if (steps.length === 0) {
    return <div className="runtime-empty-line">No steps in this branch</div>;
  }

  return (
    <div className="scenario-step-compact-list">
      {steps.map((step, index) => {
        const state = getBranchStepVisualState(branchRuntime, index, globalStartIndex + index);

        const isCurrent = state === "current" || state === "waiting" || state === "error";
        const statusLabel = getScenarioStepStatusLabel(state);
        const branchWaitRemainingMs = state === "current" || state === "waiting"
          ? getBranchWaitRemainingMs(branchRuntime, liveRuntimeNowMs)
          : null;

        const stepStatusLabel = branchWaitRemainingMs !== null
          ? formatDuration(branchWaitRemainingMs)
          : statusLabel;

        return (
          <div
            key={step.id || `${keyPrefix}-${index}`}
            className={`scenario-step-row ${state}${step.enabled === false ? " disabled" : ""}`}
          >
            <div className="scenario-step-row-main">
              <span className="scenario-step-row-index">
                {state === "done" ? "✓" : index + 1}
              </span>

              <div className="scenario-step-row-text">
                <strong>{buildSemanticStepLabel(step) || displayStepLabel(step, index)}</strong>
              </div>
            </div>

            {stepStatusLabel ? (
              <span className={`scenario-step-status ${state}${branchWaitRemainingMs !== null ? " time" : ""}`}>
                {stepStatusLabel}
              </span>
            ) : null}

            <ScenarioStepTypeIcon current={isCurrent} step={step} />
          </div>
        );
      })}
    </div>
  );
}

function renderReactiveVariants(
  branch: GmRoomScenarioBranch,
  branchIndex: number,
  scenarioId: string,
) {
  if (branch.variants.length === 0) {
    return <div className="runtime-empty-line">No reactive variants exposed by controller</div>;
  }

  return (
    <div className="scenario-reactive-variant-list">
      {branch.variants.map((variant, variantIndex) => (
        <div
          className="branch-card branch-card-compact"
          key={variant.id || `${scenarioId}-branch-${branchIndex}-variant-${variantIndex}`}
        >
          <div className="branch-card-head">
            <strong>{displayVariantLabel(variant, variantIndex)}</strong>
            <Badge tone="muted">{variant.actions.length} actions</Badge>
          </div>
          {variant.actions.length > 0 ? (
            renderCompactStepList(
            variant.actions,
              `${scenarioId}-branch-${branchIndex}-variant-${variantIndex}-action`,
            )
          ) : (
            <div className="runtime-empty-line">No actions in this variant</div>
          )}
        </div>
      ))}
    </div>
  );
}

export function ScenarioSnapshotPanel({
  runtime,
  selectedScenario,
  actionBusy,
  onNextBranch,
}: ScenarioSnapshotPanelProps) {
  const steps = selectedScenario?.steps ?? [];
  const branches = selectedScenario?.branches ?? [];
  const flowBranches = branches.filter((branch) => (branch.type || "normal") !== "reactive");
  const reactiveBranches = branches.filter((branch) => (branch.type || "normal") === "reactive");
  const hasStructuredBranches = branches.length > 0;
  const primaryRuntimeBranch =
    runtime.scenario_branches.find((branch) => (branch.type || "normal") !== "reactive") ??
    runtime.scenario_branches[0];
  const [clientNowMs, setClientNowMs] = useState(() => Date.now());

  const snapshotClock = useMemo(
    () => ({
      clientReceivedAtMs: Date.now(),
      runtimeNowMs: runtime.runtime_now_ms || 0,
    }),
    [
      runtime.runtime_now_ms,
      runtime.running_scenario_generation,
      runtime.scenario_wait_until_ms,
      runtime.scenario_wait_type,
      runtime.scenario_runtime_state,
    ],
  );

  const hasTimeWait =
    runtime.scenario_wait_type === "time" ||
    runtime.scenario_branches.some((branch) => branch.active && branch.wait_type === "time" && branch.wait_until_ms > 0);

  useEffect(() => {
    if (!hasTimeWait) {
      return;
    }

    const id = window.setInterval(() => {
      setClientNowMs(Date.now());
    }, 500);

    return () => window.clearInterval(id);
  }, [hasTimeWait]);

  const elapsedClientMs = Math.max(0, clientNowMs - snapshotClock.clientReceivedAtMs);
  const liveRuntimeNowMs =
    snapshotClock.runtimeNowMs > 0
      ? snapshotClock.runtimeNowMs + elapsedClientMs
      : 0;

  return (
    <Panel>
      {selectedScenario?.valid === false && selectedScenario.validation_issues.length > 0 ? (
        <div className="runtime-note error">
          {selectedScenario.validation_issues[0]?.message || "Scenario validation failed"}
        </div>
      ) : null}

      {selectedScenario && (selectedScenario.valid === false || (selectedScenario.warning_count ?? 0) > 0) ? (
        <div className="scenario-status-row">
          <Badge tone={selectedScenario.valid === false ? "danger" : "success"}>
            {selectedScenario.valid === false
              ? `${selectedScenario.validation_issue_count} issues`
              : "valid"}
          </Badge>
          {(selectedScenario.warning_count ?? 0) > 0 ? (
            <Badge tone="muted">{selectedScenario.warning_count} warnings</Badge>
          ) : null}
        </div>
      ) : null}

      <div className="scenario-snapshot-layout">
        <div className="scenario-steps-panel">
          <div className="runtime-section-title">{hasStructuredBranches ? "Flow Branches" : "Steps"}</div>

          {hasStructuredBranches ? (
            <>
              {flowBranches.length > 0 ? (
                <div className="scenario-branch-grid">
                  {flowBranches.map((branch, branchIndex) => {
                    const realBranchIndex = branches.findIndex((item) => item.id === branch.id);
                    const runtimeIndex = realBranchIndex >= 0 ? realBranchIndex : branchIndex;
                    const branchRuntime =
                      runtime.scenario_branches.find((item) => item.id === branch.id) ??
                      runtime.scenario_branches.find((item) => item.index === runtimeIndex);

                    return (
                      <div
                        className="branch-card branch-card-compact"
                        key={branch.id || `${selectedScenario?.id || "scenario"}-branch-${runtimeIndex}`}
                      >
                        <div className="branch-card-head">
                          <strong>{displayBranchLabel(branch, branchIndex)}</strong>

                          <div className="branch-card-actions">
                            <Badge tone={branchRuntime?.active ? "success" : "muted"}>
                              {branchRuntime?.state || "normal"}
                            </Badge>

                            {canNextRuntimeBranch(branchRuntime) && branchRuntime?.id ? (
                              <Button
                                size="sm"
                                tone="secondary"
                                disabled={actionBusy}
                                onClick={() => onNextBranch(branchRuntime.id)}
                                title={`Continue branch ${branchRuntime.id}`}
                              >
                                Next
                              </Button>
                            ) : null}
                          </div>
                        </div>
                        <div className="branch-card-meta">
                          <span>{branch.steps.length} steps</span>
                          {branchRuntime ? <span>runtime: {branchRuntime.state}</span> : null}
                        </div>
                        {renderCompactStepList(
                          branch.steps,
                          `${selectedScenario?.id || "scenario"}-branch-${runtimeIndex}-step`,
                          branchRuntime,
                          liveRuntimeNowMs,
                          Number(branchRuntime?.step_start_index) || 0,
                        )}
                      </div>
                    );
                  })}
                </div>
              ) : (
                <div className="runtime-empty-line">No flow branches exposed by controller</div>
              )}

              {reactiveBranches.length > 0 ? (
                <details className="surface-details scenario-reactive-details">
                  <summary>Reactive branches ({reactiveBranches.length})</summary>
                  <div className="scenario-reactive-grid">
                    {reactiveBranches.map((branch, branchIndex) => {
                      const realBranchIndex = branches.findIndex((item) => item.id === branch.id);
                      const runtimeIndex = realBranchIndex >= 0 ? realBranchIndex : branchIndex;
                      const branchRuntime =
                        runtime.scenario_branches.find((item) => item.id === branch.id) ??
                        runtime.scenario_branches.find((item) => item.index === runtimeIndex);

                      return (
                        <div
                          className="branch-card branch-card-compact"
                          key={branch.id || `${selectedScenario?.id || "scenario"}-reactive-${runtimeIndex}`}
                        >
                          <div className="branch-card-head">
                            <strong>{displayBranchLabel(branch, branchIndex)}</strong>
                            <Badge tone={branchRuntime?.active ? "success" : "muted"}>
                              {branchRuntime?.state || "reactive"}
                            </Badge>
                          </div>
                          <div className="branch-card-meta">
                            <span>{branch.variants.length} variants</span>
                            {branchRuntime ? <span>runtime: {branchRuntime.state}</span> : null}
                          </div>
                          {renderReactiveVariants(branch, runtimeIndex, selectedScenario?.id || "scenario")}
                        </div>
                      );
                    })}
                  </div>
                </details>
              ) : null}

                  {runtime.scenario_branches.length > 0 ? (
                    <details className="surface-details scenario-reactive-details">
                      <summary>Runtime branches ({runtime.scenario_branches.length})</summary>

                      <div className="branch-list scenario-runtime-branch-list">
                        {runtime.scenario_branches.map((branch) => (
                          <div className="branch-card branch-card-compact" key={branch.id}>
                            <div className="branch-card-head">
                              <strong>{branch.name || formatIdentifierLabel(branch.id)}</strong>

                              <div className="branch-card-actions">
                                <Badge tone={branch.active ? "success" : "muted"}>{branch.state}</Badge>

                                {canNextRuntimeBranch(branch) && branch.id ? (
                                  <Button
                                    size="sm"
                                    tone="secondary"
                                    disabled={actionBusy}
                                    onClick={() => onNextBranch(branch.id)}
                                    title={`Continue branch ${branch.id}`}
                                  >
                                    Next
                                  </Button>
                                ) : null}
                              </div>
                            </div>

                            <div className="branch-card-meta">
                              <span>type: {branch.type}</span>
                              <span>wait: {branch.wait_type}</span>
                              <span>global: {branch.current_step_index}</span>
                              <span>local: {branch.current_step_local_index}</span>
                              <span>done: {branch.done_steps ?? branch.completed_step_count}</span>
                            </div>
                          </div>
                        ))}
                      </div>
                    </details>
                  ) : null}
            </>
          ) : steps.length > 0 ? (
            renderCompactStepList(
              steps,
              selectedScenario?.id || "scenario",
              primaryRuntimeBranch,
              liveRuntimeNowMs,
              Number(primaryRuntimeBranch?.step_start_index) || 0,
            )
          ) : (
            <div className="runtime-empty-line">No scenario steps exposed by controller</div>
          )}
        </div>
      </div>
    </Panel>
  );
}
