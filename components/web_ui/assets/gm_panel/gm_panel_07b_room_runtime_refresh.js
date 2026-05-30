// GM panel source part. Edit this file, then rebuild gm_panel.js.
function gmStateSnapshotLooksUsable(data){
return !!(data&&typeof data==='object'&&Array.isArray(data.rooms)&&Array.isArray(data.devices)&&Array.isArray(data.issues));
}

function mergeGMSystemSummary(data){
if(!data||!data.summary)return false;
if(!gmStateSnapshotLooksUsable(gmState))return false;
gmState.ok=data.ok!==false;
if(Object.prototype.hasOwnProperty.call(data,'generation'))gmState.generation=data.generation;
gmState.summary=data.summary;
return true;
}

async function loadGMSystemSummaryOnly(forceRender){
if(!gmStateSnapshotLooksUsable(gmState)){
gmStatTag('render.full.request','summary_missing_state');
await loadGMFullSnapshot(true,true);
return;
}
const data=await api.gm.systemSummaryJson();
if(!mergeGMSystemSummary(data)){
gmStatTag('render.full.request','summary_merge_failed');
await loadGMFullSnapshot(true,!!forceRender);
return;
}
syncGMSummaryStatus();
if(forceRender){
gmStatTag('render.full.request','summary_force_render');
render();
return;
}
if(shouldDeferAutoRender()){
gmQueueDeferredRender('sidebar','',true);
return;
}
renderRightSidebar(false);
}

async function loadGMFullSnapshot(silent,forceRender,opts){
opts=opts||{};
gmStatTag('render.full.request',opts.reason||'snapshot');
const requestSeq=++gmSnapshotRequestSeq;
if(!silent){
setStatus('loading','state-unknown');
}
const previousState=gmStateSnapshotLooksUsable(gmState)?gmState:null;
try{
const data=await api.gm.stateJson();
if(requestSeq!==gmSnapshotRequestSeq)return;
if(!gmStateSnapshotLooksUsable(data)){
throw new Error('GM state snapshot is incomplete');
}
gmState=data;
syncRoomTimerBaselines();
loadGMVersions().then(v=>{gmLastVersions=v;}).catch(()=>{});
applyInitialOperatorRoute();
const shouldRenderBeforeStatic=currentView==='rooms';
const staticLoadPromise=loadGMStaticData(!silent||!!forceRender||!!opts.forceStatic);
if(shouldRenderBeforeStatic){
if(silent&&!forceRender&&shouldDeferAutoRender()){
gmQueueDeferredRender('full');
}
else{
render();
}
}
await staticLoadPromise;
if(requestSeq!==gmSnapshotRequestSeq)return;
if(!gmRightSidebarStructureKey)renderRightSidebar(true);
if(silent&&!forceRender&&shouldDeferAutoRender()){
gmQueueDeferredRender('full');
return;
}
render();
}
catch(err){
if(previousState)gmState=previousState;
setStatus('load failed','state-fault');
const root=document.getElementById('gm_content');
if(previousState){
try{
gmStatTag('render.full.request','snapshot_error_recover');
render();
}
catch(renderErr){
renderRightSidebar(false);
}
}
else if(root){
root.innerHTML='<div class="card empty">Failed to load GM state</div>';
}
else{
renderRightSidebar(false);
}
throw err;
}
}

function syncRoomTimerBaselines(){
const now=performance.now();
(gmState&&Array.isArray(gmState.rooms)?gmState.rooms:[]).forEach(room=>{
room._timer_synced_at_ms=now;
});
}

const ROOM_RUNTIME_FIELDS=[
'runtime_schema_version',
'session_present','session_state','timer_state','timer_duration_ms','timer_remaining_ms',
'hint_active','hint_sent_count','hint_message',
'selected_profile_id','selected_profile_name','selected_profile_scenario_id',
'selected_scenario_id','selected_scenario_name',
'running_scenario_id','running_scenario_name','running_scenario_generation',
'scenario_runtime_state','scenario_total_steps','scenario_done_steps','scenario_current_step_text',
'scenario_wait_type','scenario_wait_until_ms','scenario_wait_started_at_ms',
'scenario_wait_summary',
'scenario_wait_events',
'scenario_wait_flags',
'scenario_wait_operator_prompt','scenario_wait_operator_label',
'scenario_wait_operator_skip_allowed','scenario_wait_operator_skip_label',
'scenario_operator_message',
'scenario_device_ids','scenario_device_count',
'scenario_flags',
'scenario_branches',
'scenario_last_error',
'asset_prepare_state','asset_audio_total','asset_audio_ready',
'asset_audio_missing','asset_audio_bad','asset_audio_unsupported',
'asset_audio_io_error','asset_audio_unknown'
];

