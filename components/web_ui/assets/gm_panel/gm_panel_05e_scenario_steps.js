// GM panel source part. Edit this file, then rebuild gm_panel.js.
function scenarioStepPresetButtons(branch){
if(scenarioIsReactiveV2Branch(branch))return reactiveV2PresetButtons(branch);
const allowed=scenarioAllowedStepTypesForBranch(branch);
const allowedSet=allowed?new Set(allowed):null;
const schemas=scenarioStepSchemas().filter(schema=>!allowedSet||allowedSet.has(schema.type||''));
const title=allowedSet?(Array.isArray(branch&&branch.steps)&&branch.steps.length?'Add action':'Add trigger'):'Add step';
const hasSteps=Array.isArray(branch&&branch.steps)&&branch.steps.length>0;
const hint=allowedSet&&!hasSteps?`<div class='row-meta scenario-reaction-hint'>Add one trigger first. Actions become available after the trigger.</div>`:'';
return `<h2 class='section-title'>${esc(title)}</h2>${hint}<div class='scenario-step-presets'>${schemas.map(schema=>`<div class='scenario-step-preset-row'><button data-action='scenario.step' data-op='add_schema' data-step-type='${esc(schema.type||'WAIT_TIME')}'>${esc(schema.label||schema.type)}</button><button class='icon-btn' title='Show example' aria-label='Show step example' data-action='scenario.step.help' data-step-type='${esc(schema.type||'WAIT_TIME')}'>?</button></div>`).join('')}</div>`;
}

function scenarioStepPreviewText(step,index){
const type=scenarioStepTypeValue(step);
if(type==='DEVICE_COMMAND'){
const device=scenarioDeviceById(step.device_id);
if(String(step.device_id||'')==='system_audio')return `${index+1}. ${scenarioAudioCommandSummary(step)}`;
return `${index+1}. ${scenarioDeviceName(device)} -> ${scenarioCommandName(step.device_id,step.command_id)}`;
}
if(type==='DEVICE_COMMAND_GROUP')return `${index+1}. Command group (${(Array.isArray(step.commands)?step.commands:[]).length})`;
if(type==='WAIT_DEVICE_EVENT'){
const device=scenarioDeviceById(step.device_id);
return `${index+1}. Wait ${scenarioDeviceName(device)}: ${scenarioDeviceEventName(step.device_id,step.event_id)}`;
}
if(type==='WAIT_ANY_DEVICE_EVENT')return `${index+1}. Wait any of ${(Array.isArray(step.events)?step.events:[]).length} events`;
if(type==='WAIT_ALL_DEVICE_EVENTS')return `${index+1}. Wait all ${(Array.isArray(step.events)?step.events:[]).length} events`;
if(type==='WAIT_TIME')return `${index+1}. ${waitTimeLabel(step.duration_ms)}`;
if(type==='OPERATOR_APPROVAL')return `${index+1}. Operator: ${step.prompt||step.operator_prompt||'approval'}`;
if(type==='SHOW_OPERATOR_MESSAGE')return `${index+1}. Show operator: ${step.message||'message'}`;
if(type==='SET_FLAG')return `${index+1}. Set flag ${step.flag_name||'flag'} = ${step.value===false?'false':'true'}`;
if(type==='WAIT_FLAGS')return `${index+1}. Wait flags (${(Array.isArray(step.flags)?step.flags:[]).length})`;
if(type==='END_GAME')return `${index+1}. End game`;
return `${index+1}. ${step.label||type}`;
}

function renderScenarioDraftPreview(steps){
const list=Array.isArray(steps)?steps:[];
return `<div class='step-list scenario-preview'>${list.length?list.map((step,index)=>`<div class='step-item'><span>${esc(scenarioStepPreviewText(step,index))}</span>${step.enabled===false?` <span class='badge'>disabled</span>`:''}</div>`).join(''):`<div class='empty'>No steps yet</div>`}</div>`;
}

