import { z } from "zod";

export const gmRuntimeWaitEventSchema = z.object({
  event_type: z.string(),
  source_id: z.string(),
});

export const gmRuntimeFlagSchema = z.object({
  name: z.string(),
  value: z.boolean(),
});

export const gmRuntimeBranchStepSchema = z.object({
  index: z.number().optional().default(0),
  global_index: z.number().optional().default(0),
  state: z.string().optional().default("pending"),
});

export const gmRuntimeBranchSchema = z.object({
  index: z.number().optional().default(0),
  id: z.string(),
  name: z.string(),
  active: z.boolean().optional().default(false),
  type: z.string().optional().default("normal"),
  required_for_completion: z.boolean().optional().default(false),
  cooldown_ms: z.number().optional().default(0),
  cooldown_until_ms: z.number().optional().default(0),
  run_once: z.boolean().optional().default(false),
  fired_once: z.boolean().optional().default(false),

  step_start_index: z.number().optional().default(0),
  step_count: z.number().optional().default(0),
  total_steps: z.number().optional().default(0),

  // global index in the full scenario step array
  current_step_index: z.number().optional().default(0),

  // local index inside this branch
  current_step_local_index: z.number().optional().default(0),

  // how many steps in this branch are already completed
  completed_step_count: z.number().optional().default(0),
  done_steps: z.number().optional().default(0),

  // local failed step index, -1 if no failed step
  failed_step_index: z.number().optional().default(-1),

  // runtime state of the current step
  current_step_state: z.string().optional().default("idle"),

  state: z.string(),
  wait_type: z.string(),
  wait_until_ms: z.number().optional().default(0),
  wait_started_at_ms: z.number().optional().default(0),
  wait_operator_skip_allowed: z.boolean().optional().default(false),
  wait_operator_skip_label: z.string().optional().default(""),
  steps: z.array(gmRuntimeBranchStepSchema).optional().default([]),
});

export const gmRoomRuntimeSchema = z.object({
  ok: z.literal(true),
  runtime_schema_version: z.number().optional().default(1),
  room_id: z.string(),
  session_present: z.boolean().optional().default(false),
  session_state: z.string().optional().default("idle"),
  timer_state: z.string().optional().default("idle"),
  timer_duration_ms: z.number().optional().default(0),
  timer_remaining_ms: z.number().optional().default(0),
  hint_active: z.boolean().optional().default(false),
  hint_sent_count: z.number().optional().default(0),
  hint_message: z.string().optional().default(""),
  selected_profile_id: z.string().optional().default(""),
  selected_profile_name: z.string().optional().default(""),
  selected_profile_scenario_id: z.string().optional().default(""),
  selected_profile_duration_ms: z.number().optional().default(0),
  selected_scenario_id: z.string().optional().default(""),
  selected_scenario_name: z.string().optional().default(""),
  running_scenario_id: z.string().optional().default(""),
  running_scenario_name: z.string().optional().default(""),
  running_scenario_generation: z.number().optional().default(0),
  runtime_now_ms: z.number().optional().default(0),
  scenario_runtime_state: z.string().optional().default("idle"),
  scenario_current_step_index: z.number().optional().default(0),
  scenario_wait_type: z.string().optional().default("none"),
  scenario_wait_until_ms: z.number().optional().default(0),
  scenario_wait_started_at_ms: z.number().optional().default(0),
  scenario_wait_event_type: z.string().optional().default(""),
  scenario_wait_source_id: z.string().optional().default(""),
  scenario_wait_events: z.array(gmRuntimeWaitEventSchema).optional().default([]),
  scenario_wait_event_count: z.number().optional().default(0),
  scenario_wait_flags: z.array(gmRuntimeFlagSchema).optional().default([]),
  scenario_wait_flag_count: z.number().optional().default(0),
  scenario_wait_operator_prompt: z.string().optional().default(""),
  scenario_wait_operator_label: z.string().optional().default(""),
  scenario_wait_operator_skip_allowed: z.boolean().optional().default(false),
  scenario_wait_operator_skip_label: z.string().optional().default(""),
  scenario_operator_message: z.string().optional().default(""),
  scenario_flags: z.array(gmRuntimeFlagSchema).optional().default([]),
  scenario_flag_count: z.number().optional().default(0),
  scenario_branches: z.array(gmRuntimeBranchSchema).optional().default([]),
  scenario_branch_count: z.number().optional().default(0),
  scenario_last_error: z.string().optional().default(""),
  asset_prepare_state: z.string().optional().default("unknown"),
  asset_audio_total: z.number().optional().default(0),
  asset_audio_ready: z.number().optional().default(0),
  asset_audio_missing: z.number().optional().default(0),
  asset_audio_bad: z.number().optional().default(0),
  asset_audio_unsupported: z.number().optional().default(0),
  asset_audio_io_error: z.number().optional().default(0),
  asset_audio_unknown: z.number().optional().default(0),
});

export type GmRoomRuntime = z.infer<typeof gmRoomRuntimeSchema>;
