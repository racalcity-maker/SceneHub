// GM panel source part. Edit this file, then rebuild gm_panel.js.
function renderRoomProfileControl(room){
const profiles=roomProfiles(room.room_id);
const selectedId=roomSelectedProfileId(room.room_id)||room.selected_profile_id||'';
const selected=profiles.find(p=>p.id===selectedId)||null;
const selectedName=room.selected_profile_name||((selected&&selected.id===selectedId)?selected.name:'');
const selectedScenarioId=room.selected_profile_scenario_id||((selected&&selected.scenario_id)||'');
const canStart=!!selected&&selected.valid!==false;
return `<div class='card'><h2 class='section-title'>Game mode</h2>${profiles.length?`<label class='field-stack'><span>Selected game mode</span><select class='scenario-select' data-room-profile-room='${esc(room.room_id)}'><option value='' ${
selected?'':'selected'}
>Select game mode</option>${
profiles.map(p=>`<option value='${esc(p.id)}' ${selected&&selected.id===p.id?'selected':''} ${p.valid===false?'disabled':''}>${esc(p.name||p.id)} (${fmtClock(p.duration_ms)}${p.valid===false?', invalid':''})</option>`).join('')}
</select></label><div class='kvs' style='margin-top:12px'><div class='kv'><span class='k'>Mode</span><span class='v'>${
esc(selectedName||room.selected_profile_id||'none')}
</span></div><div class='kv'><span class='k'>Scenario</span><span class='v'> ${
esc(scenarioName(room.room_id,selectedScenarioId))}
</span></div><div class='kv'><span class='k'>Duration</span><span class='v'>${esc(selected?fmtClock(selected.duration_ms):'none')}</span></div></div><div style='height:12px'></div><div class='actions'><button class='approve' data-room-game='start' data-room-id='${esc(room.room_id)}' ${
canStart?'':'disabled'}
>Start game</button><button data-room-game='stop' data-room-id='${esc(room.room_id)}'>Stop game</button><button class='danger' data-room-game='reset' data-room-id='${esc(room.room_id)}'>Reset game</button></div>`:noProfilesHtml(room.room_id)}</div><div style='height:12px'></div>`;
}

