// GM panel source part. Edit this file, then rebuild gm_panel.js.
function gmScenarioChangeCommitDraft(draft,index,deferRender){
scenarioCommitDraft(draft);
if(Number.isFinite(index))scenarioEditor.expanded_step=index;
skipNextScenarioDomSync();
if(!deferRender)render();
}

function gmHandleScenarioBranchTypeChange(branchType){
const draft=scenarioWorkingDraft();
if(!draft)return false;
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

function reactiveV2DraftContext(control){
const draft=scenarioWorkingDraft();
if(!draft)return null;
if(!Array.isArray(draft.branches)||!draft.branches.length)draft.branches=normalizeScenarioBranches(draft);
const branchIndex=scenarioActiveBranchIndex(draft);
const branch=draft.branches[branchIndex];
if(!branch||!scenarioIsReactiveV2Branch(branch))return null;
ensureReactiveV2Branch(branch);
return {draft,branch,branchIndex,control};
}

function reactiveV2DraftActionContext(control){
const ctx=reactiveV2DraftContext(control);
if(!ctx)return null;
return reactiveV2DraftActionContextFromCtx(ctx,control);
}

function reactiveV2DraftActionContextFromCtx(ctx,control){
const actionEl=control&&control.closest&&control.closest('[data-v2-action]');
if(!actionEl)return null;
const variantIndex=Number(actionEl.dataset.variantIndex);
const actionIndex=Number(actionEl.dataset.v2Action);
if(!Number.isFinite(variantIndex)||!Number.isFinite(actionIndex))return null;
const variant=Array.isArray(ctx.branch.variants)?ctx.branch.variants[variantIndex]:null;
if(!variant)return null;
variant.actions=Array.isArray(variant.actions)?variant.actions:[];
const action=variant.actions[actionIndex];
if(!action)return null;
return {...ctx,variant,variantIndex,action,actionIndex};
}

function collectReactiveV2ActionFromDom(actionEl,baseAction,actionIndex){
if(!actionEl)return baseAction;
const typeField=actionEl.querySelector('select[data-step-field="type"]');
const type=scenarioStepTypeValue({type:typeField&&typeField.value||baseAction&&baseAction.type||'WAIT_TIME'});
const seed=newScenarioStepForType(Number.isFinite(actionIndex)?actionIndex:0,type);
const action={...seed,...scenarioClone(baseAction||{})};
action.type=type;
action.id=(baseAction&&baseAction.id)||action.id;
action.enabled=baseAction&&baseAction.enabled===false?false:true;
const labelField=actionEl.querySelector('[data-step-field="label"]');
if(labelField)action.label=labelField.value||'';
if(type==='DEVICE_COMMAND'){
const deviceField=actionEl.querySelector('[data-step-field="device_id"]');
const commandField=actionEl.querySelector('[data-step-field="command_id"]');
	const deviceId=deviceField?String(deviceField.value||''):'';
	const device=scenarioDeviceById(deviceId);
	const commandId=scenarioValidCommandId(device,commandField?String(commandField.value||''):'');
	const command=scenarioCommandById(deviceId,commandId);
	const params={...(defaultParamsForCommand(device,command)||{})};
	actionEl.querySelectorAll('[data-step-param]').forEach(field=>{
		const key=String(field.dataset.stepParam||'').trim();
		if(!key)return;
		const typeAttr=(field.getAttribute('type')||'').toLowerCase();
		const paramType=field.dataset.stepParamType||'';
		if(field.type==='checkbox')params[key]=!!field.checked;
		else if(typeAttr==='number'||paramType==='number')params[key]=Number(field.value)||0;
		else params[key]=field.value;
	});
	action.device_id=deviceId;
	action.command_id=commandId;
	action.params=scenarioHydrateCommandParams(deviceId,commandId,params,device,command);
}
else if(type==='DEVICE_COMMAND_GROUP'){
	action.commands=[];
	actionEl.querySelectorAll('[data-command-group-item]').forEach(itemEl=>{
		const deviceField=itemEl.querySelector('[data-group-command-field="device_id"]');
		const commandField=itemEl.querySelector('[data-group-command-field="command_id"]');
		const deviceId=deviceField?String(deviceField.value||''):'';
		const device=scenarioDeviceById(deviceId);
		const commandId=scenarioValidCommandId(device,commandField?String(commandField.value||''):'');
		const command=scenarioCommandById(deviceId,commandId);
		const params={...(defaultParamsForCommand(device,command)||{})};
		itemEl.querySelectorAll('[data-step-param]').forEach(field=>{
			const key=String(field.dataset.stepParam||'').trim();
			if(!key)return;
			const typeAttr=(field.getAttribute('type')||'').toLowerCase();
			const paramType=field.dataset.stepParamType||'';
			if(field.type==='checkbox')params[key]=!!field.checked;
			else if(typeAttr==='number'||paramType==='number')params[key]=Number(field.value)||0;
			else params[key]=field.value;
		});
		action.commands.push({device_id:deviceId,command_id:commandId,params:scenarioHydrateCommandParams(deviceId,commandId,params,device,command)});
	});
	if(!action.commands.length)action.commands=[defaultScenarioCommandItem()];
}
else if(type==='WAIT_TIME'){
	const field=actionEl.querySelector('[data-step-field="duration_ms"]');
	action.duration_ms=field?durationSecondsToMs(field.value||1):Number(action.duration_ms)||1000;
}
else if(type==='SHOW_OPERATOR_MESSAGE'){
	const field=actionEl.querySelector('[data-step-field="message"]');
	action.message=field?field.value||'':String(action.message||'');
}
else if(type==='SET_FLAG'){
	const flagField=actionEl.querySelector('[data-step-field="flag_name"]');
	const valueField=actionEl.querySelector('[data-step-field="value"]');
	action.flag_name=flagField?flagField.value||'':String(action.flag_name||'');
	if(valueField)action.value=valueField.type==='checkbox'?!!valueField.checked:String(valueField.value)!=='false';
}
return normalizeScenarioEditorStep(action);
}

function gmHandleReactiveV2TriggerField(field,ctx){
const key=field.dataset.v2TriggerField||'';
ctx.branch.trigger=ctx.branch.trigger&&typeof ctx.branch.trigger==='object'?ctx.branch.trigger:defaultReactiveV2Trigger();
if(key==='kind'){
const kind=field.value||'device_event';
ctx.branch.trigger={kind};
if(kind==='device_event'){
ctx.branch.trigger.device_id='';
ctx.branch.trigger.event_id='';
}
}
else if(key==='device_id'){
ctx.branch.trigger.device_id=field.value||'';
const device=scenarioDeviceById(ctx.branch.trigger.device_id);
ctx.branch.trigger.event_id=scenarioValidEventId(device,'');
}
else if(key==='event_id'){
ctx.branch.trigger.event_id=field.value||'';
if(ctx.branch.trigger.kind==='operator_event')ctx.branch.trigger.operator_event=ctx.branch.trigger.event_id;
else if(ctx.branch.trigger.kind==='runtime_event')ctx.branch.trigger.runtime_event=ctx.branch.trigger.event_id;
}
else if(key==='flag_name')ctx.branch.trigger.flag_name=field.value||'';
else if(key==='operator_event'){
ctx.branch.trigger.operator_event=field.value||'';
ctx.branch.trigger.event_id=ctx.branch.trigger.operator_event;
}
else if(key==='runtime_event'){
ctx.branch.trigger.runtime_event=field.value||'';
ctx.branch.trigger.event_id=ctx.branch.trigger.runtime_event;
}
else return false;
return true;
}

function gmHandleReactiveV2PolicyField(field,ctx){
const key=field.dataset.v2PolicyField||'';
ctx.branch.policy=ctx.branch.policy&&typeof ctx.branch.policy==='object'?ctx.branch.policy:{};
if(key==='mode'){
ctx.branch.policy.mode=field.value||'single';
normalizeReactiveV2RepeatPolicy(ctx.branch);
}
else if(key==='max_fire_count'){
ctx.branch.policy.max_fire_count=Math.max(0,Math.round(Number(field.value)||0));
ctx.branch.max_fire_count=ctx.branch.policy.max_fire_count;
}
else return false;
return true;
}

function gmHandleReactiveV2ReentryField(field,ctx){
const key=field.dataset.v2ReentryField||'';
ctx.branch.reentry=ctx.branch.reentry&&typeof ctx.branch.reentry==='object'?ctx.branch.reentry:{};
if(key!=='mode')return false;
ctx.branch.reentry.mode=field.value||'ignore';
return true;
}

function gmHandleReactiveV2ResultField(field,ctx){
const key=field.dataset.v2ResultField||'';
ctx.branch.result_policy=ctx.branch.result_policy&&typeof ctx.branch.result_policy==='object'?ctx.branch.result_policy:{};
if(key==='on_done'||key==='on_fail'||key==='on_timeout'){
ctx.branch.result_policy[key]=field.value||'';
return true;
}
if(key==='timeout_flag'){
const value=field.value||'';
if(value){
ctx.branch.result_policy.flag=value;
ctx.branch.result_policy.timeout_flag=value;
}
else{
delete ctx.branch.result_policy.flag;
delete ctx.branch.result_policy.timeout_flag;
}
return true;
}
return false;
}

function gmHandleReactiveV2GuardField(field,ctx){
const key=field.dataset.v2GuardField||'';
const guardEl=field.closest('[data-v2-guard-item]');
const guardIndex=Number(guardEl&&guardEl.dataset.v2GuardItem);
if(!Number.isFinite(guardIndex))return false;
ctx.branch.guard_flags=Array.isArray(ctx.branch.guard_flags)?ctx.branch.guard_flags:[];
const guard=ctx.branch.guard_flags[guardIndex]&&typeof ctx.branch.guard_flags[guardIndex]==='object'?ctx.branch.guard_flags[guardIndex]:{flag:'',value:true};
if(key==='flag')guard.flag=field.value||'';
else if(key==='value')guard.value=field.type==='checkbox'?!!field.checked:String(field.value)!=='false';
else return false;
ctx.branch.guard_flags[guardIndex]=guard;
return true;
}

function gmHandleReactiveV2VariantField(field,ctx){
const key=field.dataset.v2VariantField||'';
const variantEl=field.closest('[data-v2-variant]');
const variantIndex=Number(variantEl&&variantEl.dataset.v2Variant);
if(!Number.isFinite(variantIndex))return false;
ctx.branch.variants=Array.isArray(ctx.branch.variants)?ctx.branch.variants:[];
const variant=ctx.branch.variants[variantIndex];
if(!variant)return false;
if(key==='label')variant.label=field.value||'';
else if(key==='id')variant.id=field.value||variant.id||`variant_${variantIndex+1}`;
else return false;
return true;
}

function gmHandleReactiveV2ActionTypeField(field,ctx){
const previous=scenarioClone(ctx.action);
const replacement=newScenarioStepForType(ctx.actionIndex,field.value||'WAIT_TIME');
replacement.id=ctx.action.id||replacement.id;
replacement.enabled=ctx.action.enabled!==false;
scenarioRefreshAutoLabel(replacement,previous);
ctx.variant.actions[ctx.actionIndex]=normalizeScenarioEditorStep(replacement);
return true;
}

function gmHandleReactiveV2ActionDeviceField(field,ctx){
const previous=scenarioClone(ctx.action);
let action=scenarioClone(ctx.action);
const type=scenarioStepTypeValue(action);
const device=scenarioDeviceById(field.value||'');
action.device_id=field.value||'';
if(type==='DEVICE_COMMAND'){
action.command_id=scenarioValidCommandId(device,'');
action.params=defaultParamsForCommand(device,scenarioCommandById(action.device_id,action.command_id));
}
action=normalizeScenarioEditorStep(action);
ctx.variant.actions[ctx.actionIndex]=action;
scenarioRefreshAutoLabel(action,previous);
return true;
}

function gmHandleReactiveV2ActionCommandOrEventField(field,ctx){
const previous=scenarioClone(ctx.action);
let action=scenarioClone(ctx.action);
const type=scenarioStepTypeValue(action);
if(type==='DEVICE_COMMAND'&&field.matches('select[data-step-field="command_id"]')){
action.command_id=field.value||'';
action.params=defaultParamsForCommand(scenarioDeviceById(action.device_id),scenarioCommandById(action.device_id,action.command_id));
}
else return false;
action=normalizeScenarioEditorStep(action);
ctx.variant.actions[ctx.actionIndex]=action;
scenarioRefreshAutoLabel(action,previous);
return true;
}

function gmHandleReactiveV2ActionStepField(field,ctx){
const actionEl=field.closest('[data-v2-action]');
if(!actionEl)return false;
const previous=scenarioClone(ctx.action);
const action=collectReactiveV2ActionFromDom(actionEl,ctx.action,ctx.actionIndex);
ctx.variant.actions[ctx.actionIndex]=action;
scenarioRefreshAutoLabel(action,previous);
return true;
}

function gmHandleReactiveV2ActionParamField(field,ctx){
const actionEl=field.closest('[data-v2-action]');
if(!actionEl)return false;
const previous=scenarioClone(ctx.action);
const action=collectReactiveV2ActionFromDom(actionEl,ctx.action,ctx.actionIndex);
ctx.variant.actions[ctx.actionIndex]=action;
scenarioRefreshAutoLabel(action,previous);
return true;
}

function gmHandleReactiveV2ActionGroupField(field,ctx){
const itemEl=field.closest('[data-command-group-item]');
const itemIndex=Number(itemEl&&itemEl.dataset.commandGroupItem);
if(!Number.isFinite(itemIndex))return false;
let action=ctx.action;
const previous=scenarioClone(action);
action.commands=Array.isArray(action.commands)?action.commands:[];
const item=action.commands[itemIndex]||defaultScenarioCommandItem();
if(field.matches('select[data-group-command-field="device_id"]')){
const device=scenarioDeviceById(field.value||'');
item.device_id=field.value||'';
item.command_id=scenarioValidCommandId(device,'');
item.params=defaultParamsForCommand(device,scenarioCommandById(item.device_id,item.command_id));
}
else if(field.matches('select[data-group-command-field="command_id"]')){
item.command_id=field.value||'';
item.params=defaultParamsForCommand(scenarioDeviceById(item.device_id),scenarioCommandById(item.device_id,item.command_id));
}
else if(field.dataset.stepParam){
const key=field.dataset.stepParam||'';
const params=item.params&&typeof item.params==='object'?{...item.params}:{};
const typeAttr=(field.getAttribute('type')||'').toLowerCase();
const paramType=field.dataset.stepParamType||'';
if(field.type==='checkbox')params[key]=field.checked;
else if(typeAttr==='number'||paramType==='number')params[key]=Number(field.value)||0;
else params[key]=field.value;
if(String(item.device_id||'')==='system_audio'&&String(item.command_id||'')==='play')item.params=scenarioNormalizeAudioParams(params);
else if(Object.keys(params).length)item.params=params;
else delete item.params;
}
else return false;
action.commands[itemIndex]=item;
action=normalizeScenarioEditorStep(action);
ctx.variant.actions[ctx.actionIndex]=action;
scenarioRefreshAutoLabel(action,previous);
return true;
}

function gmHandleReactiveV2Change(control,deferRender){
const ctx=reactiveV2DraftContext(control);
if(!ctx)return false;
const branchField=control.closest('[data-v2-branch-field]');
const triggerField=control.closest('[data-v2-trigger-field]');
const policyField=control.closest('[data-v2-policy-field]');
const reentryField=control.closest('[data-v2-reentry-field]');
const resultField=control.closest('[data-v2-result-field]');
const guardField=control.closest('[data-v2-guard-field]');
const variantField=control.closest('[data-v2-variant-field]');
if(branchField&&gmHandleReactiveV2BranchField(branchField,ctx)){}
else if(triggerField&&gmHandleReactiveV2TriggerField(triggerField,ctx)){}
else if(policyField&&gmHandleReactiveV2PolicyField(policyField,ctx)){}
else if(reentryField&&gmHandleReactiveV2ReentryField(reentryField,ctx)){}
else if(resultField&&gmHandleReactiveV2ResultField(resultField,ctx)){}
else if(guardField&&gmHandleReactiveV2GuardField(guardField,ctx)){}
else if(variantField&&gmHandleReactiveV2VariantField(variantField,ctx)){}
else{
const actionCtx=reactiveV2DraftActionContextFromCtx(ctx,control);
if(!actionCtx)return false;
const stepType=control.closest('select[data-step-field="type"]');
const stepDevice=control.closest('select[data-step-field="device_id"]');
const stepCommand=control.closest('select[data-step-field="command_id"]');
const stepField=control.closest('[data-v2-action] [data-step-field]');
const stepParam=control.closest('[data-v2-action] [data-step-param]');
const groupField=control.closest('[data-v2-action] [data-group-command-field], [data-v2-action] [data-command-group-item] [data-step-param]');
	if(stepType&&gmHandleReactiveV2ActionTypeField(stepType,actionCtx)){}
	else if(stepDevice&&gmHandleReactiveV2ActionDeviceField(stepDevice,actionCtx)){}
	else if(stepCommand&&gmHandleReactiveV2ActionCommandOrEventField(stepCommand,actionCtx)){}
	else if(groupField&&gmHandleReactiveV2ActionGroupField(groupField,actionCtx)){}
	else if(stepParam&&gmHandleReactiveV2ActionParamField(stepParam,actionCtx)){}
	else if(stepField&&gmHandleReactiveV2ActionStepField(stepField,actionCtx)){}
	else return false;
}
gmScenarioChangeCommitDraft(ctx.draft,undefined,!!deferRender);
return true;
}

function gmHandleReactiveV2BranchField(field,ctx){
const key=field.dataset.v2BranchField||'';
if(key==='priority'){
ctx.branch.priority=Number(field.value)||0;
return true;
}
return false;
}

function gmHandleScenarioStepDeviceChange(stepDevice){
const stepEl=stepDevice.closest('[data-scenario-step]');
const index=Number(stepEl&&stepEl.dataset.scenarioStep);
const draft=scenarioWorkingDraft();
if(!draft)return false;
const steps=scenarioActiveSteps(draft);
if(Number.isFinite(index)&&steps[index]){
const step=steps[index];
const previous=scenarioClone(step);
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
scenarioRefreshAutoLabel(step,previous);
}
gmScenarioChangeCommitDraft(draft,index);
return true;
}

function gmHandleScenarioStepCommandOrEventChange(field){
const stepEl=field.closest('[data-scenario-step]');
const index=Number(stepEl&&stepEl.dataset.scenarioStep);
const draft=scenarioWorkingDraft();
if(!draft)return false;
const steps=scenarioActiveSteps(draft);
if(Number.isFinite(index)&&steps[index]){
const step=steps[index];
const previous=scenarioClone(step);
const type=scenarioStepTypeValue(step);
if(type==='DEVICE_COMMAND'&&field.matches('select[data-step-field="command_id"]')){
step.command_id=field.value||'';
step.params=defaultParamsForCommand(scenarioDeviceById(step.device_id),scenarioCommandById(step.device_id,step.command_id));
}
else if(type==='WAIT_DEVICE_EVENT'&&field.matches('select[data-step-field="event_id"]')){
step.event_id=field.value||'';
}
scenarioRefreshAutoLabel(step,previous);
}
gmScenarioChangeCommitDraft(draft,index);
return true;
}

function gmHandleScenarioStepAudioChannelChange(stepParamChannel){
const stepEl=stepParamChannel.closest('[data-scenario-step]');
const index=Number(stepEl&&stepEl.dataset.scenarioStep);
const draft=scenarioWorkingDraft();
if(!draft)return false;
const steps=scenarioActiveSteps(draft);
if(Number.isFinite(index)&&steps[index]){
const params=steps[index].params&&typeof steps[index].params==='object'?steps[index].params:{};
params.channel=stepParamChannel.value||'effect';
steps[index].params=scenarioNormalizeAudioParams(params);
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
const draft=scenarioWorkingDraft();
if(!draft)return false;
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
const draft=scenarioWorkingDraft();
if(!draft)return false;
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
const draft=scenarioWorkingDraft();
if(!draft)return false;
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

function gmHandleScenarioMetaFieldChange(field){
const draft=scenarioWorkingDraft();
if(!draft)return false;
if(field.id==='scenario_name')draft.name=field.value||'';
else if(field.id==='scenario_id')draft.id=field.value||'';
else return false;
gmScenarioChangeCommitDraft(draft,undefined,true);
return true;
}

function gmHandleScenarioBranchFieldChange(field){
const draft=scenarioWorkingDraft();
if(!draft)return false;
const branch=scenarioActiveBranch(draft);
if(!branch)return false;
const key=field.dataset.scenarioBranchField||'';
if(key==='name')branch.name=field.value||'';
else if(key==='id')branch.id=field.value||'';
else if(key==='enabled')branch.enabled=!!field.checked;
else if(key==='required_for_completion')branch.required_for_completion=!!field.checked;
else if(key==='cooldown_sec'){
branch.cooldown_ms=Math.max(0,Math.round(Number(field.value)||0))*1000;
if(branch.policy&&typeof branch.policy==='object')branch.policy.cooldown_ms=branch.cooldown_ms;
}
else if(key==='run_once')branch.run_once=field.type==='checkbox'?!!field.checked:String(field.value)==='true';
else return false;
if(scenarioIsReactiveV2Branch(branch))normalizeReactiveV2RepeatPolicy(branch);
gmScenarioChangeCommitDraft(draft,undefined,true);
return true;
}

function gmHandleScenarioStepFieldInput(field){
const stepEl=field.closest('[data-scenario-step]');
const index=Number(stepEl&&stepEl.dataset.scenarioStep);
const draft=scenarioWorkingDraft();
if(!draft)return false;
const steps=scenarioActiveSteps(draft);
if(!Number.isFinite(index)||!steps[index])return false;
const step=steps[index];
const previous=scenarioClone(step);
const key=field.dataset.stepField||'';
if(!key||key==='type'||key==='device_id'||key==='command_id'||key==='event_id')return false;
if(key==='enabled')step.enabled=!!field.checked;
else if(key==='duration_ms'||key==='timeout_ms')step[key]=field.value!==''?durationSecondsToMs(field.value):0;
else if(key==='value')step.value=field.type==='checkbox'?!!field.checked:String(field.value)!=='false';
else if(field.type==='checkbox')step[key]=!!field.checked;
else step[key]=field.value||'';
scenarioRefreshAutoLabel(step,previous);
gmScenarioChangeCommitDraft(draft,index,true);
return true;
}

function gmHandleScenarioStepParamInput(field,deferRender){
const stepEl=field.closest('[data-scenario-step]');
const index=Number(stepEl&&stepEl.dataset.scenarioStep);
const draft=scenarioWorkingDraft();
if(!draft)return false;
const steps=scenarioActiveSteps(draft);
if(!Number.isFinite(index)||!steps[index])return false;
const step=steps[index];
const key=field.dataset.stepParam||'';
if(!key||key==='channel')return false;
const params=step.params&&typeof step.params==='object'?{...step.params}:{};
const typeAttr=(field.getAttribute('type')||'').toLowerCase();
const paramType=field.dataset.stepParamType||'';
if(field.type==='checkbox')params[key]=field.checked;
else if(typeAttr==='number'||paramType==='number')params[key]=Number(field.value)||0;
else params[key]=field.value;
if(String(step.device_id||'')==='system_audio'&&String(step.command_id||'')==='play')step.params=scenarioNormalizeAudioParams(params);
else if(Object.keys(params).length)step.params=params;
else delete step.params;
gmScenarioChangeCommitDraft(draft,index,deferRender!==false);
return true;
}

function gmHandleScenarioFlagListInput(field){
const stepEl=field.closest('[data-scenario-step]');
const itemEl=field.closest('[data-flag-list-item]');
const index=Number(stepEl&&stepEl.dataset.scenarioStep);
const itemIndex=Number(itemEl&&itemEl.dataset.flagListItem);
const draft=scenarioWorkingDraft();
if(!draft)return false;
const steps=scenarioActiveSteps(draft);
if(!Number.isFinite(index)||!Number.isFinite(itemIndex)||!steps[index])return false;
const step=steps[index];
const previous=scenarioClone(step);
step.flags=Array.isArray(step.flags)?step.flags:[];
const item=normalizeScenarioFlagItem(step.flags[itemIndex]||defaultScenarioFlagItem());
const key=field.dataset.flagListField||'';
if(key==='flag_name')item.flag_name=field.value||'';
else if(key==='value')item.value=field.type==='checkbox'?!!field.checked:String(field.value)!=='false';
else return false;
step.flags[itemIndex]=item;
scenarioRefreshAutoLabel(step,previous);
gmScenarioChangeCommitDraft(draft,index,true);
return true;
}

function gmHandleScenarioGroupParamInput(field,deferRender){
const stepEl=field.closest('[data-scenario-step]');
const itemEl=field.closest('[data-command-group-item]');
const index=Number(stepEl&&stepEl.dataset.scenarioStep);
const itemIndex=Number(itemEl&&itemEl.dataset.commandGroupItem);
const draft=scenarioWorkingDraft();
if(!draft)return false;
const steps=scenarioActiveSteps(draft);
if(!Number.isFinite(index)||!Number.isFinite(itemIndex)||!steps[index])return false;
const step=steps[index];
step.commands=Array.isArray(step.commands)?step.commands:[];
const item=step.commands[itemIndex]||defaultScenarioCommandItem();
const key=field.dataset.stepParam||'';
if(!key)return false;
const params=item.params&&typeof item.params==='object'?{...item.params}:{};
const typeAttr=(field.getAttribute('type')||'').toLowerCase();
const paramType=field.dataset.stepParamType||'';
if(field.type==='checkbox')params[key]=field.checked;
else if(typeAttr==='number'||paramType==='number')params[key]=Number(field.value)||0;
else params[key]=field.value;
if(String(item.device_id||'')==='system_audio'&&String(item.command_id||'')==='play')item.params=scenarioNormalizeAudioParams(params);
else if(Object.keys(params).length)item.params=params;
else delete item.params;
step.commands[itemIndex]=item;
gmScenarioChangeCommitDraft(draft,index,deferRender!==false);
return true;
}

function gmHandleScenarioEditorInput(e){
const meta=e.target.closest('#scenario_id,#scenario_name');
const branchField=e.target.closest('[data-scenario-branch-field]');
const stepField=e.target.closest('[data-scenario-step] [data-step-field]');
const stepParam=e.target.closest('[data-scenario-step] [data-step-param]');
const flagField=e.target.closest('[data-flag-list-field]');
const groupParam=e.target.closest('[data-command-group-item] [data-step-param]');
const reactiveV2Field=e.target.closest('[data-v2-branch-field],[data-v2-trigger-field],[data-v2-policy-field],[data-v2-reentry-field],[data-v2-result-field],[data-v2-guard-field],[data-v2-variant-field],[data-v2-action] [data-step-field],[data-v2-action] [data-step-param],[data-v2-action] [data-group-command-field]');
if(reactiveV2Field&&String(reactiveV2Field.tagName||'').toLowerCase()==='select')return false;
if(reactiveV2Field)return gmHandleReactiveV2Change(reactiveV2Field,true);
if(groupParam)return gmHandleScenarioGroupParamInput(groupParam,true);
if(flagField)return gmHandleScenarioFlagListInput(flagField);
if(stepParam)return gmHandleScenarioStepParamInput(stepParam,true);
if(stepField)return gmHandleScenarioStepFieldInput(stepField);
if(branchField)return gmHandleScenarioBranchFieldChange(branchField);
if(meta)return gmHandleScenarioMetaFieldChange(meta);
return false;
}

function gmHandleScenarioEditorChange(e){
const stepType=e.target.closest('select[data-step-field="type"]');
const stepDevice=e.target.closest('select[data-step-field="device_id"]');
const stepCommand=e.target.closest('select[data-step-field="command_id"]');
const stepDeviceEvent=e.target.closest('select[data-step-field="event_id"]');
const stepParamChannel=e.target.closest('select[data-step-param="channel"]');
const stepParam=e.target.closest('[data-scenario-step] [data-step-param]');
const stepField=e.target.closest('[data-scenario-step] [data-step-field]');
const groupDevice=e.target.closest('select[data-group-command-field="device_id"]');
const groupCommand=e.target.closest('select[data-group-command-field="command_id"]');
const groupParam=e.target.closest('[data-command-group-item] [data-step-param]');
const eventGroupDevice=e.target.closest('select[data-event-group-field="device_id"]');
const eventGroupEvent=e.target.closest('select[data-event-group-field="event_id"]');
const flagField=e.target.closest('[data-flag-list-field]');
const branchField=e.target.closest('[data-scenario-branch-field]');
const meta=e.target.closest('#scenario_id,#scenario_name');
const branchType=e.target.closest('select[data-scenario-branch-field="type"]');
const reactiveV2Field=e.target.closest('[data-v2-branch-field],[data-v2-trigger-field],[data-v2-policy-field],[data-v2-reentry-field],[data-v2-result-field],[data-v2-guard-field],[data-v2-variant-field]');
const reactiveV2ActionField=e.target.closest('[data-v2-action] [data-step-field],[data-v2-action] [data-step-param],[data-v2-action] [data-group-command-field]');
if(branchType)return gmHandleScenarioBranchTypeChange(branchType);
if(reactiveV2Field||reactiveV2ActionField)return gmHandleReactiveV2Change(reactiveV2Field||reactiveV2ActionField);
if(stepDevice)return gmHandleScenarioStepDeviceChange(stepDevice);
if(stepCommand||stepDeviceEvent)return gmHandleScenarioStepCommandOrEventChange(stepCommand||stepDeviceEvent);
if(stepParamChannel)return gmHandleScenarioStepAudioChannelChange(stepParamChannel);
if(groupDevice||groupCommand)return gmHandleScenarioCommandGroupChange(groupDevice,groupCommand);
if(eventGroupDevice||eventGroupEvent)return gmHandleScenarioEventGroupChange(eventGroupDevice,eventGroupEvent);
if(stepType)return gmHandleScenarioStepTypeChange(stepType);
 if(groupParam)return gmHandleScenarioGroupParamInput(groupParam,false);
 if(flagField)return gmHandleScenarioFlagListInput(flagField);
 if(stepParam)return gmHandleScenarioStepParamInput(stepParam,false);
 if(stepField)return gmHandleScenarioStepFieldInput(stepField);
 if(branchField)return gmHandleScenarioBranchFieldChange(branchField);
 if(meta)return gmHandleScenarioMetaFieldChange(meta);
return false;
}
