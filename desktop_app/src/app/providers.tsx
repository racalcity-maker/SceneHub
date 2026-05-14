import { PropsWithChildren, useState } from "react";
import { QueryClient, QueryClientProvider } from "@tanstack/react-query";
import { useGmVersionsWs } from "@/domains/gm/queries/useGmVersionsWs";

function ControllerRealtimeBridge() {
  useGmVersionsWs();
  return null;
}

export function AppProviders({ children }: PropsWithChildren) {
  const [queryClient] = useState(
    () =>
      new QueryClient({
        defaultOptions: {
          queries: {
            retry: 1,
            refetchOnWindowFocus: false,
          },
        },
      }),
  );

  return (
    <QueryClientProvider client={queryClient}>
      <ControllerRealtimeBridge />
      {children}
    </QueryClientProvider>
  );
}