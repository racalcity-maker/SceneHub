import { ControllerPicker } from "@/widgets/controller-picker";
import { ControllerAuthPanel } from "@/widgets/controller-auth";
import { ControllerStatus } from "@/widgets/controller-status";

export function TopToolbar() {
  return (
    <header className="top-toolbar">
      <div>
        <strong>SceneHub Desktop</strong>
      </div>
      <div className="toolbar-actions">
        <ControllerPicker />
        <ControllerAuthPanel />
        <ControllerStatus />
      </div>
    </header>
  );
}
