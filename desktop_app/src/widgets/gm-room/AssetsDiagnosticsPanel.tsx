import type { GmRoomRuntime } from "@/domains/gm";
import { Panel } from "@/shared/ui/Panel";

interface AssetsDiagnosticsPanelProps {
  runtime: GmRoomRuntime;
}

export function AssetsDiagnosticsPanel({ runtime }: AssetsDiagnosticsPanelProps) {
  return (
    <Panel title="Assets And Diagnostics" subtitle="Audio asset preparation and validation hints">
      <div className="runtime-kv-grid">
        <div className="runtime-kv">
          <span>Asset state</span>
          <strong>{runtime.asset_prepare_state}</strong>
        </div>
        <div className="runtime-kv">
          <span>Audio ready</span>
          <strong>
            {runtime.asset_audio_ready}/{runtime.asset_audio_total}
          </strong>
        </div>
        <div className="runtime-kv">
          <span>Missing</span>
          <strong>{runtime.asset_audio_missing}</strong>
        </div>
        <div className="runtime-kv">
          <span>Unknown</span>
          <strong>{runtime.asset_audio_unknown}</strong>
        </div>
      </div>
      {(runtime.asset_audio_bad > 0 ||
        runtime.asset_audio_unsupported > 0 ||
        runtime.asset_audio_io_error > 0) ? (
        <div className="runtime-note error">
          bad={runtime.asset_audio_bad}, unsupported={runtime.asset_audio_unsupported}, io=
          {runtime.asset_audio_io_error}
        </div>
      ) : null}
    </Panel>
  );
}
