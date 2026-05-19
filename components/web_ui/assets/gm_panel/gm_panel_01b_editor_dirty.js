// GM panel source part. Edit this file, then rebuild gm_panel.js.
function isEditableControl(el){return !!(el&&el.closest&&el.closest('#gm_content')&&el.matches('input,select,textarea'));}
function dirtyLockControl(el){
if(!isEditableControl(el)||el.disabled||el.readOnly)return false;
return !!el.closest('#profile_id,#profile_name,#profile_duration,#profile_hint_pack,#profile_audio_pack,#profile_scenario,#profile_enabled,#scenario_id,#scenario_name,[data-scenario-branch-field],[data-step-field],[data-step-param],[data-group-command-field],[data-event-group-field],[data-flag-list-field],[data-v2-branch-field],[data-v2-trigger-field],[data-v2-policy-field],[data-v2-reentry-field],[data-v2-result-field],[data-v2-guard-field],[data-v2-variant-field],[data-quest-device-field],[data-quest-command-field],[data-quest-event-field],#gm_timer_minutes,#gm_hint_input,#storage_devices_file,#storage_scenarios_file,#storage_profiles_file');
}
function markControlEditing(el){if(!isEditableControl(el))return;el.classList.add('gm-field-editing');}
function unmarkControlEditing(el){if(!isEditableControl(el))return;el.classList.remove('gm-field-editing');}
function markControlDirty(el){if(!dirtyLockControl(el))return;gmInputDirty=true;el.classList.add('gm-field-dirty');markControlEditing(el);}
function clearTransientFieldDirty(){gmInputDirty=false;document.querySelectorAll('#gm_content .gm-field-dirty,#gm_content .gm-field-editing').forEach(el=>{el.classList.remove('gm-field-dirty','gm-field-editing');});}
function hasFocusedEditableControl(){const active=document.activeElement;return isEditableControl(active);}
function hasDirtyEditableControls(){return gmInputDirty||!!document.querySelector('#gm_content .gm-field-dirty');}
function shouldDeferAutoRender(){return !!(hasUnsavedEditorChanges()||hasFocusedEditableControl()||hasDirtyEditableControls());}
function hasTransientFieldChanges(){return hasDirtyEditableControls();}
function confirmDiscardTransientFields(){if(!hasTransientFieldChanges())return true;if(!confirm('Discard unsent field changes?'))return false;clearTransientFieldDirty();return true;}
function hasUnsavedEditorChanges(){return !!(profileEditor.dirty||scenarioEditor.dirty||questDeviceEditor.dirty);}
function confirmDiscardProfile(){if(!profileEditor.dirty)return true;if(!confirm('Discard unsaved game mode changes?'))return false;clearProfileDirty();return true;}
function confirmDiscardScenario(){if(!scenarioEditor.dirty)return true;if(!confirm('Discard unsaved scenario changes?'))return false;clearScenarioDirty();return true;}
function confirmDiscardQuestDevice(){if(!questDeviceEditor.dirty)return true;if(!confirm('Discard unsaved device changes?'))return false;clearQuestDeviceDirty();return true;}
function confirmDiscardEditorChanges(){if(!confirmDiscardScenario())return false;if(!confirmDiscardProfile())return false;if(!confirmDiscardQuestDevice())return false;if(!confirmDiscardTransientFields())return false;return true;}
function clearProfileDirty(){profileEditor.dirty=false;profileEditor.prefill=null;clearTransientFieldDirty();}
function clearScenarioDirty(){scenarioEditor.dirty=false;scenarioEditor.draft=null;scenarioEditor.original_scenario=null;scenarioEditor.validation_report=null;scenarioEditor.draft_revision=0;scenarioEditor.validation_revision=0;scenarioEditor.branch_count_shrink_allowed=false;scenarioEditor.branch_count_shrink_floor=0;clearTransientFieldDirty();}
function clearQuestDeviceDirty(){questDeviceEditor.dirty=false;questDeviceEditor.draft=null;questDeviceEditor.discovery=null;clearTransientFieldDirty();}
function scenarioSetLoadedDraft(scenario,roomId){
const editable=scenarioEditableJson(scenario,roomId||scenarioEditor.room_id);
scenarioEditor.original_scenario=scenarioClone(editable);
scenarioEditor.draft=scenarioClone(editable);
scenarioEditor.dirty=false;
scenarioEditor.validation_report=null;
scenarioEditor.draft_revision=0;
scenarioEditor.validation_revision=0;
scenarioEditor.branch_count_shrink_allowed=false;
scenarioEditor.branch_count_shrink_floor=0;
clearTransientFieldDirty();
return editable;
}
function markProfileDirty(){profileEditor.dirty=true;}
function markScenarioDirty(){scenarioEditor.dirty=true;scenarioEditor.validation_report=null;}
function skipNextScenarioDomSync(){gmSkipScenarioDomSync=true;}
function markQuestDeviceDirty(){questDeviceEditor.dirty=true;if(document.querySelector('[data-quest-device-editor]'))questDeviceEditor.draft=collectQuestDeviceEditor(false);}
function scenarioCommitDraft(draft){
scenarioEditor.draft=draft;
scenarioEditor.dirty=true;
scenarioEditor.validation_report=null;
scenarioEditor.draft_revision=(Number(scenarioEditor.draft_revision)||0)+1;
scenarioEditor.validation_revision=0;
}
