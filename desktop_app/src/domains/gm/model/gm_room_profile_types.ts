import { z } from "zod";

export const gmRoomProfileSchema = z.object({
  id: z.string(),
  name: z.string(),
  room_id: z.string(),
  scenario_id: z.string(),
  duration_ms: z.number(),
  hint_pack_id: z.string().optional().default(""),
  audio_pack_id: z.string().optional().default(""),
  enabled: z.boolean().optional().default(true),
  valid: z.boolean().optional().default(true),
});

export const gmRoomProfilesSchema = z.object({
  ok: z.literal(true),
  room_id: z.string(),
  generation: z.number(),
  selected_profile_id: z.string(),
  profiles: z.array(gmRoomProfileSchema),
});

export type GmRoomProfile = z.infer<typeof gmRoomProfileSchema>;
export type GmRoomProfiles = z.infer<typeof gmRoomProfilesSchema>;
