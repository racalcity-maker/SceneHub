import { z } from "zod";

export const sessionInfoSchema = z.object({
  role: z.enum(["admin", "user"]),
  username: z.string(),
});

export const authLoginResponseSchema = z.object({
  status: z.literal("ok"),
  role: z.enum(["admin", "user"]),
});

export const authLogoutResponseSchema = z.object({
  status: z.literal("ok"),
});

export type SessionInfo = z.infer<typeof sessionInfoSchema>;
export type AuthLoginResponse = z.infer<typeof authLoginResponseSchema>;
export type AuthLogoutResponse = z.infer<typeof authLogoutResponseSchema>;
