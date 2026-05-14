import { useEffect, useRef, useState } from "react";
import { useMutation, useQueryClient } from "@tanstack/react-query";
import { authLoginResponseSchema, authLogoutResponseSchema } from "@/domains/auth";
import { useControllerStore } from "@/domains/controller";
import { httpPostJson } from "@/platform/http/client";
import { HttpError } from "@/platform/http/http_error";
import {
  clearControllerCredentials,
  loadControllerCredentials,
  saveControllerCredentials,
} from "@/platform/persistence/controller_auth_storage";

export function useControllerAuth() {
  const activeController = useControllerStore((state) => state.activeController);
  const reportLoginStarted = useControllerStore((state) => state.reportLoginStarted);
  const reportLoginSucceeded = useControllerStore((state) => state.reportLoginSucceeded);
  const reportLoginFailed = useControllerStore((state) => state.reportLoginFailed);
  const reportLogoutSucceeded = useControllerStore((state) => state.reportLogoutSucceeded);
  const queryClient = useQueryClient();
  const [username, setUsername] = useState("");
  const [password, setPassword] = useState("");
  const [rememberPassword, setRememberPassword] = useState(false);
  const autoLoginAttemptedBaseUrlRef = useRef<string | null>(null);
  const autoLoginSuppressedBaseUrlRef = useRef<string | null>(null);

  useEffect(() => {
    const baseUrl = activeController?.baseUrl;
    if (!baseUrl) {
      setUsername("");
      setPassword("");
      setRememberPassword(false);
      autoLoginAttemptedBaseUrlRef.current = null;
      autoLoginSuppressedBaseUrlRef.current = null;
      return;
    }

    const storedCredentials = loadControllerCredentials(baseUrl);
    setUsername(storedCredentials?.username ?? "");
    setPassword(storedCredentials?.password ?? "");
    setRememberPassword(Boolean(storedCredentials));
    autoLoginAttemptedBaseUrlRef.current = null;
    autoLoginSuppressedBaseUrlRef.current = null;
  }, [activeController?.baseUrl]);

  async function invalidateControllerAuth() {
    const baseUrl = activeController?.baseUrl ?? "none";
    await Promise.all([
      queryClient.invalidateQueries({
        queryKey: ["controller", baseUrl, "meta"],
      }),
      queryClient.invalidateQueries({
        queryKey: ["controller", baseUrl, "session"],
      }),
    ]);
  }

  const loginMutation = useMutation({
    mutationFn: async () => {
      if (!activeController?.baseUrl) {
        throw new Error("No controller selected");
      }

      return httpPostJson(activeController.baseUrl, "/api/auth/login", authLoginResponseSchema, {
        username,
        password,
      });
    },
    onMutate: () => {
      reportLoginStarted();
    },
    onSuccess: async () => {
      if (activeController?.baseUrl) {
        if (rememberPassword) {
          saveControllerCredentials({
            baseUrl: activeController.baseUrl,
            username,
            password,
          });
        } else {
          clearControllerCredentials(activeController.baseUrl);
        }
      }
      autoLoginSuppressedBaseUrlRef.current = null;
      reportLoginSucceeded();
      await invalidateControllerAuth();
    },
    onError: (error) => {
      reportLoginFailed(
        error instanceof Error ? error.message : "Login failed",
        error instanceof HttpError && error.status === 401,
      );
    },
  });

  const logoutMutation = useMutation({
    mutationFn: async () => {
      if (!activeController?.baseUrl) {
        throw new Error("No controller selected");
      }

      return httpPostJson(activeController.baseUrl, "/api/auth/logout", authLogoutResponseSchema);
    },
    onSuccess: async () => {
      if (activeController?.baseUrl) {
        autoLoginSuppressedBaseUrlRef.current = activeController.baseUrl;
      }
      reportLogoutSucceeded();
      autoLoginAttemptedBaseUrlRef.current = null;
      await invalidateControllerAuth();
    },
    onError: () => {},
  });

  function canAutoLogin() {
    const baseUrl = activeController?.baseUrl;
    if (!baseUrl || !rememberPassword || !username || !password) {
      return false;
    }

    if (autoLoginSuppressedBaseUrlRef.current === baseUrl) {
      return false;
    }

    return autoLoginAttemptedBaseUrlRef.current !== baseUrl;
  }

  function autoLogin() {
    const baseUrl = activeController?.baseUrl;
    if (!baseUrl || !canAutoLogin()) {
      return;
    }

    autoLoginAttemptedBaseUrlRef.current = baseUrl;
    loginMutation.mutate();
  }

  function updateRememberPassword(nextValue: boolean) {
    setRememberPassword(nextValue);
    if (!nextValue && activeController?.baseUrl) {
      clearControllerCredentials(activeController.baseUrl);
    }
  }

  return {
    username,
    setUsername,
    password,
    setPassword,
    rememberPassword,
    setRememberPassword: updateRememberPassword,
    loginPending: loginMutation.isPending,
    logoutPending: logoutMutation.isPending,
    loginError:
      loginMutation.error instanceof Error ? loginMutation.error.message : null,
    logoutError:
      logoutMutation.error instanceof Error ? logoutMutation.error.message : null,
    login: () => loginMutation.mutate(),
    autoLogin,
    canAutoLogin,
    logout: () => logoutMutation.mutate(),
  };
}
