// GM panel source part. Edit this file, then rebuild gm_panel.js.
async function loadObserved(){
try{
const res=await gmFetch('/api/orchestrator/control/devices');
gmObserved=res.ok?await res.json():null;
}
catch(err){
gmObserved=null;
}
}

async function loadAudit(){
try{
const res=await gmFetch('/api/orchestrator/audit/recent');
gmAudit=res.ok?await res.json():null;
}
catch(err){
gmAudit=null;
}
}

async function loadTimeline(){
try{
const res=await gmFetch('/api/orchestrator/timeline/recent');
gmTimeline=res.ok?await res.json():null;
}
catch(err){
gmTimeline=null;
}
}

async function loadQuestDevices(){
try{
const res=await gmFetch('/api/gm/devices?include_system=1');
gmQuestDevices=res.ok?await res.json():null;
}
catch(err){
gmQuestDevices=null;
}
}

function gmAudioFileItems(){
return gmAudioFiles&&Array.isArray(gmAudioFiles.items)?gmAudioFiles.items:[];
}

async function gmFetchAudioDir(path,depth,seen){
if(depth<0||seen.has(path))return [];
seen.add(path);
const res=await gmFetch(`/api/files?path=${encodeURIComponent(path)}`);
if(!res.ok)throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
const list=await res.json();
const files=[];
const dirs=[];
(Array.isArray(list)?list:[]).forEach(item=>{
if(!item||!item.path)return;
if(item.dir)dirs.push(item.path);
else files.push({
path:item.path,size:item.size||0,dur:item.dur||0}
);
}
);
if(depth>0){
for(const dir of dirs.slice(0,24)){
files.push(...await gmFetchAudioDir(dir,depth-1,seen));
}
}
return files;
}

async function loadGMAudioFiles(force){
if(!isAdmin())return;
if(gmAudioFiles.loading)return;
if(gmAudioFiles.loaded&&!force)return;
gmAudioFiles.loading=true;
gmAudioFiles.error='';
if(force)render();
try{
const items=await gmFetchAudioDir('/sdcard',3,new Set());
const dedup=new Map();
items.forEach(item=>{if(item&&item.path)dedup.set(item.path,item);});
gmAudioFiles.items=Array.from(dedup.values()).sort((a,b)=>String(a.path).localeCompare(String(b.path)));
gmAudioFiles.loaded=true;
}
catch(err){
gmAudioFiles.error=err.message||'Audio file scan failed';
gmAudioFiles.loaded=false;
}
finally{
gmAudioFiles.loading=false;
if(currentView==='scenarios')render();
}
}

function scheduleGMAudioFilesLoad(){
if(!isAdmin()||gmAudioFiles.loaded||gmAudioFiles.loading||gmAudioFiles.scheduled)return;
gmAudioFiles.scheduled=true;
setTimeout(()=>{
gmAudioFiles.scheduled=false;
loadGMAudioFiles(false);
}
,0);
}

window.__gmRefreshManualSidebar=async function(){
await loadQuestDevices();
renderRightSidebar();
};

async function loadRoomScenarios(){
gmRoomScenarios={
}
;
const rooms=(gmState&&Array.isArray(gmState.rooms))?gmState.rooms:[];
await Promise.all(rooms.map(async r=>{
try{
const res=await gmFetch(`/api/gm/room/scenarios?room_id=${encodeURIComponent(r.room_id)}`);const data=res.ok?await res.json():null;gmRoomScenarios[r.room_id]=(data&&Array.isArray(data.scenarios))?data.scenarios:[];}
catch(err){
gmRoomScenarios[r.room_id]=[];}
}
));
}

async function loadRoomProfiles(){
gmRoomProfiles={
}
;
const rooms=(gmState&&Array.isArray(gmState.rooms))?gmState.rooms:[];
await Promise.all(rooms.map(async r=>{
try{
const res=await gmFetch(`/api/gm/room/profiles?room_id=${encodeURIComponent(r.room_id)}`);const data=res.ok?await res.json():null;gmRoomProfiles[r.room_id]=data&&Array.isArray(data.profiles)?data:{
profiles:[],selected_profile_id:''}
;}
catch(err){
gmRoomProfiles[r.room_id]={
profiles:[],selected_profile_id:''}
;}
}
));
}

async function loadScenarioEditorCatalogs(){
gmScenarioEditorCatalogs={
}
;
if(!isAdmin())return;
const rooms=(gmState&&Array.isArray(gmState.rooms))?gmState.rooms:[];
await Promise.all(rooms.map(async r=>{
try{
const res=await gmFetch(`/api/gm/room/scenario-editor/catalog?room_id=${encodeURIComponent(r.room_id)}`);const data=res.ok?await res.json():null;gmScenarioEditorCatalogs[r.room_id]=data&&Array.isArray(data.quest_devices)?data:{
quest_devices:[],step_schemas:[]}
;}
catch(err){
gmScenarioEditorCatalogs[r.room_id]={
quest_devices:[],step_schemas:[]}
;}
}
));
}

