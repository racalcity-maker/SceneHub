import { PropsWithChildren } from "react";

interface PanelProps {
  title?: string;
  subtitle?: string;
}

export function Panel({ children, title, subtitle }: PropsWithChildren<PanelProps>) {
  return (
    <section className="panel">
      {title ? <div className="panel-title">{title}</div> : null}
      {subtitle ? <div className="panel-subtitle">{subtitle}</div> : null}
      {children}
    </section>
  );
}

