interface LoadingStateProps {
  label: string;
  compact?: boolean;
}

export function LoadingState({ label, compact = false }: LoadingStateProps) {
  return <div className={compact ? "toolbar-chip" : "state-card"}>{label}</div>;
}

