import { useParams } from "react-router-dom";
import { useGmRoomViewModel } from "@/features/gm-room-control/model/useGmRoomViewModel";
import { EmptyState } from "@/shared/ui/EmptyState";
import { ErrorState } from "@/shared/ui/ErrorState";
import { LoadingState } from "@/shared/ui/LoadingState";
import {
  AssetsDiagnosticsPanel,
  FlagsBranchesPanel,
  GameControlPanel,
  RuntimePanel,
  ScenarioSnapshotPanel,
  SessionDetailsPanel,
} from "@/widgets/gm-room";

export function GmRoomPage() {
  const { roomId } = useParams();
  const vm = useGmRoomViewModel(roomId);

  return (
    <section className="page">
      <div className="page-title-row">
        <div className="page-title">Room Control</div>
        <div className="page-context-chip">{vm.header.roomTitle}</div>
        {vm.header.modeScenarioLabel ? (
          <div className="page-context-chip subtle">{vm.header.modeScenarioLabel}</div>
        ) : null}
      </div>

      {!vm.hasActiveController ? (
        <EmptyState
          title="No controller connected"
          description="Connect to a SceneHub Controller to load room runtime."
        />
      ) : null}

      {!roomId ? (
        <ErrorState
          title="Room not selected"
          description="Open Room Control from GM Dashboard to inspect a specific room."
        />
      ) : null}

      {vm.hasActiveController && roomId && vm.roomRuntimeQuery.isLoading ? (
        <LoadingState label={`Loading runtime for ${roomId}...`} />
      ) : null}

      {vm.hasActiveController && roomId && vm.roomRuntimeQuery.isError ? (
        <ErrorState
          title="Room runtime unavailable"
          description={
            vm.roomRuntimeQuery.error instanceof Error
              ? vm.roomRuntimeQuery.error.message
              : "Controller did not return room runtime."
          }
        />
      ) : null}

      {vm.runtime && vm.gameControl && vm.runtimePanel && vm.scenarioSnapshot && vm.sessionDetails && vm.flagsBranches && vm.assetsDiagnostics ? (
        <>
          <div className="room-control-grid">
            <GameControlPanel {...vm.gameControl} />
            <RuntimePanel {...vm.runtimePanel} />
          </div>

          <ScenarioSnapshotPanel {...vm.scenarioSnapshot} />

          <details className="surface-details room-additional-details">
            <summary>Additional details</summary>
            <div className="room-additional-stack">
              <div className="room-runtime-grid">
                <SessionDetailsPanel {...vm.sessionDetails} />
                <FlagsBranchesPanel {...vm.flagsBranches} />
              </div>

              <AssetsDiagnosticsPanel {...vm.assetsDiagnostics} />
            </div>
          </details>
        </>
      ) : null}
    </section>
  );
}