function renderRoomOperatorConsole(room){
const profiles=roomProfiles(room.room_id);
const selectedId=roomSelectedProfileId(room.room_id)||room.selected_profile_id||'';
const selected=profiles.find(p=>p.id===selectedId)||null;
const selectedName=room.selected_profile_name||((selected&&selected.id===selectedId)?selected.name:'');
const scenarioId=room.selected_profile_scenario_id||((selected&&selected.scenario_id)||room.selected_scenario_id||'');
const scenario=roomSelectedScenarioObject(room);
const steps=roomScenarioSteps(room);
const runtime=room.scenario_runtime_state||'idle';
const waitType=room.scenario_wait_type||'none';
const hasBranchRuntime=Array.isArray(room.scenario_branches)&&room.scenario_branches.length>1;
const runningName=room.running_scenario_name||scenarioDisplayName(room.room_id,room.running_scenario_id||scenarioId,'none');
const currentStep=roomCurrentScenarioStep(room);
const currentStepText=currentStep?scenarioStepText(currentStep):scenarioStepLabel(room,steps.length);
const canStart=!!selected&&selected.valid!==false;
const canStop=room.session_present&&room.session_state!=='finished';
const canReset=room.session_present;
const canPause=room.timer_state==='running';
const canResume=room.timer_state==='paused';
const canAdjust=(Number(room.timer_duration_ms)||0)>0||(Number(room.timer_remaining_ms)||0)>0;
const canApprove=!!(room.selected_scenario_id||room.running_scenario_id)&&runtime==='waiting'&&waitType==='operator';
const canSkipWait=!hasBranchRuntime&&!!(room.selected_scenario_id||room.running_scenario_id)&&runtime==='waiting'&&waitType!=='none'&&!!room.scenario_wait_operator_skip_allowed;
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
</span></div><div class='kv'><span class='k'>Duration</span><span class='v'>${esc(selected?fmtClock(selected.duration_ms):'none')}</span></div></div>${assetHtml}`:noProfilesHtml(room.room_id)}<div style='height:12px'></div><div class='actions'><button class='approve' data-room-game='start' data-room-id='${esc(room.room_id)}' ${canStart?'':'disabled'}>Start game</button><button data-room-game='stop' data-room-id='${esc(room.room_id)}' ${canStop?'':'disabled'}>Stop game</button><button class='danger' data-room-game='reset' data-room-id='${esc(room.room_id)}' ${canReset?'':'disabled'}>Reset game</button></div></div><div class='card ${canApprove?'operator-gate':(waitType!=='none'?'room-wait':'')}'><h2 class='section-title'>Runtime</h2><div class='kvs'><div class='kv'><span class='k'>Scenario</span><span class='v'>${esc(runningName)}</span></div><div class='kv'><span class='k'>Runtime</span><span class='v'>${esc(runtime)}</span></div><div class='kv'><span class='k'>Step</span><span class='v'>${esc(scenarioStepLabel(room,steps.length))}</span></div><div class='kv'><span class='k'>Current</span><span class='v'>${esc(currentStepText)}</span></div><div class='kv'><span class='k'>Waiting</span><span class='v'>${esc(scenarioWaitText(room))}</span></div></div>${canApprove?`<div class='operator-prompt'>${
esc(waitPrompt)}
</div>`:''}${canSkipWait?`<div class='operator-prompt'>Operator override available: ${esc(skipWaitLabel)}</div>`:''}${room.scenario_operator_message?`<div class='operator-prompt'>${
esc(room.scenario_operator_message)}
</div>`:''}${flagsHtml}${room.scenario_last_error?`<div class='row-meta bad-text'>${
esc(room.scenario_last_error)}
</div>`:''}<div style='height:12px'></div><div class='actions'><button class='approve' data-room-scenario-runtime='approve' data-room-id='${esc(room.room_id)}' ${canApprove?'':'disabled'}>${esc(approveLabel)}</button>${canSkipWait?`<button data-room-scenario-runtime='next' data-room-id='${esc(room.room_id)}'>${esc(skipWaitLabel)}</button>`:''}<button data-room-timer='pause' data-room-id='${esc(room.room_id)}' ${canPause?'':'disabled'}>Pause</button><button data-room-timer='resume' data-room-id='${esc(room.room_id)}' ${canResume?'':'disabled'}>Resume</button><button data-room-timer='plus1' data-room-id='${esc(room.room_id)}' ${canAdjust?'':'disabled'}>+1 min</button><button data-room-timer='minus1' data-room-id='${esc(room.room_id)}' ${canAdjust?'':'disabled'}>-1 min</button></div><details class='scenario-advanced'><summary>Manual timer start</summary><div class='timer-start'><input id='gm_timer_minutes' type='number' min='1' step='1' value='${startMinutes}' placeholder='Minutes' aria-label='Duration in minutes'><button data-room-timer='start' data-room-id='${esc(room.room_id)}'>Start timer</button></div></details></div></div><div class='card'><h2 class='section-title'>Scenario progress</h2>${renderScenarioProgress(room,scenario||steps)}</div><div style='height:12px'></div>`;
}

function renderRoomScenarioControl(room){
const scenarios=roomScenarios(room.room_id);
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
<div style='height:12px'></div><div class='actions'><button data-room-scenario-runtime='start' data-room-id='${esc(room.room_id)}' ${
canStart?'':'disabled'}
>Start</button><button data-room-scenario-runtime='stop' data-room-id='${esc(room.room_id)}' ${
canRun?'':'disabled'}
>Stop</button><button class='approve' data-room-scenario-runtime='approve' data-room-id='${esc(room.room_id)}' ${
canApprove?'':'disabled'}
>${
esc(approveLabel)}
</button><button class='danger' data-room-scenario-runtime='next' data-room-id='${esc(room.room_id)}' ${
canNext?'':'disabled'}
>Next</button><button data-room-scenario-runtime='reset' data-room-id='${esc(room.room_id)}' ${
canRun?'':'disabled'}
>Reset</button></div>`:noScenariosHtml(room.room_id)}</details><div style='height:12px'></div>`;
}

function injectRoomScenarios(){
if(currentView!=='room'||roomTab!=='control')return;
const room=roomById(currentRoomId);
const root=document.getElementById('gm_content');
if(!room||!root)return;
if(root.querySelector('[data-room-operator-console]'))return;
const first=root.querySelector('.card');
if(first)first.insertAdjacentHTML('beforebegin',renderRoomOperatorConsole(room)+(isAdmin()?renderRoomScenarioControl(room):''));
}

function tabs(active,names,scope){
return `<div class='tabs'>${names.map(n=>`<button class='tab-btn ${active===n?'active':''}' data-tab-scope='${scope}' data-tab='${n}'>${
esc(n[0].toUpperCase()+n.slice(1))}
</button>`).join('')}</div>`;
}

function setPage(title,sub){
document.getElementById('page_title').textContent=title;

document.getElementById('page_sub').textContent=sub||'';
const navView=currentView==='room'?'rooms':currentView;

document.querySelectorAll('.nav-btn').forEach(b=>b.classList.toggle('active',b.dataset.view===navView));
}
