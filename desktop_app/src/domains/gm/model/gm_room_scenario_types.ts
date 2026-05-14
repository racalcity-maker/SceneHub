import { z } from "zod";

export const gmRoomScenarioValidationIssueSchema = z
  .object({
    code: z.string().optional(),
    level: z.string().optional(),
    message: z.string().optional(),
    severity: z.string().optional(),
    step_index: z.number().optional(),
  })
  .passthrough();

export const gmRoomScenarioStepSchema = z
  .object({
    id: z.string(),
    label: z.string().optional().default(""),
    type: z.string().optional().default("unknown"),
    enabled: z.boolean().optional().default(true),
    device_id: z.string().optional().default(""),
    scenario_id: z.string().optional().default(""),
    command_id: z.string().optional().default(""),
    event_id: z.string().optional().default(""),
    event_type: z.string().optional().default(""),
    source_id: z.string().optional().default(""),
    operator_prompt: z.string().optional().default(""),
    operator_approve_label: z.string().optional().default(""),
    operator_message: z.string().optional().default(""),
    flag_name: z.string().optional().default(""),
    flag_value: z.boolean().optional(),
    duration_ms: z.number().optional().default(0),
    command_count: z.number().optional().default(0),
    event_count: z.number().optional().default(0),
    flag_count: z.number().optional().default(0),
    events: z.array(z.unknown()).optional().default([]),
    flags: z.array(z.unknown()).optional().default([]),
    params: z.unknown().optional(),
  })
  .passthrough();

export const gmRoomScenarioVariantSchema = z
  .object({
    id: z.string().optional().default(""),
    label: z.string().optional().default(""),
    actions: z.array(gmRoomScenarioStepSchema).optional().default([]),
  })
  .passthrough();

export const gmRoomScenarioBranchSchema = z
  .object({
    id: z.string(),
    name: z.string().optional().default(""),
    type: z.string().optional().default("normal"),
    enabled: z.boolean().optional().default(true),
    required_for_completion: z.boolean().optional().default(false),
    priority: z.number().optional().default(0),
    cooldown_ms: z.number().optional().default(0),
    max_fire_count: z.number().optional().default(0),
    run_once: z.boolean().optional().default(false),
    trigger: z.unknown().optional(),
    steps: z.array(gmRoomScenarioStepSchema).optional().default([]),
    variants: z.array(gmRoomScenarioVariantSchema).optional().default([]),
  })
  .passthrough();

export const gmRoomScenarioSchema = z
  .object({
    id: z.string(),
    name: z.string(),
    room_id: z.string(),
    step_count: z.number().optional().default(0),
    valid: z.boolean().optional().default(true),
    validation_issue_count: z.number().optional().default(0),
    error_count: z.number().optional().default(0),
    warning_count: z.number().optional().default(0),
    validation_issues: z.array(gmRoomScenarioValidationIssueSchema).optional().default([]),
    steps: z.array(gmRoomScenarioStepSchema).optional().default([]),
    branches: z.array(gmRoomScenarioBranchSchema).optional().default([]),
  })
  .passthrough();

export const gmRoomScenariosSchema = z.object({
  room_id: z.string(),
  count: z.number(),
  scenarios: z.array(gmRoomScenarioSchema),
});

export type GmRoomScenario = z.infer<typeof gmRoomScenarioSchema>;
export type GmRoomScenarios = z.infer<typeof gmRoomScenariosSchema>;
export type GmRoomScenarioStep = z.infer<typeof gmRoomScenarioStepSchema>;
export type GmRoomScenarioBranch = z.infer<typeof gmRoomScenarioBranchSchema>;
export type GmRoomScenarioVariant = z.infer<typeof gmRoomScenarioVariantSchema>;
