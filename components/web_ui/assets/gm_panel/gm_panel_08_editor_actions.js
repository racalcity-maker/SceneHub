// GM panel source part. Edit this file, then rebuild gm_panel.js.
function collectQuestDeviceEditor(strict){
const root=document.querySelector('[data-quest-device-editor]');
const base=questDeviceEditor.draft?JSON.parse(JSON.stringify(questDeviceEditor.draft)):currentQuestDeviceDraft();
if(!root)return base;
const field=name=>root.querySelector(`[data-quest-device-field="${name}"]`);
const name=(field('name')&&field('name').value||'').trim();
const rawId=(field('id')&&field('id').value||'').trim();
const id=rawId||(questDeviceEditor.device_id?base.id:'')||slugifyId(name,'device');
const clientId=(field('client_id')&&field('client_id').value||'').trim();
const enabled=!!(field('enabled')&&field('enabled').checked);
const commands=[];
const baseCommands=Array.isArray(base.commands)?base.commands:[];
root.querySelectorAll('[data-quest-command]').forEach(row=>{
const get=k=>row.querySelector(`[data-quest-command-field="${k}"]`);
const label=(get('label')&&get('label').value||'').trim();
const rawCmdId=(get('id')&&get('id').value||'').trim();
const cmdId=rawCmdId||slugifyId(label,'command');
const topic=(get('topic')&&get('topic').value||'').trim();
const payload=(get('payload')&&get('payload').value||'');
const kind=(get('kind')&&get('kind').value||'mqtt_publish').trim()||'mqtt_publish';
const buttonEnabled=!!(get('button_enabled')&&get('button_enabled').checked);
const dangerous=!!(get('dangerous')&&get('dangerous').checked);
const existing=baseCommands.find(c=>(c.id||'')===(rawCmdId||cmdId))||baseCommands[Number(row.dataset.questCommand)]||{};
const paramsSchema=Array.isArray(existing.params_schema)?existing.params_schema:[];
if(label||rawCmdId||topic||payload){
commands.push({id:cmdId,label:label||cmdId,kind,topic,payload,button_enabled:buttonEnabled,dangerous,params_schema:paramsSchema});
}
});
const events=[];
root.querySelectorAll('[data-quest-event]').forEach(row=>{
const get=k=>row.querySelector(`[data-quest-event-field="${k}"]`);
const label=(get('label')&&get('label').value||'').trim();
const rawEventId=(get('id')&&get('id').value||'').trim();
const eventId=rawEventId||slugifyId(label,'event');
const topic=(get('topic')&&get('topic').value||'').trim();
const payload=(get('payload')&&get('payload').value||'');
const eventType=(get('event_type')&&get('event_type').value||eventId).trim()||eventId;
if(label||rawEventId||topic||payload){
events.push({id:eventId,label:label||eventId,topic,payload,event_type:eventType});
}
});
if(strict&&(!name||!clientId))throw new Error('Fill device name and physical client ID');
return {id,name,client_id:clientId,enabled,commands,events};
}

function normalizeQuestParamSchema(items){
return (Array.isArray(items)?items:[]).map(item=>{
const key=String(item&&item.key||'').trim();
const label=String(item&&item.label||key).trim();
const type=String(item&&item.type||'text').trim()||'text';
if(!key)return null;
return {key,label,type,optional:!!(item&&item.optional)};
}).filter(Boolean);
}

function normalizeDiscoveredCommand(item,index){
const label=String(item&&item.label||item&&item.name||item&&item.id||`Command ${index+1}`).trim();
const id=String(item&&item.id||slugifyId(label,'command')).trim();
return {
id,
label:label||id,
kind:String(item&&item.kind||'mqtt_publish').trim()||'mqtt_publish',
topic:String(item&&item.topic||'').trim(),
payload:String(item&&item.payload||''),
button_enabled:item&&item.button_enabled===false?false:true,
dangerous:!!(item&&item.dangerous),
params_schema:normalizeQuestParamSchema(item&&item.params_schema)
};
}

function normalizeDiscoveredEvent(item,index){
const label=String(item&&item.label||item&&item.name||item&&item.id||`Event ${index+1}`).trim();
const id=String(item&&item.id||slugifyId(label,'event')).trim();
return {
id,
label:label||id,
topic:String(item&&item.topic||'').trim(),
payload:String(item&&item.payload||''),
event_type:String(item&&item.event_type||id).trim()||id
};
}