async function loadGM(silent,forceRender){
if(!silent){
setStatus('loading','state-unknown');
}
try{
const res=await gmFetch('/api/gm/state');
if(!res.ok)throw new Error('HTTP '+res.status);
gmState=await res.json();
syncRoomTimerBaselines();
await Promise.all([loadObserved(),loadAudit(),loadTimeline(),loadQuestDevices(),loadRoomScenarios(),loadRoomProfiles(),loadScenarioEditorCatalogs()]);
applyInitialOperatorRoute();
if(silent&&!forceRender&&shouldDeferAutoRender()){
gmAutoRenderDeferred=true;
renderRightSidebar();
return;
}
gmAutoRenderDeferred=false;
render();
}
catch(err){
setStatus('load failed','state-fault');

document.getElementById('gm_content').innerHTML='<div class="card empty">Failed to load GM state</div>';
renderRightSidebar();
}
}

function syncRoomTimerBaselines(){
const now=performance.now();
(gmState&&Array.isArray(gmState.rooms)?gmState.rooms:[]).forEach(room=>{
room._timer_synced_at_ms=now;
});
}

function mergeRoomRuntimeState(roomId,data){
if(!gmState||!Array.isArray(gmState.rooms)||!roomId||!data)return false;
const room=gmState.rooms.find(r=>(r.room_id||'')===roomId);
if(!room)return false;
[
'session_state','timer_state','timer_duration_ms','timer_remaining_ms',
'hint_active','hint_sent_count','hint_message',
'selected_profile_id','selected_profile_name','selected_profile_scenario_id','selected_profile_duration_ms',
'selected_scenario_id','selected_scenario_name',
'running_scenario_id','running_scenario_name','running_scenario_generation',
'scenario_runtime_state','scenario_current_step_index',
'scenario_wait_type','scenario_wait_until_ms','scenario_wait_started_at_ms',
'scenario_wait_event_type','scenario_wait_source_id',
'scenario_wait_events','scenario_wait_event_count',
'scenario_wait_flags','scenario_wait_flag_count',
'scenario_wait_operator_prompt','scenario_wait_operator_label',
'scenario_wait_operator_skip_allowed','scenario_wait_operator_skip_label',
'scenario_operator_message',
'scenario_flags','scenario_flag_count',
'scenario_branches','scenario_branch_count',
'scenario_last_error',
'asset_prepare_state','asset_audio_total','asset_audio_ready',
'asset_audio_missing','asset_audio_bad','asset_audio_unsupported',
'asset_audio_io_error','asset_audio_unknown'
].forEach(key=>{
if(Object.prototype.hasOwnProperty.call(data,key))room[key]=data[key];
});
room._timer_synced_at_ms=performance.now();
return true;
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

function renderRoomRuntimePanel(roomId){
if(currentView!=='room'||currentRoomId!==roomId||roomTab!=='control')return false;
const room=roomById(roomId);
if(!room)return false;
const panels=Array.from(document.querySelectorAll('[data-room-control-runtime]'));
const panel=panels.find(el=>(el.dataset.roomControlRuntime||'')===roomId);
if(!panel)return false;
panel.innerHTML=`${renderRoomOperatorConsole(room)}${isAdmin()?renderRoomScenarioControl(room):''}`;
renderRightSidebar();
return true;
}

async function loadGMRuntimeOnly(roomId,forceFullRender){
if(!roomId||!gmState){
await loadGM(true,true);
return;
}
const res=await gmFetch(`/api/gm/room/runtime?room_id=${encodeURIComponent(roomId)}`);
if(!res.ok)throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
const data=await res.json();
if(!mergeRoomRuntimeState(roomId,data)){
await loadGM(true,true);
return;
}
if(!forceFullRender&&renderRoomRuntimePanel(roomId))return;
render();
}

async function refreshAfterRuntimeAction(roomId,forceFullRender){
clearTransientFieldDirty();
await loadGMRuntimeOnly(roomId,forceFullRender);
}

async function runManualDeviceCommand(deviceId,commandId){
if(!deviceId||!commandId)throw new Error('Manual button is incomplete');
setGMStatus('Triggering button...');
const res=await gmFetch('/api/gm/device/command/run',{
method:'POST',headers:{
'Content-Type':'application/json'}
,body:JSON.stringify({
device_id:deviceId,command_id:commandId}
)}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
clearTransientFieldDirty();
if(currentRoomId){
await loadGMRuntimeOnly(currentRoomId);
}
else{
renderRightSidebar();
}
setGMStatus('Button sent','gm-ok');
}

async function createRoomFromPrompt(){
if(!isAdmin())return;
const name=(prompt('Room name')||'').trim();
if(!name)return;
const roomId=slugifyId(name,'room');
setGMStatus('Saving room...');
const res=await gmFetch('/api/gm/room/save',{
method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({room_id:roomId,name})}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
clearTransientFieldDirty();
await loadGM(true,true);
if(typeof window.__gmRefreshManualSidebar==='function'){
await window.__gmRefreshManualSidebar();
}
setGMStatus('Room saved','gm-ok');
}

async function deleteRoom(roomId){
if(!isAdmin())return;
if(!roomId)throw new Error('Room is not selected');
const room=roomById(roomId);
const name=room&&(room.title||room.name)||roomId;
if(!confirm(`Delete room ${name}? This also removes profiles and scenarios for this room. Quest devices stay untouched.`))return;
setGMStatus('Deleting room...');
const res=await gmFetch('/api/gm/room/delete',{
method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({room_id:roomId,delete_content:true})}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
currentRoomId='';
delete currentRoomProfileId[roomId];
delete currentRoomScenarioId[roomId];
roomTab='control';
clearTransientFieldDirty();
await loadGM(true,true);
currentView='rooms';
render();
setGMStatus('Room deleted','gm-ok');
}

async function runRoomTimer(action,roomId){
let url='';
if(action==='start'){
const input=document.getElementById('gm_timer_minutes');
const minutes=Number(input&&input.value);
if(!Number.isFinite(minutes)||minutes<=0)throw new Error('Duration must be greater than 0');
const durationMs=Math.round(minutes*60000);
url=`/api/gm/room/timer/start?room_id=${encodeURIComponent(roomId)}&duration_ms=${durationMs}`;
}
else if(action==='pause'){
url=`/api/gm/room/timer/pause?room_id=${encodeURIComponent(roomId)}`;
}
else if(action==='resume'){
url=`/api/gm/room/timer/resume?room_id=${encodeURIComponent(roomId)}`;
}
else if(action==='reset'){
url=`/api/gm/room/timer/reset?room_id=${encodeURIComponent(roomId)}`;
}
else if(action==='finish'){
url=`/api/gm/room/session/finish?room_id=${encodeURIComponent(roomId)}`;
}
else if(action==='plus1'){
url=`/api/gm/room/timer/add?room_id=${encodeURIComponent(roomId)}&delta_ms=60000`;
}
else if(action==='minus1'){
url=`/api/gm/room/timer/add?room_id=${encodeURIComponent(roomId)}&delta_ms=-60000`;
}
else{
throw new Error('Unsupported timer action');
}
setGMStatus('Updating timer...');
const res=await gmFetch(url,{
method:'POST'}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
clearTransientFieldDirty();
await refreshAfterRuntimeAction(roomId,false);
setGMStatus('Timer updated','gm-ok');
}

async function runRoomHint(action,roomId){
if(action==='send'){
const input=document.getElementById('gm_hint_input');
const message=(input&&input.value||'').trim();
if(!message)throw new Error('Hint message is empty');
setGMStatus('Sending hint...');
const res=await gmFetch('/api/gm/room/hint/send',{
method:'POST',headers:{
'Content-Type':'application/json'}
,body:JSON.stringify({
room_id:roomId,message}
)}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
}
else if(action==='clear'){
setGMStatus('Clearing hint...');
const res=await gmFetch(`/api/gm/room/hint/clear?room_id=${encodeURIComponent(roomId)}`,{
method:'POST'}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
}
else{
throw new Error('Unsupported hint action');
}
clearTransientFieldDirty();
await refreshAfterRuntimeAction(roomId,true);
setGMStatus('Hint updated','gm-ok');
}

async function selectRoomProfile(roomId,profileId){
if(!roomId||!profileId)throw new Error('Game mode selection is incomplete');
setGMStatus('Selecting game mode...');
const res=await gmFetch('/api/gm/room/profile/select',{
method:'POST',headers:{
'Content-Type':'application/json'}
,body:JSON.stringify({
room_id:roomId,profile_id:profileId}
)}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
currentRoomProfileId[roomId]=profileId;
clearTransientFieldDirty();
await refreshAfterRuntimeAction(roomId,false);
setGMStatus('Game mode selected','gm-ok');
}

async function runRoomGame(action,roomId){
if(!roomId||!action)throw new Error('Game command is incomplete');
if(action==='stop'&&!confirm('Stop this game session?'))return;
if(action==='reset'&&!confirm('Reset this game session?'))return;
setGMStatus('Updating game...');
const res=await gmFetch(`/api/gm/room/game/${encodeURIComponent(action)}?room_id=${encodeURIComponent(roomId)}`,{
method:'POST'}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
clearTransientFieldDirty();
await refreshAfterRuntimeAction(roomId,false);
setGMStatus('Game updated','gm-ok');
}
