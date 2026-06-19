// GM panel source part. Edit this file, then rebuild gm_panel.js.
const GM_ACTIONS={
}
;

function gmRegisterAction(name,handler){
if(!name||typeof handler!=='function')return;
GM_ACTIONS[name]=handler;
}

async function gmHandleActionClick(e){
const el=e.target.closest('[data-action]');
if(!el||el.disabled)return false;
const action=el.dataset.action||'';
const handler=GM_ACTIONS[action];
if(!handler){
setStatus(`Unknown UI action: ${action}`,'state-fault');
return true;
}
try{
const confirmed=!!el.dataset.confirm;
if(confirmed&&!confirm(el.dataset.confirm))return true;
await handler(el,e,confirmed);
}
catch(err){
setStatus(err.message||'command failed','state-fault');
}
return true;
}

gmRegisterAction('manual.device.command',async (el,_e,confirmed)=>{
let params=undefined;
if(el.dataset.params){
try{
params=JSON.parse(el.dataset.params);
}
catch(err){
throw new Error('Invalid preset parameters');
}
}
await runManualDeviceCommand(el.dataset.deviceId||'',el.dataset.commandId||'',params,confirmed);
});

gmRegisterAction('room.game',async el=>{
await runRoomGame(el.dataset.op||'',el.dataset.roomId||'',true);
});

gmRegisterAction('room.timer',async el=>{
await runRoomTimer(el.dataset.op||'',el.dataset.roomId||'');
});

gmRegisterAction('room.hint',async el=>{
await runRoomHint(el.dataset.op||'',el.dataset.roomId||'');
});

gmRegisterAction('room.create',async()=>{
if(!confirmDiscardEditorChanges())return;
await createRoomFromPrompt();
});

gmRegisterAction('room.delete',async el=>{
if(!confirmDiscardEditorChanges())return;
await deleteRoom(el.dataset.roomId||'',true);
});

gmRegisterAction('admin.open',async el=>{
if(!confirmDiscardEditorChanges())return;
const roomId=el.dataset.roomId||'';
if(roomId){
profileEditor.room_id=roomId;
scenarioEditor.room_id=roomId;
}
currentView=el.dataset.view||'profiles';
if(currentView==='profiles')profileEditor.open=true;
if(currentView==='scenarios')scenarioEditor.open=true;
await loadGMViewData(false);
render();
});

gmRegisterAction('room.open',async el=>{
const nextRoomId=el.dataset.roomId||'';
if((currentView!=='room'||currentRoomId!==nextRoomId)&&!confirmDiscardEditorChanges())return;
currentRoomId=nextRoomId;
currentView='room';
roomTab='control';
roomProgressTab='flow';
await loadGMViewData(false);
await loadGMRuntimeOnly(currentRoomId,false);
});

gmRegisterAction('device.setup.open',async el=>{
if(!isAdmin())return;
if(!confirmDiscardEditorChanges())return;
currentView='device_setup';
const setupTarget=el.dataset.deviceId||'';
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
await loadGMViewData(false);
render();
});

gmRegisterAction('room.tab',async el=>{
if(!confirmDiscardEditorChanges())return;
if((el.dataset.scope||'')==='room')roomTab=el.dataset.tab||'overview';
render();
});

gmRegisterAction('room.progress.tab',async el=>{
roomProgressTab=el.dataset.tab||'flow';
if(!refreshCurrentRoomProgressTab())render();
});

gmRegisterAction('room.scenario.runtime',async el=>{
await runRoomScenarioRuntime(el.dataset.op||'',el.dataset.roomId||'',el.dataset.branchId||'',true);
});

gmRegisterAction('storage.run',async el=>{
if(!confirmDiscardEditorChanges())return;
await runStorageAction(el.dataset.op||'');
});

gmRegisterAction('audio.files.refresh',async()=>{
await loadGMAudioFiles(true);
});

gmRegisterAction('scenario.edit',async el=>{
if(!confirmDiscardScenario())return;
const scenarioId=el.dataset.scenarioId||'';
scenarioEditor.scenario_id=scenarioId;
scenarioEditor.open=true;
scenarioEditor.expanded_step=-1;
scenarioEditor.expanded_v2_action='';
scenarioEditor.active_branch=0;
clearScenarioDirty();
const original=roomScenarioDetailById(scenarioEditor.room_id,scenarioEditor.scenario_id)||null;
if(original)scenarioSetLoadedDraft(original,scenarioEditor.room_id);
render();
if(scenarioEditor.room_id&&scenarioId&&!original){
ensureRoomScenarioDetail(scenarioEditor.room_id,scenarioId).then(detail=>{
if(!detail)return;
if(!scenarioEditor.open||String(scenarioEditor.scenario_id||'')!==String(scenarioId))return;
scenarioSetLoadedDraft(detail,scenarioEditor.room_id);
render();
}).catch(err=>{
setStatus(err.message||'scenario load failed','state-fault');
});
}
});

