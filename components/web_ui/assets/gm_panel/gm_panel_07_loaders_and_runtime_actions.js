// GM panel source part. Edit this file, then rebuild gm_panel.js.
const GM_STATIC_TTL_MS=30000;
const gmLoadTimes={observed:0,audit:0,timeline:0,questDevices:0,roomScenarios:0,roomProfiles:0,scenarioCatalogs:0};
const gmRuntimeRenderKeys={};
const gmRuntimeRequestSeq={};
let gmLastVersions=null;
let gmSnapshotRequestSeq=0;

function gmStaticFresh(key,ttl){
return !!gmLoadTimes[key]&&performance.now()-gmLoadTimes[key]<(ttl||GM_STATIC_TTL_MS);
}

function gmMarkStaticLoaded(key){
gmLoadTimes[key]=performance.now();
}

function gmVersionsKey(versions){
if(!versions)return '';
return [
versions.rooms||0,
versions.devices||0,
versions.scenarios||0,
versions.profiles||0,
versions.ingest||0,
versions.session||0,
versions.static||0,
versions.runtime||0,
].join(':');
}

function gmVersionChanged(prev,next,fields){
if(!prev||!next)return true;
return fields.some(field=>(prev[field]||0)!==(next[field]||0));
}

function gmCurrentViewUsesQuestDeviceStatic(){
return ['dashboard','room','devices','observed','device_setup','scenarios','hardware_io'].includes(currentView);
}

function gmCurrentViewUsesScenarioStatic(){
return ['room','scenarios','profiles'].includes(currentView);
}

function gmCurrentViewUsesProfileStatic(){
return ['room','profiles'].includes(currentView);
}

async function loadGMVersions(){
return await api.gm.versionsJson();
}

async function loadObserved(force){
if(!force&&gmStaticFresh('observed'))return;
try{
const res=await api.orchestrator.controlDevices();
gmObserved=await gmJsonOrNull(res);
gmMarkStaticLoaded('observed');
}
catch(err){
gmObserved=null;
}
}

async function loadAudit(force){
if(!force&&gmStaticFresh('audit'))return;
try{
const res=await api.orchestrator.auditRecent();
gmAudit=await gmJsonOrNull(res);
gmMarkStaticLoaded('audit');
}
catch(err){
gmAudit=null;
}
}

async function loadTimeline(force){
if(!force&&gmStaticFresh('timeline'))return;
try{
const res=await api.orchestrator.timelineRecent();
gmTimeline=await gmJsonOrNull(res);
gmMarkStaticLoaded('timeline');
}
catch(err){
gmTimeline=null;
}
}