const ROOM_RUNTIME_DETAIL_ONLY_FIELDS=[
'scenario_wait_events',
'scenario_wait_flags',
'scenario_flags',
'scenario_branches',
'scenario_device_ids',
'related_issue_ids',
'related_issue_count',
'asset_prepare_state',
'asset_audio_total',
'asset_audio_ready',
'asset_audio_missing',
'asset_audio_bad',
'asset_audio_unsupported',
'asset_audio_io_error',
'asset_audio_unknown'
];

const ROOM_RUNTIME_CLOCK_FIELDS=new Set([
'timer_remaining_ms',
'scenario_wait_until_ms',
'scenario_wait_started_at_ms'
]);

function mergeRoomRuntimeState(roomId,data){
if(!gmState||!Array.isArray(gmState.rooms)||!roomId||!data)return false;
const room=gmState.rooms.find(r=>(r.room_id||'')===roomId);
if(!room)return false;
const preserveVisibleDetail=currentView==='room'&&currentRoomId===roomId&&roomTab==='control'&&!Object.prototype.hasOwnProperty.call(data,'scenario_branches');
ROOM_RUNTIME_DETAIL_ONLY_FIELDS.forEach(key=>{
if(preserveVisibleDetail)return;
if(!Object.prototype.hasOwnProperty.call(data,key))delete room[key];
});
ROOM_RUNTIME_FIELDS.forEach(key=>{
if(Object.prototype.hasOwnProperty.call(data,key))room[key]=data[key];
});
room._timer_synced_at_ms=performance.now();
return true;
}

async function loadGMRoomsRuntimeOnly(roomIds,forceRender){
const rooms=gmState&&Array.isArray(gmState.rooms)?gmState.rooms:[];
if(!gmState||!rooms.length){
gmStatTag('render.full.request','rooms_runtime_missing_state');
await loadGMFullSnapshot(true,true);
return;
}
const ids=Array.from(new Set((Array.isArray(roomIds)&&roomIds.length?roomIds:rooms.map(room=>room&&room.room_id)).filter(roomId=>roomId&&roomById(roomId))));
if(!ids.length){
await loadGMSystemSummaryOnly(forceRender);
return;
}
const requestSeq=gmRoomsRuntimeRequestSeq+1;
gmRoomsRuntimeRequestSeq=requestSeq;
const payload=await api.rooms.runtimeSummaryJson();
if(gmRoomsRuntimeRequestSeq!==requestSeq)return;
const summaries=payload&&Array.isArray(payload.rooms)?payload.rooms:[];
let merged=false;
summaries.forEach(summary=>{
if(!summary||!ids.includes(summary.room_id))return;
if(mergeRoomRuntimeState(summary.room_id,summary))merged=true;
});
if(!merged){
await loadGMSystemSummaryOnly(forceRender);
return;
}
syncGMSummaryStatus();
if(forceRender){
gmStatTag('render.full.request','rooms_runtime_force');
render();
return;
}
if(shouldDeferAutoRender()){
gmQueueDeferredRender('full','',true);
return;
}
if(patchRoomsGridRuntime(ids)){
renderRightSidebar(false);
return;
}
gmStatTag('render.full.request','rooms_runtime_fallback');
render();
}

function updateVisibleRoomClocks(){
const rooms=gmState&&Array.isArray(gmState.rooms)?gmState.rooms:[];
if(!rooms.length)return;
document.querySelectorAll('[data-room-clock]').forEach(el=>{
const roomId=el.dataset.roomClock||'';
const room=rooms.find(item=>(item.room_id||'')===roomId);
if(!room)return;
const text=fmtClock(roomTimerDisplayMs(room));
if(el.textContent!==text)el.textContent=text;
});
}

