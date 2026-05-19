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
const command=(get('command')&&get('command').value||'').trim();
const capability=(get('capability')&&get('capability').value||'').trim()||(command.split('.')[0]||'');
const defaultArgsText=(get('default_args')&&get('default_args').value||'').trim();
const defaultArgs=defaultArgsText?JSON.parse(defaultArgsText):undefined;
const timeoutMs=Math.max(1,Number(get('timeout_ms')&&get('timeout_ms').value||3000)||3000);
const manualAllowed=!!(get('manual_allowed')&&get('manual_allowed').checked);
const scenarioAllowed=!!(get('scenario_allowed')&&get('scenario_allowed').checked);
const requiresConfirmation=!!(get('requires_confirmation')&&get('requires_confirmation').checked);
const resultRequired=!!(get('result_required')&&get('result_required').checked);
const dangerLevel=(get('danger_level')&&get('danger_level').value||'normal').trim()||'normal';
const existing=baseCommands.find(c=>(c.id||'')===(rawCmdId||cmdId))||baseCommands[Number(row.dataset.questCommand)]||{};
const argsSchema=Array.isArray(existing.args_schema)?existing.args_schema:[];
if(label||rawCmdId||command){
const policy={manual_allowed:manualAllowed,scenario_allowed:scenarioAllowed,requires_confirmation:requiresConfirmation,result_required:resultRequired,timeout_ms:timeoutMs,danger_level:dangerLevel};
const cmd={id:cmdId,label:label||cmdId,capability,command,policy,args_schema:argsSchema};
if(defaultArgs)cmd.default_args=defaultArgs;
commands.push(cmd);
}
});
const events=[];
root.querySelectorAll('[data-quest-event]').forEach(row=>{
const get=k=>row.querySelector(`[data-quest-event-field="${k}"]`);
const label=(get('label')&&get('label').value||'').trim();
const rawEventId=(get('id')&&get('id').value||'').trim();
const eventId=rawEventId||slugifyId(label,'event');
const eventName=(get('event')&&get('event').value||'').trim();
const capability=(get('capability')&&get('capability').value||'').trim()||(eventName.split('.')[0]||'');
const matchText=(get('match')&&get('match').value||'').trim();
const match=matchText?JSON.parse(matchText):undefined;
if(label||rawEventId||eventName){
const event={id:eventId,label:label||eventId,capability,event:eventName};
if(match)event.match=match;
events.push(event);
}
});
if(strict&&(!name||!clientId))throw new Error('Fill device name and physical client ID');
const out={id,name,client_id:clientId,enabled,commands,events};
if(compactManifest(base))out.device_description=JSON.parse(JSON.stringify(base.device_description));
return out;
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
const command=String(item&&item.command||'').trim();
const policy=item&&item.policy&&typeof item.policy==='object'?item.policy:{};
return {
id,
label:label||id,
capability:String(item&&item.capability||command.split('.')[0]||'').trim(),
command,
default_args:item&&item.default_args&&typeof item.default_args==='object'?item.default_args:undefined,
policy:{
manual_allowed:policy.manual_allowed===false?false:true,
scenario_allowed:policy.scenario_allowed===false?false:true,
requires_confirmation:!!policy.requires_confirmation,
result_required:policy.result_required===false?false:true,
timeout_ms:Number(policy.timeout_ms)||3000,
danger_level:String(policy.danger_level||'normal')
},
args_schema:normalizeQuestParamSchema(item&&item.args_schema)
};
}

function normalizeDiscoveredEvent(item,index){
const label=String(item&&item.label||item&&item.name||item&&item.id||`Event ${index+1}`).trim();
const id=String(item&&item.id||slugifyId(label,'event')).trim();
const eventName=String(item&&item.event||'').trim()||id;
return {
id,
label:label||id,
capability:String(item&&item.capability||eventName.split('.')[0]||'').trim(),
event:eventName,
match:item&&item.match&&typeof item.match==='object'?item.match:undefined
};
}

