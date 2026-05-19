// GM panel source part. Edit this file, then rebuild gm_panel.js.
function scenarioById(roomId,scenarioId){return roomScenarioRuntimeProjectionById(roomId,scenarioId);}
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
function roomCurrentScenarioText(room){return room&&room.scenario_current_step_text||'';}
function scenarioCollectDeviceIds(target,ids){
if(!target||!ids)return;
const deviceId=String(target.device_id||'');
if(deviceId&&deviceId!=='system_audio'&&deviceId!=='system_relay')ids.add(deviceId);
const commands=Array.isArray(target.commands)?target.commands:[];
commands.forEach(command=>{
const commandDeviceId=String(command&&command.device_id||'');
if(commandDeviceId&&commandDeviceId!=='system_audio'&&commandDeviceId!=='system_relay')ids.add(commandDeviceId);
});
const events=Array.isArray(target.events)?target.events:[];
events.forEach(eventRef=>{
const eventDeviceId=String(eventRef&&eventRef.device_id||'');
if(eventDeviceId&&eventDeviceId!=='system_audio'&&eventDeviceId!=='system_relay')ids.add(eventDeviceId);
});
const flags=Array.isArray(target.flags)?target.flags:[];
flags.forEach(()=>{});
}
function scenarioStaticDeviceIds(scenario){
const ids=new Set();
if(!scenario)return [];
const steps=Array.isArray(scenario.steps)?scenario.steps:[];
steps.forEach(step=>scenarioCollectDeviceIds(step,ids));
const branches=Array.isArray(scenario.branches)?scenario.branches:[];
branches.forEach(branch=>{
scenarioCollectDeviceIds(branch&&branch.trigger,ids);
const branchSteps=Array.isArray(branch&&branch.steps)?branch.steps:[];
branchSteps.forEach(step=>scenarioCollectDeviceIds(step,ids));
const variants=Array.isArray(branch&&branch.variants)?branch.variants:[];
variants.forEach(variant=>{
const actions=Array.isArray(variant&&variant.actions)?variant.actions:[];
actions.forEach(action=>scenarioCollectDeviceIds(action,ids));
});
});
return Array.from(ids);
}
function roomScenarioDeviceIds(room){
const runtimeIds=Array.isArray(room&&room.scenario_device_ids)?room.scenario_device_ids.filter(Boolean):[];
if(runtimeIds.length)return runtimeIds;
return scenarioStaticDeviceIds(roomSelectedScenarioObject(room));
}
function roomQuestDeviceIssues(room){return [];}
function roomDerivedHealth(room){return room&&room.health||'unknown';}
function scenarioRuntimeBranches(room){return Array.isArray(room&&room.scenario_branches)?room.scenario_branches:[];}
function scenarioFlowRuntimeBranches(room){const branches=scenarioRuntimeBranches(room);const flow=branches.filter(branch=>String(branch&&branch.type||'normal').toLowerCase()!=='reactive');return flow.length?flow:branches;}
function scenarioSkippableWaitBranch(room){
const branches=scenarioRuntimeBranches(room);
return branches.find(branch=>branch&&String(branch.state||'').toLowerCase()==='waiting'&&branch.wait_operator_skip_allowed&&(branch.id||''));
}
function scenarioTotalStepCount(room){return Math.max(0,Number(room&&room.scenario_total_steps)||0);}
function scenarioDoneStepCount(room){return Math.max(0,Number(room&&room.scenario_done_steps)||0);}
function scenarioAudioStepText(s){const params=s&&s.params&&typeof s.params==='object'?s.params:{};const command=String(s&&s.command_id||'');const channel=audioChannelValue(params);const file=compactText(audioBaseName(params.file||''),34);if(command==='play'){if(!file)return channel==='background'?'Background audio missing file':'Audio missing file';if(channel==='background'&&!audioFileIsWav(params.file||''))return `Invalid background audio: ${file}`;return channel==='background'?(params.repeat?`Play bg repeat: ${file}`:`Play bg: ${file}`):`Play sfx: ${file}`;}if(command==='stop')return `Stop audio${params.channel?`: ${params.channel}`:''}`;if(command==='pause')return 'Pause audio';if(command==='resume')return 'Resume audio';if(command==='set_volume')return `Set volume: ${params.volume??''}`;return `Audio: ${questDeviceCommandName('system_audio',command)}`;}
function scenarioStepText(s){if(!s)return '';const type=String(s.type||'').toLowerCase();if(type==='device_command'){if(String(s.device_id||'')==='system_audio')return scenarioAudioStepText(s);return `${deviceDisplayName(s.device_id)} -> ${questDeviceCommandName(s.device_id,s.command_id)}`;}if(type==='wait_device_event')return `Wait ${deviceDisplayName(s.device_id)}: ${questDeviceEventName(s.device_id,s.event_id)}`;if(type==='wait_time')return `Wait ${Math.max(1,Math.round((Number(s.duration_ms)||1000)/1000))} sec`;if(type==='operator_approval')return `Operator approval: ${s.operator_prompt||s.prompt||s.label||'Confirm'}`;return s.label||s.id||s.type||'Step';}
function scenarioStepLabel(room){const runtime=String(room&&room.scenario_runtime_state||'idle').toLowerCase();const totalSteps=scenarioTotalStepCount(room);const doneSteps=scenarioDoneStepCount(room);if(!totalSteps)return '0 / 0';if(runtime==='idle'||runtime==='stopped')return `0 / ${totalSteps}`;if(runtime==='done')return `${totalSteps} / ${totalSteps}`;return `${Math.min(totalSteps,doneSteps+1)} / ${totalSteps}`;}
function scenarioWaitText(room){return room&&room.scenario_wait_summary||'none';}
function scenarioProgressBranchStepState(branchRuntime,localIndex){const steps=Array.isArray(branchRuntime&&branchRuntime.steps)?branchRuntime.steps:[];const step=steps.find(item=>Number(item&&item.index)===localIndex);const state=String(step&&step.state||'').toLowerCase();return state||null;}
function scenarioProgressBranchState(room,branchRuntime,localIndex,globalIndex){
const explicit=scenarioProgressBranchStepState(branchRuntime,localIndex);
if(explicit)return explicit;
if(!branchRuntime)return 'pending';
const branchState=String(branchRuntime.state||'').toLowerCase();
const failed=Number(branchRuntime.failed_step_index);
if(Number.isFinite(failed)&&(failed===localIndex||failed===globalIndex))return 'error';
const done=Math.max(0,Number(branchRuntime.done_steps??branchRuntime.completed_step_count)||0);
if(branchState==='done'||localIndex<done)return 'done';
const currentLocal=Number(branchRuntime.current_step_local_index);
const currentGlobal=Number(branchRuntime.current_step_index);
const isCurrent=(Number.isFinite(currentLocal)&&currentLocal===localIndex)||
(Number.isFinite(currentGlobal)&&currentGlobal===globalIndex);
if(isCurrent&&(branchState==='running'||branchState==='waiting'||branchState==='error'))return branchState==='error'?'error':'current';
return 'pending';
}
function scenarioProgressIcon(state){if(state==='done')return '&#10003;';if(state==='current')return '&rarr;';if(state==='error')return '!';return '';}
function scenarioProgressBranches(room,scenarioOrSteps){if(room&&Array.isArray(room.scenario_branches)&&room.scenario_branches.some(branch=>Array.isArray(branch&&branch.steps)&&branch.steps.length))return room.scenario_branches.map((branch,index)=>({id:branch.id||`branch_${index+1}`,name:branch.name||`Branch ${index+1}`,type:String(branch.type||'normal').toLowerCase()==='reactive'?'reactive':'normal',enabled:branch.active!==false,required_for_completion:branch.required_for_completion!==false,trigger:branch.trigger||null,current_step_text:branch.current_step_text||'',wait_summary:branch.wait_summary||'',steps:Array.isArray(branch.steps)?branch.steps:[]}));if(scenarioOrSteps&&Array.isArray(scenarioOrSteps.branches)&&scenarioOrSteps.branches.length)return scenarioOrSteps.branches.map((branch,index)=>{const type=String(branch.type||'normal').toLowerCase()==='reactive'?'reactive':'normal';return {id:branch.id||`branch_${index+1}`,name:branch.name||`Branch ${index+1}`,type,enabled:branch.enabled!==false,required_for_completion:type==='normal'&&branch.required_for_completion!==false,trigger:branch.trigger||null,steps:scenarioBranchDisplaySteps(branch)};});const steps=Array.isArray(scenarioOrSteps)?scenarioOrSteps:(scenarioOrSteps&&Array.isArray(scenarioOrSteps.steps)?scenarioOrSteps.steps:[]);return steps.length?[{id:'main',name:'Main',type:'normal',enabled:true,required_for_completion:true,steps}]:[];}
function scenarioProgressBranchRuntime(room,branch,index){const runtimes=Array.isArray(room&&room.scenario_branches)?room.scenario_branches:[];const byIndex=runtimes.find(item=>Number(item.index)===index);if(byIndex)return byIndex;const branchId=branch&&branch.id||'';if(branchId)return runtimes.find(item=>(item.id||'')===branchId)||null;return null;}
function renderScenarioProgressStep(room,step,index,globalIndex,branchRuntime){const disabled=step&&step.enabled===false;const state=disabled?'disabled':scenarioProgressBranchState(room,branchRuntime,index,globalIndex);const text=step&&step.text||scenarioStepText(step);return `<div class='scenario-progress-step ${state}'><span class='scenario-progress-icon'>${scenarioProgressIcon(state)}</span><span class='scenario-progress-index'>${esc(index+1)}.</span><span class='scenario-progress-text'>${esc(text)}</span>${disabled?`<span class='badge'>disabled</span>`:''}</div>`;}
function scenarioBranchDoneCount(room,branch,branchRuntime,globalStart){
const steps=Array.isArray(branch&&branch.steps)?branch.steps:[];
const total=steps.length;
if(!total)return 0;
return Math.min(total,Math.max(0,Number(branchRuntime&&((branchRuntime.done_steps??branchRuntime.completed_step_count)||0))||0));
}
function scenarioBranchCurrentStep(branch,branchRuntime){
const steps=Array.isArray(branch&&branch.steps)?branch.steps:[];
if(!steps.length)return branch&&branch.type==='reactive'?'No actions':'No steps';
if(branch&&branch.current_step_text)return branch.current_step_text;
return '';
}
function scenarioProgressBar(done,total){const pct=total?Math.max(0,Math.min(100,Math.round(done*100/total))):0;return `<div class='scenario-progress-bar' title='${esc(done)} / ${esc(total)}'><span style='width:${pct}%'></span></div>`;}
function scenarioProgressTypeLabel(branch){return branch.type==='reactive'?'reaction':(branch.required_for_completion?'required':'optional');}
function scenarioBranchWaitText(branchRuntime,branch){
return branch&&branch.wait_summary||'none';
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
return '';
}
function renderScenarioProgressBranch(room,item){
const branch=item.branch;
const steps=Array.isArray(branch.steps)?branch.steps:[];
const branchRuntime=item.runtime;
const state=(branchRuntime&&branchRuntime.state)||(!branch.enabled?'disabled':'idle');
const waitType=(branchRuntime&&branchRuntime.wait_type)||'none';
const waitText=scenarioBranchWaitText(branchRuntime,branch);
const done=scenarioBranchDoneCount(room,branch,branchRuntime,item.start);
const current=scenarioBranchCurrentStep(branch,branchRuntime);
const detailsKey=`room-progress-steps:${room&&room.room_id||'room'}:${branch.id||item.index}`;
const unit=branch.type==='reactive'?'actions':'steps';
const meta=branch.type==='reactive'?`${esc(done)} / ${esc(steps.length)} ${esc(unit)} / ${esc(scenarioProgressTypeLabel(branch))}`:`${esc(done)} / ${esc(steps.length)} ${esc(unit)} / ${esc(scenarioProgressTypeLabel(branch))}${waitType&&waitType!=='none'?` / waiting ${esc(waitText)}`:''}`;
const actionRuntime=branch.type==='reactive'&&state==='waiting'&&(!waitType||waitType==='none')?null:branchRuntime;
return `<section class='scenario-progress-branch ${!branch.enabled?'disabled':''} ${branch.type==='reactive'?'reactive':''} ${state}'><div class='scenario-progress-branch-head'><div class='scenario-progress-branch-main'><div class='scenario-progress-title-row'><div class='scenario-progress-branch-title'>${esc(branch.name||branch.id||`Branch ${item.index+1}`)}</div><span class='badge'>${esc(state)}</span></div><div class='row-meta'>${meta}</div><div class='scenario-progress-current'>${esc(current)}</div>${scenarioProgressBar(done,steps.length)}</div></div><details class='scenario-progress-step-details' ${detailsAttrs(detailsKey,false)}><summary>Show ${esc(unit)}</summary><div class='scenario-progress'>${steps.length?steps.map((step,stepIndex)=>renderScenarioProgressStep(room,step,stepIndex,item.start+stepIndex,actionRuntime)).join(''):`<div class='empty'>No ${esc(unit)}</div>`}</div></details></section>`;
}
function renderScenarioProgressSection(title,items,mode){
if(!items.length)return '';
return `<div class='scenario-progress-section'><div class='scenario-progress-section-title'>${esc(title)}</div><div class='scenario-progress-branches ${esc(mode||'flow')}'>${items.map(item=>renderScenarioProgressBranch(item.room,item)).join('')}</div></div>`;
}
function renderScenarioProgress(room,scenarioOrSteps){
const branches=scenarioProgressBranches(room,scenarioOrSteps);
 if(!branches.length){
 const total=Math.max(0,Number(room&&room.scenario_total_steps)||0);
 const done=Math.max(0,Number(room&&room.scenario_done_steps)||0);
 const current=roomCurrentScenarioText(room)||'No active step';
 if(!total)return `<div class='scenario-progress empty'>No scenario steps</div>`;
 return `<div class='scenario-progress-wrap'><div class='scenario-progress-overview'><div><div class='scenario-progress-overview-title'>${esc(done)} / ${esc(total)} steps</div><div class='row-meta'>Current: ${esc(current)}</div></div>${scenarioProgressBar(done,total)}</div><div class='row-meta'>Detailed step layout loads in the Scenarios view.</div></div>`;
 }
 let offset=0;
const items=branches.map((branch,index)=>{
const steps=Array.isArray(branch.steps)?branch.steps:[];
const branchRuntime=scenarioProgressBranchRuntime(room,branch,index);
const start=offset;
offset+=steps.length;
return {room,branch,index,runtime:branchRuntime,start};
});
const hasRuntimeDetails=items.some(item=>item.runtime);
const flow=items.filter(item=>item.branch.type!=='reactive');
const reactions=items.filter(item=>item.branch.type==='reactive');
const progressItems=flow.length?flow:items;
const total=Math.max(scenarioTotalStepCount(room),progressItems.reduce((sum,item)=>sum+(item.branch.steps||[]).length,0));
const done=hasRuntimeDetails
?progressItems.reduce((sum,item)=>sum+scenarioBranchDoneCount(room,item.branch,item.runtime,item.start),0)
:scenarioDoneStepCount(room);
const active=items.find(item=>item.runtime&&(item.runtime.state==='waiting'||item.runtime.state==='running'||item.runtime.state==='error'));
const activeText=active?`${active.branch.name||active.branch.id}: ${scenarioBranchCurrentStep(active.branch,active.runtime)}`:(roomCurrentScenarioText(room)||'No active branch');
if(!hasRuntimeDetails){
return `<div class='scenario-progress-wrap'><div class='scenario-progress-overview'><div><div class='scenario-progress-overview-title'>${esc(done)} / ${esc(total)} steps</div><div class='row-meta'>Current: ${esc(activeText)}</div></div>${scenarioProgressBar(done,total)}</div>${renderScenarioProgressSection('Scenario layout',progressItems,'flow')}${renderScenarioProgressSection('Reaction branches',reactions,'reactions')}</div>`;
}
return `<div class='scenario-progress-wrap'><div class='scenario-progress-overview'><div><div class='scenario-progress-overview-title'>${esc(done)} / ${esc(total)} steps</div><div class='row-meta'>Current: ${esc(activeText)}</div></div>${scenarioProgressBar(done,total)}</div>${renderScenarioActiveWaits(room,items)}${renderScenarioProgressSection('Flow branches',flow,'flow')}${renderScenarioProgressSection('Reaction branches',reactions,'reactions')}</div>`;
}
function scenarioValidationText(s){if(!s)return 'No scenario selected';const n=Number(s.validation_issue_count)||0;if(s.valid===false)return `${n||1} validation issue${n===1?'':'s'}`;return n?`Valid, ${n} warning${n===1?'':'s'}`:'Valid';}
function scenarioIssueLocationText(issue){
const branchId=String(issue&&issue.branch_id||'').trim();
const variantIndex=Number(issue&&issue.variant_index);
const actionIndex=Number(issue&&issue.action_index);
if(branchId){
if(Number.isFinite(variantIndex)&&variantIndex>=0&&Number.isFinite(actionIndex)&&actionIndex>=0){
return `reaction ${branchId} / action ${actionIndex+1}`;
}
return `reaction ${branchId}`;
}
return `step ${issue&&issue.step_index||0}`;
}
function scenarioIssueHtml(issues){return Array.isArray(issues)&&issues.length?`<div class='validation-list'>${issues.map(i=>`<div class='validation-item'>${esc(i.level||'error')} ${esc(scenarioIssueLocationText(i))} / ${esc(i.code||'VALIDATION')}: ${esc(i.message||'')}</div>`).join('')}</div>`:'';}
function scenarioDraftValidationHtml(){const r=scenarioValidationReportCurrent()||scenarioClientValidationReportCurrent();if(!r)return '';const errors=Number(r.error_count)||0;const warnings=Number(r.warning_count)||0;const summary=errors?`${errors} error${errors===1?'':'s'}, ${warnings} warning${warnings===1?'':'s'}`:(warnings?`${warnings} warning${warnings===1?'':'s'}`:'valid');const source=String(r._source||'')==='client'?'Editor validation':'Draft validation';const layers=r&&r._layers&&typeof r._layers==='object'?r._layers:null;const fieldCount=Number(layers&&layers.field&&layers.field.issue_count)||0;const draftCount=Number(layers&&layers.draft&&layers.draft.issue_count)||0;const layerText=layers?` <span class='row-meta'>(field ${fieldCount}, draft ${draftCount})</span>`:'';return `<div class='row-meta ${errors?'bad-text':''}'>${esc(source)}: ${esc(summary)}${layerText}</div>${scenarioIssueHtml(r.issues)}`;}