function runtimeRenderHash(text){
let hash=2166136261;
for(let i=0;i<text.length;i++){
hash^=text.charCodeAt(i);
hash=Math.imul(hash,16777619);
}
return (hash>>>0).toString(16);
}

function roomRuntimeSectionKey(room,fields,extras){
const parts=[];
fields.forEach(field=>{
const value=room[field];
let text='';
if(value!==undefined&&value!==null){
text=typeof value==='object'?JSON.stringify(value):String(value);
}
parts.push(`${field}:${text}`);
});
const extraList=Array.isArray(extras)?extras:[];
extraList.forEach(extra=>parts.push(extra));
return runtimeRenderHash(parts.join('|'));
}

function roomCardRenderKey(room){
return roomRuntimeSectionKey(room,[
'room_id','title','name',
'scenario_device_count','device_count','issue_count',
'selected_profile_id','selected_profile_name','selected_profile_scenario_id',
'selected_scenario_id','selected_scenario_name',
'running_scenario_id','running_scenario_name','running_scenario_generation',
'scenario_runtime_state','scenario_current_step_text','scenario_wait_summary',
'session_state'
],[
`health:${String(roomDerivedHealth(room)||'')}`
]);
}

function roomRuntimeGameStatusKey(room){
return roomRuntimeSectionKey(room,[
'selected_profile_id','selected_profile_name','selected_profile_scenario_id',
'selected_scenario_id',
'asset_prepare_state','asset_audio_total','asset_audio_ready',
'asset_audio_missing','asset_audio_bad','asset_audio_unsupported',
'asset_audio_io_error','asset_audio_unknown'
]);
}

function roomRuntimeGameActionsKey(room){
return roomRuntimeSectionKey(room,[
'selected_profile_id',
'session_present','session_state','timer_state','timer_duration_ms'
],[
`health:${String(roomDerivedHealth(room)||'')}`,
`draft_minutes:${gmRoomTimerDraftValue(room.room_id)||''}`
]);
}

function roomRuntimeStatusKey(room){
return roomRuntimeSectionKey(room,[
'selected_profile_id','selected_profile_scenario_id',
'selected_scenario_id',
'running_scenario_id','running_scenario_name','running_scenario_generation',
'scenario_runtime_state','scenario_total_steps','scenario_done_steps','scenario_current_step_text',
'scenario_wait_type','scenario_wait_summary',
'scenario_wait_operator_prompt','scenario_wait_operator_skip_allowed','scenario_wait_operator_skip_label',
'scenario_operator_message','scenario_flags','scenario_last_error'
],[
`step_label:${scenarioStepLabel(room)}`,
`wait_text:${scenarioWaitText(room)}`
]);
}

function roomRuntimeActionsKey(room){
return roomRuntimeSectionKey(room,[
'selected_scenario_id','running_scenario_id',
'scenario_runtime_state','scenario_wait_type',
'scenario_wait_operator_label','scenario_wait_operator_skip_allowed','scenario_wait_operator_skip_label',
'scenario_branches','scenario_total_steps','scenario_done_steps','scenario_current_step_text','scenario_wait_summary',
'timer_state','timer_duration_ms'
],[
`can_adjust:${(Number(room.timer_duration_ms)||0)>0||(Number(room.timer_remaining_ms)||0)>0?'1':'0'}`
]);
}

function roomRuntimeAdminStatusKey(room){
return roomRuntimeSectionKey(room,[
'selected_scenario_id','selected_scenario_name',
'running_scenario_id','running_scenario_name','running_scenario_generation',
'scenario_last_error'
]);
}

function roomRuntimeAdminActionsKey(room){
return roomRuntimeSectionKey(room,[
'selected_scenario_id','running_scenario_id',
'scenario_runtime_state','scenario_wait_type','scenario_wait_operator_label'
]);
}

function roomRuntimeProgressKey(room){
const parts=[];
ROOM_RUNTIME_FIELDS.forEach(field=>{
if(ROOM_RUNTIME_CLOCK_FIELDS.has(field))return;
const value=room[field];
let text='';
if(value!==undefined&&value!==null){
text=typeof value==='object'?JSON.stringify(value):String(value);
}
parts.push(`${field}:${text}`);
});
parts.push(`scenario_gen:${room.running_scenario_generation||0}`);
parts.push(`session_present:${room.session_present?'1':'0'}`);
return runtimeRenderHash(parts.join('|'));
}

