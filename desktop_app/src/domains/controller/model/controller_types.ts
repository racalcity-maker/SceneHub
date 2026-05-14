import { z } from "zod";

export const controllerCapabilitiesSchema = z
  .object({
    gm: z.boolean().optional(),
    ota: z.boolean().optional(),
    audio: z.boolean().optional(),
    hardware_io: z.boolean().optional(),
    ws: z.boolean().optional(),
  })
  .optional()
  .transform((value) => value ?? {});

export const controllerLimitsSchema = z
  .object({
    max_rooms: z.number().optional(),
    max_devices: z.number().optional(),
    max_ws_clients: z.number().optional(),
  })
  .optional();

export const controllerBuildSchema = z
  .object({
    git_sha: z.string().optional(),
    build_date: z.string().optional(),
  })
  .optional();

export const controllerMetaSchema = z.object({
  product_id: z.string().optional(),
  device_id: z.string(),
  device_name: z.string(),
  hostname: z.string().optional(),
  firmware_version: z.string(),
  api_version: z.number(),
  build: controllerBuildSchema,
  capabilities: controllerCapabilitiesSchema,
  limits: controllerLimitsSchema,
});

export type ControllerMeta = z.infer<typeof controllerMetaSchema>;
