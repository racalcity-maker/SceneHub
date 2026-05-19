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
const profiles=roomProfiles(room.room_id);
const selectedId=roomSelectedProfileId(room.room_id)||room.selected_profile_id||'';
const selected=profiles.find(p=>p.id===selectedId)||null;
const selectedName=room.selected_profile_name||((selected&&selected.id===selectedId)?selected.name:'');
const scenarioId=room.selected_profile_scenario_id||((selected&&selected.scenario_id)||room.selected_scenario_id||'');
const scenario=roomSelectedScenarioObject(room);
const runtime=room.scenario_runtime_state||'idle';
const waitType=room.scenario_wait_type||'none';
const runningName=room.running_scenario_name||scenarioDisplayName(room.room_id,room.running_scenario_id||scenarioId,'none');
const currentStepText=roomCurrentScenarioText(room)||'';
const canStart=roomCanStartGame(room,selected);
const sessionPresent=!!room.session_present||['running','paused','finished'].includes(room.session_state||'');
const canStop=sessionPresent&&room.session_state!=='finished';
const canReset=sessionPresent;
const canPause=room.timer_state==='running';
const canResume=room.timer_state==='paused';
const canAdjust=(Number(room.timer_duration_ms)||0)>0||(Number(room.timer_remaining_ms)||0)>0;
const canApprove=!!(room.selected_scenario_id||room.running_scenario_id)&&runtime==='waiting'&&waitType==='operator';
const skippableBranch=scenarioSkippableWaitBranch(room);
const skipBranchId=(skippableBranch&&skippableBranch.id)||'';
const canSkipWait=!!(room.selected_scenario_id||room.running_scenario_id)&&runtime==='waiting'&&waitType!=='none'&&!!room.scenario_wait_operator_skip_allowed;
const approveLabel=room.scenario_wait_operator_label||'Continue';
const skipWaitLabel=room.scenario_wait_operator_skip_label||'Skip wait';
const waitPrompt=room.scenario_wait_operator_prompt||scenarioWaitText(room);
const flags=Array.isArray(room.scenario_flags)?room.scenario_flags:[];
const flagsHtml=flags.length?`<details class='scenario-advanced'><summary>Runtime flags</summary><div class='step-list'>${flags.map(flag=>`<div class='step-item'><span>${esc(flag.name||'flag')}</span><span class='badge'>${flag.value?'true':'false'}</span></div>`).join('')}</div></details>`:'';
const assetTotal=Number(room.asset_audio_total)||0;
const assetProblem=(Number(room.asset_audio_missing)||0)+(Number(room.asset_audio_bad)||0)+(Number(room.asset_audio_unsupported)||0)+(Number(room.asset_audio_io_error)||0);
const assetPending=Number(room.asset_audio_unknown)||0;
const assetClass=assetProblem?'bad-text':(assetPending?'warn-text':'');
const assetHtml=assetTotal?`<div class='row-meta ${assetClass}'>Assets: ${esc(room.asset_prepare_state||'unknown')} / ${esc(room.asset_audio_ready||0)} ready of ${esc(assetTotal)}${assetProblem?`, ${esc(assetProblem)} error`:''}${assetPending?`, ${esc(assetPending)} pending`:''}</div>`:'';
const clockState=room.timer_state||room.session_state||'idle';
const startMinutes=Math.max(1,Math.round(((Number(room.timer_duration_ms)||3600000)/60000)));
return `<div class='room-console' data-room-operator-console='1'><div class='card room-primary'><div class='card-head'><div><h2 class='section-title'>Game control</h2>${roomClockHtml(room,'div','room-clock')}<div class='row-meta'>${esc(clockState)} / session ${esc(room.session_state||'idle')}</div></div>${status(roomDerivedHealth(room))}</div>${profiles.length?`<label class='field-stack'><span>Game mode</span><select class='scenario-select' data-room-profile-room='${esc(room.room_id)}'><option value='' ${
selected?'':'selected'}
>Select game mode</option>${
profiles.map(p=>`<option value='${esc(p.id)}' ${selected&&selected.id===p.id?'selected':''} ${p.valid===false?'disabled':''}>${esc(p.name||p.id)} (${fmtClock(p.duration_ms)}${p.valid===false?', invalid':''})</option>`).join('')}
</select></label><div class='kvs' style='margin-top:12px'><div class='kv'><span class='k'>Mode</span><span class='v'>${
esc(selectedName||selectedId||'none')}
</span></div><div class='kv'><span class='k'>Scenario</span><span class='v'> ${
esc(scenarioName(room.room_id,scenarioId))}
</span></div><div class='kv'><span class='k'>Duration</span><span class='v'>${esc(selected?fmtClock(selected.duration_ms):'none')}</span></div></div>${assetHtml}`:noProfilesHtml(room.room_id)}<div style='height:12px'></div>${renderRoomGameButtons(room,canStart,canStop,canReset)}</div><div class='card ${canApprove?'operator-gate':(waitType!=='none'?'room-wait':'')}'><h2 class='section-title'>Runtime</h2><div class='kvs'><div class='kv'><span class='k'>Scenario</span><span class='v'>${esc(runningName)}</span></div><div class='kv'><span class='k'>Runtime</span><span class='v'>${esc(runtime)}</span></div><div class='kv'><span class='k'>Step</span><span class='v'>${esc(scenarioStepLabel(room))}</span></div><div class='kv'><span class='k'>Current</span><span class='v'>${esc(currentStepText||'none')}</span></div><div class='kv'><span class='k'>Waiting</span><span class='v'>${esc(scenarioWaitText(room))}</span></div></div>${canApprove?`<div class='operator-prompt'>${
esc(waitPrompt)}
</div>`:''}${canSkipWait?`<div class='operator-prompt'>Operator override available: ${esc(skipWaitLabel)}</div>`:''}${room.scenario_operator_message?`<div class='operator-prompt'>${
esc(room.scenario_operator_message)}
</div>`:''}${flagsHtml}${room.scenario_last_error?`<div class='row-meta bad-text'>${
esc(room.scenario_last_error)}
</div>`:''}<div style='height:12px'></div>${uiActions([
uiButton({label:approveLabel,kind:'approve',action:'room.scenario.runtime',dataset:{op:'approve','room-id':room.room_id},disabled:!canApprove}),
canSkipWait?uiButton({label:skipWaitLabel,action:'room.scenario.runtime',dataset:{op:'next','room-id':room.room_id,'branch-id':skipBranchId},confirm:'Force complete current scenario wait?'}):'',
uiButton({label:'Pause',action:'room.timer',dataset:{op:'pause','room-id':room.room_id},disabled:!canPause}),
uiButton({label:'Resume',action:'room.timer',dataset:{op:'resume','room-id':room.room_id},disabled:!canResume}),
uiButton({label:'+1 min',action:'room.timer',dataset:{op:'plus1','room-id':room.room_id},disabled:!canAdjust}),
uiButton({label:'-1 min',action:'room.timer',dataset:{op:'minus1','room-id':room.room_id},disabled:!canAdjust}),
])}<details class='scenario-advanced'><summary>Manual timer start</summary><div class='timer-start'><input id='gm_timer_minutes' type='number' min='1' step='1' value='${startMinutes}' placeholder='Minutes' aria-label='Duration in minutes'>${uiButton({label:'Start timer',action:'room.timer',dataset:{op:'start','room-id':room.room_id}})}</div></details></div></div><div class='card' data-room-scenario-progress='${esc(room.room_id)}'><h2 class='section-title'>Scenario progress</h2>${renderScenarioProgress(room,scenario)}</div><div style='height:12px'></div>`;
}