function gmRoomTimerDefaultMinutes(room){
return Math.max(1,Math.round(((Number(room&&room.timer_duration_ms)||3600000)/60000)));
}

function gmRoomTimerDraftValue(roomId){
return roomId&&Object.prototype.hasOwnProperty.call(gmRoomTimerMinutesDraft,roomId)?gmRoomTimerMinutesDraft[roomId]:'';
}

function gmRoomTimerStartMinutes(room){
const roomId=room&&room.room_id||'';
const draft=gmRoomTimerDraftValue(roomId);
if(draft!==''&&draft!==null&&draft!==undefined)return draft;
return gmRoomTimerDefaultMinutes(room);
}

function gmSyncRoomTimerDraft(roomId,container,room){
if(!roomId||!container)return;
const input=container.querySelector('#gm_timer_minutes');
if(!input)return;
const raw=String(input.value||'').trim();
const defaultText=String(gmRoomTimerDefaultMinutes(room));
if(!raw||raw===defaultText){
delete gmRoomTimerMinutesDraft[roomId];
return;
}
gmRoomTimerMinutesDraft[roomId]=raw;
}

function gmPatchRuntimeSection(container,html){
if(!container)return false;
if(container.__gmRenderHtml===html)return false;
patchRoomRuntimeContainer(container,html);
return true;
}

function gmCreateRuntimeNode(html){
const tpl=document.createElement('template');
tpl.innerHTML=String(html||'').trim();
return tpl.content.firstElementChild||null;
}

function gmPatchRuntimeOuterElement(element,html){
if(!element)return null;
if(element.__gmRenderHtml===html)return element;
const next=gmCreateRuntimeNode(html);
if(!next)return element;
next.__gmRenderHtml=html;
element.replaceWith(next);
return next;
}

function patchRoomRuntimeStatus(container,room){
if(!container)return;
const kvs=container.querySelector('[data-room-runtime-kvs]');
const messages=container.querySelector('[data-room-runtime-messages]');
if(!kvs||!messages){
patchRoomRuntimeContainer(container,renderRoomOperatorRuntimeStatus(room));
return;
}
const model=roomRuntimeStatusModel(room);
const values={
scenario:model.scenario,
runtime:model.runtime,
step:model.step,
current:model.current,
waiting:model.waiting
};
Object.keys(values).forEach(key=>{
const el=kvs.querySelector(`[data-runtime-value="${key}"]`);
if(!el)return;
const nextText=String(values[key]||'');
if(el.textContent!==nextText)el.textContent=nextText;
});
gmPatchRuntimeSection(messages,renderRoomRuntimeMessagesHtml(model));
}

function patchRoomRuntimeContainer(container,html){
if(!container)return;
if(container.__gmRenderHtml===html)return;
container.__gmRenderHtml=html;
const tpl=document.createElement('template');
tpl.innerHTML=html;
container.replaceChildren(tpl.content.cloneNode(true));
}

function patchRoomRuntimeConsole(panel,room,renderState){
const consoleContainer=panel.querySelector('[data-room-runtime-console]');
if(!consoleContainer)return;
const gameStatus=consoleContainer.querySelector('[data-room-game-status]');
const gameActions=consoleContainer.querySelector('[data-room-game-actions]');
const runtimeStatus=consoleContainer.querySelector('[data-room-runtime-status]');
const runtimeActions=consoleContainer.querySelector('[data-room-runtime-actions]');
const runtimeCard=consoleContainer.querySelector('[data-room-runtime-card]');
if(!gameStatus||!gameActions||!runtimeStatus||!runtimeActions||!runtimeCard){
patchRoomRuntimeContainer(consoleContainer,renderRoomOperatorConsole(room));
return;
}
if(renderState.gameStatusChanged){
gmPatchRuntimeSection(gameStatus,renderState.gameStatusHtml);
}
if(renderState.gameActionsChanged){
gmSyncRoomTimerDraft(room.room_id,gameActions,room);
gmPatchRuntimeSection(gameActions,renderState.gameActionsHtml);
}
if(renderState.runtimeStatusChanged||renderState.runtimeActionsChanged){
const nextRuntimeCardClass=roomRuntimeCardClass(room);
if(runtimeCard.className!==nextRuntimeCardClass)runtimeCard.className=nextRuntimeCardClass;
}
if(renderState.runtimeStatusChanged){
patchRoomRuntimeStatus(runtimeStatus,room);
}
if(renderState.runtimeActionsChanged){
gmPatchRuntimeSection(runtimeActions,renderState.runtimeActionsHtml);
}
}