function questDeviceFromDiscoveredInterface(clientId,iface){
const base=collectQuestDeviceEditor(false);
const name=(base.name||iface&&iface.name||iface&&iface.label||clientId||'Quest device').trim();
const id=(base.id||questDeviceEditor.device_id||slugifyId(name,'device')).trim();
const commands=(Array.isArray(iface&&iface.commands)?iface.commands:[]).map(normalizeDiscoveredCommand).filter(c=>c.id);
const events=(Array.isArray(iface&&iface.events)?iface.events:[]).map(normalizeDiscoveredEvent).filter(ev=>ev.id);
return {
id,
client_id:clientId,
name,
enabled:base.enabled!==false,
commands,
events
};
}

async function discoverQuestDeviceInterface(){
if(!isAdmin())throw new Error('Admin role required');
const current=collectQuestDeviceEditor(false);
const clientId=(current.client_id||'').trim();
if(!clientId)throw new Error('Select physical client');
setGMStatus('Requesting device config...');
const res=await gmFetch('/api/gm/device/describe-interface',{
method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({client_id:clientId})}
);
let body=null;
try{body=await res.json();}catch(err){}
if(!res.ok){
const msg=body&&(body.message||body.error||body.code);
throw new Error(msg||('HTTP '+res.status));
}
const iface=body&&body.quest_interface;
if(!iface||typeof iface!=='object')throw new Error('Device returned no quest_interface');
const device=questDeviceFromDiscoveredInterface(clientId,iface);
questDeviceEditor.draft=current;
questDeviceEditor.discovery={client_id:clientId,quest_interface:iface,device};
setGMStatus('Config received','gm-ok');
render();
}

function applyQuestDeviceDiscovery(){
const discovery=questDeviceEditor.discovery;
if(!discovery||!discovery.device)return;
if(!confirm('Import discovered commands and events into this device?'))return;
questDeviceEditor.draft=JSON.parse(JSON.stringify(discovery.device));
questDeviceEditor.discovery=null;
questDeviceEditor.dirty=true;
clearTransientFieldDirty();
render();
}

function discardQuestDeviceDiscovery(){
questDeviceEditor.discovery=null;
render();
}

function setQuestDeviceDraftFromEditor(){
questDeviceEditor.draft=collectQuestDeviceEditor(false);
}

function addQuestDeviceCommand(){
setQuestDeviceDraftFromEditor();
questDeviceEditor.draft.commands=Array.isArray(questDeviceEditor.draft.commands)?questDeviceEditor.draft.commands:[];
questDeviceEditor.draft.commands.push({id:'',label:'',kind:'mqtt_publish',topic:'',payload:'',button_enabled:true,dangerous:false,params_schema:[]});
markQuestDeviceDirty();
render();
}

function addQuestDeviceEvent(){
setQuestDeviceDraftFromEditor();
questDeviceEditor.draft.events=Array.isArray(questDeviceEditor.draft.events)?questDeviceEditor.draft.events:[];
questDeviceEditor.draft.events.push({id:'',label:'',topic:'',payload:'',event_type:''});
markQuestDeviceDirty();
render();
}

function deleteQuestDeviceCommand(index){
setQuestDeviceDraftFromEditor();
questDeviceEditor.draft.commands.splice(index,1);
markQuestDeviceDirty();
render();
}

function deleteQuestDeviceEvent(index){
setQuestDeviceDraftFromEditor();
questDeviceEditor.draft.events.splice(index,1);
markQuestDeviceDirty();
render();
}

