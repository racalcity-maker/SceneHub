import { PropsWithChildren } from "react";
import { NavLink } from "react-router-dom";
import { useControllerConnectionManager } from "@/features/controller-connection/model/useControllerConnectionManager";
import { TopToolbar } from "@/app/shell/TopToolbar";
import { StatusBar } from "@/app/shell/StatusBar";

const navItems = [
  { to: "/gm/dashboard", label: "GM" },
  { to: "/devices", label: "Devices" },
  { to: "/issues", label: "Issues" },
  { to: "/timeline", label: "Timeline" },
  { to: "/settings", label: "Settings" },
];

export function AppFrame({ children }: PropsWithChildren) {
  useControllerConnectionManager();

  return (
    <div className="app-shell">
      <TopToolbar />
      <div className="app-body">
        <aside className="app-sidebar">
          <div className="brand-block">
            <div className="brand-title">SceneHub</div>
            <div className="brand-subtitle">Desktop Console</div>
          </div>
          <nav className="sidebar-nav">
            {navItems.map((item) => (
              <NavLink
                key={item.to}
                to={item.to}
                className={({ isActive }) =>
                  `sidebar-link${isActive ? " active" : ""}`
                }
              >
                {item.label}
              </NavLink>
            ))}
          </nav>
        </aside>
        <main className="app-main">{children}</main>
      </div>
      <StatusBar />
    </div>
  );
}