function refreshScenarioStepLabel(stepEl){
if(!stepEl)return;
const label=stepEl.querySelector('[data-step-field="label"]');
const typeField=stepEl.querySelector('[data-step-field="type"]');
if(!label||!typeField)return;
const type=typeField.value||'WAIT_TIME';
if(type==='DEVICE_COMMAND'){
const deviceId=(stepEl.querySelector('[data-step-field="device_id"]')||{}).value||'';
const commandId=(stepEl.querySelector('[data-step-field="command_id"]')||{}).value||'';
const device=scenarioDeviceById(deviceId);
label.value=`${scenarioRoomNameForDevice(device)}: ${scenarioDeviceName(device)} - ${scenarioCommandName(deviceId,commandId)}`;
}
else if(type==='WAIT_DEVICE_EVENT'){
const deviceId=(stepEl.querySelector('[data-step-field="device_id"]')||{}).value||'';
const eventId=(stepEl.querySelector('[data-step-field="event_id"]')||{}).value||'';
const device=scenarioDeviceById(deviceId);
label.value=`${scenarioRoomNameForDevice(device)}: wait ${scenarioDeviceName(device)} - ${scenarioDeviceEventName(deviceId,eventId)}`;
}
else if(type==='WAIT_ANY_DEVICE_EVENT'){
const count=stepEl.querySelectorAll('[data-event-group-item]').length||1;
label.value=`Wait any device event (${count})`;
}
else if(type==='WAIT_ALL_DEVICE_EVENTS'){
const count=stepEl.querySelectorAll('[data-event-group-item]').length||1;
label.value=`Wait all device events (${count})`;
}
else if(type==='WAIT_TIME'){
const seconds=(stepEl.querySelector('[data-step-field="duration_ms"]')||{}).value||1;
label.value=`Wait ${seconds} sec`;
}
else if(type==='OPERATOR_APPROVAL'){
const prompt=(stepEl.querySelector('[data-step-field="prompt"]')||{}).value||'approval';
label.value=`Operator approval: ${prompt}`;
}
else if(type==='SHOW_OPERATOR_MESSAGE'){
const message=(stepEl.querySelector('[data-step-field="message"]')||{}).value||'message';
label.value=`Show operator: ${message}`;
}
else if(type==='DEVICE_COMMAND_GROUP'){
const count=stepEl.querySelectorAll('[data-command-group-item]').length||1;
label.value=`Command group (${count})`;
}
else if(type==='SET_FLAG'){
const flag=(stepEl.querySelector('[data-step-field="flag_name"]')||{}).value||'flag';
const valueField=stepEl.querySelector('[data-step-field="value"]');
const value=valueField?(valueField.type==='checkbox'?valueField.checked:valueField.value!=='false'):true;
label.value=`Set ${flag} = ${value?'true':'false'}`;
}
else if(type==='WAIT_FLAGS'){
const count=stepEl.querySelectorAll('[data-flag-list-item]').length||1;
label.value=`Wait flags (${count})`;
}
else if(type==='END_GAME'){
label.value='End game';
}
}

function scenarioStepSummaryText(step){
const type=scenarioStepTypeValue(step);
if(type==='DEVICE_COMMAND'){
if(String(step.device_id||'')==='system_audio')return scenarioAudioCommandSummary(step);
const device=scenarioDeviceById(step.device_id);
return `${scenarioDeviceName(device)} -> ${scenarioCommandName(step.device_id,step.command_id)}`;
}
if(type==='DEVICE_COMMAND_GROUP')return `Command group (${(Array.isArray(step.commands)?step.commands:[]).length})`;
if(type==='WAIT_DEVICE_EVENT'){
const device=scenarioDeviceById(step.device_id);
return `Wait ${scenarioDeviceName(device)}: ${scenarioDeviceEventName(step.device_id,step.event_id)}`;
}
if(type==='WAIT_ANY_DEVICE_EVENT')return `Wait any event (${(Array.isArray(step.events)?step.events:[]).length})`;
if(type==='WAIT_ALL_DEVICE_EVENTS')return `Wait all events (${(Array.isArray(step.events)?step.events:[]).length})`;
if(type==='WAIT_TIME')return waitTimeLabel(step.duration_ms);
if(type==='OPERATOR_APPROVAL')return `Operator: ${step.prompt||step.operator_prompt||'approval'}`;
if(type==='SHOW_OPERATOR_MESSAGE')return `Show operator: ${step.message||'message'}`;
if(type==='SET_FLAG')return `Set ${step.flag_name||'flag'} = ${step.value===false?'false':'true'}`;
if(type==='WAIT_FLAGS')return `Wait flags (${(Array.isArray(step.flags)?step.flags:[]).length})`;
if(type==='END_GAME')return 'End game';
return step.label||type;
}

function scenarioStepVisualType(step){
const type=scenarioStepTypeValue(step);
if(type==='WAIT_TIME')return 'wait-time';
if(type==='WAIT_DEVICE_EVENT')return 'wait-event';
if(type==='WAIT_ANY_DEVICE_EVENT')return 'wait-event';
if(type==='WAIT_ALL_DEVICE_EVENTS')return 'wait-event';
if(type==='OPERATOR_APPROVAL')return 'operator';
if(type==='SHOW_OPERATOR_MESSAGE')return 'operator';
if(type==='DEVICE_COMMAND_GROUP')return 'command-group';
if(type==='SET_FLAG')return 'flag';
if(type==='WAIT_FLAGS')return 'flag';
if(type==='END_GAME')return 'end-game';
if(type==='DEVICE_COMMAND'&&String(step.device_id||'')==='system_audio')return 'audio';
return 'command';
}

