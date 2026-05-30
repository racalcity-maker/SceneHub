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
function scenarioReactiveTriggerLabel(branch){
const trigger=branch&&branch.trigger&&typeof branch.trigger==='object'?branch.trigger:{};
const kind=String(trigger.kind||'device_event').toLowerCase();
if(kind==='device_event'){
const device=deviceDisplayName(trigger.device_id||'');
const event=questDeviceEventName(trigger.device_id||'',trigger.event_id||'');
return compactText(`${device}: ${event}`,40);
}
if(kind==='flag_changed')return compactText(`Flag: ${trigger.flag_name||'flag'}`,36);
if(kind==='operator_event')return compactText(`Operator: ${trigger.event_id||trigger.operator_event||'event'}`,36);
if(kind==='runtime_event')return compactText(`Runtime: ${trigger.event_id||trigger.runtime_event||'event'}`,36);
return compactText(branch&&branch.name||'Reactive trigger',36);
}
function scenarioProgressStepCompactText(step){
if(!step)return 'Step';
const type=scenarioStepTypeValue(step);
if(type==='DEVICE_COMMAND'&&String(step.device_id||'')==='system_audio'){
const file=audioBaseName(step&&step.params&&step.params.file||'');
return compactText(file||scenarioStepSummaryText(step)||'Audio',34);
}
if(type==='DEVICE_COMMAND'){
const device=deviceDisplayName(step.device_id);
const command=questDeviceCommandName(step.device_id,step.command_id);
return compactText(`${device}: ${command}`,40);
}
if(type==='WAIT_DEVICE_EVENT'){
return compactText(`Wait ${deviceDisplayName(step.device_id)}: ${questDeviceEventName(step.device_id,step.event_id)}`,40);
}
if(type==='WAIT_TIME')return compactText(waitTimeLabel(step.duration_ms),20);
if(type==='OPERATOR_APPROVAL')return compactText(step.prompt||step.operator_prompt||step.label||'Operator approval',34);
if(type==='SHOW_OPERATOR_MESSAGE')return compactText(step.message||'Operator message',34);
if(type==='SET_FLAG')return compactText(`Set ${step.flag_name||'flag'}`,24);
if(type==='WAIT_FLAGS')return compactText(`Wait flags (${(Array.isArray(step.flags)?step.flags:[]).length})`,24);
if(type==='END_GAME')return 'End game';
if(typeof scenarioStepSummaryText==='function')return compactText(scenarioStepSummaryText(step),40);
return compactText(scenarioStepText(step),40);
}
function scenarioProgressBranches(room,scenarioOrSteps){if(room&&Array.isArray(room.scenario_branches)&&room.scenario_branches.some(branch=>Array.isArray(branch&&branch.steps)&&branch.steps.length))return room.scenario_branches.map((branch,index)=>({id:branch.id||`branch_${index+1}`,name:branch.name||`Branch ${index+1}`,type:String(branch.type||'normal').toLowerCase()==='reactive'?'reactive':'normal',enabled:branch.active!==false,required_for_completion:branch.required_for_completion!==false,trigger:branch.trigger||null,current_step_text:branch.current_step_text||'',wait_summary:branch.wait_summary||'',steps:Array.isArray(branch.steps)?branch.steps:[]}));if(scenarioOrSteps&&Array.isArray(scenarioOrSteps.branches)&&scenarioOrSteps.branches.length)return scenarioOrSteps.branches.map((branch,index)=>{const type=String(branch.type||'normal').toLowerCase()==='reactive'?'reactive':'normal';return {id:branch.id||`branch_${index+1}`,name:branch.name||`Branch ${index+1}`,type,enabled:branch.enabled!==false,required_for_completion:type==='normal'&&branch.required_for_completion!==false,trigger:branch.trigger||null,steps:scenarioBranchDisplaySteps(branch)};});const steps=Array.isArray(scenarioOrSteps)?scenarioOrSteps:(scenarioOrSteps&&Array.isArray(scenarioOrSteps.steps)?scenarioOrSteps.steps:[]);return steps.length?[{id:'main',name:'Main',type:'normal',enabled:true,required_for_completion:true,steps}]:[];}
function scenarioProgressBranchRuntime(room,branch,index){const runtimes=Array.isArray(room&&room.scenario_branches)?room.scenario_branches:[];const byIndex=runtimes.find(item=>Number(item.index)===index);if(byIndex)return byIndex;const branchId=branch&&branch.id||'';if(branchId)return runtimes.find(item=>(item.id||'')===branchId)||null;return null;}
function renderScenarioProgressStep(room,step,index,globalIndex,branchRuntime){const disabled=step&&step.enabled===false;const state=disabled?'disabled':scenarioProgressBranchState(room,branchRuntime,index,globalIndex);const visual=typeof scenarioStepVisualType==='function'?scenarioStepVisualType(step):'command';const icon=typeof scenarioStepIcon==='function'?scenarioStepIcon(step):scenarioProgressIcon(state);const text=scenarioProgressStepCompactText(step);const fullText=(typeof scenarioStepSummaryText==='function'?scenarioStepSummaryText(step):scenarioStepText(step))||text;return `<div class='scenario-progress-step ${state} scenario-step-${esc(visual)}' title='${esc(fullText)}'><span class='scenario-progress-index'>${esc(index+1)}.</span><span class='scenario-step-icon scenario-progress-type-icon'>${icon}</span><span class='scenario-progress-text'>${esc(text)}</span>${disabled?`<span class='badge'>disabled</span>`:''}</div>`;}
function scenarioProgressBranchDomId(room,item){
const branch=item&&item.branch||{};
const roomId=room&&room.room_id||item&&item.room&&item.room.room_id||'room';
const type=branch.type==='reactive'?'reactive':'flow';
return `${roomId}:${type}:${branch.id||item&&item.index||0}`;
}
function scenarioProgressBranchRenderKey(item){
const room=item&&item.room||null;
const branch=item&&item.branch||{};
const branchRuntime=item&&item.runtime||null;
const start=Number(item&&item.start)||0;
const steps=Array.isArray(branch.steps)?branch.steps:[];
const stepStates=steps.map((step,stepIndex)=>{
const disabled=step&&step.enabled===false;
const state=disabled?'disabled':scenarioProgressBranchState(room,branchRuntime,stepIndex,start+stepIndex);
const text=step&&step.text||scenarioStepText(step);
return `${step&&step.id||stepIndex}:${disabled?'0':'1'}:${state}:${text}`;
}).join('~');
const runtimeSteps=Array.isArray(branchRuntime&&branchRuntime.steps)?branchRuntime.steps.map(step=>{
return `${Number(step&&step.index)}:${String(step&&step.state||'')}:${String(step&&step.wait_type||'')}`;
}).join('~'):'';
return runtimeRenderHash([
String(branch.id||item&&item.index||''),
String(branch.name||''),
String(branch.type||'normal'),
branch.enabled===false?'0':'1',
branch.required_for_completion===false?'0':'1',
String(branchRuntime&&branchRuntime.state||''),
String(branchRuntime&&branchRuntime.wait_type||''),
String(branchRuntime&&branchRuntime.wait_summary||branch.wait_summary||''),
String(branchRuntime&&branchRuntime.current_step_text||branch.current_step_text||''),
String(branchRuntime&&((branchRuntime.done_steps??branchRuntime.completed_step_count)||0)||0),
String(branchRuntime&&branchRuntime.failed_step_index!==undefined&&branchRuntime.failed_step_index!==null?branchRuntime.failed_step_index:''),
String(branchRuntime&&branchRuntime.current_step_local_index!==undefined&&branchRuntime.current_step_local_index!==null?branchRuntime.current_step_local_index:''),
String(branchRuntime&&branchRuntime.current_step_index!==undefined&&branchRuntime.current_step_index!==null?branchRuntime.current_step_index:''),
stepStates,
runtimeSteps
].join('|'));
}
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
const unit=branch.type==='reactive'?'actions':'steps';
const progressLabel=`${done} / ${steps.length} ${unit}`;
const metaParts=[scenarioProgressTypeLabel(branch)];
if(waitType&&waitType!=='none'&&state==='waiting')metaParts.push(`waiting ${waitText}`);
const meta=metaParts.filter(Boolean).join(' / ');
const actionRuntime=branch.type==='reactive'&&state==='waiting'&&(!waitType||waitType==='none')?null:branchRuntime;
const fullStepsHtml=steps.length?steps.map((step,stepIndex)=>renderScenarioProgressStep(room,step,stepIndex,item.start+stepIndex,actionRuntime)).join(''):`<div class='empty'>No ${esc(unit)}</div>`;
if(branch.type==='reactive'){
const triggerLabel=scenarioReactiveTriggerLabel(branch);
return `<section class='scenario-progress-branch reactive compact-reaction ${!branch.enabled?'disabled':''} ${state}' data-scenario-progress-branch='${esc(scenarioProgressBranchDomId(room,item))}' data-scenario-progress-branch-key='${esc(scenarioProgressBranchRenderKey(item))}'><div class='scenario-progress-branch-head'><div class='scenario-progress-branch-main'><div class='scenario-progress-title-row'><div class='scenario-progress-branch-title'>${esc(triggerLabel)}</div><div class='scenario-progress-branch-headside'><span class='scenario-progress-count'>${esc(progressLabel)}</span><span class='badge'>${esc(state)}</span></div></div></div></div></section>`;
}
return `<section class='scenario-progress-branch ${!branch.enabled?'disabled':''} ${branch.type==='reactive'?'reactive':''} ${state}' data-scenario-progress-branch='${esc(scenarioProgressBranchDomId(room,item))}' data-scenario-progress-branch-key='${esc(scenarioProgressBranchRenderKey(item))}'><div class='scenario-progress-branch-head'><div class='scenario-progress-branch-main'><div class='scenario-progress-title-row'><div class='scenario-progress-branch-title'>${esc(branch.name||branch.id||`Branch ${item.index+1}`)}</div><div class='scenario-progress-branch-headside'><span class='scenario-progress-count'>${esc(progressLabel)}</span><span class='badge'>${esc(state)}</span></div></div>${meta?`<div class='row-meta'>${esc(meta)}</div>`:''}<div class='scenario-progress-current'>${esc(current)}</div></div></div><div class='scenario-progress-step-details scenario-progress-step-details-static'><div class='scenario-progress'>${fullStepsHtml}</div></div></section>`;
}
function renderScenarioProgressSection(title,items,mode){
if(!items.length)return '';
return `<div class='scenario-progress-section' data-scenario-progress-section='${esc(mode||'flow')}'><div class='scenario-progress-section-title'>${esc(title)}</div><div class='scenario-progress-branches ${esc(mode||'flow')}' data-scenario-progress-branches='${esc(mode||'flow')}'>${items.map(item=>renderScenarioProgressBranch(item.room,item)).join('')}</div></div>`;
}
function scenarioProgressData(room,scenarioOrSteps){
const branches=scenarioProgressBranches(room,scenarioOrSteps);
if(!branches.length){
const total=Math.max(0,Number(room&&room.scenario_total_steps)||0);
const done=Math.max(0,Number(room&&room.scenario_done_steps)||0);
const current=roomCurrentScenarioText(room)||'No active step';
return {
room,
mode:total?'summary':'empty',
total,
done,
activeText:current,
items:[],
flow:[],
reactions:[],
progressItems:[],
hasRuntimeDetails:false
};
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
const active=progressItems.find(item=>item.runtime&&(item.runtime.state==='waiting'||item.runtime.state==='running'||item.runtime.state==='error'))
||items.find(item=>item.runtime&&(item.runtime.state==='waiting'||item.runtime.state==='running'||item.runtime.state==='error'));
const activeText=active?`${active.branch.name||active.branch.id}: ${scenarioBranchCurrentStep(active.branch,active.runtime)}`:(roomCurrentScenarioText(room)||'No active branch');
return {
room,
mode:hasRuntimeDetails?'runtime':'layout',
total,
done,
activeText,
items,
flow,
reactions,
progressItems,
hasRuntimeDetails
};
}

function renderScenarioProgressOverviewHtml(data){
if(!data||data.mode==='empty')return `<div class='scenario-progress empty'>No scenario steps</div>`;
return `<div class='scenario-progress-overview'><div><div class='scenario-progress-overview-title'>${esc(data.done)} / ${esc(data.total)} steps</div><div class='row-meta'>Current: ${esc(data.activeText)}</div></div>${scenarioProgressBar(data.done,data.total)}</div>`;
}

function renderScenarioProgressOverviewCompactHtml(data){
if(!data||data.mode==='empty')return `<div class='scenario-progress-overview compact'><div class='row-meta'>No scenario steps</div></div>`;
return `<div class='scenario-progress-overview compact'><div><div class='scenario-progress-overview-title'>${esc(data.done)} / ${esc(data.total)} steps</div><div class='row-meta'>Current: ${esc(data.activeText)}</div></div>${scenarioProgressBar(data.done,data.total)}</div>`;
}

function renderScenarioProgressWaitsHtml(data){
if(!data)return '';
if(data.mode==='summary')return `<div class='row-meta'>Detailed step layout loads in the Scenarios view.</div>`;
if(data.mode!=='runtime')return `<div class='row-meta'></div>`;
return renderScenarioActiveWaits(data.room,data.items);
}
function scenarioProgressSectionItems(data,mode){
if(!data||data.mode==='empty'||data.mode==='summary')return [];
if(mode==='reactions')return Array.isArray(data.reactions)?data.reactions:[];
if(data.mode==='layout')return Array.isArray(data.progressItems)?data.progressItems:[];
return Array.isArray(data.flow)?data.flow:[];
}

function renderScenarioProgressFlowHtml(data){
if(!data||data.mode==='empty'||data.mode==='summary')return '';
return renderScenarioProgressSection('Flow branches',scenarioProgressSectionItems(data,'flow'),'flow');
}

function renderScenarioProgressReactionsHtml(data){
if(!data||data.mode==='empty'||data.mode==='summary')return '';
const items=scenarioProgressSectionItems(data,'reactions');
if(!items.length)return '';
return renderScenarioProgressSection(`Reactive branches (${items.length})`,items,'reactions');
}

function renderScenarioProgress(room,scenarioOrSteps){
const data=scenarioProgressData(room,scenarioOrSteps);
return `<div class='scenario-progress-wrap' data-room-scenario-progress-wrap='1'><div data-room-scenario-progress-overview='1'></div><div data-room-scenario-progress-waits='1'></div><div data-room-scenario-progress-reactions='1'>${renderScenarioProgressReactionsHtml(data)}</div><div data-room-scenario-progress-flow='1'>${renderScenarioProgressFlowHtml(data)}</div></div>`;
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
