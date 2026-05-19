// GM panel source part. Edit this file, then rebuild gm_panel.js.
const GM_STATIC_TTL_MS=30000;
const gmLoadTimes={observed:0,audit:0,timeline:0,questDevices:0,sidebarPresets:0,roomScenarios:0,roomProfiles:0,scenarioCatalogs:0};
const gmRuntimeRenderKeys={};
const gmRuntimeRequestSeq={};
const gmRoomScenarioDetailRequestSeq={};
let gmRoomsRuntimeRequestSeq=0;
const gmLocalRuntimeRefreshUntil={};
const gmRuntimeLastRefreshAt={};
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
return ['room','devices','observed','device_setup','scenarios','hardware_io'].includes(currentView);
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

async function loadSidebarPresets(force){
if(!force&&gmStaticFresh('sidebarPresets')&&gmQuickPresets!==null)return;
try{
const data=await api.sidebarPresets.listJson();
applySidebarPresetPayload(data);
}
catch(err){
gmQuickPresets=[];
gmMarkStaticLoaded('sidebarPresets');
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
else files.push({path:item.path,size:item.size||0,dur:item.dur||0});
});
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
if(force&&!shouldDeferAutoRender())render();
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
if(currentView==='scenarios'){
const canRefreshAudioControls=!hasFocusedEditableControl();
if(shouldDeferAutoRender()&&!canRefreshAudioControls){
gmAutoRenderDeferred=true;
}
else{
render();
}
}
}
}

function scheduleGMAudioFilesLoad(){
if(!isAdmin()||gmAudioFiles.loaded||gmAudioFiles.loading||gmAudioFiles.scheduled)return;
gmAudioFiles.scheduled=true;
setTimeout(()=>{
gmAudioFiles.scheduled=false;
loadGMAudioFiles(false);
},0);
}

window.__gmRefreshManualSidebar=async function(){
renderRightSidebar(true);
};

async function loadRoomScenarios(force){
if(!force&&gmStaticFresh('roomScenarios'))return;
gmRoomScenarios={};
const rooms=(gmState&&Array.isArray(gmState.rooms))?gmState.rooms:[];
await Promise.all(rooms.map(async r=>loadRoomScenariosForRoom(r.room_id,true)));
gmMarkStaticLoaded('roomScenarios');
}

function normalizeRoomScenarioSelection(roomId){
if(!roomId)return;
const scenarios=scenarioSummariesByRoom(roomId);
const override=currentRoomScenarioId[roomId]||'';
if(override&&scenarios.some(scenario=>(scenario&&scenario.id||'')===override))return;
delete currentRoomScenarioId[roomId];
}

function roomScenarioHasDetail(scenario){
return !!(scenario&&(Array.isArray(scenario.branches)||Array.isArray(scenario.steps)));
}

function setRoomScenarioDetail(roomId,scenario){
if(!roomId||!scenario||!scenario.id)return;
const key=roomScenarioDetailKey(roomId,scenario.id);
gmRoomScenarioDetails[key]=scenario;
}

function invalidateRoomScenarioDetail(roomId,scenarioId){
if(!roomId||!scenarioId||!gmRoomScenarioDetails)return;
delete gmRoomScenarioDetails[roomScenarioDetailKey(roomId,scenarioId)];
}

function pruneRoomScenarioDetails(roomId,summaries){
if(!roomId||!gmRoomScenarioDetails)return;
const allowed=new Set((Array.isArray(summaries)?summaries:[]).map(item=>String(item&&item.id||'')).filter(Boolean));
Object.keys(gmRoomScenarioDetails).forEach(key=>{
if(key.indexOf(`${String(roomId)}::`)!==0)return;
const scenarioId=key.slice(String(roomId).length+2);
if(!allowed.has(scenarioId))delete gmRoomScenarioDetails[key];
});
}

