// GM panel source part. Edit this file, then rebuild gm_panel.js.
function setGMStatus(text,cls){
setStatus(text,cls==='gm-bad'?'state-fault':(cls==='gm-ok'?'state-ok':'state-unknown'));
}

document.getElementById('gm_nav').onclick=e=>{
const btn=e.target.closest('.nav-btn');
if(!btn)return;
const view=btn.dataset.view||'dashboard';
if(!canOpenView(view))return;
if(view!==currentView&&!confirmDiscardEditorChanges())return;
currentView=view;
render();
}
;

document.getElementById('gm_content').onclick=async e=>{
const room=e.target.closest('[data-open-room]');
const roomNew=e.target.closest('button[data-room-new]');
const roomDelete=e.target.closest('button[data-room-delete]');
const setup=e.target.closest('[data-open-device-setup]');
const adminView=e.target.closest('button[data-open-admin-view]');
const tab=e.target.closest('[data-tab-scope]');
const timer=e.target.closest('button[data-room-timer]');
const hint=e.target.closest('button[data-room-hint]');
const game=e.target.closest('button[data-room-game]');
const profileEdit=e.target.closest('button[data-profile-edit]');
const profileDelete=e.target.closest('button[data-profile-delete]');
const profileNew=e.target.closest('button[data-profile-new]');
const profileSave=e.target.closest('button[data-profile-save]');
const profileSelect=e.target.closest('button[data-profile-select]');
const scenarioEdit=e.target.closest('button[data-scenario-edit]');
const scenarioDelete=e.target.closest('button[data-scenario-delete]');
const scenarioMode=e.target.closest('button[data-scenario-mode]');
const scenarioNew=e.target.closest('button[data-scenario-new]');
const scenarioSave=e.target.closest('button[data-scenario-save]');
const scenarioValidate=e.target.closest('button[data-scenario-validate]');
const scenarioBranchAction=e.target.closest('button[data-scenario-branch-action]');
const scenarioStepAction=e.target.closest('button[data-scenario-step-action]');
const scenarioStepHelp=e.target.closest('button[data-scenario-step-help]');
const audioFilesRefresh=e.target.closest('button[data-audio-files-refresh]');
const storageAction=e.target.closest('button[data-storage-action]');
const scenarioRuntime=e.target.closest('button[data-room-scenario-runtime]');
const questDeviceEdit=e.target.closest('button[data-quest-device-edit]');
const questDeviceNew=e.target.closest('button[data-quest-device-new]');
const questDeviceSave=e.target.closest('button[data-quest-device-save]');
const questDeviceDelete=e.target.closest('button[data-quest-device-delete]');
const questDeviceDiscover=e.target.closest('button[data-quest-device-discover]');
const questDiscoveryApply=e.target.closest('button[data-quest-discovery-apply]');
const questDiscoveryDiscard=e.target.closest('button[data-quest-discovery-discard]');
const questCommandAdd=e.target.closest('button[data-quest-command-add]');
const questCommandDelete=e.target.closest('button[data-quest-command-delete]');
const questEventAdd=e.target.closest('button[data-quest-event-add]');
const questEventDelete=e.target.closest('button[data-quest-event-delete]');
try{
if(roomNew&&isAdmin()){
await createRoomFromPrompt();
return;
}
if(roomDelete&&isAdmin()){
if(!confirmDiscardEditorChanges())return;
await deleteRoom(roomDelete.dataset.roomDelete||'');
return;
}
if(room){
const nextRoomId=room.dataset.openRoom||'';
if((currentView!=='room'||currentRoomId!==nextRoomId)&&!confirmDiscardEditorChanges())return;
currentRoomId=nextRoomId;
currentView='room';
roomTab='control';
render();
return;
}
if(setup&&isAdmin()){
if(!confirmDiscardEditorChanges())return;
currentView='device_setup';
const setupTarget=setup.dataset.openDeviceSetup||'';
if(setupTarget==='new'){
questDeviceEditor.device_id='';
questDeviceEditor.open=true;
questDeviceEditor.draft=newQuestDeviceDraft();
clearQuestDeviceDirty();
}
else if(setupTarget&&setupTarget!=='1'){
questDeviceEditor.device_id=setupTarget;
questDeviceEditor.open=true;
questDeviceEditor.draft=null;
clearQuestDeviceDirty();
}
render();
return;
}
if(adminView&&isAdmin()){
if(!confirmDiscardEditorChanges())return;
const roomId=adminView.dataset.openAdminRoom||'';
if(roomId){
profileEditor.room_id=roomId;
scenarioEditor.room_id=roomId;
}
currentView=adminView.dataset.openAdminView||'profiles';
if(currentView==='profiles')profileEditor.open=true;
if(currentView==='scenarios')scenarioEditor.open=true;
render();
return;
}
if(tab){
if(!confirmDiscardEditorChanges())return;
if(tab.dataset.tabScope==='room')roomTab=tab.dataset.tab||'overview';
render();
return;
}
if(profileEdit){
if(!confirmDiscardProfile())return;
profileEditor.profile_id=profileEdit.dataset.profileEdit||'';
profileEditor.open=true;
clearProfileDirty();
render();
return;
}
if(profileNew){
if(!confirmDiscardProfile())return;
profileEditor.profile_id='';
profileEditor.open=true;
profileEditor.prefill=null;
clearProfileDirty();
render();
return;
}
if(profileDelete&&!profileDelete.disabled){
if(!confirmDiscardProfile())return;
await deleteProfileEditor(profileDelete.dataset.profileDelete||'');
return;
}
if(profileSave&&!profileSave.disabled){
await saveProfileEditor();
return;
}
if(profileSelect&&!profileSelect.disabled){
if(!confirmDiscardProfile())return;
await selectRoomProfile(profileEditor.room_id,profileSelect.dataset.profileSelect||'');
return;
}
if(scenarioEdit){
if(!confirmDiscardScenario())return;
scenarioEditor.scenario_id=scenarioEdit.dataset.scenarioEdit||'';
scenarioEditor.open=true;
scenarioEditor.expanded_step=-1;
scenarioEditor.active_branch=0;
clearScenarioDirty();
render();
return;
}
if(scenarioMode){
if(!confirmDiscardEditorChanges())return;
const scenarioId=scenarioMode.dataset.scenarioMode||'';
const scenario=roomScenarios(scenarioEditor.room_id).find(s=>s.id===scenarioId)||null;
profileEditor.room_id=scenarioEditor.room_id;
profileEditor.profile_id='';
profileEditor.open=true;
clearProfileDirty();
profileEditor.prefill={
room_id:scenarioEditor.room_id,
scenario_id:scenarioId,
name:scenario&&(scenario.name||scenario.id)||'New mode',
id:'',
duration_ms:3600000,
hint_pack_id:'',
audio_pack_id:''}
;
currentView='profiles';
render();
return;
}
if(scenarioNew){
if(!confirmDiscardScenario())return;
scenarioEditor.scenario_id='';
scenarioEditor.open=true;
scenarioEditor.expanded_step=-1;
scenarioEditor.active_branch=0;
clearScenarioDirty();
scenarioEditor.draft={id:'',name:'',room_id:scenarioEditor.room_id,branches:[defaultScenarioBranch(0,[])]};
skipNextScenarioDomSync();
render();
return;
}
if(scenarioDelete&&!scenarioDelete.disabled){
if(!confirmDiscardScenario())return;
await deleteScenarioEditor(scenarioDelete.dataset.scenarioDelete||'');
return;
}
if(scenarioValidate&&!scenarioValidate.disabled){
await validateScenarioEditor();
return;
}
if(scenarioSave&&!scenarioSave.disabled){
await saveScenarioEditor();
return;
}
if(scenarioStepHelp){
alert(scenarioStepHelpText(scenarioStepHelp.dataset.scenarioStepHelp||''));
return;
}
if(scenarioBranchAction&&!scenarioBranchAction.disabled){
const index=Number(scenarioBranchAction.dataset.branchIndex);
applyScenarioBranchAction(scenarioBranchAction.dataset.scenarioBranchAction||'',Number.isFinite(index)?index:0);
return;
}
if(scenarioStepAction&&!scenarioStepAction.disabled){
const stepEl=scenarioStepAction.closest('[data-scenario-step]');
const fallbackIndex=Number(stepEl&&stepEl.dataset.scenarioStep);
const index=Number.isFinite(Number(scenarioStepAction.dataset.stepIndex))?Number(scenarioStepAction.dataset.stepIndex):fallbackIndex;
applyScenarioStepAction(scenarioStepAction.dataset.scenarioStepAction||'',index,scenarioStepAction.dataset.scenarioStepType||scenarioStepAction.dataset.commandIndex||scenarioStepAction.dataset.eventIndex||scenarioStepAction.dataset.flagIndex||'');
return;
}
if(audioFilesRefresh&&!audioFilesRefresh.disabled){
await loadGMAudioFiles(true);
return;
}
if(storageAction&&!storageAction.disabled){
if(!confirmDiscardEditorChanges())return;
await runStorageAction(storageAction.dataset.storageAction||'');
return;
}
if(timer&&!timer.disabled){
await runRoomTimer(timer.dataset.roomTimer||'',timer.dataset.roomId||'');
return;
}
if(hint&&!hint.disabled){
await runRoomHint(hint.dataset.roomHint||'',hint.dataset.roomId||'');
return;
}
if(game&&!game.disabled){
await runRoomGame(game.dataset.roomGame||'',game.dataset.roomId||'');
return;
}
if(scenarioRuntime&&!scenarioRuntime.disabled){
await runRoomScenarioRuntime(scenarioRuntime.dataset.roomScenarioRuntime||'',scenarioRuntime.dataset.roomId||'',scenarioRuntime.dataset.roomScenarioBranch||'');
return;
}
if(questDeviceEdit){
if(!confirmDiscardQuestDevice())return;
questDeviceEditor.device_id=questDeviceEdit.dataset.questDeviceEdit||'';
questDeviceEditor.open=true;
questDeviceEditor.draft=null;
clearQuestDeviceDirty();
render();
return;
}
if(questDeviceNew){
if(!confirmDiscardQuestDevice())return;
questDeviceEditor.device_id='';
questDeviceEditor.open=true;
questDeviceEditor.draft=newQuestDeviceDraft();
clearQuestDeviceDirty();
render();
return;
}
if(questDeviceDiscover&&!questDeviceDiscover.disabled){
await discoverQuestDeviceInterface();
return;
}
if(questDiscoveryApply&&!questDiscoveryApply.disabled){
applyQuestDeviceDiscovery();
return;
}
if(questDiscoveryDiscard&&!questDiscoveryDiscard.disabled){
discardQuestDeviceDiscovery();
return;
}
if(questDeviceSave&&!questDeviceSave.disabled){
await saveQuestDeviceEditor();
return;
}
if(questDeviceDelete&&!questDeviceDelete.disabled){
if(!confirmDiscardQuestDevice())return;
await deleteQuestDeviceEditor(questDeviceDelete.dataset.questDeviceDelete||'');
return;
}
if(questCommandAdd&&!questCommandAdd.disabled){
addQuestDeviceCommand();
return;
}
if(questCommandDelete&&!questCommandDelete.disabled){
deleteQuestDeviceCommand(Number(questCommandDelete.dataset.questCommandDelete));
return;
}
if(questEventAdd&&!questEventAdd.disabled){
addQuestDeviceEvent();
return;
}
if(questEventDelete&&!questEventDelete.disabled){
deleteQuestDeviceEvent(Number(questEventDelete.dataset.questEventDelete));
return;
}
}
catch(err){
setStatus(err.message||'command failed','state-fault');
}
}
;

