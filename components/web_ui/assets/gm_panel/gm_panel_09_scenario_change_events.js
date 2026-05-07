// GM panel source part. Edit this file, then rebuild gm_panel.js.
function gmScenarioChangeCommitDraft(draft,index){
scenarioEditor.draft=draft;
if(Number.isFinite(index))scenarioEditor.expanded_step=index;
scenarioEditor.dirty=true;
scenarioEditor.validation_report=null;
skipNextScenarioDomSync();
render();
}

function gmHandleScenarioBranchTypeChange(branchType){
const draft=collectScenarioEditor();
const branch=scenarioActiveBranch(draft);
if(branch){
branch.type=scenarioBranchTypeValue({type:branchType.value});
branch.required_for_completion=branch.type==='normal'&&branch.required_for_completion!==false;
if(branch.type==='reactive'){
branch.required_for_completion=false;
ensureReactiveV2Branch(branch);
}
}
gmScenarioChangeCommitDraft(draft);
return true;
}

function gmHandleReactiveV2Change(){
gmScenarioChangeCommitDraft(collectScenarioEditor());
return true;
}

function gmHandleScenarioStepDeviceChange(stepDevice){
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
}
gmScenarioChangeCommitDraft(draft,index);
return true;
}

function gmHandleScenarioStepCommandOrEventChange(field){
refreshScenarioStepLabel(field.closest('[data-scenario-step]'));
markScenarioDirty();
render();
return true;
}

function gmHandleScenarioStepAudioChannelChange(stepParamChannel){
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
}
gmScenarioChangeCommitDraft(draft,index);
return true;
}

function gmHandleScenarioCommandGroupChange(groupDevice,groupCommand){
const control=groupDevice||groupCommand;
const stepEl=control.closest('[data-scenario-step]');
const itemEl=control.closest('[data-command-group-item]');
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
}
else{
item.command_id=groupCommand.value||'';
item.params=defaultParamsForCommand(scenarioDeviceById(item.device_id),scenarioCommandById(item.device_id,item.command_id));
}
step.commands[itemIndex]=item;
}
gmScenarioChangeCommitDraft(draft,index);
return true;
}

function gmHandleScenarioEventGroupChange(eventGroupDevice,eventGroupEvent){
const control=eventGroupDevice||eventGroupEvent;
const stepEl=control.closest('[data-scenario-step]');
const itemEl=control.closest('[data-event-group-item]');
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
}
else{
item.event_id=eventGroupEvent.value||'';
}
step.events[itemIndex]=item;
}
gmScenarioChangeCommitDraft(draft,index);
return true;
}

function gmHandleScenarioStepTypeChange(stepType){
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
}
gmScenarioChangeCommitDraft(draft,index);
return true;
}

function gmHandleScenarioEditorChange(e){
const stepType=e.target.closest('select[data-step-field="type"]');
const stepDevice=e.target.closest('select[data-step-field="device_id"]');
const stepCommand=e.target.closest('select[data-step-field="command_id"]');
const stepDeviceEvent=e.target.closest('select[data-step-field="event_id"]');
const stepParamChannel=e.target.closest('select[data-step-param="channel"]');
const groupDevice=e.target.closest('select[data-group-command-field="device_id"]');
const groupCommand=e.target.closest('select[data-group-command-field="command_id"]');
const eventGroupDevice=e.target.closest('select[data-event-group-field="device_id"]');
const eventGroupEvent=e.target.closest('select[data-event-group-field="event_id"]');
const branchType=e.target.closest('select[data-scenario-branch-field="type"]');
const reactiveV2Field=e.target.closest('[data-v2-branch-field],[data-v2-trigger-field],[data-v2-policy-field],[data-v2-reentry-field],[data-v2-result-field],[data-v2-guard-field],[data-v2-variant-field]');
const reactiveV2ActionField=e.target.closest('[data-v2-action] [data-step-field],[data-v2-action] [data-step-param],[data-v2-action] [data-group-command-field]');
if(branchType)return gmHandleScenarioBranchTypeChange(branchType);
if(reactiveV2Field||reactiveV2ActionField)return gmHandleReactiveV2Change();
if(stepDevice)return gmHandleScenarioStepDeviceChange(stepDevice);
if(stepCommand||stepDeviceEvent)return gmHandleScenarioStepCommandOrEventChange(stepCommand||stepDeviceEvent);
if(stepParamChannel)return gmHandleScenarioStepAudioChannelChange(stepParamChannel);
if(groupDevice||groupCommand)return gmHandleScenarioCommandGroupChange(groupDevice,groupCommand);
if(eventGroupDevice||eventGroupEvent)return gmHandleScenarioEventGroupChange(eventGroupDevice,eventGroupEvent);
if(stepType)return gmHandleScenarioStepTypeChange(stepType);
return false;
}
