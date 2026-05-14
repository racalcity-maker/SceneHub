import { z } from "zod";

export const wsEnvelopeSchema = z.object({
  type: z.string(),
  seq: z.number(),
  schema_version: z.number(),
  snapshot_generation: z.number(),
  server_time_ms: z.number(),
  payload: z.unknown(),
});

export type WsEnvelope = z.infer<typeof wsEnvelopeSchema>;

export const gmInvalidationSchema = z.object({
  slice: z.string(),
  target_id: z.string().optional().default(""),
  scope: z.string().optional().default("global"),
  generation: z.number().optional().default(0),
  reason: z.string().optional().default(""),
});

export type GmInvalidation = z.infer<typeof gmInvalidationSchema>;

export const gmResyncRequiredSchema = z.object({
  target_id: z.string().optional().default(""),
  generation: z.number().optional().default(0),
  reason: z.string().optional().default(""),
});

export type GmResyncRequired = z.infer<typeof gmResyncRequiredSchema>;
