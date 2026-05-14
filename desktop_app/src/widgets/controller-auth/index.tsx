import { useEffect } from "react";
import { useControllerStore } from "@/domains/controller";
import { Badge } from "@/shared/ui/Badge";
import { Button } from "@/shared/ui/Button";
import { useControllerAuth } from "@/features/controller-auth/model/useControllerAuth";

export function ControllerAuthPanel() {
  const activeController = useControllerStore((state) => state.activeController);
  const connectionStatus = useControllerStore((state) => state.connectionStatus);
  const lastError = useControllerStore((state) => state.lastError);
  const sessionInfo = useControllerStore((state) => state.sessionInfo);
  const auth = useControllerAuth();
  const {
    autoLogin,
    canAutoLogin,
    loginPending,
    password,
    rememberPassword,
    setPassword,
    setRememberPassword,
    setUsername,
    username,
  } = auth;

  useEffect(() => {
    if (
      activeController?.baseUrl &&
      connectionStatus === "auth_required" &&
      !loginPending &&
      canAutoLogin()
    ) {
      autoLogin();
    }
  }, [
    activeController?.baseUrl,
    autoLogin,
    canAutoLogin,
    connectionStatus,
    loginPending,
  ]);

  if (!activeController?.baseUrl) {
    return null;
  }

  if (connectionStatus === "auth_required") {
    return (
      <div className="controller-auth-panel">
        <input
          className="toolbar-input auth-input"
          value={username}
          onChange={(event) => setUsername(event.target.value)}
          placeholder="Username"
          aria-label="Controller username"
        />
        <input
          className="toolbar-input auth-input"
          type="password"
          value={password}
          onChange={(event) => setPassword(event.target.value)}
          placeholder="Password"
          aria-label="Controller password"
        />
        <label className="auth-checkbox">
          <input
            type="checkbox"
            checked={rememberPassword}
            onChange={(event) => setRememberPassword(event.target.checked)}
          />
          <span>Save password</span>
        </label>
        <Button size="sm" onClick={auth.login} disabled={loginPending || !username || !password}>
          Login
        </Button>
        {auth.loginError || lastError ? (
          <span className="auth-error-text">{auth.loginError || lastError}</span>
        ) : null}
      </div>
    );
  }

  if (sessionInfo) {
    return (
      <div className="toolbar-chip">
        <div className="toolbar-meta">
          <span>{sessionInfo.username}</span>
          <span>{sessionInfo.role}</span>
        </div>
        <Badge tone="success">session</Badge>
        <Button size="sm" tone="secondary" onClick={auth.logout} disabled={auth.logoutPending}>
          Logout
        </Button>
      </div>
    );
  }

  return null;
}
