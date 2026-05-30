// GM panel source part. Edit this file, then rebuild gm_panel.js.
function renderRoomGameButtons(room,canStart,canStop,canReset){
return uiActions([
uiButton({label:'Start game',action:'room.game',kind:'approve',dataset:{op:'start','room-id':room.room_id},disabled:!canStart}),
uiButton({label:'Stop game',action:'room.game',dataset:{op:'stop','room-id':room.room_id},disabled:!canStop,confirm:'Stop this game session?'}),
uiButton({label:'Reset game',action:'room.game',kind:'danger',dataset:{op:'reset','room-id':room.room_id},disabled:!canReset,confirm:'Reset this game session?'}),
]);
}

function roomCanStartGame(room,selected){
return !!selected&&selected.valid!==false&&roomDerivedHealth(room)!=='fault';
}

function renderRoomOperatorProfileStatus(room){
const profiles=roomProfiles(room.room_id);
const selectedId=roomSelectedProfileId(room.room_id)||room.selected_profile_id||'';
const selected=profiles.find(p=>p.id===selectedId)||null;
const selectedName=room.selected_profile_name||((selected&&selected.id===selectedId)?selected.name:'');
const scenarioId=room.selected_profile_scenario_id||((selected&&selected.scenario_id)||room.selected_scenario_id||'');
const assetTotal=Number(room.asset_audio_total)||0;
const assetProblem=(Number(room.asset_audio_missing)||0)+(Number(room.asset_audio_bad)||0)+(Number(room.asset_audio_unsupported)||0)+(Number(room.asset_audio_io_error)||0);
const assetPending=Number(room.asset_audio_unknown)||0;
const assetClass=assetProblem?'bad-text':(assetPending?'warn-text':'');
const assetHtml=assetTotal?`<div class='row-meta ${assetClass}'>Assets: ${esc(room.asset_prepare_state||'unknown')} / ${esc(room.asset_audio_ready||0)} ready of ${esc(assetTotal)}${assetProblem?`, ${esc(assetProblem)} error`:''}${assetPending?`, ${esc(assetPending)} pending`:''}</div>`:'';
if(!profiles.length)return noProfilesHtml(room.room_id);
return `<label class='field-stack'><span>Game mode</span><select class='scenario-select' data-room-profile-room='${esc(room.room_id)}'><option value='' ${
selected?'':'selected'}
>Select game mode</option>${
profiles.map(p=>`<option value='${esc(p.id)}' ${selected&&selected.id===p.id?'selected':''} ${p.valid===false?'disabled':''}>${esc(p.name||p.id)} (${fmtClock(p.duration_ms)}${p.valid===false?', invalid':''})</option>`).join('')}
</select></label><div class='kvs' style='margin-top:12px'><div class='kv'><span class='k'>Mode</span><span class='v'>${
esc(selectedName||selectedId||'none')}
</span></div><div class='kv'><span class='k'>Scenario</span><span class='v'> ${
esc(scenarioName(room.room_id,scenarioId))}
</span></div><div class='kv'><span class='k'>Duration</span><span class='v'>${esc(selected?fmtClock(selected.duration_ms):'none')}</span></div></div>${assetHtml}`;
}

function renderRoomOperatorGameActions(room){
const profiles=roomProfiles(room.room_id);
const selectedId=roomSelectedProfileId(room.room_id)||room.selected_profile_id||'';
const selected=profiles.find(p=>p.id===selectedId)||null;
const canStart=roomCanStartGame(room,selected);
const sessionPresent=!!room.session_present||['running','paused','finished'].includes(room.session_state||'');
const canStop=sessionPresent&&room.session_state!=='finished';
const canReset=sessionPresent;
const startMinutes=gmRoomTimerStartMinutes(room);
return `<div class='room-game-actions-bar'><div class='room-game-main-actions'>${renderRoomGameButtons(room,canStart,canStop,canReset)}</div><div class='room-game-timer-inline'><input id='gm_timer_minutes' type='number' min='1' step='1' value='${startMinutes}' placeholder='Minutes' aria-label='Duration in minutes'>${uiButton({label:'Manual timer',action:'room.timer',dataset:{op:'start','room-id':room.room_id}})}</div></div>`;
}