function questDeviceFromDiscoveredInterface(clientId,iface){
const base=collectQuestDeviceEditor(false);
const manifest=iface&&Number(iface.manifest_version)===2&&iface.format==='compact_resources'&&iface.node_kind&&iface.capability_contract==='scenehub.node.compact.v1'?iface:null;
const manifestDevice=manifest&&manifest.device&&typeof manifest.device==='object'?manifest.device:{};
const name=(base.name||manifestDevice.name||iface&&iface.name||iface&&iface.label||clientId||'Quest device').trim();
const id=(base.id||questDeviceEditor.device_id||slugifyId(name,'device')).trim();
if(manifest){
return {id,client_id:clientId,name,enabled:base.enabled!==false,device_description:JSON.parse(JSON.stringify(manifest)),commands:[],events:[]};
}
const commands=(Array.isArray(iface&&iface.commands)?iface.commands:[]).map(normalizeDiscoveredCommand).filter(c=>c.id&&c.command);
const events=(Array.isArray(iface&&iface.events)?iface.events:[]).map(normalizeDiscoveredEvent).filter(ev=>ev.id&&ev.event);
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
const res=await api.device.describeInterface(clientId);
let body=null;
try{body=await res.json();}catch(err){}
if(!res.ok){
const msg=body&&(body.message||body.error||body.code);
throw new Error(msg||('HTTP '+res.status));
}
const iface=body&&body.device_description;
if(!iface||typeof iface!=='object')throw new Error('Device returned no device_description');
const device=questDeviceFromDiscoveredInterface(clientId,iface);
questDeviceEditor.draft=current;
questDeviceEditor.discovery={client_id:clientId,device_description:iface,device};
setGMStatus('Config received','gm-ok');
render();
}

function applyQuestDeviceDiscovery(){
const discovery=questDeviceEditor.discovery;
if(!discovery||!discovery.device)return;
if(!confirm(compactManifest(discovery.device)?'Import compact node interface into this device?':'Import discovered commands and events into this device?'))return;
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
questDeviceEditor.draft.commands.push({id:'',label:'',capability:'',command:'',policy:{manual_allowed:true,scenario_allowed:true,requires_confirmation:false,result_required:true,timeout_ms:3000,danger_level:'normal'},args_schema:[]});
markQuestDeviceDirty();
render();
}

function addQuestDeviceEvent(){
setQuestDeviceDraftFromEditor();
questDeviceEditor.draft.events=Array.isArray(questDeviceEditor.draft.events)?questDeviceEditor.draft.events:[];
questDeviceEditor.draft.events.push({id:'',label:'',capability:'',event:''});
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
if(!compactManifest(device)&&!device.commands.length&&!device.events.length)throw new Error('Add at least one command or event');
setGMStatus('Saving device...');
const res=await api.device.save(device);
await gmExpectOk(res);
questDeviceEditor.device_id=device.id;
questDeviceEditor.open=true;
clearQuestDeviceDirty();
await refreshQuestDevicesAfterMutation();
setGMStatus('Device saved','gm-ok');
}

async function deleteQuestDeviceEditor(deviceId,confirmHandled){
if(!isAdmin())throw new Error('Admin role required');
if(!deviceId)return;
if(!confirmHandled&&!confirm(`Delete device ${deviceId}?`))return;
setGMStatus('Deleting device...');
const res=await api.device.delete(deviceId);
await gmExpectOk(res);
if(questDeviceEditor.device_id===deviceId){
questDeviceEditor.device_id='';
questDeviceEditor.open=false;
}
clearQuestDeviceDirty();
await refreshQuestDevicesAfterMutation();
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
const roomId=profile.room_id;
setGMStatus('Saving game mode...');
const res=await api.room.profileSave({profile});
await gmExpectOk(res);
profileEditor.profile_id=id;
profileEditor.open=true;
clearProfileDirty();
await refreshRoomProfilesAfterMutation(roomId);
setGMStatus('Game mode saved','gm-ok');
}

async function deleteProfileEditor(profileId,confirmHandled){
if(!isAdmin())throw new Error('Admin role required');
if(!profileId)return;
if(!confirmHandled&&!confirm(`Delete game mode ${profileId}?`))return;
const roomId=profileEditor.room_id||roomIdForProfile(profileId);
setGMStatus('Deleting game mode...');
const res=await api.room.profileDelete(profileId);
await gmExpectOk(res);
if(profileEditor.profile_id===profileId){
profileEditor.profile_id='';
profileEditor.open=false;
}
clearProfileDirty();
await refreshRoomProfilesAfterMutation(roomId);
setGMStatus('Game mode deleted','gm-ok');
}

