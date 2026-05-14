import { z } from "zod";

export const gmSummarySchema = z.object({
  rooms_total: z.number(),
  devices_total: z.number(),
  issues_total: z.number(),
  active_sessions: z.number(),
  active_hints: z.number(),
  has_degraded: z.boolean(),
  has_fault: z.boolean(),
});

export const gmScenarioFlagSchema = z.object({
  name: z.string(),
  value: z.boolean(),
});

export const gmScenarioBranchSchema = z.object({
  id: z.string(),
  name: z.string().optional(),
  state: z.string(),
  wait_type: z.string().optional(),
  current_step_index: z.number().optional(),
});

export const gmRoomSchema = z.object({
  room_id: z.string(),
  title: z.string(),
  sort_order: z.number(),
  health: z.string(),
  device_count: z.number(),
  active_device_count: z.number(),
  issue_count: z.number(),
  session_present: z.boolean(),
  session_state: z.string(),
  session_started_at_ms: z.number(),
  timer_state: z.string(),
  timer_duration_ms: z.number(),
  timer_remaining_ms: z.number(),
  hint_active: z.boolean(),
  hint_sent_count: z.number(),
  hint_message: z.string(),
  selected_profile_id: z.string(),
  selected_profile_name: z.string(),
  selected_profile_scenario_id: z.string(),
  selected_profile_duration_ms: z.number(),
  selected_scenario_id: z.string(),
  selected_scenario_name: z.string(),
  selected_scenario_generation: z.number(),
  running_scenario_id: z.string(),
  running_scenario_name: z.string(),
  running_scenario_generation: z.number(),
  scenario_runtime_state: z.string(),
  scenario_current_step_index: z.number(),
  scenario_wait_type: z.string(),
  scenario_wait_until_ms: z.number(),
  scenario_wait_started_at_ms: z.number(),
  scenario_wait_event_type: z.string(),
  scenario_wait_source_id: z.string(),
  scenario_wait_events: z.array(z.unknown()).optional().default([]),
  scenario_wait_event_count: z.number().optional().default(0),
  scenario_wait_flags: z.array(z.unknown()).optional().default([]),
  scenario_wait_flag_count: z.number().optional().default(0),
  scenario_wait_operator_prompt: z.string(),
  scenario_wait_operator_label: z.string(),
  scenario_wait_operator_skip_allowed: z.boolean(),
  scenario_wait_operator_skip_label: z.string(),
  scenario_operator_message: z.string(),
  scenario_flags: z.array(gmScenarioFlagSchema),
  scenario_flag_count: z.number(),
  scenario_branches: z.array(gmScenarioBranchSchema),
  scenario_branch_count: z.number(),
  scenario_last_error: z.string(),
});

export const gmDeviceSchema = z.object({
  device_id: z.string(),
  client_id: z.string(),
  display_name: z.string(),
  room_id: z.string().optional().default(""),
  kind: z.string(),
  health: z.string(),
  connectivity: z.string(),
  last_seen_ms: z.number().optional().default(0),
  runtime_state: z.string(),
  state_text: z.string().optional().default(""),
  fw_version: z.string().optional().default(""),
  boot_id: z.string().optional().default(""),
  last_diag_code: z.string().optional().default(""),
  last_diag_message: z.string().optional().default(""),
  last_result_status: z.string().optional().default(""),
  last_result_error_code: z.string().optional().default(""),
  has_runtime: z.boolean().optional().default(false),
  badges: z.array(z.string()).optional().default([]),
});

export const gmIssueSchema = z.object({
  issue_id: z.string(),
  room_id: z.string().optional().default(""),
  device_id: z.string().optional().default(""),
  scope: z.string(),
  severity: z.string(),
  code: z.string(),
  title: z.string().optional().default(""),
  active: z.boolean(),
});

export const gmStateSchema = z.object({
  ok: z.literal(true),
  generation: z.number(),
  active_profile: z.string(),
  summary: gmSummarySchema,
  rooms: z.array(gmRoomSchema),
  devices: z.array(gmDeviceSchema),
  issues: z.array(gmIssueSchema),
});

export type GmState = z.infer<typeof gmStateSchema>;
export type GmRoom = z.infer<typeof gmRoomSchema>;
export type GmSummary = z.infer<typeof gmSummarySchema>;