function scenarioStepIcon(step){
const visual=scenarioStepVisualType(step);
if(visual==='wait-time')return '&#9201;';
if(visual==='wait-event')return '&#9678;';
if(visual==='operator')return '&#10003;';
if(visual==='audio')return '&#9835;';
if(visual==='command-group')return '&#9658;&#9658;';
if(visual==='flag')return '&#9873;';
if(visual==='end-game')return '&#9632;';
return '&#9658;';
}

function scenarioStepBadgeLabel(step){
const visual=scenarioStepVisualType(step);
if(visual==='wait-time')return 'Wait';
if(visual==='wait-event')return 'Event';
if(visual==='operator')return 'Operator';
if(visual==='audio')return 'Audio';
if(visual==='command-group')return 'Group';
if(visual==='flag')return 'Flag';
if(visual==='end-game')return 'End';
return 'Command';
}

function renderScenarioStepEditor(step,index,total,expanded,issues){
const type=scenarioStepTypeValue(step);
const summary=scenarioStepSummaryText(step);
const visual=scenarioStepVisualType(step);
const badge=scenarioStepBadgeLabel(step);
const fullType=scenarioStepTypeLabel(type);
const stepIssues=Array.isArray(issues)?issues:[];
const hasErrors=stepIssues.some(scenarioIssueIsError);
const hasWarnings=stepIssues.length&&!hasErrors;
const validationClass=hasErrors?'has-validation-error':(hasWarnings?'has-validation-warning':'');
const issueBadge=stepIssues.length?`<span class='badge scenario-issue-badge ${hasErrors?'error':'warning'}'>${hasErrors?'Error':'Warning'} ${stepIssues.length}</span>`:'';
const controls=`<div class='actions compact-actions'><button class='icon-btn' title='${expanded?'Close':'Edit'}' aria-label='${expanded?'Close':'Edit'}' data-action='scenario.step' data-op='edit' data-step-index='${index}'>${expanded?'&#10005;':'&#9998;'}</button><button class='icon-btn' title='Move up' aria-label='Move up' data-action='scenario.step' data-op='up' data-step-index='${index}' ${index<=0?'disabled':''}>&uarr;</button><button class='icon-btn' title='Move down' aria-label='Move down' data-action='scenario.step' data-op='down' data-step-index='${index}' ${index>=total-1?'disabled':''}>&darr;</button><button class='icon-btn danger' title='Delete' aria-label='Delete' data-action='scenario.step' data-op='delete' data-step-index='${index}'>&times;</button></div>`;
if(!expanded){
return `<div class='builder-step scenario-step-row scenario-step-${visual} ${validationClass} compact-step' data-scenario-step='${index}'><div class='scenario-step-line'><div class='scenario-step-line-main'><span class='scenario-step-number'>${index+1}.</span><span class='scenario-step-icon'>${scenarioStepIcon(step)}</span><span class='scenario-step-summary'>${esc(summary)}</span><span class='badge scenario-type-badge' title='${esc(fullType)}'>${esc(badge)}</span>${issueBadge}${step.enabled===false?`<span class='badge'>disabled</span>`:''}</div>${controls}</div>${renderScenarioInlineIssues(stepIssues)}</div>`;
}
return `<div class='builder-step scenario-step-row scenario-step-${visual} scenario-step-expanded ${validationClass} compact-step' data-scenario-step='${index}'><div class='scenario-step-line'><div class='scenario-step-line-main'><span class='scenario-step-number'>${index+1}.</span><span class='scenario-step-icon'>${scenarioStepIcon(step)}</span><span class='scenario-step-summary'>${esc(summary)}</span><span class='badge scenario-type-badge' title='${esc(fullType)}'>${esc(badge)}</span>${issueBadge}</div>${controls}</div>${renderScenarioInlineIssues(stepIssues)}<div class='scenario-step-edit'><div class='row compact-row'><input data-step-field='label' placeholder='Step label' value='${esc(step.label||'')}'><select data-step-field='type'>${scenarioTypeOptions(type)}</select><label class='row-meta enabled-inline'><input data-step-field='enabled' type='checkbox' ${step.enabled!==false?'checked':''} style='min-width:auto'> Enabled</label></div>${renderScenarioStepPayload(step,type)}</div></div>`;
}