function collectScenarioForSave(){
let scenario=null;
if(scenarioEditor.draft&&String(scenarioEditor.draft.room_id||'')===String(scenarioEditor.room_id||'')){
scenario=scenarioClone(scenarioEditor.draft);
}
else if(document.getElementById('scenario_id')){
throw new Error('Scenario draft is not ready. Reopen the editor and try again.');
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
const existing=scenarioEditor.original_scenario&&String(scenarioEditor.original_scenario.id||'')===String(scenario.id||'')
  ? scenarioEditor.original_scenario
  : roomScenarioDetailById(scenario.room_id,scenario.id)||roomScenarioSummaryById(scenario.room_id,scenario.id)||null;
if(existing&&Array.isArray(existing.branches)&&Array.isArray(scenario.branches)&&
existing.branches.length>scenario.branches.length&&
(!scenarioEditor.branch_count_shrink_allowed||(Number(scenarioEditor.branch_count_shrink_floor)||0)>scenario.branches.length)){
throw new Error(`Refusing to save incomplete scenario: editor has ${scenario.branches.length} branches, saved scenario has ${existing.branches.length}. Refresh and try again; use Delete on a branch when you really want to remove it.`);
}
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
localReport._session_key=scenarioEditorSessionKey(draft.room_id,draft.id);
scenarioEditor.validation_report=localReport;
scenarioEditor.draft=draft;
scenarioEditor.validation_revision=Number(scenarioEditor.draft_revision)||0;
if(showStatus){
setGMStatus('Scenario has editor validation errors','state-fault');
render();
}
return localReport;
}
setGMStatus('Validating scenario...');
const res=await api.room.scenarioValidate(draft);
await gmExpectOk(res);
const report=await res.json();
report._session_key=scenarioEditorSessionKey(draft.room_id,draft.id);
scenarioEditor.validation_report=report;
scenarioEditor.draft=draft;
scenarioEditor.validation_revision=Number(scenarioEditor.draft_revision)||0;
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
const res=await api.room.scenarioSave(scenario);
await gmExpectOk(res);
const savedScenario=JSON.parse(JSON.stringify(scenario));
scenarioEditor.scenario_id=scenario.id;
scenarioEditor.open=true;
invalidateRoomScenarioDetail(scenario.room_id,scenario.id);
await refreshRoomScenariosAfterMutation(scenario.room_id);
const refreshed=await ensureRoomScenarioDetail(scenario.room_id,scenario.id,true);
scenarioSetLoadedDraft(refreshed||savedScenario,scenario.room_id);
setGMStatus('Scenario saved','gm-ok');
}

async function deleteScenarioEditor(scenarioId,confirmHandled){
if(!isAdmin())throw new Error('Admin role required');
if(!scenarioId)return;
if(!confirmHandled&&!confirm(`Delete scenario ${scenarioId}?`))return;
const roomId=scenarioEditor.room_id||roomIdForScenario(scenarioId);
setGMStatus('Deleting scenario...');
const res=await api.room.scenarioDelete(scenarioId);
await gmExpectOk(res);
if(scenarioEditor.scenario_id===scenarioId){
scenarioEditor.scenario_id='';
scenarioEditor.open=false;
}
clearScenarioDirty();
await refreshRoomScenariosAfterMutation(roomId);
setGMStatus('Scenario deleted','gm-ok');
}

async function importStorageJson(inputId,url,label){
const input=document.getElementById(inputId);
if(!input||!input.files||!input.files[0])throw new Error('Select JSON file');
const text=await input.files[0].text();
JSON.parse(text);
setGMStatus(`Importing ${label}...`);
const res=await api.storage.importJson(url,text);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
clearTransientFieldDirty();
if(url==='/api/gm/devices/import'){
await refreshQuestDevicesAfterMutation();
}
else if(url==='/api/gm/room/scenarios/import'){
await loadRoomScenarios(true);
if(isAdmin())await loadScenarioEditorCatalogs(true);
render();
}
else if(url==='/api/gm/profiles/import'){
await loadRoomProfiles(true);
render();
}
else if(url==='/api/gm/sidebar-presets/import'){
await loadSidebarPresets(true);
render();
}
else{
await loadGMFullSnapshot(true,true);
}
setGMStatus(`${label} imported`,'gm-ok');
}

