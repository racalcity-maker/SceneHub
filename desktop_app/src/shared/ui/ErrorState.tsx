interface ErrorStateProps {
  title: string;
  description: string;
  compact?: boolean;
}

export function ErrorState({ title, description, compact = false }: ErrorStateProps) {
  return (
    <div className={compact ? "toolbar-chip error" : "state-card error"}>
      <strong>{title}</strong>
      <div className="state-description">{description}</div>
    </div>
  );
}