function patchRoomRuntimeAdmin(panel,room,renderState){
const adminContainer=panel.querySelector('[data-room-runtime-admin]');
if(!adminContainer)return;
const adminStatus=adminContainer.querySelector('[data-room-scenario-admin-status]');
const adminActions=adminContainer.querySelector('[data-room-scenario-admin-actions]');
if(!adminStatus||!adminActions){
patchRoomRuntimeContainer(adminContainer,isAdmin()?renderRoomScenarioControl(room):'');
return;
}
if(renderState.adminStatusChanged){
gmPatchRuntimeSection(adminStatus,renderState.adminStatusHtml);
}
if(renderState.adminActionsChanged){
gmPatchRuntimeSection(adminActions,renderState.adminActionsHtml);
}
}

function patchRoomScenarioProgress(container,room,renderState){
if(!container)return;
const scenario=roomSelectedScenarioObject(room);
const wrap=container.querySelector('[data-room-scenario-progress-wrap]');
const overview=wrap&&wrap.querySelector('[data-room-scenario-progress-overview]');
const waits=wrap&&wrap.querySelector('[data-room-scenario-progress-waits]');
const flow=wrap&&wrap.querySelector('[data-room-scenario-progress-flow]');
const reactions=wrap&&wrap.querySelector('[data-room-scenario-progress-reactions]');
if(!wrap||!overview||!waits||!flow||!reactions){
patchRoomRuntimeContainer(container,renderScenarioProgress(room,scenario));
return;
}
if(renderState.progressFlowChanged){
patchRoomScenarioProgressSection(flow,renderState.progressFlowHtml,scenarioProgressSectionItems(renderState.progressData,'flow'),'flow');
}
if(renderState.progressReactionsChanged){
patchRoomScenarioProgressSection(reactions,renderState.progressReactionsHtml,scenarioProgressSectionItems(renderState.progressData,'reactions'),'reactions');
}
}

function patchRoomScenarioProgressSection(container,html,items,mode){
if(!container)return;
const nextHtml=String(html||'');
const expectedItems=Array.isArray(items)?items:[];
if(!nextHtml||!expectedItems.length){
gmPatchRuntimeSection(container,nextHtml);
return;
}
const section=container.querySelector(`[data-scenario-progress-section="${mode}"]`);
const grid=section&&section.querySelector(`[data-scenario-progress-branches="${mode}"]`);
if(!section||!grid){
gmPatchRuntimeSection(container,nextHtml);
return;
}
const existingCards=new Map(Array.from(grid.children).filter(child=>child&&child.dataset&&child.dataset.scenarioProgressBranch).map(child=>[child.dataset.scenarioProgressBranch,child]));
const expectedIds=new Set();
let patchCount=0;
let skipCount=0;
let fallbackToSectionPatch=false;
expectedItems.forEach((item,index)=>{
if(fallbackToSectionPatch)return;
const branchId=scenarioProgressBranchDomId(item.room,item);
const branchKey=scenarioProgressBranchRenderKey(item);
const branchHtml=renderScenarioProgressBranch(item.room,item);
expectedIds.add(branchId);
let node=existingCards.get(branchId)||null;
if(!node){
node=gmCreateRuntimeNode(branchHtml);
if(!node){
fallbackToSectionPatch=true;
return;
}
node.__gmRenderHtml=branchHtml;
}
else if((node.dataset.scenarioProgressBranchKey||'')!==branchKey){
node=gmPatchRuntimeOuterElement(node,branchHtml)||node;
patchCount++;
}
else{
skipCount++;
}
const anchor=grid.children[index]||null;
if(node.parentElement!==grid){
grid.insertBefore(node,anchor);
patchCount++;
}
else if(anchor!==node){
grid.insertBefore(node,anchor);
patchCount++;
}
});
if(fallbackToSectionPatch){
gmPatchRuntimeSection(container,nextHtml);
return;
}
Array.from(grid.children).forEach(child=>{
if(!child||!child.dataset||!child.dataset.scenarioProgressBranch)return;
if(expectedIds.has(child.dataset.scenarioProgressBranch))return;
child.remove();
patchCount++;
});
if(patchCount)gmStatInc('patch.runtime.progress_branch',patchCount);
if(skipCount)gmStatInc('patch.runtime.progress_branch_skip',skipCount);
container.__gmRenderHtml=nextHtml;
}