async function postStorageCommand(url,label){
setGMStatus(`${label}...`);
const res=await api.storage.post(url);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
clearTransientFieldDirty();
if(url===api.storage.commandUrl('device','save')||
   url===api.storage.commandUrl('scenario','save')||
   url===api.storage.commandUrl('profile','save')){
setGMStatus(`${label} done`,'gm-ok');
return;
}
if(url===api.storage.commandUrl('device','load')){
await refreshQuestDevicesAfterMutation();
}
else if(url===api.storage.commandUrl('scenario','load')){
await loadRoomScenarios(true);
if(isAdmin())await loadScenarioEditorCatalogs(true);
render();
}
else if(url===api.storage.commandUrl('profile','load')){
await loadRoomProfiles(true);
render();
}
else if(url===api.storage.commandUrl('preset','load')){
await loadSidebarPresets(true);
render();
}
else{
await loadGMFullSnapshot(true,true);
}
setGMStatus(`${label} done`,'gm-ok');
}

async function runStorageAction(action){
if(!isAdmin())throw new Error('Admin role required');
if(action==='scenario_export'){
window.location=api.storage.exportUrl('scenario');
return;
}
if(action==='device_export'){
window.location=api.storage.exportUrl('device');
return;
}
if(action==='profile_export'){
window.location=api.storage.exportUrl('profile');
return;
}
if(action==='preset_export'){
window.location=api.storage.exportUrl('preset');
return;
}
if(action==='device_import')return importStorageJson('storage_devices_file','/api/gm/devices/import','Devices');
if(action==='scenario_import')return importStorageJson('storage_scenarios_file','/api/gm/room/scenarios/import','Scenarios');
if(action==='profile_import')return importStorageJson('storage_profiles_file','/api/gm/profiles/import','Game modes');
if(action==='preset_import')return importStorageJson('storage_presets_file','/api/gm/sidebar-presets/import','GM quick actions');
if(action==='device_save')return postStorageCommand(api.storage.commandUrl('device','save'),'Save devices');
if(action==='device_load')return postStorageCommand(api.storage.commandUrl('device','load'),'Load devices');
if(action==='scenario_save')return postStorageCommand(api.storage.commandUrl('scenario','save'),'Save scenarios');
if(action==='scenario_load')return postStorageCommand(api.storage.commandUrl('scenario','load'),'Load scenarios');
if(action==='profile_save')return postStorageCommand(api.storage.commandUrl('profile','save'),'Save game modes');
if(action==='profile_load')return postStorageCommand(api.storage.commandUrl('profile','load'),'Load game modes');
if(action==='preset_load')return postStorageCommand(api.storage.commandUrl('preset','load'),'Load GM quick actions');
throw new Error('Unsupported storage action');
}

async function selectRoomScenario(roomId,scenarioId){
if(!roomId||!scenarioId)throw new Error('Scenario selection is incomplete');
setGMStatus('Selecting scenario...');
const res=await api.room.scenarioSelect({room_id:roomId,scenario_id:scenarioId});
await gmExpectOk(res);
currentRoomScenarioId[roomId]=scenarioId;
clearTransientFieldDirty();
await refreshAfterRuntimeAction(roomId,false);
setGMStatus('Scenario selected','gm-ok');
}

async function runRoomScenarioRuntime(action,roomId,branchId,confirmHandled){
if(!roomId||!action)throw new Error('Scenario command is incomplete');
if(!confirmHandled&&action==='next'&&!confirm(branchId?'Force complete this branch wait?':'Force complete current scenario wait?'))return;
setGMStatus('Updating scenario...');
const res=await api.room.scenarioRuntime(roomId,action,branchId);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
clearTransientFieldDirty();
await refreshAfterRuntimeAction(roomId,false);
setGMStatus('Scenario updated','gm-ok');
}
