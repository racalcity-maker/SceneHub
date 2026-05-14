import { PropsWithChildren } from "react";

type BadgeTone = "muted" | "success" | "danger";

export function Badge({
  children,
  tone = "muted",
}: PropsWithChildren<{ tone?: BadgeTone }>) {
  return <span className={`ui-badge ${tone}`}>{children}</span>;
}

