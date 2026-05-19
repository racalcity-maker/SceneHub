import { useMutation, useQueryClient } from "@tanstack/react-query";
import { z } from "zod";
import { useControllerStore } from "@/domains/controller";
import { httpPostJson } from "@/platform/http/client";
import type { CommandFeedback } from "@/features/gm-room-control/model/useRoomCommands";

const selectionResultSchema = z
  .object({
    ok: z.boolean(),
    room_id: z.string().optional(),
    selected_profile_id: z.string().optional(),
    selected_scenario_id: z.string().optional(),
  })
  .passthrough();

export function useRoomSelections(
  roomId: string | undefined,
  setCommandFeedback: (feedback: CommandFeedback) => void,
) {
  const activeController = useControllerStore((state) => state.activeController);
  const queryClient = useQueryClient();

  function invalidateRoomData() {
    const baseUrl = activeController?.baseUrl ?? "none";
    queryClient.invalidateQueries({
      queryKey: ["controller", baseUrl, "gm", "room-runtime", roomId ?? "none"],
    });
    queryClient.invalidateQueries({
      queryKey: ["controller", baseUrl, "gm", "room-profiles", roomId ?? "none"],
    });
  }

  const profileSelectMutation = useMutation({
    mutationFn: async (profileId: string) => {
      if (!activeController?.baseUrl || !roomId) {
        throw new Error("No controller connected");
      }

      return httpPostJson(
        activeController.baseUrl,
        "/api/gm/room/profile/select",
        selectionResultSchema,
        { room_id: roomId, profile_id: profileId },
      );
    },
    onMutate: () => {
      setCommandFeedback({
        status: "pending",
        message: "Selecting game mode...",
      });
    },
    onSuccess: () => {
      setCommandFeedback({
        status: "success",
        message: "Game mode updated",
      });
      invalidateRoomData();
    },
    onError: (error) => {
      setCommandFeedback({
        status: "error",
        message: error instanceof Error ? error.message : "Profile select failed",
      });
    },
  });

  return {
    selectionBusy: profileSelectMutation.isPending,
    selectProfile: profileSelectMutation.mutate,
  };
}