async function loadRoomScenariosForRoom(roomId,force){
if(!roomId)return;
const wantsFullDetails=false;
const cached=gmRoomScenarios[roomId];
const cacheHasEnough=Array.isArray(cached)&&(!wantsFullDetails||cached.every(roomScenarioHasDetail));
if(!force&&gmStaticFresh('roomScenarios')&&cacheHasEnough)return;
try{
const res=await api.room.scenarios(roomId,wantsFullDetails?null:{detail:'summary'});
const data=res.ok?await res.json():null;
gmRoomScenarios[roomId]=(data&&Array.isArray(data.scenarios))?data.scenarios:[];
pruneRoomScenarioDetails(roomId,gmRoomScenarios[roomId]);
}
catch(err){
gmRoomScenarios[roomId]=[];
pruneRoomScenarioDetails(roomId,[]);
}
normalizeRoomScenarioSelection(roomId);
if(currentView==='room'&&currentRoomId===roomId){
await ensureRoomActiveScenarioDetail(roomId);
}
}

async function ensureRoomScenarioDetail(roomId,scenarioId,force){
if(!roomId||!scenarioId)return null;
if(force)invalidateRoomScenarioDetail(roomId,scenarioId);
const cached=roomScenarioDetailById(roomId,scenarioId);
if(cached&&roomScenarioHasDetail(cached))return cached;
const requestSeq=(gmRoomScenarioDetailRequestSeq[roomId]||0)+1;
gmRoomScenarioDetailRequestSeq[roomId]=requestSeq;
try{
const detailRes=await api.room.scenarios(roomId,{scenario_id:scenarioId,detail:'layout'});
if(gmRoomScenarioDetailRequestSeq[roomId]!==requestSeq)return null;
const detailData=detailRes.ok?await detailRes.json():null;
const detail=(detailData&&Array.isArray(detailData.scenarios)&&detailData.scenarios[0])||null;
if(detail&&detail.id){
setRoomScenarioDetail(roomId,detail);
const list=Array.isArray(gmRoomScenarios[roomId])?gmRoomScenarios[roomId].slice():[];
const idx=list.findIndex(item=>(item&&item.id||'')===detail.id);
if(idx>=0)list[idx]={...list[idx],...detail,branches:undefined,steps:undefined,variants:undefined};
else list.push({id:detail.id,name:detail.name||detail.id,room_id:detail.room_id||roomId,step_count:scenarioTotalStepCount(Array.isArray(detail.branches)?detail.branches:[]),branch_count:Array.isArray(detail.branches)?detail.branches.length:1,valid:detail.valid,validation_issue_count:detail.validation_issue_count});
gmRoomScenarios[roomId]=list;
return detail;
}
}
catch(err){
}
return null;
}

async function ensureRoomActiveScenarioDetail(roomId){
if(!roomId)return;
const activeScenarioId=roomActiveScenarioId(roomId);
if(!activeScenarioId)return;
await ensureRoomScenarioDetail(roomId,activeScenarioId);
}

async function ensureOpenScenarioEditorDetail(){
if(currentView!=='scenarios'||!scenarioEditor.open||!scenarioEditor.room_id||!scenarioEditor.scenario_id)return null;
const detail=await ensureRoomScenarioDetail(scenarioEditor.room_id,scenarioEditor.scenario_id);
if(detail&&!scenarioEditor.dirty){
scenarioSetLoadedDraft(detail,scenarioEditor.room_id);
}
return detail;
}

function roomIdForScenario(scenarioId){
const target=String(scenarioId||'');
if(!target)return '';
const roomIds=Object.keys(gmRoomScenarios||{});
for(const roomId of roomIds){
if(scenarioSummariesByRoom(roomId).some(scenario=>(scenario&&scenario.id||'')===target)){
return roomId;
}
}
return '';
}

async function refreshRoomScenariosAfterMutation(roomId){
if(!roomId){
await loadRoomScenarios(true);
if(isAdmin())await loadScenarioEditorCatalogs(true);
await ensureOpenScenarioEditorDetail();
render();
return;
}
await loadRoomScenariosForRoom(roomId,true);
if(currentView==='scenarios'){
await ensureOpenScenarioEditorDetail();
render();
return;
}
if(roomById(roomId)){
await loadGMRuntimeOnly(roomId,false);
return;
}
render();
}

async function loadRoomProfiles(force){
if(!force&&gmStaticFresh('roomProfiles'))return;
gmRoomProfiles={};
const rooms=(gmState&&Array.isArray(gmState.rooms))?gmState.rooms:[];
await Promise.all(rooms.map(async r=>loadRoomProfilesForRoom(r.room_id,true)));
gmMarkStaticLoaded('roomProfiles');
}

