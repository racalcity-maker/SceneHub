// GM panel source part. Edit this file, then rebuild gm_panel.js.
function scenarioById(roomId,scenarioId){return roomScenarios(roomId).find(s=>s.id===scenarioId)||null;}
function roomSelectedScenarioObject(room){if(!room)return null;const profiles=roomProfiles(room.room_id);const profileId=roomSelectedProfileId(room.room_id)||room.selected_profile_id||'';const profile=profiles.find(p=>p.id===profileId)||null;const preferred=room.running_scenario_id||room.selected_profile_scenario_id||(profile&&profile.scenario_id)||room.selected_scenario_id||'';return scenarioById(room.room_id,preferred)||scenarioById(room.room_id,room.selected_scenario_id)||null;}
function scenarioBranchDisplaySteps(branch){
if(!branch)return [];
const steps=Array.isArray(branch.steps)?branch.steps:[];
if(steps.length)return steps;
const variants=Array.isArray(branch.variants)?branch.variants:[];
if(!variants.length)return [];
const mode=String(branch.policy&&branch.policy.mode||'single').toLowerCase();
const multi=mode!=='single'&&variants.length>1;
const out=[];
variants.forEach((variant,variantIndex)=>{
const actions=Array.isArray(variant&&variant.actions)?variant.actions:[];
actions.forEach((action,actionIndex)=>{
const copy=action?JSON.parse(JSON.stringify(action)):{type:'WAIT_TIME',duration_ms:1000};
if(!copy.id)copy.id=`variant_${variantIndex+1}_action_${actionIndex+1}`;
if(multi){
const prefix=mode==='escalate'?`Level ${variantIndex+1}`:`Variant ${variantIndex+1}`;
copy.label=copy.label?`${prefix}: ${copy.label}`:`${prefix}: ${scenarioStepText(copy)}`;
}
out.push(copy);
});
});
return out;
}
function scenarioFlattenedSteps(scenario){if(!scenario)return [];if(Array.isArray(scenario.branches)&&scenario.branches.length){return scenario.branches.reduce((out,branch)=>out.concat(scenarioBranchDisplaySteps(branch)),[]);}return Array.isArray(scenario.steps)?scenario.steps:[];}
function roomScenarioSteps(room){const scenario=roomSelectedScenarioObject(room);return scenarioFlattenedSteps(scenario);}
function roomCurrentScenarioStep(room){const steps=roomScenarioSteps(room);const index=Math.max(0,Number(room&&room.scenario_current_step_index)||0);return steps[index]||null;}
function roomScenarioDeviceIds(room){const ids=new Set();roomScenarioSteps(room).forEach(step=>{const type=String(step&&step.type||'').toLowerCase();if((type==='device_command'||type==='wait_device_event')&&step.device_id)ids.add(step.device_id);});return Array.from(ids);}
function roomQuestDeviceIssues(room){return roomScenarioDeviceIds(room).map(id=>questDeviceById(id)).filter(Boolean).map(dev=>{const health=questDeviceHealth(dev);if(health==='ok')return null;return {device_id:dev.id,title:health==='fault'?'Device offline':'Device degraded',details:`${dev.name||dev.id}: ${questDeviceStatusText(dev)}`,severity:health};}).filter(Boolean);}
function roomDerivedHealth(room){const issues=roomQuestDeviceIssues(room);if(issues.some(i=>i.severity==='fault'))return 'fault';if(issues.some(i=>i.severity==='degraded'))return 'degraded';return room&&room.health||'unknown';}
function scenarioAudioStepText(s){const params=s&&s.params&&typeof s.params==='object'?s.params:{};const command=String(s&&s.command_id||'');const channel=String(params.channel||'effect').toLowerCase();const file=compactText(audioBaseName(params.file||''),34);if(command==='play'){const kind=channel==='background'||channel==='bg'||channel==='music'?'bg':'sfx';return file?`Play ${kind}: ${file}`:`Play ${kind}`;}if(command==='stop')return `Stop audio${params.channel?`: ${params.channel}`:''}`;if(command==='pause')return 'Pause audio';if(command==='resume')return 'Resume audio';if(command==='set_volume')return `Set volume: ${params.volume??''}`;return `Audio: ${questDeviceCommandName('system_audio',command)}`;}
function scenarioStepText(s){if(!s)return '';const type=String(s.type||'').toLowerCase();if(type==='device_command'){if(String(s.device_id||'')==='system_audio')return scenarioAudioStepText(s);return `${deviceDisplayName(s.device_id)} -> ${questDeviceCommandName(s.device_id,s.command_id)}`;}if(type==='wait_device_event')return `Wait ${deviceDisplayName(s.device_id)}: ${questDeviceEventName(s.device_id,s.event_id)}`;if(type==='wait_time')return `Wait ${Math.max(1,Math.round((Number(s.duration_ms)||1000)/1000))} sec`;if(type==='operator_approval')return `Operator approval: ${s.operator_prompt||s.prompt||s.label||'Confirm'}`;return s.label||s.id||s.type||'Step';}
function scenarioStepLabel(room,total){const state=room.scenario_runtime_state||'idle';const idx=Number(room.scenario_current_step_index)||0;if(!total)return '0 / 0';if(state==='idle'||state==='stopped')return `0 / ${total}`;if(state==='done')return `${total} / ${total}`;return `${Math.min(total,idx+1)} / ${total}`;}
function scenarioWaitText(room){const t=room.scenario_wait_type||'none';if(t==='time')return `time until ${room.scenario_wait_until_ms||0}`;if(t==='command_result')return `command result ${room.scenario_wait_event_type||''}`;if(t==='event'||t==='any_events'||t==='all_events'){const events=Array.isArray(room.scenario_wait_events)?room.scenario_wait_events:[];if(events.length>1)return `${t==='all_events'?'all of':'any of'} ${events.length} events`;const source=room.scenario_wait_source_id||'';const eventType=room.scenario_wait_event_type||'any';return `${source?deviceDisplayName(source):'Any device'}: ${source?questDeviceEventNameByType(source,eventType):eventType}`;}if(t==='flags'){const flags=Array.isArray(room.scenario_wait_flags)?room.scenario_wait_flags:[];return flags.length?`flags: ${flags.map(flag=>`${flag.name||'flag'}=${flag.value?'true':'false'}`).join(', ')}`:'flags';}if(t==='operator')return `operator: ${room.scenario_wait_operator_prompt||'approval'}`;return 'none';}
function scenarioProgressStepState(room,index){const runtime=room&&room.scenario_runtime_state||'idle';const current=Math.max(0,Number(room&&room.scenario_current_step_index)||0);if(runtime==='done')return 'done';if(runtime==='error')return index<current?'done':(index===current?'error':'pending');if(runtime==='running'||runtime==='waiting')return index<current?'done':(index===current?'current':'pending');return 'pending';}
function scenarioProgressBranchCurrentIndex(branchRuntime){if(!branchRuntime)return null;const rawNumber=Number(branchRuntime.current_step_index);if(!Number.isFinite(rawNumber))return null;const raw=Math.max(0,Math.floor(rawNumber));const start=Math.max(0,Number(branchRuntime.step_start_index)||0);const count=Math.max(0,Number(branchRuntime.step_count)||0);if(count>0&&start>0&&raw>=start&&raw<start+count){return raw-start;}if(count>0){return Math.max(0,Math.min(count-1,raw));}return raw;}
function scenarioProgressBranchState(room,branchRuntime,localIndex,globalIndex){const runtime=(branchRuntime&&branchRuntime.state)||room&&room.scenario_runtime_state||'idle';const branchCurrent=scenarioProgressBranchCurrentIndex(branchRuntime);const current=branchCurrent!==null?branchCurrent:Math.max(0,Number(room&&room.scenario_current_step_index)||0);const index=branchRuntime?localIndex:globalIndex;if(runtime==='done')return 'done';if(runtime==='error')return index<current?'done':(index===current?'error':'pending');if(runtime==='running'||runtime==='waiting')return index<current?'done':(index===current?'current':'pending');return 'pending';}
function scenarioProgressIcon(state){if(state==='done')return '&#10003;';if(state==='current')return '&rarr;';if(state==='error')return '!';return '';}
function scenarioProgressBranches(scenarioOrSteps){if(scenarioOrSteps&&Array.isArray(scenarioOrSteps.branches)&&scenarioOrSteps.branches.length)return scenarioOrSteps.branches.map((branch,index)=>{const type=String(branch.type||'normal').toLowerCase()==='reactive'?'reactive':'normal';return {id:branch.id||`branch_${index+1}`,name:branch.name||`Branch ${index+1}`,type,enabled:branch.enabled!==false,required_for_completion:type==='normal'&&branch.required_for_completion!==false,trigger:branch.trigger||null,steps:scenarioBranchDisplaySteps(branch)};});const steps=Array.isArray(scenarioOrSteps)?scenarioOrSteps:(scenarioOrSteps&&Array.isArray(scenarioOrSteps.steps)?scenarioOrSteps.steps:[]);return steps.length?[{id:'main',name:'Main',type:'normal',enabled:true,required_for_completion:true,steps}]:[];}
function scenarioProgressBranchRuntime(room,branch,index){const runtimes=Array.isArray(room&&room.scenario_branches)?room.scenario_branches:[];const byIndex=runtimes.find(item=>Number(item.index)===index);if(byIndex)return byIndex;const branchId=branch&&branch.id||'';if(branchId)return runtimes.find(item=>(item.id||'')===branchId)||null;return null;}
function renderScenarioProgressStep(room,step,index,globalIndex,branchRuntime){const disabled=step&&step.enabled===false;const state=disabled?'disabled':scenarioProgressBranchState(room,branchRuntime,index,globalIndex);return `<div class='scenario-progress-step ${state}'><span class='scenario-progress-icon'>${scenarioProgressIcon(state)}</span><span class='scenario-progress-index'>${esc(index+1)}.</span><span class='scenario-progress-text'>${esc(scenarioStepText(step))}</span>${disabled?`<span class='badge'>disabled</span>`:''}</div>`;}
function scenarioBranchDoneCount(room,branch,branchRuntime,globalStart){
const steps=Array.isArray(branch&&branch.steps)?branch.steps:[];
const total=steps.length;
if(!total)return 0;
const runtime=(branchRuntime&&branchRuntime.state)||room&&room.scenario_runtime_state||'idle';
if(branch&&branch.type==='reactive'&&runtime==='waiting'&&(!branchRuntime||!branchRuntime.wait_type||branchRuntime.wait_type==='none'))return 0;
if(runtime==='done')return total;
if(runtime==='idle'||runtime==='stopped'||runtime==='disabled')return 0;
if(runtime==='running'||runtime==='waiting'||runtime==='error'){
const current=scenarioProgressBranchCurrentIndex(branchRuntime);
if(current!==null)return Math.max(0,Math.min(total,current));
return Math.max(0,Math.min(total,Math.max(0,Number(room&&room.scenario_current_step_index)||0)-globalStart));
}
return 0;
}
function scenarioReactiveTriggerText(branch){
const trigger=branch&&branch.trigger||{};
const kind=String(trigger.kind||'device_event').toLowerCase();
if(kind==='device_event'){
const dev=questDeviceById(trigger.device_id);
const name=dev?questDeviceDisplayName(dev):deviceDisplayName(trigger.device_id);
const eventName=questDeviceEventName(trigger.device_id,trigger.event_id);
return `Waiting for ${name}: ${eventName}`;
}
if(kind==='flag_changed')return `Waiting for flag: ${trigger.flag_name||trigger.event_id||'flag'}`;
if(kind==='operator_event')return `Waiting for operator event: ${trigger.operator_event||trigger.event_id||'event'}`;
if(kind==='runtime_event')return `Waiting for runtime event: ${trigger.runtime_event||trigger.event_id||'event'}`;
return 'Waiting for trigger';
}
function scenarioBranchCurrentStep(branch,branchRuntime){
const steps=Array.isArray(branch&&branch.steps)?branch.steps:[];
if(!steps.length)return branch&&branch.type==='reactive'?'No actions':'No steps';
const runtime=branchRuntime&&branchRuntime.state||'idle';
if(branch&&branch.type==='reactive'&&runtime==='waiting'&&(!branchRuntime||!branchRuntime.wait_type||branchRuntime.wait_type==='none'))return scenarioReactiveTriggerText(branch);
if(runtime==='done')return 'Complete';
const current=scenarioProgressBranchCurrentIndex(branchRuntime);
if(current!==null&&steps[current])return scenarioStepText(steps[current]);
return scenarioStepText(steps[0]);
}
function scenarioProgressBar(done,total){const pct=total?Math.max(0,Math.min(100,Math.round(done*100/total))):0;return `<div class='scenario-progress-bar' title='${esc(done)} / ${esc(total)}'><span style='width:${pct}%'></span></div>`;}
function scenarioProgressTypeLabel(branch){return branch.type==='reactive'?'reaction':(branch.required_for_completion?'required':'optional');}
function scenarioBranchWaitText(branchRuntime){
const t=branchRuntime&&branchRuntime.wait_type||'none';
if(t==='time')return `time until ${branchRuntime.wait_until_ms||0}`;
if(t==='command_result')return `command result ${branchRuntime.wait_event_type||''}`;
if(t==='event'||t==='any_events'||t==='all_events'){const events=Array.isArray(branchRuntime.wait_events)?branchRuntime.wait_events:[];if(events.length>1)return `${t==='all_events'?'all of':'any of'} ${events.length} events`;return branchRuntime.wait_event_type||branchRuntime.wait_event_id||'device event';}
if(t==='flags'){const flags=Array.isArray(branchRuntime.wait_flags)?branchRuntime.wait_flags:[];return flags.length?flags.map(flag=>`${flag.name||flag.flag_name||'flag'}=${flag.value?'true':'false'}`).join(', '):'flags';}
if(t==='operator')return branchRuntime.wait_operator_prompt||'operator approval';
return 'none';
}
function renderScenarioBranchSkipButton(room,branch,branchRuntime){
if(!room||!branchRuntime||branchRuntime.state!=='waiting'||!branchRuntime.wait_operator_skip_allowed)return '';
const label=branchRuntime.wait_operator_skip_label||'Skip wait';
const branchId=branchRuntime.id||branch.id||'';
return uiButton({
label,
action:'room.scenario.runtime',
dataset:{op:'next','room-id':room.room_id||'','branch-id':branchId},
confirm:'Force complete this branch wait?',
});
}
function renderScenarioActiveWaits(room,items){
const waits=items.filter(item=>item.runtime&&item.runtime.state==='waiting'&&item.runtime.wait_operator_skip_allowed);
if(!waits.length)return '';
return `<div class='scenario-active-waits'>${waits.map(item=>{
const skip=renderScenarioBranchSkipButton(room,item.branch,item.runtime);
return `<div class='scenario-active-wait'><div><span class='badge'>operator skip</span> <strong>${esc(item.branch.name||item.branch.id)}</strong><div class='row-meta'>${esc(scenarioBranchWaitText(item.runtime))}</div></div>${skip?`<div class='branch-runtime-actions'>${skip}</div>`:''}</div>`;
}).join('')}</div>`;
}
function renderScenarioProgressBranch(room,item){
const branch=item.branch;
const steps=Array.isArray(branch.steps)?branch.steps:[];
const branchRuntime=item.runtime;
const state=(branchRuntime&&branchRuntime.state)||(!branch.enabled?'disabled':'idle');
const waitType=(branchRuntime&&branchRuntime.wait_type)||'none';
const done=scenarioBranchDoneCount(room,branch,branchRuntime,item.start);
const current=scenarioBranchCurrentStep(branch,branchRuntime);
const detailsKey=`room-progress-steps:${room&&room.room_id||'room'}:${branch.id||item.index}`;
const skip=renderScenarioBranchSkipButton(room,branch,branchRuntime);
const unit=branch.type==='reactive'?'actions':'steps';
const meta=branch.type==='reactive'?`${esc(done)} / ${esc(steps.length)} ${esc(unit)} / ${esc(scenarioProgressTypeLabel(branch))}`:`${esc(done)} / ${esc(steps.length)} ${esc(unit)} / ${esc(scenarioProgressTypeLabel(branch))}${waitType&&waitType!=='none'?` / waiting ${esc(waitType)}`:''}`;
const actionRuntime=branch.type==='reactive'&&state==='waiting'&&(!waitType||waitType==='none')?null:branchRuntime;
return `<section class='scenario-progress-branch ${!branch.enabled?'disabled':''} ${branch.type==='reactive'?'reactive':''} ${state}'><div class='scenario-progress-branch-head'><div class='scenario-progress-branch-main'><div class='scenario-progress-title-row'><div class='scenario-progress-branch-title'>${esc(branch.name||branch.id||`Branch ${item.index+1}`)}</div><span class='badge'>${esc(state)}</span></div><div class='row-meta'>${meta}</div><div class='scenario-progress-current'>${esc(current)}</div>${scenarioProgressBar(done,steps.length)}</div>${skip?`<div class='branch-runtime-actions'>${skip}</div>`:''}</div><details class='scenario-progress-step-details' ${detailsAttrs(detailsKey,false)}><summary>Show ${esc(unit)}</summary><div class='scenario-progress'>${steps.length?steps.map((step,stepIndex)=>renderScenarioProgressStep(room,step,stepIndex,item.start+stepIndex,actionRuntime)).join(''):`<div class='empty'>No ${esc(unit)}</div>`}</div></details></section>`;
}
function renderScenarioProgressSection(title,items,mode){
if(!items.length)return '';
return `<div class='scenario-progress-section'><div class='scenario-progress-section-title'>${esc(title)}</div><div class='scenario-progress-branches ${esc(mode||'flow')}'>${items.map(item=>renderScenarioProgressBranch(item.room,item)).join('')}</div></div>`;
}
function renderScenarioProgress(room,scenarioOrSteps){
const branches=scenarioProgressBranches(scenarioOrSteps);
if(!branches.length)return `<div class='scenario-progress empty'>No scenario steps</div>`;
let offset=0;
const items=branches.map((branch,index)=>{
const steps=Array.isArray(branch.steps)?branch.steps:[];
const branchRuntime=scenarioProgressBranchRuntime(room,branch,index);
const start=offset;
offset+=steps.length;
return {room,branch,index,runtime:branchRuntime,start};
});
const flow=items.filter(item=>item.branch.type!=='reactive');
const reactions=items.filter(item=>item.branch.type==='reactive');
const progressItems=flow.length?flow:items;
const total=progressItems.reduce((sum,item)=>sum+(item.branch.steps||[]).length,0);
const done=progressItems.reduce((sum,item)=>sum+scenarioBranchDoneCount(room,item.branch,item.runtime,item.start),0);
const active=items.find(item=>item.runtime&&(item.runtime.state==='waiting'||item.runtime.state==='running'||item.runtime.state==='error'));
const activeText=active?`${active.branch.name||active.branch.id}: ${scenarioBranchCurrentStep(active.branch,active.runtime)}`:'No active branch';
return `<div class='scenario-progress-wrap'><div class='scenario-progress-overview'><div><div class='scenario-progress-overview-title'>${esc(done)} / ${esc(total)} steps</div><div class='row-meta'>Current: ${esc(activeText)}</div></div>${scenarioProgressBar(done,total)}</div>${renderScenarioActiveWaits(room,items)}${renderScenarioProgressSection('Flow branches',flow,'flow')}${renderScenarioProgressSection('Reaction branches',reactions,'reactions')}</div>`;
}
function scenarioValidationText(s){if(!s)return 'No scenario selected';const n=Number(s.validation_issue_count)||0;if(s.valid===false)return `${n||1} validation issue${n===1?'':'s'}`;return n?`Valid, ${n} warning${n===1?'':'s'}`:'Valid';}
function scenarioIssueHtml(issues){return Array.isArray(issues)&&issues.length?`<div class='validation-list'>${issues.map(i=>`<div class='validation-item'>${esc(i.level||'error')} step ${esc(i.step_index||0)} / ${esc(i.code||'VALIDATION')}: ${esc(i.message||'')}</div>`).join('')}</div>`:'';}
function scenarioDraftValidationHtml(){const r=scenarioEditor.validation_report;if(!r)return '';const errors=Number(r.error_count)||0;const warnings=Number(r.warning_count)||0;const summary=errors?`${errors} error${errors===1?'':'s'}, ${warnings} warning${warnings===1?'':'s'}`:(warnings?`${warnings} warning${warnings===1?'':'s'}`:'valid');return `<div class='row-meta ${errors?'bad-text':''}'>Draft validation: ${esc(summary)}</div>${scenarioIssueHtml(r.issues)}`;}