function patchRoomsGridRuntime(roomIds){
if(currentView!=='rooms')return false;
const grid=document.querySelector('[data-rooms-grid]');
if(!grid)return false;
if(grid.querySelector('.empty'))return false;
const targetIds=Array.from(new Set((Array.isArray(roomIds)&&roomIds.length?roomIds:(gmState&&Array.isArray(gmState.rooms)?gmState.rooms.map(room=>room&&room.room_id):[])).filter(Boolean)));
if(!targetIds.length)return false;
let patchedAny=false;
targetIds.forEach(roomId=>{
const room=roomById(roomId);
const card=Array.from(grid.querySelectorAll('[data-room-card]')).find(el=>(el.dataset.roomCard||'')===roomId);
if(!room||!card)return;
const key=roomCardRenderKey(room);
if(gmRoomCardRenderKeys[roomId]===key){
gmStatInc('patch.rooms.card_skip');
return;
}
gmRoomCardRenderKeys[roomId]=key;
patchRoomRuntimeContainer(card,roomCard(room));
patchedAny=true;
gmStatInc('patch.rooms.card');
});
if(patchedAny)gmStatInc('render.rooms_grid');
return patchedAny;
}

function renderRoomRuntimePanel(roomId){
gmStatInc('render.runtime_attempt');
if(gmInteractionActive){
gmStatInc('render.runtime_deferred_interaction');
gmQueueDeferredRender('runtime',roomId);
return false;
}
if(currentView!=='room'||currentRoomId!==roomId||roomTab!=='control')return false;
const room=roomById(roomId);
if(!room)return false;
const panels=Array.from(document.querySelectorAll('[data-room-control-runtime]'));
const panel=panels.find(el=>(el.dataset.roomControlRuntime||'')===roomId);
if(!panel)return false;
const runtimeActionsContainer=panel.querySelector('[data-room-runtime-actions]');
if(runtimeActionsContainer)gmSyncRoomTimerDraft(roomId,runtimeActionsContainer,room);
const keys=gmRuntimeRenderKeys[roomId]&&typeof gmRuntimeRenderKeys[roomId]==='object'?gmRuntimeRenderKeys[roomId]:{};
const nextKeys={
gameStatus:roomRuntimeGameStatusKey(room),
gameActions:roomRuntimeGameActionsKey(room),
runtimeStatus:roomRuntimeStatusKey(room),
runtimeActions:roomRuntimeActionsKey(room),
adminStatus:roomRuntimeAdminStatusKey(room),
adminActions:roomRuntimeAdminActionsKey(room),
progress:roomRuntimeProgressKey(room)
};
const renderState={
gameStatusChanged:keys.gameStatus!==nextKeys.gameStatus,
gameActionsChanged:keys.gameActions!==nextKeys.gameActions,
runtimeStatusChanged:keys.runtimeStatus!==nextKeys.runtimeStatus,
runtimeActionsChanged:keys.runtimeActions!==nextKeys.runtimeActions,
adminStatusChanged:keys.adminStatus!==nextKeys.adminStatus,
adminActionsChanged:keys.adminActions!==nextKeys.adminActions,
progressFlowChanged:keys.progress!==nextKeys.progress,
progressReactionsChanged:keys.progress!==nextKeys.progress
};
if(!renderState.gameStatusChanged&&!renderState.gameActionsChanged&&!renderState.runtimeStatusChanged&&!renderState.runtimeActionsChanged&&!renderState.adminStatusChanged&&!renderState.adminActionsChanged&&!renderState.progressFlowChanged&&!renderState.progressReactionsChanged){
gmStatInc('render.runtime_skip_key');
return true;
}
gmStatInc('render.runtime');
if(renderState.gameStatusChanged)renderState.gameStatusHtml=renderRoomOperatorProfileStatus(room);
if(renderState.gameActionsChanged)renderState.gameActionsHtml=renderRoomOperatorGameActions(room);
if(renderState.runtimeStatusChanged)renderState.runtimeStatusHtml=renderRoomOperatorRuntimeStatus(room);
if(renderState.runtimeActionsChanged)renderState.runtimeActionsHtml=renderRoomOperatorRuntimeActions(room);
if(renderState.adminStatusChanged)renderState.adminStatusHtml=renderRoomScenarioControlStatus(room);
if(renderState.adminActionsChanged)renderState.adminActionsHtml=renderRoomScenarioControlActions(room);
if(renderState.progressFlowChanged||renderState.progressReactionsChanged){
const scenario=roomSelectedScenarioObject(room);
const progressData=scenarioProgressData(room,scenario);
renderState.progressData=progressData;
if(renderState.progressFlowChanged)renderState.progressFlowHtml=renderScenarioProgressFlowHtml(progressData);
if(renderState.progressReactionsChanged)renderState.progressReactionsHtml=renderScenarioProgressReactionsHtml(progressData);
}
if(renderState.gameStatusChanged)gmStatInc('patch.runtime.game_status');
if(renderState.gameActionsChanged)gmStatInc('patch.runtime.game_actions');
if(renderState.runtimeStatusChanged)gmStatInc('patch.runtime.runtime_status');
if(renderState.runtimeActionsChanged)gmStatInc('patch.runtime.runtime_actions');
if(renderState.adminStatusChanged)gmStatInc('patch.runtime.admin_status');
if(renderState.adminActionsChanged)gmStatInc('patch.runtime.admin_actions');
if(renderState.progressFlowChanged)gmStatInc('patch.runtime.progress_flow');
if(renderState.progressReactionsChanged)gmStatInc('patch.runtime.progress_reactions');
gmRuntimeRenderKeys[roomId]=nextKeys;
patchRoomRuntimeConsole(panel,room,renderState);
patchRoomRuntimeAdmin(panel,room,renderState);
const progressContainer=Array.from(document.querySelectorAll('[data-room-scenario-progress]')).find(el=>(el.dataset.roomScenarioProgress||'')===roomId);
if(progressContainer)patchRoomScenarioProgress(progressContainer,room,renderState);
return true;
}