function normalizeRoomProfilesSelection(roomId){
if(!roomId)return;
const data=gmRoomProfiles&&gmRoomProfiles[roomId]?gmRoomProfiles[roomId]:null;
const profiles=data&&Array.isArray(data.profiles)?data.profiles:[];
const selectedId=data&&data.selected_profile_id?data.selected_profile_id:'';
if(selectedId){
currentRoomProfileId[roomId]=selectedId;
return;
}
const override=currentRoomProfileId[roomId]||'';
if(override&&profiles.some(profile=>(profile&&profile.id||'')===override))return;
delete currentRoomProfileId[roomId];
}

async function loadRoomProfilesForRoom(roomId,force){
if(!roomId)return;
if(!force&&gmStaticFresh('roomProfiles')&&gmRoomProfiles[roomId])return;
try{
const res=await api.room.profiles(roomId);
const data=res.ok?await res.json():null;
gmRoomProfiles[roomId]=data&&Array.isArray(data.profiles)?data:{profiles:[],selected_profile_id:''};
}
catch(err){
gmRoomProfiles[roomId]={profiles:[],selected_profile_id:''};
}
normalizeRoomProfilesSelection(roomId);
}

function roomIdForProfile(profileId){
const target=String(profileId||'');
if(!target)return '';
const roomIds=Object.keys(gmRoomProfiles||{});
for(const roomId of roomIds){
if(roomProfiles(roomId).some(profile=>(profile&&profile.id||'')===target)){
return roomId;
}
}
return '';
}

async function refreshRoomProfilesAfterMutation(roomId){
if(!roomId){
await loadRoomProfiles(true);
render();
return;
}
await loadRoomProfilesForRoom(roomId,true);
if(roomById(roomId)){
await loadGMRuntimeOnly(roomId,false);
return;
}
render();
}

async function loadScenarioEditorCatalogs(force){
if(!force&&gmStaticFresh('scenarioCatalogs'))return;
gmScenarioEditorCatalogs={};
if(!isAdmin()){
gmMarkStaticLoaded('scenarioCatalogs');
return;
}
const rooms=(gmState&&Array.isArray(gmState.rooms))?gmState.rooms:[];
await Promise.all(rooms.map(async r=>{
try{
const res=await api.room.scenarioEditorCatalog(r.room_id);
const data=res.ok?await res.json():null;
gmScenarioEditorCatalogs[r.room_id]=data&&Array.isArray(data.quest_devices)?data:{quest_devices:[],step_schemas:[]};
}
catch(err){
gmScenarioEditorCatalogs[r.room_id]={quest_devices:[],step_schemas:[]};
}
}));
gmMarkStaticLoaded('scenarioCatalogs');
}

async function refreshQuestDevicesAfterMutation(){
if(!gmState){
await loadGMFullSnapshot(true,true);
return;
}
if(isAdmin()){
await Promise.all([loadQuestDevices(true),loadScenarioEditorCatalogs(true)]);
}
else{
await loadQuestDevices(true);
}
render();
}

async function loadGMLightStaticData(force){
await Promise.all([loadObserved(force),loadQuestDevices(force),loadSidebarPresets(force)]);
}

async function loadGMViewData(force){
await loadSidebarPresets(force);
if(currentView==='audit')await loadAudit(force);
else if(currentView==='timeline')await loadTimeline(force);
else if(currentView==='scenarios'){
await Promise.all([loadRoomScenarios(force),loadQuestDevices(force),loadScenarioEditorCatalogs(force)]);
await ensureOpenScenarioEditorDetail();
}
else if(currentView==='profiles')await Promise.all([loadRoomProfiles(force),loadRoomScenarios(force)]);
else if(currentView==='room')await Promise.all([loadRoomProfilesForRoom(currentRoomId,force),loadRoomScenariosForRoom(currentRoomId,force),loadQuestDevices(force)]);
else if(currentView==='device_setup'||currentView==='observed')await Promise.all([loadObserved(force),loadQuestDevices(force)]);
else if(currentView==='devices')await Promise.all([loadObserved(force),loadQuestDevices(force),loadHardwareIoStatus(false)]);
}

async function loadGMStaticData(force){
await loadGMLightStaticData(force);
await loadGMViewData(force);
}