function applyScenarioStepAction(action,index,type){
const wasDirty=!!scenarioEditor.dirty;
const draft=collectScenarioEditor();
const activeBranch=scenarioActiveBranch(draft);
const steps=scenarioActiveSteps(draft);
const nextIndex=scenarioNextStepLocalIndex(steps);
if(action==='add_schema'){
const allowed=scenarioAllowedStepTypesForBranch(activeBranch);
if(allowed&&!allowed.includes(scenarioStepTypeValue({type:type||'WAIT_TIME'}))){
alert(steps.length?'This reaction can only add action steps after its trigger.':'Add a reaction trigger first.');
return;
}
steps.push(newScenarioStepForType(nextIndex,type||'WAIT_TIME'));
scenarioEditor.expanded_step=steps.length-1;
}
else if(action==='add'){
steps.push(newScenarioStep(nextIndex,'wait_time'));
scenarioEditor.expanded_step=steps.length-1;
}
else if(action==='add_run'){
steps.push(newScenarioStep(nextIndex,'device_command'));
scenarioEditor.expanded_step=steps.length-1;
}
else if(action==='add_command'){
steps.push(newScenarioStep(nextIndex,'device_command'));
scenarioEditor.expanded_step=steps.length-1;
}
else if(action==='add_wait_device_event'){
steps.push(newScenarioStep(nextIndex,'wait_device_event'));
scenarioEditor.expanded_step=steps.length-1;
}
else if(action==='add_wait_time'){
steps.push(newScenarioStep(nextIndex,'wait_time'));
scenarioEditor.expanded_step=steps.length-1;
}
else if(action==='add_operator'){
steps.push(newScenarioStep(nextIndex,'operator'));
scenarioEditor.expanded_step=steps.length-1;
}
else if(action==='group_add'&&index>=0&&steps[index]){
steps[index].commands=Array.isArray(steps[index].commands)?steps[index].commands:[];
steps[index].commands.push(defaultScenarioCommandItem());
scenarioEditor.expanded_step=index;
}
else if(action==='group_delete'&&index>=0&&steps[index]){
const commandIndex=Number(type);
steps[index].commands=Array.isArray(steps[index].commands)?steps[index].commands:[];
if(Number.isFinite(commandIndex))steps[index].commands.splice(commandIndex,1);
if(!steps[index].commands.length)steps[index].commands.push(defaultScenarioCommandItem());
scenarioEditor.expanded_step=index;
}
else if(action==='event_group_add'&&index>=0&&steps[index]){
steps[index].events=Array.isArray(steps[index].events)?steps[index].events:[];
steps[index].events.push(defaultScenarioEventItem());
scenarioEditor.expanded_step=index;
}
else if(action==='event_group_delete'&&index>=0&&steps[index]){
const eventIndex=Number(type);
steps[index].events=Array.isArray(steps[index].events)?steps[index].events:[];
if(Number.isFinite(eventIndex))steps[index].events.splice(eventIndex,1);
if(!steps[index].events.length)steps[index].events.push(defaultScenarioEventItem());
scenarioEditor.expanded_step=index;
}
else if(action==='flag_list_add'&&index>=0&&steps[index]){
steps[index].flags=Array.isArray(steps[index].flags)?steps[index].flags:[];
steps[index].flags.push(defaultScenarioFlagItem());
scenarioEditor.expanded_step=index;
}
else if(action==='flag_list_delete'&&index>=0&&steps[index]){
const flagIndex=Number(type);
steps[index].flags=Array.isArray(steps[index].flags)?steps[index].flags:[];
if(Number.isFinite(flagIndex))steps[index].flags.splice(flagIndex,1);
if(!steps[index].flags.length)steps[index].flags.push(defaultScenarioFlagItem());
scenarioEditor.expanded_step=index;
}
else if(action==='delete'&&index>=0){
steps.splice(index,1);
if(scenarioEditor.expanded_step>=steps.length)scenarioEditor.expanded_step=Math.max(0,steps.length-1);
else if(scenarioEditor.expanded_step>index)scenarioEditor.expanded_step--;
}
else if(action==='up'&&index>0){
const t=steps[index-1];
steps[index-1]=steps[index];
steps[index]=t;
scenarioEditor.expanded_step=index-1;
}
else if(action==='down'&&index>=0&&index<steps.length-1){
const t=steps[index+1];
steps[index+1]=steps[index];
steps[index]=t;
scenarioEditor.expanded_step=index+1;
}
else if(action==='edit'&&index>=0){
scenarioEditor.expanded_step=scenarioEditor.expanded_step===index?-1:index;
scenarioEditor.draft=draft;
scenarioEditor.dirty=wasDirty;
skipNextScenarioDomSync();
render();
return;
}
scenarioEditor.draft=draft;
scenarioEditor.dirty=true;
skipNextScenarioDomSync();
render();
}
