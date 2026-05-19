import { useControllerStore } from "@/domains/controller";
import {
  useGmStateQuery,
  useGmRoomProfilesQuery,
  useGmRoomRuntimeQuery,
  useGmRoomScenariosQuery,
} from "@/domains/gm";
import { describeWaitTarget } from "@/domains/gm/lib/room_runtime_format";
import { useRoomCommands } from "@/features/gm-room-control/model/useRoomCommands";
import { useRoomSelections } from "@/features/gm-room-control/model/useRoomSelections";

export function useGmRoomViewModel(roomId: string | undefined) {
  const activeController = useControllerStore((state) => state.activeController);
  const roomRuntimeQuery = useGmRoomRuntimeQuery(roomId);
  const roomProfilesQuery = useGmRoomProfilesQuery(roomId);
  const roomScenariosQuery = useGmRoomScenariosQuery(roomId);
  const gmStateQuery = useGmStateQuery();

  const runtime = roomRuntimeQuery.data;
  const profiles = roomProfilesQuery.data?.profiles ?? [];
  const scenarios = roomScenariosQuery.data?.scenarios ?? [];
  const roomTitle =
    gmStateQuery.data?.rooms.find((room) => room.room_id === roomId)?.title ?? runtime?.room_id ?? roomId ?? "unknown";

  const commands = useRoomCommands(roomId, runtime);
  const selections = useRoomSelections(roomId, commands.setCommandFeedback);

  const selectedProfileId = roomProfilesQuery.data?.selected_profile_id || runtime?.selected_profile_id || "";
  const selectedProfile = profiles.find((profile) => profile.id === selectedProfileId) ?? null;
  const selectedScenarioId =
    runtime?.running_scenario_id ||
    runtime?.selected_scenario_id ||
    selectedProfile?.scenario_id ||
    runtime?.selected_profile_scenario_id ||
    "";
  const selectedScenario = scenarios.find((scenario) => scenario.id === selectedScenarioId) ?? null;
  const selectedProfileScenarioName =
    scenarios.find((scenario) => scenario.id === (runtime?.selected_profile_scenario_id || selectedProfile?.scenario_id))
      ?.name ??
    runtime?.selected_profile_scenario_id ??
    "none";
  const headerModeName = selectedProfile?.name || runtime?.selected_profile_name || "";
  const headerScenarioName = selectedScenario?.name || runtime?.selected_scenario_name || "";
  const waitSummary = runtime ? describeWaitTarget(runtime) : "none";

  const actionBusy = commands.actionBusy || selections.selectionBusy;
  const canStartGame = !!selectedProfile && selectedProfile.valid !== false;
  const canStopGame =
    !!runtime && commands.sessionPresent && runtime.session_state !== "finished" && runtime.session_state !== "idle";
  const canResetGame = !!runtime && commands.sessionPresent;
  const canApproveWait =
    (!!runtime?.selected_scenario_id || !!runtime?.running_scenario_id) &&
    runtime?.scenario_runtime_state === "waiting" &&
    runtime.scenario_wait_type === "operator";
  

const waitType = runtime?.scenario_wait_type ?? "none";

const canSkipWait =
  (!!runtime?.selected_scenario_id || !!runtime?.running_scenario_id) &&
  runtime?.scenario_runtime_state === "waiting" &&
  waitType !== "none" &&
  (waitType === "time" || runtime.scenario_wait_operator_skip_allowed);

  return {
    hasActiveController: !!activeController,
    roomId,
    roomRuntimeQuery,
    runtime,
    header: {
      roomTitle,
      modeScenarioLabel: [headerModeName, headerScenarioName].filter(Boolean).join(" / "),
    },
    gameControl: runtime
      ? {
          runtime,
          profiles,
          selectedProfileId,
          selectedProfile,
          selectedProfileScenarioName,
          commandFeedback: commands.commandFeedback,
          actionBusy,
          selectDisabled: actionBusy || roomProfilesQuery.isLoading || profiles.length === 0,
          canStartGame,
          canStopGame,
          canResetGame,
          onSelectProfile: selections.selectProfile,
          onStartGame: commands.startGame,
          onStopGame: commands.stopGame,
          onResetGame: commands.resetGame,
        }
      : null,
    runtimePanel: runtime
      ? {
          runtime,
          selectedScenario,
          waitSummary,
          actionBusy,
          canApproveWait,
          canSkipWait,
          canPauseTimer: commands.canPauseTimer,
          canResumeTimer: commands.canResumeTimer,
          canAdjustTimer: commands.canAdjustTimer,
          timerStartMinutes: commands.timerStartMinutes,
          onTimerStartMinutesChange: commands.setTimerStartMinutes,
          onApprove: commands.approveScenarioWait,
          onSkipWait: commands.nextScenarioStep,
          onPause: commands.pauseTimer,
          onResume: commands.resumeTimer,
          onPlusMinute: () => commands.addTimerMs(60000),
          onMinusMinute: () => commands.addTimerMs(-60000),
          onStartTimer: commands.startTimer,
          onResetTimer: commands.resetTimer,
        }
      : null,
    scenarioSnapshot: runtime
      ? {
          runtime,
          selectedScenario,
          actionBusy,
          onNextBranch: commands.nextScenarioBranchStep,
        }
      : null,
    sessionDetails: runtime
      ? {
          runtime,
          actionBusy,
          hintMessage: commands.hintMessage,
          onHintMessageChange: commands.setHintMessage,
          onSendHint: commands.sendHint,
          onClearHint: commands.clearHint,
        }
      : null,
    flagsBranches: runtime
      ? {
          runtime,
        }
      : null,
    assetsDiagnostics: runtime
      ? {
          runtime,
        }
      : null,
  };
}