const gmRightSidebar=document.getElementById('gm_right_sidebar');
if(gmRightSidebar){
gmRightSidebar.onclick=async e=>{
const btn=e.target.closest('button[data-manual-device][data-manual-command]');
if(!btn||btn.disabled)return;
try{
if(btn.dataset.dangerous==='1'&&!confirm('Run this manual command?'))return;
await runManualDeviceCommand(btn.dataset.manualDevice||'',btn.dataset.manualCommand||'');
}
catch(err){
setStatus(err.message||'button failed','state-fault');
}
}
;
}

document.getElementById('gm_content').addEventListener('focusin',e=>{
markControlEditing(e.target);
});

document.getElementById('gm_content').addEventListener('focusout',e=>{
unmarkControlEditing(e.target);
});

document.addEventListener('toggle',e=>{
const detail=e.target;
if(!detail||String(detail.tagName||'').toLowerCase()!=='details')return;
const key=detailsKeyFor(detail);
if(key)gmOpenDetails[key]=detail.open;
}
,true);

document.getElementById('gm_content').oninput=e=>{
markControlDirty(e.target);
const profileField=e.target.closest('#profile_id,#profile_name,#profile_duration,#profile_hint_pack,#profile_audio_pack,#profile_scenario,#profile_enabled');
const scenarioField=e.target.closest('#scenario_id,#scenario_name,[data-scenario-branch-field],[data-step-field],[data-step-param],[data-group-command-field],[data-event-group-field],[data-flag-list-field]');
const questDeviceField=e.target.closest('[data-quest-device-field],[data-quest-command-field],[data-quest-event-field]');
if(profileField)markProfileDirty();
if(scenarioField)markScenarioDirty();
if(questDeviceField)markQuestDeviceDirty();
}
;

