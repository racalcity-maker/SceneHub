interface EmptyStateProps {
  title: string;
  description: string;
}

export function EmptyState({ title, description }: EmptyStateProps) {
  return (
    <div className="state-card">
      <strong>{title}</strong>
      <div className="state-description">{description}</div>
    </div>
  );
}

