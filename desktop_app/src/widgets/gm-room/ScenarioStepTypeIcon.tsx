import type { GmRoomScenarioStep } from "@/domains/gm";
import {
  AlarmClock,
  Bell,
  BellDot,
  Check,
  Clock3,
  Flag,
  Hand,
  MessageSquareMore,
  Music4,
  Radio,
  Square,
  ToggleLeft,
  Waypoints,
} from "lucide-react";

interface ScenarioStepTypeIconProps {
  current?: boolean;
  step: GmRoomScenarioStep;
}

function normalizedStepType(step: GmRoomScenarioStep): string {
  return String(step.type || "step").trim().toLowerCase();
}

function getStepTypeLabel(step: GmRoomScenarioStep): string {
  return step.type || "step";
}

function getStepTypeIcon(step: GmRoomScenarioStep) {
  switch (normalizedStepType(step)) {
    case "device_command":
      if (String(step.device_id || "") === "system_audio") {
        return Music4;
      }
      return ToggleLeft;
    case "device_command_group":
      return Waypoints;
    case "wait_device_event":
      return Radio;
    case "wait_time":
      return Clock3;
    case "operator_approval":
      return Hand;
    case "show_operator_message":
      return MessageSquareMore;
    case "set_flag":
      return Flag;
    case "wait_flags":
      return Bell;
    case "wait_any_device_event":
      return BellDot;
    case "wait_all_device_events":
      return AlarmClock;
    case "end_game":
      return Square;
    default:
      return Check;
  }
}

export function ScenarioStepTypeIcon({ current = false, step }: ScenarioStepTypeIconProps) {
  const Icon = getStepTypeIcon(step);
  const label = getStepTypeLabel(step);

  return (
    <span className={`scenario-step-type-icon${current ? " current" : ""}`} title={label} aria-label={label}>
      <Icon size={16} strokeWidth={2.2} />
    </span>
  );
}
