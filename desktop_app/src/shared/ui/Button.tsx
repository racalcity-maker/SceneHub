import { ButtonHTMLAttributes, PropsWithChildren } from "react";

type ButtonTone = "primary" | "secondary";
type ButtonSize = "sm" | "md";

interface ButtonProps extends ButtonHTMLAttributes<HTMLButtonElement> {
  tone?: ButtonTone;
  size?: ButtonSize;
}

export function Button({
  children,
  className = "",
  tone = "primary",
  size = "md",
  ...props
}: PropsWithChildren<ButtonProps>) {
  return (
    <button
      className={`ui-button ${tone} ${size} ${className}`.trim()}
      {...props}
    >
      {children}
    </button>
  );
}