function roomRuntimeCardClass(room){
const runtime=room.scenario_runtime_state||'idle';
const waitType=room.scenario_wait_type||'none';
if(runtime==='waiting'&&waitType==='operator')return 'card runtime-card runtime-card-operator';
if(runtime==='waiting'||runtime==='running')return 'card runtime-card runtime-card-waiting';
return 'card runtime-card';
}

function roomRuntimeStatusModel(room){
const profiles=roomProfiles(room.room_id);
const selectedId=roomSelectedProfileId(room.room_id)||room.selected_profile_id||'';
const selected=profiles.find(p=>p.id===selectedId)||null;
const scenarioId=room.selected_profile_scenario_id||((selected&&selected.scenario_id)||room.selected_scenario_id||'');
const runtime=room.scenario_runtime_state||'idle';
const waitType=room.scenario_wait_type||'none';
const runningName=room.running_scenario_name||scenarioDisplayName(room.room_id,room.running_scenario_id||scenarioId,'none');
const currentStepText=roomCurrentScenarioText(room)||'none';
const canApprove=!!(room.selected_scenario_id||room.running_scenario_id)&&runtime==='waiting'&&waitType==='operator';
const canSkipWait=!!(room.selected_scenario_id||room.running_scenario_id)&&runtime==='waiting'&&waitType!=='none'&&!!room.scenario_wait_operator_skip_allowed;
const skipWaitLabel=room.scenario_wait_operator_skip_label||'Skip wait';
const waitPrompt=room.scenario_wait_operator_prompt||scenarioWaitText(room);
const flags=Array.isArray(room.scenario_flags)?room.scenario_flags:[];
return {
scenario:runningName,
runtime,
step:scenarioStepLabel(room),
current:currentStepText||'none',
waiting:scenarioWaitText(room),
canApprove,
canSkipWait,
skipWaitLabel,
waitPrompt,
flags,
operatorMessage:room.scenario_operator_message||'',
lastError:room.scenario_last_error||''
};
}

function renderRoomRuntimeMessagesHtml(model){
const hasCallout=!!(model.canApprove||model.canSkipWait||model.operatorMessage);
const calloutPrimary=model.operatorMessage||model.waitPrompt||'';
const calloutMeta=[
model.canApprove&&model.waitPrompt&&model.operatorMessage&&model.waitPrompt!==model.operatorMessage?model.waitPrompt:'',
model.canSkipWait?`Override available: ${model.skipWaitLabel}`:''
].filter(Boolean).join(' / ');
return `${hasCallout?`<div class='runtime-operator-callout'><div class='runtime-operator-callout-main'>${esc(calloutPrimary||'Waiting for operator')}</div>${calloutMeta?`<div class='runtime-operator-callout-meta'>${esc(calloutMeta)}</div>`:''}</div>`:''}${model.lastError?`<div class='row-meta bad-text'>${esc(model.lastError)}</div>`:''}`;
}

function renderRoomOperatorRuntimeStatus(room){
const model=roomRuntimeStatusModel(room);
return `<h2 class='section-title'>Runtime</h2><div class='kvs room-runtime-kvs' data-room-runtime-kvs='1'><div class='kv room-runtime-kv room-runtime-kv-scenario'><span class='k'>Scenario</span><span class='v' data-runtime-value='scenario'>${esc(model.scenario)}</span></div><div class='kv room-runtime-kv room-runtime-kv-runtime'><span class='k'>Runtime</span><span class='v' data-runtime-value='runtime'>${esc(model.runtime)}</span></div><div class='kv room-runtime-kv room-runtime-kv-step'><span class='k'>Step</span><span class='v' data-runtime-value='step'>${esc(model.step)}</span></div><div class='kv room-runtime-kv room-runtime-kv-current'><span class='k'>Current</span><span class='v' data-runtime-value='current'>${esc(model.current)}</span></div><div class='kv room-runtime-kv room-runtime-kv-waiting'><span class='k'>Waiting</span><span class='v' data-runtime-value='waiting'>${esc(model.waiting)}</span></div></div><div class='room-runtime-messages' data-room-runtime-messages='1'>${renderRoomRuntimeMessagesHtml(model)}</div>`;
}