function renderRoomScenarioControl(room){
const scenarios=scenarioSummariesByRoom(room.room_id);
const selectedId=currentRoomScenarioId[room.room_id]||room.selected_scenario_id||'';
const selected=scenarios.find(s=>s.id===selectedId)||null;
const selectedName=room.selected_scenario_name||((selected&&selected.id===room.selected_scenario_id)?selected.name:'');
const runningName=room.running_scenario_name||scenarioDisplayName(room.room_id,room.running_scenario_id,'');
const runtime=room.scenario_runtime_state||'idle';
const waitType=room.scenario_wait_type||'none';
const canRun=!!(room.selected_scenario_id||room.running_scenario_id);
const canStart=canRun&&(!selected||selected.valid!==false);
const canNext=canRun&&(runtime==='running'||runtime==='waiting');
const canApprove=canRun&&runtime==='waiting'&&waitType==='operator';
const approveLabel=room.scenario_wait_operator_label||'Continue';
if(!isAdmin()){
return '';
}
return `<details class='scenario-advanced'><summary>Advanced scenario control</summary>${scenarios.length?`<div class='row'><select class='scenario-select' data-room-scenario-room='${esc(room.room_id)}'><option value='' ${
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
room.scenario_last_error?`<div class='row-meta bad-text'>${esc(room.scenario_last_error)}</div>`:''}
<div style='height:12px'></div>${uiActions([
uiButton({label:'Start',action:'room.scenario.runtime',dataset:{op:'start','room-id':room.room_id},disabled:!canStart}),
uiButton({label:'Stop',action:'room.scenario.runtime',dataset:{op:'stop','room-id':room.room_id},disabled:!canRun}),
uiButton({label:approveLabel,kind:'approve',action:'room.scenario.runtime',dataset:{op:'approve','room-id':room.room_id},disabled:!canApprove}),
uiButton({label:'Next',kind:'danger',action:'room.scenario.runtime',dataset:{op:'next','room-id':room.room_id},disabled:!canNext,confirm:'Force complete current scenario wait?'}),
uiButton({label:'Reset',action:'room.scenario.runtime',dataset:{op:'reset','room-id':room.room_id},disabled:!canRun}),
])}`:noScenariosHtml(room.room_id)}</details><div style='height:12px'></div>`;
}

function injectRoomScenarios(){
if(currentView!=='room'||roomTab!=='control')return;
const room=roomById(currentRoomId);
const root=document.getElementById('gm_content');
if(!room||!root)return;
if(root.querySelector('[data-room-operator-console]'))return;
const first=root.querySelector('.card');
if(first)first.insertAdjacentHTML('beforebegin',`<div data-room-control-runtime='${esc(room.room_id)}'><div data-room-runtime-console='1'>${renderRoomOperatorConsole(room)}</div>${isAdmin()?`<div data-room-runtime-admin='1'>${renderRoomScenarioControl(room)}</div>`:''}</div>`);
}

function tabs(active,names,scope){
return `<div class='tabs'>${names.map(n=>`<button class='tab-btn ${active===n?'active':''}' data-action='room.tab' data-scope='${scope}' data-tab='${n}'>${
esc(n[0].toUpperCase()+n.slice(1))}
</button>`).join('')}</div>`;
}

function setPage(title,sub){
document.getElementById('page_title').textContent=title;

document.getElementById('page_sub').textContent=sub||'';
const navView=currentView==='room'?'rooms':currentView;

document.querySelectorAll('.nav-btn').forEach(b=>b.classList.toggle('active',b.dataset.view===navView));
}
