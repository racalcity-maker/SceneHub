import { HashRouter, Navigate, Route, Routes } from "react-router-dom";
import { AppFrame } from "@/app/shell/AppFrame";
import { GmDashboardPage } from "@/pages/gm-dashboard";
import { GmRoomsPage } from "@/pages/gm-rooms";
import { GmRoomPage } from "@/pages/gm-room";
import { DevicesPage } from "@/pages/devices";
import { IssuesPage } from "@/pages/issues";
import { TimelinePage } from "@/pages/timeline";
import { SettingsPage } from "@/pages/settings";

export function AppRouter() {
  return (
    <HashRouter>
      <AppFrame>
        <Routes>
          <Route path="/" element={<Navigate to="/gm/dashboard" replace />} />
          <Route path="/gm/dashboard" element={<GmDashboardPage />} />
          <Route path="/gm/rooms" element={<GmRoomsPage />} />
          <Route path="/gm/rooms/:roomId" element={<GmRoomPage />} />
          <Route path="/devices" element={<DevicesPage />} />
          <Route path="/issues" element={<IssuesPage />} />
          <Route path="/timeline" element={<TimelinePage />} />
          <Route path="/settings/*" element={<SettingsPage />} />
        </Routes>
      </AppFrame>
    </HashRouter>
  );
}