function renderRoomOperatorRuntimeActions(room){
const profiles=roomProfiles(room.room_id);
const selectedId=roomSelectedProfileId(room.room_id)||room.selected_profile_id||'';
const selected=profiles.find(p=>p.id===selectedId)||null;
const runtime=room.scenario_runtime_state||'idle';
const waitType=room.scenario_wait_type||'none';
const canPause=room.timer_state==='running';
const canResume=room.timer_state==='paused';
const canAdjust=(Number(room.timer_duration_ms)||0)>0||(Number(room.timer_remaining_ms)||0)>0;
const canApprove=!!(room.selected_scenario_id||room.running_scenario_id)&&runtime==='waiting'&&waitType==='operator';
const skippableBranch=scenarioSkippableWaitBranch(room);
const skipBranchId=(skippableBranch&&skippableBranch.id)||'';
const canSkipWait=!!(room.selected_scenario_id||room.running_scenario_id)&&runtime==='waiting'&&waitType!=='none'&&!!room.scenario_wait_operator_skip_allowed;
const approveLabel=room.scenario_wait_operator_label||'Continue';
const progressData=scenarioProgressData(room,roomSelectedScenarioObject(room));
return `${uiActions([
uiButton({label:approveLabel,kind:'approve',action:'room.scenario.runtime',dataset:{op:'approve','room-id':room.room_id},disabled:!canApprove}),
canSkipWait?uiButton({label:skipWaitLabel,action:'room.scenario.runtime',dataset:{op:'next','room-id':room.room_id,'branch-id':skipBranchId},confirm:'Force complete current scenario wait?'}):'',
uiButton({label:'Pause',action:'room.timer',dataset:{op:'pause','room-id':room.room_id},disabled:!canPause}),
uiButton({label:'Resume',action:'room.timer',dataset:{op:'resume','room-id':room.room_id},disabled:!canResume}),
uiButton({label:'+1 min',action:'room.timer',dataset:{op:'plus1','room-id':room.room_id},disabled:!canAdjust}),
uiButton({label:'-1 min',action:'room.timer',dataset:{op:'minus1','room-id':room.room_id},disabled:!canAdjust}),
])}<div class='room-runtime-progress-compact' data-room-runtime-progress-compact='1'>${renderScenarioProgressOverviewCompactHtml(progressData)}</div>`;
}

