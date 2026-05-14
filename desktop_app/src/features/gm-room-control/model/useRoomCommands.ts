import { useState } from "react";
import { useMutation, useQueryClient } from "@tanstack/react-query";
import { useControllerStore } from "@/domains/controller";
import {
  addTimerMs,
  approveScenarioWait,
  clearHint as clearRoomHint,
  nextScenarioStep,
  pauseTimer,
  resetGame,
  resetTimer,
  resumeTimer,
  sendHint as sendRoomHint,
  startGame,
  startTimer,
  stopGame,
  type GmRoomRuntime,
} from "@/domains/gm";

export interface CommandFeedback {
  status: "idle" | "pending" | "success" | "error";
  message: string;
}

type RoomInvalidationScope =
  | "runtime"
  | "profiles"
  | "scenarios"
  | "state";

export function useRoomCommands(roomId?: string, runtime?: GmRoomRuntime) {
  const activeController = useControllerStore((state) => state.activeController);
  const queryClient = useQueryClient();
  const [hintMessage, setHintMessage] = useState("");
  const [timerStartMinutes, setTimerStartMinutes] = useState(
    runtime ? String(Math.max(1, Math.round((runtime.timer_duration_ms || 3600000) / 60000))) : "60",
  );
  const [commandFeedback, setCommandFeedback] = useState<CommandFeedback>({
    status: "idle",
    message: "",
  });

  async function invalidateRoomData(scopes: RoomInvalidationScope[]) {
    const baseUrl = activeController?.baseUrl ?? "none";
    await Promise.all(
      scopes.map((scope) => {
        switch (scope) {
          case "runtime":
            return queryClient.invalidateQueries({
              queryKey: ["controller", baseUrl, "gm", "room-runtime", roomId ?? "none"],
            });
          case "profiles":
            return queryClient.invalidateQueries({
              queryKey: ["controller", baseUrl, "gm", "room-profiles", roomId ?? "none"],
            });
          case "scenarios":
            return queryClient.invalidateQueries({
              queryKey: ["controller", baseUrl, "gm", "room-scenarios", roomId ?? "none"],
            });
          case "state":
            return queryClient.invalidateQueries({
              queryKey: ["controller", baseUrl, "gm", "state"],
            });
        }
      }),
    );
  }

  const commandMutation = useMutation({
    mutationFn: async ({
      action,
      successMessage,
      invalidateScopes,
    }: {
      action: (baseUrl: string, roomId: string) => Promise<unknown>;
      successMessage: string;
      invalidateScopes: RoomInvalidationScope[];
    }) => {
      if (!activeController?.baseUrl) {
        throw new Error("No controller connected");
      }
      if (!roomId) {
        throw new Error("No room selected");
      }

      return {
        result: await action(activeController.baseUrl, roomId),
        successMessage,
        invalidateScopes,
      };
    },
    onMutate: ({ successMessage }) => {
      setCommandFeedback({
        status: "pending",
        message: `Sending command: ${successMessage}...`,
      });
    },
    onSuccess: async ({ successMessage, invalidateScopes }) => {
      setCommandFeedback({
        status: "success",
        message: successMessage,
      });
      await invalidateRoomData(invalidateScopes);
    },
    onError: (error) => {
      setCommandFeedback({
        status: "error",
        message: error instanceof Error ? error.message : "Command failed",
      });
    },
  });

  function runRoomCommand(
    action: (baseUrl: string, roomId: string) => Promise<unknown>,
    successMessage: string,
    invalidateScopes: RoomInvalidationScope[] = ["runtime"],
  ) {
    commandMutation.mutate({
      action,
      successMessage,
      invalidateScopes,
    });
  }

  function normalizedTimerDurationMs() {
    return Math.max(1, Number(timerStartMinutes || "0")) * 60000;
  }

  const sessionPresent =
    !!runtime?.session_present ||
    runtime?.session_state === "running" ||
    runtime?.session_state === "paused" ||
    runtime?.session_state === "finished";
  const canPauseTimer = runtime?.timer_state === "running";
  const canResumeTimer = runtime?.timer_state === "paused";
  const canAdjustTimer = !!runtime && (runtime.timer_duration_ms > 0 || runtime.timer_remaining_ms > 0);

  return {
    actionBusy: commandMutation.isPending,
    commandFeedback,
    setCommandFeedback,
    hintMessage,
    setHintMessage,
    timerStartMinutes,
    setTimerStartMinutes,
    sessionPresent,
    canPauseTimer,
    canResumeTimer,
    canAdjustTimer,
    startGame: () => runRoomCommand(startGame, "Game start accepted", ["runtime"]),
    stopGame: () => runRoomCommand(stopGame, "Game stop accepted", ["runtime"]),
    resetGame: () => runRoomCommand(resetGame, "Game reset accepted", ["runtime"]),
    approveScenarioWait: () => runRoomCommand(approveScenarioWait, "Scenario approve accepted", ["runtime"]),
    nextScenarioStep: () => runRoomCommand(nextScenarioStep, "Scenario next accepted", ["runtime"]),
    nextScenarioBranchStep: (branchId: string) =>
      runRoomCommand(
        (baseUrl, selectedRoomId) => nextScenarioStep(baseUrl, selectedRoomId, branchId),
        `Scenario branch next accepted: ${branchId}`,
        ["runtime"],
      ),
    pauseTimer: () => runRoomCommand(pauseTimer, "Timer pause accepted", ["runtime"]),
    resumeTimer: () => runRoomCommand(resumeTimer, "Timer resume accepted", ["runtime"]),
    addTimerMs: (deltaMs: number) =>
      runRoomCommand(
        (baseUrl, selectedRoomId) => addTimerMs(baseUrl, selectedRoomId, deltaMs),
        deltaMs >= 0 ? "Timer +1 min accepted" : "Timer -1 min accepted",
        ["runtime"],
      ),
    startTimer: () =>
      runRoomCommand(
        (baseUrl, selectedRoomId) => startTimer(baseUrl, selectedRoomId, normalizedTimerDurationMs()),
        "Timer start accepted",
        ["runtime"],
      ),
    resetTimer: () =>
      runRoomCommand(
        (baseUrl, selectedRoomId) => resetTimer(baseUrl, selectedRoomId, normalizedTimerDurationMs()),
        "Timer reset accepted",
        ["runtime"],
      ),
    sendHint: () =>
      runRoomCommand(
        (baseUrl, selectedRoomId) =>
          sendRoomHint(baseUrl, selectedRoomId, hintMessage.trim() || "Operator hint"),
        "Hint send accepted",
        ["runtime"],
      ),
    clearHint: () => runRoomCommand(clearRoomHint, "Hint clear accepted", ["runtime"]),
  };
}