async function loadQuestDevices(force){
if(!force&&gmStaticFresh('questDevices'))return;
try{
const res=await api.device.list(true);
gmQuestDevices=await gmJsonOrNull(res);
gmMarkStaticLoaded('questDevices');
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
const res=await api.files.list(path);
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
await loadQuestDevices(true);
renderRightSidebar(true);
};

async function loadRoomScenarios(force){
if(!force&&gmStaticFresh('roomScenarios'))return;
gmRoomScenarios={
}
;
const rooms=(gmState&&Array.isArray(gmState.rooms))?gmState.rooms:[];
await Promise.all(rooms.map(async r=>{
try{
const res=await api.room.scenarios(r.room_id);const data=res.ok?await res.json():null;gmRoomScenarios[r.room_id]=(data&&Array.isArray(data.scenarios))?data.scenarios:[];}
catch(err){
gmRoomScenarios[r.room_id]=[];}
}
));
gmMarkStaticLoaded('roomScenarios');
}

async function loadRoomProfiles(force){
if(!force&&gmStaticFresh('roomProfiles'))return;
gmRoomProfiles={
}
;
const rooms=(gmState&&Array.isArray(gmState.rooms))?gmState.rooms:[];
await Promise.all(rooms.map(async r=>{
try{
const res=await api.room.profiles(r.room_id);const data=res.ok?await res.json():null;gmRoomProfiles[r.room_id]=data&&Array.isArray(data.profiles)?data:{
profiles:[],selected_profile_id:''}
;}
catch(err){
gmRoomProfiles[r.room_id]={
profiles:[],selected_profile_id:''}
;}
}
));
gmMarkStaticLoaded('roomProfiles');
}

async function loadScenarioEditorCatalogs(force){
if(!force&&gmStaticFresh('scenarioCatalogs'))return;
gmScenarioEditorCatalogs={
}
;
if(!isAdmin()){
gmMarkStaticLoaded('scenarioCatalogs');
return;
}
const rooms=(gmState&&Array.isArray(gmState.rooms))?gmState.rooms:[];
await Promise.all(rooms.map(async r=>{
try{
const res=await api.room.scenarioEditorCatalog(r.room_id);const data=res.ok?await res.json():null;gmScenarioEditorCatalogs[r.room_id]=data&&Array.isArray(data.quest_devices)?data:{
quest_devices:[],step_schemas:[]}
;}
catch(err){
gmScenarioEditorCatalogs[r.room_id]={
quest_devices:[],step_schemas:[]}
;}
}
));
gmMarkStaticLoaded('scenarioCatalogs');
}

async function loadGMLightStaticData(force){
await Promise.all([loadObserved(force),loadQuestDevices(force),loadRoomScenarios(force),loadRoomProfiles(force)]);
}

async function loadGMViewData(force){
if(currentView==='audit')await loadAudit(force);
else if(currentView==='timeline')await loadTimeline(force);
else if(currentView==='scenarios')await Promise.all([loadRoomScenarios(force),loadQuestDevices(force),loadScenarioEditorCatalogs(force)]);
else if(currentView==='profiles')await Promise.all([loadRoomProfiles(force),loadRoomScenarios(force)]);
else if(currentView==='room')await Promise.all([loadRoomProfiles(force),loadRoomScenarios(force),loadQuestDevices(force)]);
else if(currentView==='device_setup'||currentView==='devices'||currentView==='observed')await Promise.all([loadObserved(force),loadQuestDevices(force)]);
else if(currentView==='dashboard')await Promise.all([loadObserved(force),loadQuestDevices(force)]);
}

async function loadGMStaticData(force){
await loadGMLightStaticData(force);
await loadGMViewData(force);
}

async function loadGM(silent,forceRender,opts){
opts=opts||{};
const requestSeq=++gmSnapshotRequestSeq;
if(!silent){
setStatus('loading','state-unknown');
}
try{
const data=await api.gm.stateJson();
if(requestSeq!==gmSnapshotRequestSeq)return;
gmState=data;
syncRoomTimerBaselines();
loadGMVersions().then(v=>{gmLastVersions=v;}).catch(()=>{});
if(!opts.runtimeOnly){
await loadGMStaticData(!silent||!!forceRender||!!opts.forceStatic);
if(requestSeq!==gmSnapshotRequestSeq)return;
}
applyInitialOperatorRoute();
if(silent&&!forceRender&&shouldDeferAutoRender()){
gmAutoRenderDeferred=true;
renderRightSidebar(false);
return;
}
gmAutoRenderDeferred=false;
render();
}
catch(err){
setStatus('load failed','state-fault');

document.getElementById('gm_content').innerHTML='<div class="card empty">Failed to load GM state</div>';
renderRightSidebar(false);
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
ROOM_RUNTIME_FIELDS.forEach(key=>{
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

function roomRuntimeRenderKey(room){
const key={};
ROOM_RUNTIME_FIELDS.forEach(field=>{
if(ROOM_RUNTIME_CLOCK_FIELDS.has(field))return;
key[field]=room[field]===undefined?null:room[field];
});
return JSON.stringify(key);
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
panel.innerHTML=`${renderRoomOperatorConsole(room)}${isAdmin()?renderRoomScenarioControl(room):''}`;
return true;
}

async function loadGMRuntimeOnly(roomId,forceFullRender){
if(!roomId||!gmState){
await loadGM(true,true);
return;
}
const requestSeq=(gmRuntimeRequestSeq[roomId]||0)+1;
gmRuntimeRequestSeq[roomId]=requestSeq;
const data=await api.room.runtimeJson(roomId);
if(gmRuntimeRequestSeq[roomId]!==requestSeq)return;
if(!mergeRoomRuntimeState(roomId,data)){
await loadGM(true,true);
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

async function refreshGMByVersions(versions){
if(!versions)return;
const prev=gmLastVersions;
if(!prev){
gmLastVersions=versions;
return;
}
if(gmVersionsKey(versions)===gmVersionsKey(prev))return;
gmLastVersions=versions;
if(gmVersionChanged(prev,versions,['rooms'])){
await loadGM(true,true,{forceStatic:true});
return;
}
let shouldRender=false;
let shouldPatchSidebar=false;
if(gmVersionChanged(prev,versions,['devices','ingest'])){
await Promise.all([loadObserved(true),loadQuestDevices(true)]);
shouldPatchSidebar=true;
shouldRender=shouldRender||gmCurrentViewUsesQuestDeviceStatic();
}
if(gmVersionChanged(prev,versions,['scenarios'])){
await loadRoomScenarios(true);
shouldRender=shouldRender||gmCurrentViewUsesScenarioStatic();
}
if(gmVersionChanged(prev,versions,['profiles'])){
await loadRoomProfiles(true);
shouldRender=shouldRender||gmCurrentViewUsesProfileStatic();
}
if(gmVersionChanged(prev,versions,['session','runtime'])){
if(currentView==='room'&&roomTab==='control'&&currentRoomId){
await loadGMRuntimeOnly(currentRoomId,false);
}
else if(!shouldRender){
await loadGM(true,false,{runtimeOnly:true});
return;
}
}
if(shouldRender){
if(shouldDeferAutoRender()){
gmAutoRenderDeferred=true;
renderRightSidebar(shouldPatchSidebar);
}
else{
render();
}
}
else if(shouldPatchSidebar){
renderRightSidebar(true);
}
}

async function refreshAfterRuntimeAction(roomId,forceFullRender){
clearTransientFieldDirty();
await loadGMRuntimeOnly(roomId,forceFullRender);
}

async function runManualDeviceCommand(deviceId,commandId){
if(!deviceId||!commandId)throw new Error('Manual button is incomplete');
setGMStatus('Triggering button...');
const command=scenarioCommandById(deviceId,commandId);
const body={device_id:deviceId,command_id:commandId};
if(command&&command.default_args&&typeof command.default_args==='object'){
body.params=command.default_args;
}
const res=await api.device.runCommand(body.device_id,body.command_id,body.params);
await gmExpectOk(res);
setGMStatus('Button sent','gm-ok');
}

async function createRoomFromPrompt(){
if(!isAdmin())return;
const name=(prompt('Room name')||'').trim();
if(!name)return;
const roomId=slugifyId(name,'room');
setGMStatus('Saving room...');
const res=await api.room.save({room_id:roomId,name});
await gmExpectOk(res);
clearTransientFieldDirty();
await loadGM(true,true);
if(typeof window.__gmRefreshManualSidebar==='function'){
await window.__gmRefreshManualSidebar();
}
setGMStatus('Room saved','gm-ok');
}

async function deleteRoom(roomId,confirmHandled){
if(!isAdmin())return;
if(!roomId)throw new Error('Room is not selected');
const room=roomById(roomId);
const name=room&&(room.title||room.name)||roomId;
if(!confirmHandled&&!confirm(`Delete room ${name}? This also removes profiles and scenarios for this room. Quest devices stay untouched.`))return;
setGMStatus('Deleting room...');
const res=await api.room.delete({room_id:roomId,delete_content:true});
await gmExpectOk(res);
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
let res=null;
setGMStatus('Updating timer...');
if(action==='start'){
const input=document.getElementById('gm_timer_minutes');
const minutes=Number(input&&input.value);
if(!Number.isFinite(minutes)||minutes<=0)throw new Error('Duration must be greater than 0');
const durationMs=Math.round(minutes*60000);
res=await api.room.timerStart(roomId,durationMs);
}
else if(action==='pause'){
res=await api.room.timer(roomId,'pause');
}
else if(action==='resume'){
res=await api.room.timer(roomId,'resume');
}
else if(action==='reset'){
res=await api.room.timer(roomId,'reset');
}
else if(action==='finish'){
res=await api.room.sessionFinish(roomId);
}
else if(action==='plus1'){
res=await api.room.timerAdd(roomId,60000);
}
else if(action==='minus1'){
res=await api.room.timerAdd(roomId,-60000);
}
else{
throw new Error('Unsupported timer action');
}
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
const res=await api.room.hintSend({room_id:roomId,message});
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
}
else if(action==='clear'){
setGMStatus('Clearing hint...');
const res=await api.room.hintClear(roomId);
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
const res=await api.room.profileSelect({room_id:roomId,profile_id:profileId});
await gmExpectOk(res);
currentRoomProfileId[roomId]=profileId;
clearTransientFieldDirty();
await refreshAfterRuntimeAction(roomId,false);
setGMStatus('Game mode selected','gm-ok');
}

async function runRoomGame(action,roomId,confirmHandled){
if(!roomId||!action)throw new Error('Game command is incomplete');
if(!confirmHandled&&action==='stop'&&!confirm('Stop this game session?'))return;
if(!confirmHandled&&action==='reset'&&!confirm('Reset this game session?'))return;
setGMStatus('Updating game...');
const res=await api.room.game(roomId,action);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
clearTransientFieldDirty();
await refreshAfterRuntimeAction(roomId,false);
setGMStatus('Game updated','gm-ok');
}
