// GM panel source part. Edit this file, then rebuild gm_panel.js.
function gmMarkEditorDirtyFromEvent(e){
markControlDirty(e.target);
const profileField=e.target.closest('#profile_id,#profile_name,#profile_duration,#profile_hint_pack,#profile_audio_pack,#profile_scenario,#profile_enabled');
const scenarioField=e.target.closest('#scenario_id,#scenario_name,[data-scenario-branch-field],[data-step-field],[data-step-param],[data-group-command-field],[data-event-group-field],[data-flag-list-field],[data-v2-branch-field],[data-v2-trigger-field],[data-v2-policy-field],[data-v2-reentry-field],[data-v2-result-field],[data-v2-guard-field],[data-v2-variant-field]');
const questDeviceField=e.target.closest('[data-quest-device-field],[data-quest-command-field],[data-quest-event-field]');
if(profileField)markProfileDirty();
if(scenarioField)markScenarioDirty();
if(questDeviceField)markQuestDeviceDirty();
}

function initGMEditorFocusAndDetails(content){
content.addEventListener('focusin',e=>{
markControlEditing(e.target);
});
content.addEventListener('focusout',e=>{
unmarkControlEditing(e.target);
if(gmAutoRenderDeferred&&!shouldDeferAutoRender())gmFlushDeferredRender();
});
document.addEventListener('toggle',e=>{
const detail=e.target;
if(!detail||String(detail.tagName||'').toLowerCase()!=='details')return;
const key=detailsKeyFor(detail);
if(key)gmOpenDetails[key]=detail.open;
}
,true);
}

function gmReleaseAsyncSelectionControl(control){
if(control&&typeof control.blur==='function'&&document.activeElement===control){
control.blur();
}
if(gmAutoRenderDeferred&&!shouldDeferAutoRender())gmFlushDeferredRender();
}

async function gmHandleProfileRoomChange(editorRoom){
if(!confirmDiscardProfile()){
render();
return;
}
profileEditor.room_id=editorRoom.value||'';
profileEditor.profile_id='';
profileEditor.open=false;
clearProfileDirty();
render();
}

async function gmHandleScenarioRoomChange(scenarioRoom){
if(!confirmDiscardScenario()){
render();
return;
}
scenarioEditor.room_id=scenarioRoom.value||'';
scenarioEditor.scenario_id='';
scenarioEditor.open=false;
scenarioEditor.expanded_step=-1;
scenarioEditor.expanded_v2_action='';
scenarioEditor.active_branch=0;
clearScenarioDirty();
skipNextScenarioDomSync();
render();
}

function gmHandleDeviceFilterChange(deviceRoom){
deviceFilterRoom=deviceRoom.value||'';
clearTransientFieldDirty();
render();
}

function gmHandleObservedFilterChange(observed){
observedFilter=observed.value||'all';
clearTransientFieldDirty();
render();
}

async function gmHandleEditorChange(e){
const control=e.target;
const editorRoom=e.target.closest('select[data-profile-room-select]');
const scenarioRoom=e.target.closest('select[data-scenario-room-select]');
const deviceRoom=e.target.closest('select[data-device-room-filter]');
const observed=e.target.closest('select[data-observed-filter]');
const scenarioField=e.target.closest('#scenario_id,#scenario_name,[data-scenario-branch-field],[data-step-field],[data-step-param],[data-group-command-field],[data-event-group-field],[data-flag-list-field],[data-v2-branch-field],[data-v2-trigger-field],[data-v2-policy-field],[data-v2-reentry-field],[data-v2-result-field],[data-v2-guard-field],[data-v2-variant-field]');
const sidebarPresetField=e.target.closest('[data-sidebar-preset-field],[data-field]');
const profile=e.target.closest('select[data-room-profile-room]');
const scenario=e.target.closest('select[data-room-scenario-room]');
try{
gmMarkEditorDirtyFromEvent(e);
if(editorRoom)return gmHandleProfileRoomChange(editorRoom);
if(scenarioRoom)return gmHandleScenarioRoomChange(scenarioRoom);
if(deviceRoom)return gmHandleDeviceFilterChange(deviceRoom);
if(observed)return gmHandleObservedFilterChange(observed);
if(handleSidebarPresetFieldChange(sidebarPresetField))return;
if(gmHandleScenarioEditorChange(e))return;
if(profile&&profile.value){
await selectRoomProfile(profile.dataset.roomProfileRoom||'',profile.value||'');
gmReleaseAsyncSelectionControl(control);
return;
}
if(scenario&&scenario.value){
await selectRoomScenario(scenario.dataset.roomScenarioRoom||'',scenario.value||'');
gmReleaseAsyncSelectionControl(control);
return;
}
if(scenarioField)return;
}
catch(err){
setStatus(err.message||'selection failed','state-fault');
}
}

function initGMEditorEventHandlers(){
const content=document.getElementById('gm_content');
if(!content)return;
initGMEditorFocusAndDetails(content);
content.oninput=e=>{
gmMarkEditorDirtyFromEvent(e);
if(gmHandleScenarioEditorInput(e))return;
if(e.target.closest('#scenario_id,#scenario_name,[data-scenario-branch-field],[data-step-field],[data-step-param],[data-group-command-field],[data-event-group-field],[data-flag-list-field],[data-v2-branch-field],[data-v2-trigger-field],[data-v2-policy-field],[data-v2-reentry-field],[data-v2-result-field],[data-v2-guard-field],[data-v2-variant-field]'))return;
handleSidebarPresetFieldInput(e.target.closest('[data-sidebar-preset-field],[data-field]'));
};
content.onchange=gmHandleEditorChange;
}
