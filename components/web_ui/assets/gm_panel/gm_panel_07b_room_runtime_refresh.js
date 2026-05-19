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
await loadGMFullSnapshot(true,true);
return;
}
const data=await api.gm.systemSummaryJson();
if(!mergeGMSystemSummary(data)){
await loadGMFullSnapshot(true,!!forceRender);
return;
}
syncGMSummaryStatus();
if(forceRender){
render();
return;
}
if(shouldDeferAutoRender()){
gmAutoRenderDeferred=true;
renderRightSidebar(true);
return;
}
renderRightSidebar(true);
}

async function loadGMFullSnapshot(silent,forceRender,opts){
opts=opts||{};
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
gmAutoRenderDeferred=true;
renderRightSidebar(false);
}
else{
gmAutoRenderDeferred=false;
render();
}
}
await staticLoadPromise;
if(requestSeq!==gmSnapshotRequestSeq)return;
if(silent&&!forceRender&&shouldDeferAutoRender()){
gmAutoRenderDeferred=true;
renderRightSidebar(false);
return;
}
gmAutoRenderDeferred=false;
render();
}
catch(err){
if(previousState)gmState=previousState;
setStatus('load failed','state-fault');
const root=document.getElementById('gm_content');
if(previousState){
try{
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
render();
return;
}
if(shouldDeferAutoRender()){
gmAutoRenderDeferred=true;
renderRightSidebar(true);
return;
}
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

function roomRuntimeRenderKey(room){
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

function patchRoomRuntimeContainer(container,html){
if(!container)return;
const tpl=document.createElement('template');
tpl.innerHTML=html;
container.replaceChildren(tpl.content.cloneNode(true));
}

function renderRoomRuntimePanel(roomId){
if(currentView!=='room'||currentRoomId!==roomId||roomTab!=='control')return false;
const room=roomById(roomId);
if(!room)return false;
const panels=Array.from(document.querySelectorAll('[data-room-control-runtime]'));
const panel=panels.find(el=>(el.dataset.roomControlRuntime||'')===roomId);
if(!panel)return false;
const key=roomRuntimeRenderKey(room);
if(gmRuntimeRenderKeys[roomId]===key)return true;
gmRuntimeRenderKeys[roomId]=key;
patchRoomRuntimeContainer(panel.querySelector('[data-room-runtime-console]'),renderRoomOperatorConsole(room));
const adminContainer=panel.querySelector('[data-room-runtime-admin]');
if(adminContainer)patchRoomRuntimeContainer(adminContainer,isAdmin()?renderRoomScenarioControl(room):'');
const progressContainer=Array.from(document.querySelectorAll('[data-room-scenario-progress]')).find(el=>(el.dataset.roomScenarioProgress||'')===roomId);
if(progressContainer){
const scenario=roomSelectedScenarioObject(room);
patchRoomRuntimeContainer(progressContainer,`<h2 class='section-title'>Scenario progress</h2>${renderScenarioProgress(room,scenario)}`);
}
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
await ensureRoomActiveScenarioDetail(roomId);
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