gmRegisterAction('scenario.new',async()=>{
if(!confirmDiscardScenario())return;
scenarioEditor.scenario_id='';
scenarioEditor.open=true;
scenarioEditor.expanded_step=-1;
scenarioEditor.expanded_v2_action='';
scenarioEditor.active_branch=0;
clearScenarioDirty();
scenarioEditor.draft={id:'',name:'',room_id:scenarioEditor.room_id,branches:[defaultScenarioBranch(0,[])]};
scenarioEditor.original_scenario=null;
skipNextScenarioDomSync();
render();
});

gmRegisterAction('scenario.cancel',async()=>{
if(!confirmDiscardScenario())return;
scenarioEditor.open=false;
scenarioEditor.scenario_id='';
clearScenarioDirty();
render();
});

gmRegisterAction('scenario.delete',async el=>{
if(!confirmDiscardScenario())return;
await deleteScenarioEditor(el.dataset.scenarioId||'',true);
});

gmRegisterAction('scenario.validate',async()=>{
await validateScenarioEditor();
});

gmRegisterAction('scenario.save',async()=>{
await saveScenarioEditor();
});

gmRegisterAction('scenario.create_game_mode',async el=>{
if(!confirmDiscardEditorChanges())return;
const scenarioId=el.dataset.scenarioId||'';
const scenario=roomScenarioDetailById(scenarioEditor.room_id,scenarioId)||roomScenarioSummaryById(scenarioEditor.room_id,scenarioId)||null;
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
audio_pack_id:''};
currentView='profiles';
await loadGMViewData(false);
render();
});

gmRegisterAction('scenario.branch',async el=>{
const index=Number(el.dataset.branchIndex);
applyScenarioBranchAction(el.dataset.op||'',Number.isFinite(index)?index:0);
});

gmRegisterAction('scenario.reactive_v2',async el=>{
applyReactiveV2Action(el.dataset.op||'',el.dataset.variantIndex,el.dataset.actionIndex||el.dataset.guardIndex,el.dataset.actionType||el.dataset.triggerEventIndex||el.dataset.eventIndex||el.dataset.flagIndex||'');
});

gmRegisterAction('scenario.step.help',async el=>{
alert(scenarioStepHelpText(el.dataset.stepType||''));
});

gmRegisterAction('scenario.step',async el=>{
const v2ActionEl=el.closest('[data-v2-action]');
if(v2ActionEl){
applyReactiveV2Action(el.dataset.op||'',v2ActionEl.dataset.variantIndex,v2ActionEl.dataset.v2Action,el.dataset.commandIndex||el.dataset.eventIndex||el.dataset.flagIndex||'');
return;
}
const stepEl=el.closest('[data-scenario-step]');
const fallbackIndex=Number(stepEl&&stepEl.dataset.scenarioStep);
const index=Number.isFinite(Number(el.dataset.stepIndex))?Number(el.dataset.stepIndex):fallbackIndex;
applyScenarioStepAction(el.dataset.op||'',index,el.dataset.stepType||el.dataset.commandIndex||el.dataset.eventIndex||el.dataset.flagIndex||'');
});

gmRegisterAction('profile.edit',async el=>{
if(!confirmDiscardProfile())return;
profileEditor.profile_id=el.dataset.profileId||'';
profileEditor.open=true;
clearProfileDirty();
render();
});

gmRegisterAction('profile.new',async()=>{
if(!confirmDiscardProfile())return;
profileEditor.profile_id='';
profileEditor.open=true;
profileEditor.prefill=null;
clearProfileDirty();
render();
});

gmRegisterAction('profile.cancel',async()=>{
if(!confirmDiscardProfile())return;
profileEditor.open=false;
profileEditor.profile_id='';
clearProfileDirty();
render();
});

gmRegisterAction('profile.delete',async el=>{
if(!confirmDiscardProfile())return;
await deleteProfileEditor(el.dataset.profileId||'',true);
});