document.getElementById('gm_content').onchange=async e=>{
const editorRoom=e.target.closest('select[data-profile-room-select]');
const scenarioRoom=e.target.closest('select[data-scenario-room-select]');
const deviceRoom=e.target.closest('select[data-device-room-filter]');
const observed=e.target.closest('select[data-observed-filter]');
const stepType=e.target.closest('select[data-step-field="type"]');
const stepDevice=e.target.closest('select[data-step-field="device_id"]');
const stepCommand=e.target.closest('select[data-step-field="command_id"]');
const stepDeviceEvent=e.target.closest('select[data-step-field="event_id"]');
const stepParamChannel=e.target.closest('select[data-step-param="channel"]');
const groupDevice=e.target.closest('select[data-group-command-field="device_id"]');
const groupCommand=e.target.closest('select[data-group-command-field="command_id"]');
const eventGroupDevice=e.target.closest('select[data-event-group-field="device_id"]');
const eventGroupEvent=e.target.closest('select[data-event-group-field="event_id"]');
const flagSuggest=e.target.closest('select[data-scenario-flag-suggest]');
const branchType=e.target.closest('select[data-scenario-branch-field="type"]');
const profile=e.target.closest('select[data-room-profile-room]');
const scenario=e.target.closest('select[data-room-scenario-room]');
const profileField=e.target.closest('#profile_id,#profile_name,#profile_duration,#profile_hint_pack,#profile_audio_pack,#profile_scenario,#profile_enabled');
const scenarioField=e.target.closest('#scenario_id,#scenario_name,[data-scenario-branch-field],[data-step-field],[data-step-param],[data-group-command-field],[data-event-group-field],[data-flag-list-field]');
const questDeviceField=e.target.closest('[data-quest-device-field],[data-quest-command-field],[data-quest-event-field]');
try{
markControlDirty(e.target);
if(profileField)markProfileDirty();
if(scenarioField)markScenarioDirty();
if(questDeviceField)markQuestDeviceDirty();
if(editorRoom){
if(!confirmDiscardProfile()){
render();
return;
}
profileEditor.room_id=editorRoom.value||'';
profileEditor.profile_id='';
profileEditor.open=false;
clearProfileDirty();
render();
return;
}
if(scenarioRoom){
if(!confirmDiscardScenario()){
render();
return;
}
scenarioEditor.room_id=scenarioRoom.value||'';
scenarioEditor.scenario_id='';
scenarioEditor.open=false;
scenarioEditor.expanded_step=-1;
scenarioEditor.active_branch=0;
clearScenarioDirty();
skipNextScenarioDomSync();
render();
return;
}
if(deviceRoom){
deviceFilterRoom=deviceRoom.value||'';
clearTransientFieldDirty();
render();
return;
}
if(observed){
observedFilter=observed.value||'all';
clearTransientFieldDirty();
render();
return;
}
if(flagSuggest){
const wrapper=flagSuggest.closest('.flag-picker');
const input=wrapper&&wrapper.querySelector('[data-step-field="flag_name"],[data-flag-list-field="flag_name"]');
if(input&&flagSuggest.value){
input.value=flagSuggest.value;
markControlDirty(input);
refreshScenarioStepLabel(input.closest('[data-scenario-step]'));
}
markScenarioDirty();
return;
}
if(branchType){
const draft=collectScenarioEditor();
const branch=scenarioActiveBranch(draft);
if(branch){
branch.type=scenarioBranchTypeValue({type:branchType.value});
branch.required_for_completion=branch.type==='normal'&&branch.required_for_completion!==false;
if(branch.type==='reactive')branch.required_for_completion=false;
}
scenarioEditor.draft=draft;
scenarioEditor.dirty=true;
scenarioEditor.validation_report=null;
skipNextScenarioDomSync();
render();
return;
}
if(stepDevice){
const stepEl=stepDevice.closest('[data-scenario-step]');
const index=Number(stepEl&&stepEl.dataset.scenarioStep);
const draft=collectScenarioEditor();
const steps=scenarioActiveSteps(draft);
if(Number.isFinite(index)&&steps[index]){
const step=steps[index];
const type=scenarioStepTypeValue(step);
const device=scenarioDeviceById(stepDevice.value||'');
step.device_id=stepDevice.value||'';
if(type==='DEVICE_COMMAND'){
step.command_id=scenarioValidCommandId(device,'');
step.params=defaultParamsForCommand(device,scenarioCommandById(step.device_id,step.command_id));
}
else if(type==='WAIT_DEVICE_EVENT'){
step.event_id=scenarioValidEventId(device,'');
}
scenarioEditor.draft=draft;
scenarioEditor.expanded_step=index;
skipNextScenarioDomSync();
}
scenarioEditor.dirty=true;
scenarioEditor.validation_report=null;
render();
return;
}
if(stepCommand||stepDeviceEvent){
refreshScenarioStepLabel((stepCommand||stepDeviceEvent).closest('[data-scenario-step]'));
markScenarioDirty();
render();
return;
}
if(stepParamChannel){
const stepEl=stepParamChannel.closest('[data-scenario-step]');
const index=Number(stepEl&&stepEl.dataset.scenarioStep);
const draft=collectScenarioEditor();
const steps=scenarioActiveSteps(draft);
if(Number.isFinite(index)&&steps[index]){
const params=steps[index].params&&typeof steps[index].params==='object'?steps[index].params:{};
params.channel=stepParamChannel.value||'effect';
if(params.channel==='background'&&params.file&&!/\.wav$/i.test(String(params.file))){
delete params.file;
}
if(params.channel!=='background'){
params.repeat=false;
}
steps[index].params=params;
scenarioEditor.draft=draft;
scenarioEditor.expanded_step=index;
skipNextScenarioDomSync();
}
markScenarioDirty();
render();
return;
}
if(groupDevice||groupCommand){
const stepEl=(groupDevice||groupCommand).closest('[data-scenario-step]');
const itemEl=(groupDevice||groupCommand).closest('[data-command-group-item]');
const index=Number(stepEl&&stepEl.dataset.scenarioStep);
const itemIndex=Number(itemEl&&itemEl.dataset.commandGroupItem);
const draft=collectScenarioEditor();
const steps=scenarioActiveSteps(draft);
if(Number.isFinite(index)&&Number.isFinite(itemIndex)&&steps[index]){
const step=steps[index];
step.commands=Array.isArray(step.commands)?step.commands:[];
const item=step.commands[itemIndex]||defaultScenarioCommandItem();
if(groupDevice){
const device=scenarioDeviceById(groupDevice.value||'');
item.device_id=groupDevice.value||'';
item.command_id=scenarioValidCommandId(device,'');
item.params=defaultParamsForCommand(device,scenarioCommandById(item.device_id,item.command_id));
}else{
item.command_id=groupCommand.value||'';
item.params=defaultParamsForCommand(scenarioDeviceById(item.device_id),scenarioCommandById(item.device_id,item.command_id));
}
step.commands[itemIndex]=item;
scenarioEditor.draft=draft;
scenarioEditor.expanded_step=index;
skipNextScenarioDomSync();
}
markScenarioDirty();
render();
return;
}
if(eventGroupDevice||eventGroupEvent){
const stepEl=(eventGroupDevice||eventGroupEvent).closest('[data-scenario-step]');
const itemEl=(eventGroupDevice||eventGroupEvent).closest('[data-event-group-item]');
const index=Number(stepEl&&stepEl.dataset.scenarioStep);
const itemIndex=Number(itemEl&&itemEl.dataset.eventGroupItem);
const draft=collectScenarioEditor();
const steps=scenarioActiveSteps(draft);
if(Number.isFinite(index)&&Number.isFinite(itemIndex)&&steps[index]){
const step=steps[index];
step.events=Array.isArray(step.events)?step.events:[];
const item=step.events[itemIndex]||defaultScenarioEventItem();
if(eventGroupDevice){
const device=scenarioDeviceById(eventGroupDevice.value||'');
item.device_id=eventGroupDevice.value||'';
item.event_id=scenarioValidEventId(device,'');
}else{
item.event_id=eventGroupEvent.value||'';
}
step.events[itemIndex]=item;
scenarioEditor.draft=draft;
scenarioEditor.expanded_step=index;
skipNextScenarioDomSync();
}
markScenarioDirty();
render();
return;
}
if(stepType){
const stepEl=stepType.closest('[data-scenario-step]');
const index=Number(stepEl&&stepEl.dataset.scenarioStep);
const draft=collectScenarioEditor();
const steps=scenarioActiveSteps(draft);
if(Number.isFinite(index)&&steps[index]){
const previous=steps[index];
const replacement=newScenarioStepForType(index,stepType.value||'WAIT_TIME');
replacement.id=previous.id||replacement.id;
replacement.enabled=previous.enabled!==false;
steps[index]=replacement;
scenarioEditor.draft=draft;
scenarioEditor.expanded_step=index;
}
markScenarioDirty();
skipNextScenarioDomSync();
render();
return;
}
if(profile&&profile.value){
await selectRoomProfile(profile.dataset.roomProfileRoom||'',profile.value||'');
return;
}
if(scenario&&scenario.value){
await selectRoomScenario(scenario.dataset.roomScenarioRoom||'',scenario.value||'');
return;
}
}
catch(err){
setStatus(err.message||'selection failed','state-fault');
}
}
;

document.getElementById('gm_refresh').onclick=()=>{
if(!confirmDiscardEditorChanges())return;
clearProfileDirty();
clearScenarioDirty();
clearQuestDeviceDirty();
clearTransientFieldDirty();
loadGM();
}
;

document.getElementById('gm_logout').onclick=()=>{
if(!confirmDiscardEditorChanges())return;
clearProfileDirty();
clearScenarioDirty();
clearQuestDeviceDirty();
clearTransientFieldDirty();
fetch('/api/auth/logout',{
method:'POST'}
).finally(()=>window.location='/login');
}
;

const gmAdminHome=document.getElementById('gm_admin_home');
if(gmAdminHome){
gmAdminHome.onclick=()=>{
clearProfileDirty();
clearScenarioDirty();
clearQuestDeviceDirty();
clearTransientFieldDirty();
}
;
}

window.addEventListener('beforeunload',e=>{
if(!hasUnsavedEditorChanges())return;e.preventDefault();e.returnValue='';}
);

window.__sessionRolePromise=loadGMSession();

window.__sessionRolePromise.then(()=>loadGM());

setInterval(()=>loadGM(true),3000);