async function loadGMRuntimeOnly(roomId,forceFullRender){
if(!roomId){
await loadGMSystemSummaryOnly(false);
return;
}
if(!gmState||!Array.isArray(gmState.rooms)){
await loadGMFullSnapshot(true,true);
return;
}
const requestSeq=(gmRuntimeRequestSeq[roomId]||0)+1;
gmRuntimeRequestSeq[roomId]=requestSeq;
const data=await api.room.runtimeJson(roomId,'detail',{include_assets:false});
if(gmRuntimeRequestSeq[roomId]!==requestSeq)return;
gmRuntimeLastRefreshAt[roomId]=Date.now();
if(!mergeRoomRuntimeState(roomId,data)){
await loadGMSystemSummaryOnly(false);
return;
}
scheduleRoomActiveScenarioDetail(roomId);
if(!forceFullRender&&shouldDeferAutoRender()){
gmQueueDeferredRender('runtime',roomId);
return;
}
if(!forceFullRender&&renderRoomRuntimePanel(roomId))return;
render();
}

let gmRuntimePollBusy=false;
let gmStatePollBusy=false;

async function pollActiveRoomRuntime(){
if(gmRuntimePollBusy)return;
if(currentView!=='room'||roomTab!=='control'||!currentRoomId||!gmState)return;
gmRuntimePollBusy=true;
try{
await loadGMRuntimeOnly(currentRoomId,false);
}
catch(err){
setGMStatus('Runtime refresh failed','gm-bad');
}
finally{
gmRuntimePollBusy=false;
}
}

async function pollGMStateSnapshot(){
if(gmStatePollBusy)return;
gmStatePollBusy=true;
try{
const versions=await loadGMVersions();
await refreshGMByVersions(versions);
}
finally{
gmStatePollBusy=false;
}
}