function renderRoomScenarioControlStatus(room){
const scenarios=scenarioSummariesByRoom(room.room_id);
const selectedId=currentRoomScenarioId[room.room_id]||room.selected_scenario_id||'';
const selected=scenarios.find(s=>s.id===selectedId)||null;
const selectedName=room.selected_scenario_name||((selected&&selected.id===room.selected_scenario_id)?selected.name:'');
const runningName=room.running_scenario_name||scenarioDisplayName(room.room_id,room.running_scenario_id,'');
if(!scenarios.length)return noScenariosHtml(room.room_id);
return `<div class='row'><select class='scenario-select' data-room-scenario-room='${esc(room.room_id)}'><option value='' ${
selected?'':'selected'}
>Select scenario</option>${
scenarios.map(s=>`<option value='${esc(s.id)}' ${selected&&selected.id===s.id?'selected':''}>${esc(s.name||s.id)} (${esc(s.step_count||0)} steps${s.valid===false?', invalid':''})</option>`).join('')}
</select></div><div class='row-meta'>Selected: ${
esc(selectedName||room.selected_scenario_id||'none')}
 / ${
esc(scenarioValidationText(selected))}
</div>${
runningName?`<div class='row-meta'>Running snapshot: ${esc(runningName)} #${esc(room.running_scenario_generation||0)}</div>`:''}
${selected&&selected.valid===false&&Array.isArray(selected.validation_issues)?`<div class='row-meta bad-text'>${esc((selected.validation_issues[0]&&selected.validation_issues[0].message)||'Scenario validation failed')}</div>`:''}
${
room.scenario_last_error?`<div class='row-meta bad-text'>${esc(room.scenario_last_error)}</div>`:''}`;
}

function renderRoomScenarioControlActions(room){
const scenarios=scenarioSummariesByRoom(room.room_id);
const selectedId=currentRoomScenarioId[room.room_id]||room.selected_scenario_id||'';
const selected=scenarios.find(s=>s.id===selectedId)||null;
const runtime=room.scenario_runtime_state||'idle';
const waitType=room.scenario_wait_type||'none';
const canRun=!!(room.selected_scenario_id||room.running_scenario_id);
const canStart=canRun&&(!selected||selected.valid!==false);
const canNext=canRun&&(runtime==='running'||runtime==='waiting');
const canApprove=canRun&&runtime==='waiting'&&waitType==='operator';
const approveLabel=room.scenario_wait_operator_label||'Continue';
if(!scenarios.length)return '';
return `<div style='height:12px'></div>${uiActions([
uiButton({label:'Start',action:'room.scenario.runtime',dataset:{op:'start','room-id':room.room_id},disabled:!canStart}),
uiButton({label:'Stop',action:'room.scenario.runtime',dataset:{op:'stop','room-id':room.room_id},disabled:!canRun}),
uiButton({label:approveLabel,kind:'approve',action:'room.scenario.runtime',dataset:{op:'approve','room-id':room.room_id},disabled:!canApprove}),
uiButton({label:'Next',kind:'danger',action:'room.scenario.runtime',dataset:{op:'next','room-id':room.room_id},disabled:!canNext,confirm:'Force complete current scenario wait?'}),
uiButton({label:'Reset',action:'room.scenario.runtime',dataset:{op:'reset','room-id':room.room_id},disabled:!canRun}),
])}`;
}

function renderRoomProfileControl(room){
const profiles=roomProfiles(room.room_id);
const selectedId=roomSelectedProfileId(room.room_id)||room.selected_profile_id||'';
const selected=profiles.find(p=>p.id===selectedId)||null;
const selectedName=room.selected_profile_name||((selected&&selected.id===selectedId)?selected.name:'');
const selectedScenarioId=room.selected_profile_scenario_id||((selected&&selected.scenario_id)||'');
const canStart=roomCanStartGame(room,selected);
return `<div class='card'><h2 class='section-title'>Game mode</h2>${profiles.length?`<label class='field-stack'><span>Selected game mode</span><select class='scenario-select' data-room-profile-room='${esc(room.room_id)}'><option value='' ${
selected?'':'selected'}
>Select game mode</option>${
profiles.map(p=>`<option value='${esc(p.id)}' ${selected&&selected.id===p.id?'selected':''} ${p.valid===false?'disabled':''}>${esc(p.name||p.id)} (${fmtClock(p.duration_ms)}${p.valid===false?', invalid':''})</option>`).join('')}
</select></label><div class='kvs' style='margin-top:12px'><div class='kv'><span class='k'>Mode</span><span class='v'>${
esc(selectedName||room.selected_profile_id||'none')}
</span></div><div class='kv'><span class='k'>Scenario</span><span class='v'> ${
esc(scenarioName(room.room_id,selectedScenarioId))}
</span></div><div class='kv'><span class='k'>Duration</span><span class='v'>${esc(selected?fmtClock(selected.duration_ms):'none')}</span></div></div><div style='height:12px'></div>${renderRoomGameButtons(room,canStart,true,true)}`:noProfilesHtml(room.room_id)}</div><div style='height:12px'></div>`;
}

function renderRoomOperatorConsole(room){
const scenario=roomSelectedScenarioObject(room);
const clockState=room.timer_state||room.session_state||'idle';
return `<div class='room-control-shell' data-room-operator-console='1'><div class='room-console'><section class='card room-primary room-game-card'><div class='card-head room-card-head'><div><h2 class='section-title'>Game control</h2>${roomClockHtml(room,'div','room-clock')}<div class='row-meta room-hero-meta'>${esc(clockState)} / session ${esc(room.session_state||'idle')}</div></div><div class='room-card-status'>${status(roomDerivedHealth(room))}</div></div><div data-room-game-status='1'>${renderRoomOperatorProfileStatus(room)}</div><div data-room-game-actions='1'>${renderRoomOperatorGameActions(room)}</div></section><section class='${roomRuntimeCardClass(room)} room-runtime-panel' data-room-runtime-card='1'><div data-room-runtime-status='1'>${renderRoomOperatorRuntimeStatus(room)}</div><div data-room-runtime-actions='1'>${renderRoomOperatorRuntimeActions(room)}</div></section></div><section class='card room-progress-card' data-room-scenario-progress='${esc(room.room_id)}'>${renderScenarioProgress(room,scenario)}</section><div style='height:12px'></div></div>`;
}

function renderRoomScenarioControl(room){
if(!isAdmin()){
return '';
}
return `<details class='scenario-advanced room-admin-advanced' data-room-scenario-admin='1'><summary>Advanced scenario control</summary><div data-room-scenario-admin-status='1'>${renderRoomScenarioControlStatus(room)}</div><div data-room-scenario-admin-actions='1'>${renderRoomScenarioControlActions(room)}</div></details><div style='height:12px'></div>`;
}

function renderRoomControlRuntimeShell(room){
return `<div data-room-control-runtime='${esc(room.room_id)}'><div data-room-runtime-console='1'>${renderRoomOperatorConsole(room)}</div></div>`;
}

function renderRoomControlHintCard(room){
return `<div class='card room-support-card'><div class='card-head room-card-head'><div><h2 class='section-title'>Hint</h2><div class='card-sub'>Operator note or player-facing hint.</div></div></div><div class='hint-row'><input id='gm_hint_input' value='${esc(room.hint_message||'')}' placeholder='Hint for players / operator note'>${uiButton({label:'Send hint',action:'room.hint',dataset:{op:'send','room-id':room.room_id}})}${uiButton({label:'Clear',action:'room.hint',dataset:{op:'clear','room-id':room.room_id},disabled:!room.hint_active})}</div></div>`;
}

function renderRoomControlIssuesCard(room){
const issues=roomRelatedIssues(room);
return `<div class='card room-support-card'><div class='card-head room-card-head'><div><h2 class='section-title'>Device issues</h2><div class='card-sub'>Problems affecting this room right now.</div></div></div><div class='list'>${issues.length?issues.slice(0,5).map(issueRow).join(''):`<div class='empty'>No room issues</div>`}</div></div>`;
}

function renderRoomControlEmergencyHtml(room){
const canReset=room.session_present;
const canFinish=room.session_present&&room.session_state!=='finished';
const canScenarioNext=(room.selected_scenario_id||room.running_scenario_id)&&(room.scenario_runtime_state==='running'||room.scenario_runtime_state==='waiting');
return `<div class='card room-support-card room-emergency-card'><div class='card-head room-card-head'><div><h2 class='section-title'>Emergency controls</h2><div class='card-sub'>Rare recovery actions. Use only when the regular flow is blocked.</div></div></div>${uiActions([
uiButton({label:'Stop game',action:'room.game',dataset:{op:'stop','room-id':room.room_id},disabled:!canFinish,confirm:'Stop this game session?'}),
uiButton({label:'Reset timer',action:'room.timer',dataset:{op:'reset','room-id':room.room_id},disabled:!canReset}),
uiButton({label:'Finish session',kind:'danger',action:'room.timer',dataset:{op:'finish','room-id':room.room_id},disabled:!canFinish}),
uiButton({label:'Force next step',kind:'danger',action:'room.scenario.runtime',dataset:{op:'next','room-id':room.room_id},disabled:!canScenarioNext,confirm:'Force complete current scenario wait?'}),
])}</div>`;
}

function renderRoomControlDeleteHtml(room){
if(!isAdmin())return '';
const roomNameText=room.title||room.name||room.room_id;
return `<div class='actions' style='margin-top:14px;justify-content:flex-end'>${uiButton({label:'Delete room',kind:'danger',action:'room.delete',dataset:{'room-id':room.room_id},confirm:`Delete room ${roomNameText}? This also removes profiles and scenarios for this room. Quest devices stay untouched.`})}</div>`;
}

function renderRoomControlView(room){
return `<div class='room-control-view' data-room-control-view='${esc(room.room_id)}'>${renderRoomControlRuntimeShell(room)}<div class='room-support-grid'><div data-room-control-hint='1'>${renderRoomControlHintCard(room)}</div><div data-room-control-issues='1'>${renderRoomControlIssuesCard(room)}</div><div data-room-control-emergency='1'>${renderRoomControlEmergencyHtml(room)}</div></div><div data-room-control-delete='1'>${renderRoomControlDeleteHtml(room)}</div></div>`;
}

function patchRoomControlView(root,room){
if(!root||!room)return false;
const container=root.querySelector('[data-room-control-view]');
if(!container||String(container.dataset.roomControlView||'')!==String(room.room_id||''))return false;
const runtimeShell=container.querySelector(`[data-room-control-runtime="${room.room_id}"]`);
const hint=container.querySelector('[data-room-control-hint]');
const issues=container.querySelector('[data-room-control-issues]');
const emergency=container.querySelector('[data-room-control-emergency]');
const del=container.querySelector('[data-room-control-delete]');
if(!runtimeShell||!hint||!issues||!emergency||!del)return false;
setPage(`Room: ${room.title||room.room_id}`,'Room control',{titleHtml:`<div class='page-room-titlebar'><span class='page-room-title-text'>${esc(`Room: ${room.title||room.room_id}`)}</span><div class='page-room-tabs'>${tabs('control',['control','overview','devices','issues'],'room')}</div></div>`});
document.querySelectorAll('.tab-btn').forEach(btn=>{
btn.classList.toggle('active',(btn.dataset.scope||'')==='room'&&(btn.dataset.tab||'')==='control');
});
if(!renderRoomRuntimePanel(room.room_id)){
patchRoomRuntimeContainer(runtimeShell,renderRoomControlRuntimeShell(room));
}
gmPatchRuntimeSection(hint,renderRoomControlHintCard(room));
gmPatchRuntimeSection(issues,renderRoomControlIssuesCard(room));
gmPatchRuntimeSection(emergency,renderRoomControlEmergencyHtml(room));
gmPatchRuntimeSection(del,renderRoomControlDeleteHtml(room));
return true;
}

function injectRoomScenarios(){
if(currentView!=='room'||roomTab!=='control')return;
const room=roomById(currentRoomId);
const root=document.getElementById('gm_content');
if(!room||!root)return;
if(root.querySelector('[data-room-operator-console]'))return;
const first=root.querySelector('.card');
if(first)first.insertAdjacentHTML('beforebegin',`<div data-room-control-runtime='${esc(room.room_id)}'><div data-room-runtime-console='1'>${renderRoomOperatorConsole(room)}</div></div>`);
}

function tabs(active,names,scope){
return `<div class='tabs'>${names.map(n=>`<button class='tab-btn ${active===n?'active':''}' data-action='room.tab' data-scope='${scope}' data-tab='${n}'>${
esc(n[0].toUpperCase()+n.slice(1))}
</button>`).join('')}</div>`;
}

function setPage(title,sub){
const pageTitle=document.getElementById('page_title');
const pageSub=document.getElementById('page_sub');
const opts=arguments[2]||{};
if(pageTitle){
pageTitle.classList.toggle('page-title-rich',!!opts.titleHtml);
if(Object.prototype.hasOwnProperty.call(opts,'titleHtml'))pageTitle.innerHTML=opts.titleHtml||'';
else pageTitle.textContent=title;
}
if(pageSub){
pageSub.classList.toggle('page-sub-tabs',!!opts.subHtml);
if(Object.prototype.hasOwnProperty.call(opts,'subHtml'))pageSub.innerHTML=opts.subHtml||'';
else pageSub.textContent=sub||'';
const heading=pageTitle&&pageTitle.parentElement;
if(heading)heading.classList.toggle('page-heading-inline',!!opts.inlineSub);
}
const navView=currentView==='room'?'rooms':currentView;

document.querySelectorAll('.nav-btn').forEach(b=>b.classList.toggle('active',b.dataset.view===navView));
}