async function saveQuestDeviceEditor(){
if(!isAdmin())throw new Error('Admin role required');
const device=collectQuestDeviceEditor(true);
if(!device.commands.length&&!device.events.length)throw new Error('Add at least one command or event');
setGMStatus('Saving device...');
const res=await gmFetch('/api/gm/device/save',{
method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({device})}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
questDeviceEditor.device_id=device.id;
questDeviceEditor.open=true;
clearQuestDeviceDirty();
await loadGM(true,true);
if(typeof window.__gmRefreshManualSidebar==='function')await window.__gmRefreshManualSidebar();
setGMStatus('Device saved','gm-ok');
}

async function deleteQuestDeviceEditor(deviceId){
if(!isAdmin())throw new Error('Admin role required');
if(!deviceId)return;
if(!confirm(`Delete device ${deviceId}?`))return;
setGMStatus('Deleting device...');
const res=await gmFetch('/api/gm/device/delete',{
method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({device_id:deviceId})}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
if(questDeviceEditor.device_id===deviceId){
questDeviceEditor.device_id='';
questDeviceEditor.open=false;
}
clearQuestDeviceDirty();
await loadGM(true,true);
if(typeof window.__gmRefreshManualSidebar==='function')await window.__gmRefreshManualSidebar();
setGMStatus('Device deleted','gm-ok');
}

async function saveProfileEditor(){
if(!isAdmin())throw new Error('Admin role required');
const name=(document.getElementById('profile_name')&&document.getElementById('profile_name').value||'').trim();
const rawId=(document.getElementById('profile_id')&&document.getElementById('profile_id').value||'').trim();
const id=rawId||slugifyId(name,'mode');
const scenarioId=(document.getElementById('profile_scenario')&&document.getElementById('profile_scenario').value||'').trim();
const minutes=Number(document.getElementById('profile_duration')&&document.getElementById('profile_duration').value);
const hintPack=(document.getElementById('profile_hint_pack')&&document.getElementById('profile_hint_pack').value||'').trim();
const audioPack=(document.getElementById('profile_audio_pack')&&document.getElementById('profile_audio_pack').value||'').trim();
const enabled=!!(document.getElementById('profile_enabled')&&document.getElementById('profile_enabled').checked);
if(!name||!scenarioId||!Number.isFinite(minutes)||minutes<=0)throw new Error('Fill mode name, scenario and duration');
const profile={
id,name,room_id:profileEditor.room_id,scenario_id:scenarioId,duration_ms:Math.round(minutes*60000),hint_pack_id:hintPack,audio_pack_id:audioPack,enabled}
;
setGMStatus('Saving game mode...');
const res=await gmFetch('/api/gm/room/profile/save',{
method:'POST',headers:{
'Content-Type':'application/json'}
,body:JSON.stringify({
profile}
)}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
profileEditor.profile_id=id;
profileEditor.open=true;
clearProfileDirty();
await loadGM(true,true);
setGMStatus('Game mode saved','gm-ok');
}

async function deleteProfileEditor(profileId){
if(!isAdmin())throw new Error('Admin role required');
if(!profileId)return;
if(!confirm(`Delete game mode ${profileId}?`))return;
setGMStatus('Deleting game mode...');
const res=await gmFetch('/api/gm/room/profile/delete',{
method:'POST',headers:{
'Content-Type':'application/json'}
,body:JSON.stringify({
profile_id:profileId}
)}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
if(profileEditor.profile_id===profileId){
profileEditor.profile_id='';
profileEditor.open=false;
}
clearProfileDirty();
await loadGM(true,true);
setGMStatus('Game mode deleted','gm-ok');
}

function collectScenarioForSave(){
let scenario=null;
if(document.getElementById('scenario_id')){
scenario=collectScenarioEditor();
}
else{
const box=document.getElementById('scenario_json');
try{
scenario=JSON.parse(box&&box.value||'');
}
catch(err){
throw new Error('Scenario JSON is invalid');
}
}
if(scenario&&!scenario.id&&scenario.name)scenario.id=slugifyId(scenario.name,'scenario');
if(!scenario||!scenario.name)throw new Error('Scenario name is required');
scenario.room_id=scenarioEditor.room_id;
if(!Array.isArray(scenario.branches)||!scenario.branches.length){
if(!Array.isArray(scenario.steps))scenario.steps=[];
}
else{
delete scenario.steps;
}
return scenario;
}

async function validateScenarioDraft(scenario,showStatus){
if(!isAdmin())throw new Error('Admin role required');
const draft=scenario||collectScenarioForSave();
const localReport=scenarioClientValidationReport(draft);
if(!localReport.valid){
scenarioEditor.validation_report=localReport;
scenarioEditor.draft=draft;
if(showStatus){
setGMStatus('Scenario has editor validation errors','state-fault');
render();
}
return localReport;
}
setGMStatus('Validating scenario...');
const res=await gmFetch('/api/gm/room/scenario/validate',{
method:'POST',headers:{
'Content-Type':'application/json'}
,body:JSON.stringify({
scenario:draft}
)}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
const report=await res.json();
scenarioEditor.validation_report=report;
scenarioEditor.draft=draft;
if(showStatus){
const errors=Number(report.error_count)||0;
const warnings=Number(report.warning_count)||0;
setGMStatus(errors?'Scenario validation failed':(warnings?'Scenario has warnings':'Scenario valid'),errors?'state-fault':'state-ok');
render();
}
return report;
}

async function validateScenarioEditor(){
const scenario=collectScenarioForSave();
await validateScenarioDraft(scenario,true);
}

async function saveScenarioEditor(){
if(!isAdmin())throw new Error('Admin role required');
const scenario=collectScenarioForSave();
const report=await validateScenarioDraft(scenario,false);
const errors=Number(report.error_count)||0;
const warnings=Number(report.warning_count)||0;
if(errors>0){
setGMStatus('Scenario has validation errors','state-fault');
render();
return;
}
if(warnings>0&&!confirm(`Save scenario with ${warnings} validation warning${warnings===1?'':'s'}?`)){
render();
return;
}
setGMStatus('Saving scenario...');
const res=await gmFetch('/api/gm/room/scenario/save',{
method:'POST',headers:{
'Content-Type':'application/json'}
,body:JSON.stringify({
scenario}
)}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
scenarioEditor.scenario_id=scenario.id;
scenarioEditor.open=true;
clearScenarioDirty();
await loadGM(true,true);
setGMStatus('Scenario saved','gm-ok');
}

async function deleteScenarioEditor(scenarioId){
if(!isAdmin())throw new Error('Admin role required');
if(!scenarioId)return;
if(!confirm(`Delete scenario ${scenarioId}?`))return;
setGMStatus('Deleting scenario...');
const res=await gmFetch('/api/gm/room/scenario/delete',{
method:'POST',headers:{
'Content-Type':'application/json'}
,body:JSON.stringify({
scenario_id:scenarioId}
)}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
if(scenarioEditor.scenario_id===scenarioId){
scenarioEditor.scenario_id='';
scenarioEditor.open=false;
}
clearScenarioDirty();
await loadGM(true,true);
setGMStatus('Scenario deleted','gm-ok');
}

async function importStorageJson(inputId,url,label){
const input=document.getElementById(inputId);
if(!input||!input.files||!input.files[0])throw new Error('Select JSON file');
const text=await input.files[0].text();
JSON.parse(text);
setGMStatus(`Importing ${label}...`);
const res=await gmFetch(url,{
method:'POST',headers:{
'Content-Type':'application/json'}
,body:text}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
clearTransientFieldDirty();
await loadGM(true,true);
setGMStatus(`${label} imported`,'gm-ok');
}

async function postStorageCommand(url,label){
setGMStatus(`${label}...`);
const res=await gmFetch(url,{
method:'POST'}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
clearTransientFieldDirty();
await loadGM(true,true);
setGMStatus(`${label} done`,'gm-ok');
}

async function runStorageAction(action){
if(!isAdmin())throw new Error('Admin role required');
if(action==='scenario_export'){
window.location='/api/gm/room/scenarios/export';
return;
}
if(action==='device_export'){
window.location='/api/gm/devices/export';
return;
}
if(action==='profile_export'){
window.location='/api/gm/profiles/export';
return;
}
if(action==='device_import')return importStorageJson('storage_devices_file','/api/gm/devices/import','Devices');
if(action==='scenario_import')return importStorageJson('storage_scenarios_file','/api/gm/room/scenarios/import','Scenarios');
if(action==='profile_import')return importStorageJson('storage_profiles_file','/api/gm/profiles/import','Game modes');
if(action==='device_save')return postStorageCommand('/api/gm/devices/save','Save devices');
if(action==='device_load')return postStorageCommand('/api/gm/devices/load','Load devices');
if(action==='scenario_save')return postStorageCommand('/api/gm/room/scenarios/save','Save scenarios');
if(action==='scenario_load')return postStorageCommand('/api/gm/room/scenarios/load','Load scenarios');
if(action==='profile_save')return postStorageCommand('/api/gm/profiles/save','Save game modes');
if(action==='profile_load')return postStorageCommand('/api/gm/profiles/load','Load game modes');
throw new Error('Unsupported storage action');
}

async function selectRoomScenario(roomId,scenarioId){
if(!roomId||!scenarioId)throw new Error('Scenario selection is incomplete');
setGMStatus('Selecting scenario...');
const res=await gmFetch('/api/gm/room/scenario/select',{
method:'POST',headers:{
'Content-Type':'application/json'}
,body:JSON.stringify({
room_id:roomId,scenario_id:scenarioId}
)}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
currentRoomScenarioId[roomId]=scenarioId;
clearTransientFieldDirty();
await loadGM(true,true);
setGMStatus('Scenario selected','gm-ok');
}

async function runRoomScenarioRuntime(action,roomId,branchId){
if(!roomId||!action)throw new Error('Scenario command is incomplete');
if(action==='next'&&!confirm(branchId?'Force complete this branch wait?':'Force complete current scenario wait?'))return;
setGMStatus('Updating scenario...');
let url=`/api/gm/room/scenario/${encodeURIComponent(action)}?room_id=${encodeURIComponent(roomId)}`;
if(branchId)url+=`&branch_id=${encodeURIComponent(branchId)}`;
const res=await gmFetch(url,{
method:'POST'}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
clearTransientFieldDirty();
await loadGM(true,true);
setGMStatus('Scenario updated','gm-ok');
}