gmRegisterAction('profile.save',async()=>{
await saveProfileEditor();
});

gmRegisterAction('profile.select',async el=>{
if(!confirmDiscardProfile())return;
await selectRoomProfile(profileEditor.room_id,el.dataset.profileId||'');
});

gmRegisterAction('quest.device.edit',async el=>{
if(!confirmDiscardQuestDevice())return;
questDeviceEditor.device_id=el.dataset.deviceId||'';
questDeviceEditor.open=true;
questDeviceEditor.draft=null;
clearQuestDeviceDirty();
render();
loadQuestDevices(true).then(()=>{
if(currentView==='device_setup'&&questDeviceEditor.open)render();
}).catch(err=>{
setStatus(err.message||'device load failed','state-fault');
});
});

gmRegisterAction('quest.device.new',async()=>{
if(!confirmDiscardQuestDevice())return;
questDeviceEditor.device_id='';
questDeviceEditor.open=true;
questDeviceEditor.draft=newQuestDeviceDraft();
clearQuestDeviceDirty();
render();
});

gmRegisterAction('quest.device.cancel',async()=>{
if(!confirmDiscardQuestDevice())return;
questDeviceEditor.device_id='';
questDeviceEditor.open=false;
clearQuestDeviceDirty();
render();
});

gmRegisterAction('quest.device.discover',async()=>{
await discoverQuestDeviceInterface();
});

gmRegisterAction('quest.device.admin.load_bundle',async()=>{
await loadQuestDeviceStoredBundle();
});

gmRegisterAction('quest.device.admin.format_bundle',async()=>{
formatQuestDeviceBundleJson();
});

gmRegisterAction('quest.device.admin.validate_bundle',async()=>{
await validateQuestDeviceBundle();
});

gmRegisterAction('quest.device.admin.apply_bundle',async()=>{
await applyQuestDeviceBundle();
});

gmRegisterAction('quest.device.admin.clear_bundle',async()=>{
await clearQuestDeviceBundle();
});

gmRegisterAction('quest.device.admin.quick',async(el,_e,confirmed)=>{
await runQuestDeviceQuickAdmin(el.dataset.deviceId||'',el.dataset.commandId||'',confirmed);
});

gmRegisterAction('quest.discovery.apply',async()=>{
applyQuestDeviceDiscovery();
});

gmRegisterAction('sidebar.preset.new',async()=>{
if(!isAdmin())return;
openSidebarPresetWizard();
render();
});

gmRegisterAction('sidebar.preset.cancel',async()=>{
if(!isAdmin())return;
cancelSidebarPresetWizard();
});

gmRegisterAction('sidebar.preset.save',async()=>{
if(!isAdmin())return;
await saveSidebarPresetWizard();
});

gmRegisterAction('sidebar.preset.edit',async el=>{
if(!isAdmin())return;
editSidebarPreset(el.dataset.presetId||'');
});

gmRegisterAction('sidebar.preset.delete',async el=>{
if(!isAdmin())return;
await deleteSidebarPreset(el.dataset.presetId||'');
});

gmRegisterAction('sidebar.preset.move',async el=>{
if(!isAdmin())return;
await moveSidebarPreset(el.dataset.presetId||'',el.dataset.direction||'down');
});

gmRegisterAction('sidebar.preset.run',async el=>{
if(!isAdmin())return;
await runSidebarPreset(el.dataset.presetId||'');
});

gmRegisterAction('sidebar.preset.import_legacy',async()=>{
if(!isAdmin())return;
await importLegacySidebarPresets();
});

gmRegisterAction('quest.discovery.discard',async()=>{
discardQuestDeviceDiscovery();
});

gmRegisterAction('quest.device.save',async()=>{
await saveQuestDeviceEditor();
});

gmRegisterAction('quest.device.delete',async el=>{
if(!confirmDiscardQuestDevice())return;
await deleteQuestDeviceEditor(el.dataset.deviceId||'',true);
});

gmRegisterAction('quest.command.add',async()=>{
addQuestDeviceCommand();
});

gmRegisterAction('quest.command.delete',async el=>{
deleteQuestDeviceCommand(Number(el.dataset.index));
});

gmRegisterAction('quest.event.add',async()=>{
addQuestDeviceEvent();
});

gmRegisterAction('quest.event.delete',async el=>{
deleteQuestDeviceEvent(Number(el.dataset.index));
});
