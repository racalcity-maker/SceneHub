// GM panel source part. Edit this file, then rebuild gm_panel.js.
function scenarioStepPresetButtons(branch){
if(scenarioIsReactiveV2Branch(branch))return reactiveV2PresetButtons(branch);
const allowed=scenarioAllowedStepTypesForBranch(branch);
const allowedSet=allowed?new Set(allowed):null;
const schemas=scenarioStepSchemas().filter(schema=>!allowedSet||allowedSet.has(schema.type||''));
const title=allowedSet?(Array.isArray(branch&&branch.steps)&&branch.steps.length?'Add action':'Add trigger'):'Add step';
const hasSteps=Array.isArray(branch&&branch.steps)&&branch.steps.length>0;
const hint=allowedSet&&!hasSteps?`<div class='row-meta scenario-reaction-hint'>Add one trigger first. Actions become available after the trigger.</div>`:'';
return `<h2 class='section-title'>${esc(title)}</h2>${hint}<div class='scenario-step-presets'>${schemas.map(schema=>`<div class='scenario-step-preset-row'><button data-scenario-step-action='add_schema' data-scenario-step-type='${esc(schema.type||'WAIT_TIME')}'>${esc(schema.label||schema.type)}</button><button class='icon-btn' title='Show example' aria-label='Show step example' data-scenario-step-help='${esc(schema.type||'WAIT_TIME')}'>?</button></div>`).join('')}</div>`;
}

function scenarioIsReactiveV2Branch(branch){
return scenarioBranchTypeValue(branch)==='reactive'&&(Array.isArray(branch&&branch.variants)||!!(branch&&branch.trigger));
}

function reactiveV2ActionTypes(){
return ['DEVICE_COMMAND','WAIT_TIME','SET_FLAG','SHOW_OPERATOR_MESSAGE'];
}

function reactiveV2ActionTypeOptions(type){
const selected=scenarioStepTypeValue({type});
const types=reactiveV2ActionTypes();
const all=types.includes(selected)?types:[selected].concat(types);
return all.map(t=>`<option value='${esc(t)}' ${selected===t?'selected':''}>${esc(scenarioStepTypeLabel(t))}</option>`).join('');
}

function defaultReactiveV2Trigger(){
const device=firstDeviceWithEvent();
const event=firstEventForDevice(device);
return {kind:'device_event',device_id:device&&device.id||'',event_id:event&&event.id||''};
}

function defaultReactiveV2BranchFields(){
return {priority:0,max_fire_count:0,trigger:defaultReactiveV2Trigger(),guard_flags:[],policy:{mode:'single',cooldown_ms:0,max_fire_count:0},reentry:{mode:'ignore'},variants:[{id:'variant_1',label:'Actions',actions:[]}],result_policy:{on_done:'continue',on_fail:'fail_reaction',on_timeout:'fail_reaction'},on_complete:[]};
}

function ensureReactiveV2Branch(branch){
if(!branch)return branch;
const defaults=defaultReactiveV2BranchFields();
branch.priority=Number(branch.priority)||0;
branch.max_fire_count=Number(branch.max_fire_count)||Number(branch.policy&&branch.policy.max_fire_count)||0;
branch.trigger=branch.trigger&&typeof branch.trigger==='object'?branch.trigger:defaults.trigger;
branch.guard_flags=Array.isArray(branch.guard_flags)?branch.guard_flags:[];
branch.policy=branch.policy&&typeof branch.policy==='object'?branch.policy:defaults.policy;
branch.policy.mode=branch.policy.mode||'single';
branch.policy.cooldown_ms=Number(branch.policy.cooldown_ms)||Number(branch.cooldown_ms)||0;
branch.policy.max_fire_count=Number(branch.policy.max_fire_count)||branch.max_fire_count||0;
branch.cooldown_ms=Number(branch.cooldown_ms)||Number(branch.policy.cooldown_ms)||0;
branch.reentry=branch.reentry&&typeof branch.reentry==='object'?branch.reentry:defaults.reentry;
branch.reentry.mode=branch.reentry.mode||'ignore';
branch.result_policy=branch.result_policy&&typeof branch.result_policy==='object'?branch.result_policy:defaults.result_policy;
branch.result_policy.on_done=branch.result_policy.on_done||'continue';
branch.result_policy.on_fail=branch.result_policy.on_fail||'fail_reaction';
branch.result_policy.on_timeout=branch.result_policy.on_timeout||'fail_reaction';
branch.variants=Array.isArray(branch.variants)&&branch.variants.length?branch.variants:defaults.variants;
branch.variants=branch.variants.map((variant,index)=>({id:variant&&variant.id||`variant_${index+1}`,label:variant&&variant.label||variant&&variant.name||(index===0?'Actions':`Variant ${index+1}`),actions:Array.isArray(variant&&variant.actions)?variant.actions:[]}));
branch.on_complete=Array.isArray(branch.on_complete)?branch.on_complete:[];
return branch;
}

function reactiveV2PresetButtons(branch){
const variants=Array.isArray(branch&&branch.variants)?branch.variants:[];
return `<h2 class='section-title'>Reaction</h2><div class='row-meta'>${esc(variants.length)} variant${variants.length===1?'':'s'}. Use the rule editor on the right.</div>`;
}

function renderReactiveV2Trigger(branch){
const trigger=branch.trigger||defaultReactiveV2Trigger();
const kind=String(trigger.kind||'device_event');
const devices=scenarioCatalogDevices().filter(device=>Array.isArray(device.events)&&device.events.length);
let selectedDevice=trigger.device_id||((devices[0]&&devices[0].id)||'');
const device=scenarioDeviceById(selectedDevice)||devices[0]||null;
if(device&&!selectedDevice)selectedDevice=device.id||'';
const events=device&&Array.isArray(device.events)?device.events:[];
const selectedEvent=scenarioValidEventId(device,trigger.event_id||'');
const kindOptions=['device_event','flag_changed','operator_event','runtime_event'].map(item=>`<option value='${item}' ${kind===item?'selected':''}>${item}</option>`).join('');
let body='';
if(kind==='device_event'){
const deviceControl=devices.length?`<select class='scenario-select' data-v2-trigger-field='device_id'>${optionList(devices,selectedDevice,'Select device')}</select>`:`<input data-v2-trigger-field='device_id' placeholder='Device ID' value='${esc(selectedDevice)}'>`;
const eventControl=events.length?`<select class='scenario-select' data-v2-trigger-field='event_id'>${optionList(events,selectedEvent,'Select event')}</select>`:`<input data-v2-trigger-field='event_id' placeholder='Event ID' value='${esc(trigger.event_id||selectedEvent)}'>`;
body=`<div class='scenario-v2-inline-fields'>${deviceControl}${eventControl}</div>`;
}else if(kind==='flag_changed'){
body=`<div class='scenario-v2-inline-fields'>${renderScenarioFlagInput(trigger.flag_name||'',`data-v2-trigger-field='flag_name'`)}</div>`;
}else if(kind==='operator_event'){
body=`<div class='scenario-v2-inline-fields'><input data-v2-trigger-field='operator_event' placeholder='Operator event' value='${esc(trigger.operator_event||'')}'></div>`;
}else{
body=`<div class='scenario-v2-inline-fields'><input data-v2-trigger-field='runtime_event' placeholder='Runtime event' value='${esc(trigger.runtime_event||'')}'></div>`;
}
return `<section class='scenario-v2-rule'><div class='scenario-v2-rule-label'>When</div><div class='scenario-v2-rule-body'><div class='scenario-v2-inline-fields narrow'><select class='scenario-select' data-v2-trigger-field='kind'>${kindOptions}</select></div>${body}</div></section>`;
}

function renderReactiveV2Type(branch){
const policy=branch.policy||{};
const mode=String(policy.mode||'single');
const item=(value,label,sub)=>`<label class='scenario-v2-type-option ${mode===value?'active':''}'><input data-v2-policy-field='mode' type='radio' name='reactive_v2_mode' value='${esc(value)}' ${mode===value?'checked':''}><span><strong>${esc(label)}</strong><em>${esc(sub)}</em></span></label>`;
return `<section class='scenario-v2-type'><div class='scenario-v2-type-title'>Reaction type</div><div class='scenario-v2-type-grid'>${item('single','Same actions','Run the same action list on every trigger.')}${item('escalate','Escalate','Run level 1, then level 2, then the next levels.')}${item('rotate','Rotate','Cycle through variants on each trigger.')}${item('random','Random','Pick one variant randomly.')}</div></section>`;
}

function renderReactiveV2Policy(branch){
const policy=branch.policy||{};
const reentry=branch.reentry||{};
const result=branch.result_policy||{};
const reentryMode=String(reentry.mode||'ignore');
const isEscalate=String(policy.mode||'single')==='escalate';
const resultAction=value=>['continue','set_flag','fail_reaction','fail_scenario','retry'].map(item=>`<option value='${item}' ${String(value||'')===item?'selected':''}>${item}</option>`).join('');
return `<details class='scenario-advanced scenario-v2-settings'><summary>Advanced reaction settings</summary><div class='scenario-v2-settings-grid'><label class='field-stack'><span>Cooldown, sec</span><input data-scenario-branch-field='cooldown_sec' type='number' min='0' step='1' value='${esc(Math.round((Number(branch.cooldown_ms)||0)/1000))}'></label><label class='row-meta branch-toggle'><input data-scenario-branch-field='run_once' type='checkbox' ${branch.run_once?'checked':''}> Run once</label><label class='field-stack'><span>Reentry while running</span><select data-v2-reentry-field='mode'><option value='ignore' ${reentryMode==='ignore'?'selected':''}>ignore</option><option value='queue_one' ${reentryMode==='queue_one'?'selected':''}>queue_one</option></select></label>${isEscalate?'':`<label class='field-stack'><span>Max fires</span><input data-v2-policy-field='max_fire_count' type='number' min='0' step='1' value='${esc(Number(policy.max_fire_count)||Number(branch.max_fire_count)||0)}'></label>`}<label class='field-stack'><span>Priority</span><input data-v2-branch-field='priority' type='number' step='1' value='${esc(Number(branch.priority)||0)}'></label><label class='field-stack'><span>On done</span><select data-v2-result-field='on_done'>${resultAction(result.on_done||'continue')}</select></label><label class='field-stack'><span>On fail</span><select data-v2-result-field='on_fail'>${resultAction(result.on_fail||'fail_reaction')}</select></label><label class='field-stack'><span>On timeout</span><select data-v2-result-field='on_timeout'>${resultAction(result.on_timeout||'fail_reaction')}</select></label><label class='field-stack'><span>Result flag</span>${renderScenarioFlagInput(result.timeout_flag||result.flag||'',`data-v2-result-field='timeout_flag'`)}</label></div></details>`;
}

function renderReactiveV2Guards(branch){
const guards=Array.isArray(branch.guard_flags)?branch.guard_flags:[];
return `<section class='scenario-v2-rule'><div class='scenario-v2-rule-label'>If</div><div class='scenario-v2-rule-body'><div class='scenario-v2-guard-list'>${guards.length?guards.map((guard,index)=>`<div class='scenario-v2-guard' data-v2-guard-item='${index}'><span class='row-meta'>Flag</span>${renderScenarioFlagInput(guard.flag||guard.flag_name||guard.name||'',`data-v2-guard-field='flag'`)}<select data-v2-guard-field='value'><option value='true' ${guard.value!==false?'selected':''}>is true</option><option value='false' ${guard.value===false?'selected':''}>is false</option></select><button class='icon-btn danger' data-reactive-v2-action='delete_guard' data-guard-index='${index}'>&times;</button></div>`).join(''):`<div class='empty compact-empty'>No guard flags. The reaction can run whenever the trigger arrives.</div>`}</div><button data-reactive-v2-action='add_guard'>Add condition</button></div></section>`;
}

function renderReactiveV2Action(action,variantIndex,actionIndex){
const type=scenarioStepTypeValue(action);
const summary=scenarioStepSummaryText(action);
return `<div class='builder-step scenario-step-row scenario-step-${scenarioStepVisualType(action)} compact-step scenario-v2-action' data-v2-action='${actionIndex}' data-variant-index='${variantIndex}'><div class='scenario-step-line'><div class='scenario-step-line-main'><span class='scenario-step-number'>${actionIndex+1}.</span><span class='scenario-step-icon'>${scenarioStepIcon(action)}</span><span class='scenario-step-summary'>${esc(summary)}</span><span class='badge scenario-type-badge'>${esc(scenarioStepBadgeLabel(action))}</span></div><div class='actions compact-actions'><button class='icon-btn danger' data-reactive-v2-action='delete_action' data-variant-index='${variantIndex}' data-action-index='${actionIndex}'>&times;</button></div></div><div class='scenario-step-edit'><div class='scenario-v2-action-fields'><input data-step-field='label' placeholder='Action label' value='${esc(action.label||'')}'><select data-step-field='type'>${reactiveV2ActionTypeOptions(type)}</select></div>${renderScenarioStepPayload(action,type)}</div></div>`;
}

function renderReactiveV2ActionAddButtons(variantIndex){
return `<div class='scenario-v2-action-add'><button data-reactive-v2-action='add_action' data-action-type='DEVICE_COMMAND' data-variant-index='${variantIndex}'>Add device command</button><button data-reactive-v2-action='add_action' data-action-type='WAIT_TIME' data-variant-index='${variantIndex}'>Add wait</button><button data-reactive-v2-action='add_action' data-action-type='SET_FLAG' data-variant-index='${variantIndex}'>Add flag</button><button data-reactive-v2-action='add_action' data-action-type='SHOW_OPERATOR_MESSAGE' data-variant-index='${variantIndex}'>Add message</button></div>`;
}

function renderReactiveV2Variants(branch){
const variants=Array.isArray(branch.variants)?branch.variants:[];
const mode=String(branch.policy&&branch.policy.mode||'single');
const isSingle=mode==='single';
const isEscalate=mode==='escalate';
const title=isEscalate?'Escalation levels':(isSingle?'Actions':'Variants');
const addLabel=isEscalate?'Add level':'Add variant';
const shown=isSingle?(variants.length?[variants[0]]:[{id:'variant_1',label:'Actions',actions:[]}]):variants;
const maxFireValue=Number(branch.policy&&branch.policy.max_fire_count)||Number(branch.max_fire_count)||shown.length||1;
const escalateControls=isEscalate?`<label class='scenario-v2-max-fire'><span>Stop after level</span><input data-v2-policy-field='max_fire_count' type='number' min='0' step='1' value='${esc(maxFireValue)}'></label>`:'';
return `<section class='scenario-v2-rule scenario-v2-then'><div class='scenario-v2-rule-label'>Then</div><div class='scenario-v2-rule-body'><div class='scenario-v2-subtitle-row'><div class='scenario-v2-subtitle'>${esc(title)}</div>${escalateControls}</div><div class='scenario-v2-variant-list'>${shown.map((variant,index)=>{const originalIndex=isSingle?0:index;const label=isEscalate?`Level ${index+1}`:(isSingle?'Actions':`Variant ${index+1}`);const nameValue=isSingle?'Actions':(variant.label||variant.name||label);return `<div class='scenario-v2-variant' data-v2-variant='${originalIndex}'><div class='scenario-v2-variant-head'><label class='field-stack'><span>${esc(label)}</span><input data-v2-variant-field='label' placeholder='${esc(label)} label' value='${esc(nameValue)}' ${isSingle?'readonly':''}></label><div class='actions'>${isSingle?'':`<button class='danger' data-reactive-v2-action='delete_variant' data-variant-index='${originalIndex}' ${variants.length<=1?'disabled':''}>Delete ${isEscalate?'level':'variant'}</button>`}</div></div><details class='scenario-advanced compact-advanced scenario-v2-variant-id'><summary>${esc(isEscalate?'Level id':'Variant id')}</summary><div class='row'><input data-v2-variant-field='id' placeholder='Variant ID' value='${esc(variant.id||'')}'></div></details><div class='scenario-v2-action-list'>${(Array.isArray(variant.actions)?variant.actions:[]).map((action,actionIndex)=>renderReactiveV2Action(action,originalIndex,actionIndex)).join('')||`<div class='empty'>No actions yet. Add one or more actions below.</div>`}</div>${renderReactiveV2ActionAddButtons(originalIndex)}</div>`;}).join('')}</div>${isSingle?'':`<button data-reactive-v2-action='add_variant'>${esc(addLabel)}</button>`}</div></section>`;
}

function renderReactiveV2Editor(branch){
ensureReactiveV2Branch(branch);
return `<div class='scenario-v2-editor'>${renderReactiveV2Type(branch)}${renderReactiveV2Trigger(branch)}${renderReactiveV2Guards(branch)}${renderReactiveV2Variants(branch)}${renderReactiveV2Policy(branch)}</div>`;
}

function scenarioDeviceById(deviceId){
return scenarioCatalogDevices().find(device=>device.id===deviceId)||null;
}

function scenarioCommandById(deviceId,commandId){
const device=scenarioDeviceById(deviceId);
return device&&Array.isArray(device.commands)?device.commands.find(cmd=>cmd.id===commandId)||null:null;
}

function scenarioValidCommandId(device,commandId){
const commands=device&&Array.isArray(device.commands)?device.commands:[];
if(commandId&&commands.some(cmd=>cmd.id===commandId))return commandId;
return commands[0]&&commands[0].id||'';
}

function scenarioValidEventId(device,eventId){
const events=device&&Array.isArray(device.events)?device.events:[];
if(eventId&&events.some(ev=>ev.id===eventId))return eventId;
return events[0]&&events[0].id||'';
}

function scenarioEventById(deviceId,eventId){
const device=scenarioDeviceById(deviceId);
return device&&Array.isArray(device.events)?device.events.find(ev=>ev.id===eventId)||null:null;
}

function scenarioCommandName(deviceId,commandId){
const command=scenarioCommandById(deviceId,commandId);
return command&&(command.label||command.id)||commandId||'command';
}

function scenarioAudioCommandSummary(step){
const commandId=String(step&&step.command_id||'');
if(commandId==='play'){
const params=step&&step.params||{};
return audioChannelValue(params)==='background'?(params.repeat?'Play background repeat':'Play background'):'Play audio';
}
return scenarioCommandName('system_audio',commandId);
}

function scenarioDeviceEventName(deviceId,eventId){
const event=scenarioEventById(deviceId,eventId);
return event&&(event.label||event.id)||eventId||'event';
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

function scenarioActiveValidationIssues(savedIssues){
const report=scenarioEditor.validation_report;
if(report&&Array.isArray(report.issues))return report.issues;
return Array.isArray(savedIssues)?savedIssues:[];
}

function scenarioIssueIsError(issue){
return String(issue&&issue.level||'error').toLowerCase()==='error';
}

function scenarioClientValidationReport(scenario){
const issues=[];
const add=(stepIndex,code,message)=>issues.push({level:'error',step_index:stepIndex,code,message});
let globalIndex=0;
(Array.isArray(scenario&&scenario.branches)?scenario.branches:[]).forEach(branch=>{
const seenStepIds=new Set();
(Array.isArray(branch.steps)?branch.steps:[]).forEach((step,localIndex)=>{
const type=scenarioStepTypeValue(step);
const stepIndex=globalIndex++;
const stepLabel=`Step ${localIndex+1}`;
const stepId=String(step&&step.id||'').trim();
if(!stepId)add(stepIndex,'STEP_ID_EMPTY',`${stepLabel}: internal step id is empty`);
else if(seenStepIds.has(stepId))add(stepIndex,'STEP_ID_DUPLICATE',`${stepLabel}: duplicate step id inside this branch`);
seenStepIds.add(stepId);
if(type==='DEVICE_COMMAND'){
if(!String(step.device_id||'').trim()||!String(step.command_id||'').trim())add(stepIndex,'DEVICE_COMMAND_INCOMPLETE',`${stepLabel}: choose a device and command`);
}
else if(type==='DEVICE_COMMAND_GROUP'){
const commands=Array.isArray(step.commands)?step.commands:[];
if(!commands.length)add(stepIndex,'COMMAND_GROUP_EMPTY',`${stepLabel}: add at least one command`);
commands.forEach((cmd,cmdIndex)=>{if(!String(cmd&&cmd.device_id||'').trim()||!String(cmd&&cmd.command_id||'').trim())add(stepIndex,'COMMAND_GROUP_INCOMPLETE',`${stepLabel}: command ${cmdIndex+1} needs a device and command`);});
}
else if(type==='WAIT_DEVICE_EVENT'){
if(!String(step.device_id||'').trim()||!String(step.event_id||'').trim())add(stepIndex,'WAIT_DEVICE_EVENT_INCOMPLETE',`${stepLabel}: choose a device and event`);
}
else if(type==='WAIT_ANY_DEVICE_EVENT'||type==='WAIT_ALL_DEVICE_EVENTS'){
const events=Array.isArray(step.events)?step.events:[];
if(!events.length)add(stepIndex,'WAIT_EVENTS_EMPTY',`${stepLabel}: add at least one device event`);
events.forEach((ev,eventIndex)=>{if(!String(ev&&ev.device_id||'').trim()||!String(ev&&ev.event_id||'').trim())add(stepIndex,'WAIT_EVENT_INCOMPLETE',`${stepLabel}: event ${eventIndex+1} needs a device and event`);});
}
else if(type==='WAIT_TIME'){
if(!Number.isFinite(Number(step.duration_ms))||Number(step.duration_ms)<=0)add(stepIndex,'WAIT_TIME_INVALID',`${stepLabel}: duration must be greater than zero`);
}
else if(type==='OPERATOR_APPROVAL'){
if(!String(step.prompt||step.operator_prompt||'').trim())add(stepIndex,'OPERATOR_PROMPT_EMPTY',`${stepLabel}: write the operator prompt`);
}
else if(type==='SHOW_OPERATOR_MESSAGE'){
if(!String(step.message||'').trim())add(stepIndex,'OPERATOR_MESSAGE_EMPTY',`${stepLabel}: write the operator message`);
}
else if(type==='SET_FLAG'){
if(!String(step.flag_name||'').trim())add(stepIndex,'FLAG_NAME_EMPTY',`${stepLabel}: choose or type a flag name`);
}
else if(type==='WAIT_FLAGS'){
const flags=Array.isArray(step.flags)?step.flags:[];
if(!flags.length)add(stepIndex,'WAIT_FLAGS_EMPTY',`${stepLabel}: add at least one flag`);
flags.forEach((flag,flagIndex)=>{if(!String(flag&&flag.flag_name||'').trim())add(stepIndex,'FLAG_NAME_EMPTY',`${stepLabel}: flag ${flagIndex+1} needs a name`);});
}
});
});
return {ok:true,valid:!issues.length,issue_count:issues.length,error_count:issues.length,warning_count:0,issues};
}

function scenarioIssueIsStepSpecific(issue,stepCount){
const idx=Number(issue&&issue.step_index);
if(!Number.isFinite(idx)||idx<0||idx>=stepCount)return false;
const code=String(issue&&issue.code||'');
return !(code.indexOf('SCENARIO_')===0||code==='ROOM_ID_EMPTY'||code==='STEP_COUNT_LIMIT'||code==='SCENARIO_NULL');
}

function scenarioIssuesByStep(issues,stepCount){
const out={};
(Array.isArray(issues)?issues:[]).forEach(issue=>{
if(!scenarioIssueIsStepSpecific(issue,stepCount))return;
const idx=Number(issue.step_index);
out[idx]=out[idx]||[];
out[idx].push(issue);
});
return out;
}

function scenarioGlobalIssues(issues,stepCount){
return (Array.isArray(issues)?issues:[]).filter(issue=>!scenarioIssueIsStepSpecific(issue,stepCount));
}

function renderScenarioInlineIssues(issues){
const list=Array.isArray(issues)?issues:[];
if(!list.length)return '';
return `<div class='scenario-step-issues'>${list.map(issue=>`<div class='scenario-step-issue ${scenarioIssueIsError(issue)?'error':'warning'}'><span>${esc(issue.level||'error')}</span><strong>${esc(issue.code||'VALIDATION')}</strong><em>${esc(issue.message||'')}</em></div>`).join('')}</div>`;
}

function renderScenarioValidationSummary(issues,stepCount){
const list=Array.isArray(issues)?issues:[];
if(!list.length)return '';
const errors=list.filter(scenarioIssueIsError).length;
const warnings=list.length-errors;
const global=scenarioGlobalIssues(list,stepCount);
const summary=errors?`${errors} error${errors===1?'':'s'}, ${warnings} warning${warnings===1?'':'s'}`:(warnings?`${warnings} warning${warnings===1?'':'s'}`:'valid');
return `<div class='scenario-validation-summary ${errors?'error':(warnings?'warning':'')}'>Validation: ${esc(summary)}</div>${scenarioIssueHtml(global)}`;
}

function scenarioIssuesForBranch(issues,branches,branchIndex){
const branch=(Array.isArray(branches)?branches:[])[branchIndex]||null;
const stepCount=branch&&Array.isArray(branch.steps)?branch.steps.length:0;
const offset=scenarioBranchStepOffset(branches,branchIndex);
const out={};
(Array.isArray(issues)?issues:[]).forEach(issue=>{
const idx=Number(issue&&issue.step_index);
if(!Number.isFinite(idx)||idx<offset||idx>=offset+stepCount)return;
const local={...issue,step_index:idx-offset};
if(!scenarioIssueIsStepSpecific(local,stepCount))return;
out[local.step_index]=out[local.step_index]||[];
out[local.step_index].push(local);
});
return out;
}

function renderScenarioBranchTabs(base,activeIndex){
const branches=Array.isArray(base&&base.branches)?base.branches:[];
if(!branches.length)return '';
const flow=branches.map((branch,index)=>({branch,index})).filter(item=>scenarioBranchTypeValue(item.branch)==='normal');
const reactions=branches.map((branch,index)=>({branch,index})).filter(item=>scenarioBranchTypeValue(item.branch)==='reactive');
const tab=item=>`<button class='scenario-branch-tab ${item.index===activeIndex?'active':''}' data-scenario-branch-action='select' data-branch-index='${item.index}'><span>${esc(item.branch.name||item.branch.id||`Branch ${item.index+1}`)}</span><em>${esc(scenarioIsReactiveV2Branch(item.branch)?(Array.isArray(item.branch.variants)?item.branch.variants.length:0):(Array.isArray(item.branch.steps)?item.branch.steps.length:0))}</em></button>`;
return `<div class='scenario-branch-tabs grouped'><div class='scenario-branch-tab-group'><span class='row-meta'>Scenario flow</span>${flow.map(tab).join('')}<button class='scenario-branch-add' data-scenario-branch-action='add'>+ Branch</button></div><div class='scenario-branch-tab-group'><span class='row-meta'>Reactions</span>${reactions.map(tab).join('')}<button class='scenario-branch-add' data-scenario-branch-action='add_reactive'>+ Reaction</button></div></div>`;
}

function renderScenarioBranchSettings(branch,index,total){
if(!branch)return '';
const branchIdKey=`scenario:branch:${scenarioEditor.room_id}:${branch.id||index}`;
const type=scenarioBranchTypeValue(branch);
const isV2=scenarioIsReactiveV2Branch(branch);
const typeField=type==='normal'?`<div class='field-stack'><span>Type</span><select data-scenario-branch-field='type'><option value='normal' selected>Scenario flow</option><option value='reactive'>Reaction</option></select></div>`:`<input type='hidden' data-scenario-branch-field='type' value='reactive'>`;
const controls=type==='normal'?`<label class='row-meta branch-toggle'><input data-scenario-branch-field='required_for_completion' type='checkbox' ${branch.required_for_completion!==false?'checked':''}> Required for finish</label>`:(isV2?'':`<label class='row-meta branch-toggle'><input data-scenario-branch-field='run_once' type='checkbox' ${branch.run_once?'checked':''}> Run once</label><div class='field-stack compact-field'><span>Cooldown, sec</span><input data-scenario-branch-field='cooldown_sec' type='number' min='0' step='1' value='${esc(Math.round((Number(branch.cooldown_ms)||0)/1000))}'></div>`);
return `<div class='scenario-branch-settings ${type==='reactive'?'reactive':''}'><div class='field-stack branch-name-field'><span>${type==='reactive'?'Reaction name':'Branch name'}</span><input data-scenario-branch-field='name' placeholder='${type==='reactive'?'Reaction name':'Branch name'}' value='${esc(branch.name||'')}'></div>${typeField}<label class='row-meta branch-toggle'><input data-scenario-branch-field='enabled' type='checkbox' ${branch.enabled!==false?'checked':''}> Enabled</label>${controls}<button class='danger scenario-branch-delete' data-scenario-branch-action='delete' data-branch-index='${index}' ${total<=1?'disabled':''}>Delete</button><details class='scenario-advanced compact-advanced' ${detailsAttrs(branchIdKey,false)}><summary>Branch id</summary><div class='row'><input data-scenario-branch-field='id' placeholder='Branch ID' value='${esc(branch.id||'')}'></div></details></div>`;
}

function applyScenarioBranchAction(action,index){
const wasDirty=!!scenarioEditor.dirty;
const draft=collectScenarioEditor();
draft.branches=Array.isArray(draft.branches)&&draft.branches.length?draft.branches:[defaultScenarioBranch(0,[])];
if(action==='select'){
scenarioEditor.active_branch=Number.isFinite(index)?index:0;
scenarioEditor.expanded_step=-1;
scenarioEditor.draft=draft;
scenarioEditor.dirty=wasDirty;
skipNextScenarioDomSync();
render();
return;
}
if(action==='add'||action==='add_reactive'){
const nextIndex=draft.branches.length;
if(nextIndex>=8){
alert('A scenario can have up to 8 branches.');
return;
}
const branchType=action==='add_reactive'?'reactive':'normal';
const branchId=uniqueScenarioBranchId(draft.branches,branchType);
const branch=defaultScenarioBranch(nextIndex,[],branchType);

branch.id=branchId;
branch.name=defaultScenarioBranchName(branchId,branchType);
if(branchType==='reactive')ensureReactiveV2Branch(branch);
draft.branches.push(branch);
scenarioEditor.active_branch=draft.branches.length-1;
scenarioEditor.expanded_step=-1;
}
else if(action==='delete'){
const removeIndex=Number.isFinite(index)?index:scenarioActiveBranchIndex(draft);
if(draft.branches.length<=1)return;
if(!confirm('Delete this scenario branch?'))return;
draft.branches.splice(removeIndex,1);
scenarioEditor.branch_count_shrink_allowed=true;
scenarioEditor.branch_count_shrink_floor=draft.branches.length;
scenarioEditor.active_branch=Math.max(0,Math.min(removeIndex,draft.branches.length-1));
scenarioEditor.expanded_step=-1;
}
else return;
scenarioEditor.draft=draft;
scenarioEditor.dirty=true;
scenarioEditor.validation_report=null;
skipNextScenarioDomSync();
render();
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
const controls=`<div class='actions compact-actions'><button class='icon-btn' title='${expanded?'Close':'Edit'}' aria-label='${expanded?'Close':'Edit'}' data-scenario-step-action='edit' data-step-index='${index}'>${expanded?'&#10005;':'&#9998;'}</button><button class='icon-btn' title='Move up' aria-label='Move up' data-scenario-step-action='up' data-step-index='${index}' ${index<=0?'disabled':''}>&uarr;</button><button class='icon-btn' title='Move down' aria-label='Move down' data-scenario-step-action='down' data-step-index='${index}' ${index>=total-1?'disabled':''}>&darr;</button><button class='icon-btn danger' title='Delete' aria-label='Delete' data-scenario-step-action='delete' data-step-index='${index}'>&times;</button></div>`;
if(!expanded){
return `<div class='builder-step scenario-step-row scenario-step-${visual} ${validationClass} compact-step' data-scenario-step='${index}'><div class='scenario-step-line'><div class='scenario-step-line-main'><span class='scenario-step-number'>${index+1}.</span><span class='scenario-step-icon'>${scenarioStepIcon(step)}</span><span class='scenario-step-summary'>${esc(summary)}</span><span class='badge scenario-type-badge' title='${esc(fullType)}'>${esc(badge)}</span>${issueBadge}${step.enabled===false?`<span class='badge'>disabled</span>`:''}</div>${controls}</div>${renderScenarioInlineIssues(stepIssues)}</div>`;
}
return `<div class='builder-step scenario-step-row scenario-step-${visual} scenario-step-expanded ${validationClass} compact-step' data-scenario-step='${index}'><div class='scenario-step-line'><div class='scenario-step-line-main'><span class='scenario-step-number'>${index+1}.</span><span class='scenario-step-icon'>${scenarioStepIcon(step)}</span><span class='scenario-step-summary'>${esc(summary)}</span><span class='badge scenario-type-badge' title='${esc(fullType)}'>${esc(badge)}</span>${issueBadge}</div>${controls}</div>${renderScenarioInlineIssues(stepIssues)}<div class='scenario-step-edit'><div class='row compact-row'><input data-step-field='label' placeholder='Step label' value='${esc(step.label||'')}'><select data-step-field='type'>${scenarioTypeOptions(type)}</select><label class='row-meta enabled-inline'><input data-step-field='enabled' type='checkbox' ${step.enabled!==false?'checked':''} style='min-width:auto'> Enabled</label></div>${renderScenarioStepPayload(step,type)}</div></div>`;
}

function collectReactiveActionFromElement(el,previous,index){
const get=name=>{const n=el.querySelector(`[data-step-field='${name}']`);return n?n.value:'';};
const type=get('type')||previous.type||'SET_FLAG';
if(scenarioStepTypeValue(previous)!==scenarioStepTypeValue({type})){
previous=newScenarioStepForType(index,type);
}
let label=get('label')||previous.label||'';
const action={id:previous.id||slugifyId(label||`action_${index+1}`,'action'),label,type};
if(type==='DEVICE_COMMAND'){
action.device_id=get('device_id')||previous.device_id||'';
const device=scenarioDeviceById(action.device_id);
action.command_id=scenarioValidCommandId(device,get('command_id')||previous.command_id||'');
const commandName=scenarioCommandName(action.device_id,action.command_id);
if(!label||label===previous.label||label.indexOf(' - ')>=0){
label=action.device_id==='system_audio'?commandName:`${scenarioRoomNameForDevice(device)}: ${scenarioDeviceName(device)} - ${commandName}`;
action.label=label;
}
const command=scenarioCommandById(action.device_id,action.command_id);
const params=commandSupportsScenarioParams(command)?{...(previous.params&&typeof previous.params==='object'?previous.params:{})}:{};
el.querySelectorAll('[data-step-param]').forEach(input=>{const key=input.dataset.stepParam||'';if(!key)return;const typeAttr=(input.getAttribute('type')||'').toLowerCase();if(input.type==='checkbox')params[key]=input.checked;else if(typeAttr==='number')params[key]=Number(input.value)||0;else params[key]=input.value;});
if(Object.keys(params).length)action.params=params;
}else if(type==='DEVICE_COMMAND_GROUP'){
action.mode=previous.mode||'sequential';
action.commands=[];
el.querySelectorAll('[data-command-group-item]').forEach((item,itemIndex)=>{const deviceField=item.querySelector('[data-group-command-field="device_id"]');const commandField=item.querySelector('[data-group-command-field="command_id"]');const previousItem=Array.isArray(previous.commands)?(previous.commands[itemIndex]||{}):{};const deviceId=(deviceField?deviceField.value:'')||previousItem.device_id||'';const device=scenarioDeviceById(deviceId);const commandId=scenarioValidCommandId(device,(commandField?commandField.value:'')||previousItem.command_id||'');const command=scenarioCommandById(deviceId,commandId);const params=commandSupportsScenarioParams(command)?{...(previousItem.params&&typeof previousItem.params==='object'?previousItem.params:{})}:{};item.querySelectorAll('[data-step-param]').forEach(input=>{const key=input.dataset.stepParam||'';if(!key)return;const typeAttr=(input.getAttribute('type')||'').toLowerCase();if(input.type==='checkbox')params[key]=input.checked;else if(typeAttr==='number')params[key]=Number(input.value)||0;else params[key]=input.value;});const out={device_id:deviceId,command_id:commandId};if(Object.keys(params).length)out.params=params;action.commands.push(out);});
}else if(type==='WAIT_TIME'){
action.duration_ms=get('duration_ms')?durationSecondsToMs(get('duration_ms')):(previous.duration_ms||1000);
}else if(type==='SHOW_OPERATOR_MESSAGE'){
action.message=get('message')||previous.message||'';
}else if(type==='SET_FLAG'){
const valueField=el.querySelector(`[data-step-field='value']`);
action.flag_name=get('flag_name')||previous.flag_name||previous.flag||'';
action.value=valueField?(valueField.type==='checkbox'?valueField.checked:valueField.value!=='false'):(previous.value!==false);
}
return action;
}

function collectReactiveV2BranchFromDom(branch,root){
if(!branch||!root)return branch;
ensureReactiveV2Branch(branch);
const value=(selector,def='')=>{const nodes=Array.from(root.querySelectorAll(selector));if(!nodes.length)return def;const checked=nodes.find(n=>n.type==='radio'&&n.checked);const n=checked||nodes[0];return n?n.value:def;};
branch.priority=Number(value('[data-v2-branch-field="priority"]',branch.priority))||0;
branch.policy=branch.policy&&typeof branch.policy==='object'?branch.policy:{};
branch.policy.mode=value('[data-v2-policy-field="mode"]',branch.policy.mode||'single')||'single';
branch.policy.cooldown_ms=Number(branch.cooldown_ms)||Number(branch.policy.cooldown_ms)||0;
branch.policy.max_fire_count=Math.max(0,Math.round(Number(value('[data-v2-policy-field="max_fire_count"]',branch.policy.max_fire_count||0))||0));
branch.max_fire_count=branch.policy.max_fire_count;
branch.reentry={mode:value('[data-v2-reentry-field="mode"]',branch.reentry&&branch.reentry.mode||'ignore')||'ignore'};
branch.result_policy={
on_done:value('[data-v2-result-field="on_done"]',branch.result_policy&&branch.result_policy.on_done||'continue')||'continue',
on_fail:value('[data-v2-result-field="on_fail"]',branch.result_policy&&branch.result_policy.on_fail||'fail_reaction')||'fail_reaction',
on_timeout:value('[data-v2-result-field="on_timeout"]',branch.result_policy&&branch.result_policy.on_timeout||'fail_reaction')||'fail_reaction'}
;const resultFlag=value('[data-v2-result-field="timeout_flag"]',branch.result_policy&&branch.result_policy.timeout_flag||branch.result_policy&&branch.result_policy.flag||'');if(resultFlag){branch.result_policy.flag=resultFlag;branch.result_policy.timeout_flag=resultFlag;}
const kind=value('[data-v2-trigger-field="kind"]',branch.trigger&&branch.trigger.kind||'device_event')||'device_event';
branch.trigger={kind};
if(kind==='device_event'){branch.trigger.device_id=value('[data-v2-trigger-field="device_id"]',branch.trigger.device_id||'');const device=scenarioDeviceById(branch.trigger.device_id);branch.trigger.event_id=scenarioValidEventId(device,value('[data-v2-trigger-field="event_id"]',branch.trigger.event_id||''));}
else if(kind==='flag_changed')branch.trigger.flag_name=value('[data-v2-trigger-field="flag_name"]',branch.trigger.flag_name||'');
else if(kind==='operator_event')branch.trigger.operator_event=value('[data-v2-trigger-field="operator_event"]',branch.trigger.operator_event||'');
else if(kind==='runtime_event')branch.trigger.runtime_event=value('[data-v2-trigger-field="runtime_event"]',branch.trigger.runtime_event||'');
branch.guard_flags=[];
root.querySelectorAll('[data-v2-guard-item]').forEach(item=>{const name=(item.querySelector('[data-v2-guard-field="flag"]')||{}).value||'';const val=(item.querySelector('[data-v2-guard-field="value"]')||{}).value;if(name)branch.guard_flags.push({flag:name,value:val!=='false'});});
const previousVariants=Array.isArray(branch.variants)?branch.variants.map(variant=>JSON.parse(JSON.stringify(variant))):[];
branch.variants=[];
root.querySelectorAll('[data-v2-variant]').forEach((variantEl,variantIndex)=>{const id=(variantEl.querySelector('[data-v2-variant-field="id"]')||{}).value||`variant_${variantIndex+1}`;const label=(variantEl.querySelector('[data-v2-variant-field="label"]')||{}).value||`Variant ${variantIndex+1}`;const previous=previousVariants[variantIndex]||{};const variant={id,label,actions:[]};variantEl.querySelectorAll('[data-v2-action]').forEach((actionEl,actionIndex)=>{const previousAction=Array.isArray(previous.actions)?(previous.actions[actionIndex]||{}):{};variant.actions.push(collectReactiveActionFromElement(actionEl,previousAction,actionIndex));});branch.variants.push(variant);});
if(!branch.variants.length)branch.variants=defaultReactiveV2BranchFields().variants;
branch.steps=[];
if(!Array.isArray(branch.on_complete)||!branch.on_complete.length)delete branch.on_complete;
return branch;
}

function scenarioEditorSource(){
const roomId=scenarioEditor.room_id;
let source=null;
if(scenarioEditor.draft&&scenarioEditor.draft.room_id===roomId)source=JSON.parse(JSON.stringify(scenarioEditor.draft));
else{
const editing=roomScenarios(roomId).find(s=>s.id===scenarioEditor.scenario_id)||null;
source=scenarioEditableJson(editing,roomId);
}
return scenarioRestoreMissingOriginalBranches(source);
}

function collectScenarioEditor(){
const source=scenarioEditorSource();
if(!Array.isArray(source.branches)||!source.branches.length)source.branches=normalizeScenarioBranches(source);
const editor=document.querySelector('[data-scenario-editor]');
const renderedBranchIndex=Number(editor&&editor.dataset.activeBranchIndex);
const branchIndex=Number.isFinite(renderedBranchIndex)
  ? Math.max(0,Math.min(source.branches.length-1,Math.floor(renderedBranchIndex)))
  : scenarioActiveBranchIndex(source);
const branches=source.branches.map((branch,index)=>({
...normalizeScenarioBranch(branch,index),
steps:Array.isArray(branch.steps)?branch.steps.map(step=>JSON.parse(JSON.stringify(step))):[]}
));
const activeBranch=branches[branchIndex]||branches[0];
const root=editor||document;
const branchName=root.querySelector('[data-scenario-branch-field="name"]');
const branchId=root.querySelector('[data-scenario-branch-field="id"]');
const branchType=root.querySelector('[data-scenario-branch-field="type"]');
const branchEnabled=root.querySelector('[data-scenario-branch-field="enabled"]');
const branchRequired=root.querySelector('[data-scenario-branch-field="required_for_completion"]');
const branchCooldown=root.querySelector('[data-scenario-branch-field="cooldown_sec"]');
const branchRunOnce=root.querySelector('[data-scenario-branch-field="run_once"]');
const previousActiveSteps=activeBranch&&Array.isArray(activeBranch.steps)?activeBranch.steps.map(step=>JSON.parse(JSON.stringify(step))):[];
if(activeBranch){
activeBranch.name=(branchName&&branchName.value)||activeBranch.name||`Branch ${branchIndex+1}`;
activeBranch.id=(branchId&&branchId.value)||activeBranch.id||slugifyId(activeBranch.name,`branch_${branchIndex+1}`);
activeBranch.type=branchType?scenarioBranchTypeValue({type:branchType.value}):scenarioBranchTypeValue(activeBranch);
activeBranch.enabled=branchEnabled?branchEnabled.checked:activeBranch.enabled!==false;
activeBranch.required_for_completion=activeBranch.type==='normal'&&(branchRequired?branchRequired.checked:activeBranch.required_for_completion!==false);
activeBranch.cooldown_ms=activeBranch.type==='reactive'?Math.max(0,Math.round(Number(branchCooldown&&branchCooldown.value)||0))*1000:0;
if(activeBranch.type==='reactive'&&activeBranch.policy&&typeof activeBranch.policy==='object')activeBranch.policy.cooldown_ms=activeBranch.cooldown_ms;
activeBranch.run_once=activeBranch.type==='reactive'&&!!(branchRunOnce&&branchRunOnce.checked);
activeBranch.steps=[];
if(scenarioIsReactiveV2Branch(activeBranch)){
collectReactiveV2BranchFromDom(activeBranch,root);
}
}
const scenario={
id:(root.querySelector('#scenario_id')||{}).value||'',
name:(root.querySelector('#scenario_name')||{}).value||'',
room_id:scenarioEditor.room_id,
branches}
;

const stepsPanel=root.querySelector('.scenario-steps-panel');
if(!scenarioIsReactiveV2Branch(activeBranch))(stepsPanel?stepsPanel.querySelectorAll('[data-scenario-step]'):[]).forEach((el,index)=>{
const previous=previousActiveSteps[index]?JSON.parse(JSON.stringify(previousActiveSteps[index])):{};
if(!el.querySelector(`[data-step-field='type']`)&&previous.type){
if(activeBranch)activeBranch.steps.push(previous);
return;
}
const get=name=>{
const n=el.querySelector(`[data-step-field='${name}']`);return n?n.value:'';}
;const enabled=el.querySelector(`[data-step-field='enabled']`);const type=get('type')||previous.type||'WAIT_TIME';const label=get('label')||previous.label||'';const step={
id:previous.id||slugifyId(label||`step_${index+1}`,'step'),label,enabled:enabled?enabled.checked:(previous.enabled!==false),type}
;if(type==='DEVICE_COMMAND'){
step.device_id=get('device_id')||previous.device_id||'';step.command_id=get('command_id')||previous.command_id||'';const command=scenarioCommandById(step.device_id,step.command_id);const params=commandSupportsScenarioParams(command)?{...(previous.params&&typeof previous.params==='object'?previous.params:{})}:{};el.querySelectorAll('[data-step-param]').forEach(input=>{const key=input.dataset.stepParam||'';if(!key)return;const typeAttr=(input.getAttribute('type')||'').toLowerCase();if(input.type==='checkbox')params[key]=input.checked;else if(typeAttr==='number')params[key]=Number(input.value)||0;else params[key]=input.value;});if(step.device_id==='system_audio'&&step.command_id==='play'&&params.channel!=='background')params.repeat=false;if(Object.keys(params).length)step.params=params;else delete step.params;}
else if(type==='DEVICE_COMMAND_GROUP'){
const renderedItems=el.querySelectorAll('[data-command-group-item]');
step.commands=[];
if(!renderedItems.length&&Array.isArray(previous.commands))step.commands=previous.commands.map(cmd=>({device_id:cmd.device_id||'',command_id:cmd.command_id||'',params:cmd.params&&typeof cmd.params==='object'?cmd.params:{}}));
renderedItems.forEach((item,itemIndex)=>{const deviceField=item.querySelector('[data-group-command-field="device_id"]');const commandField=item.querySelector('[data-group-command-field="command_id"]');const previousItem=Array.isArray(previous.commands)?(previous.commands[itemIndex]||{}):{};const deviceId=(deviceField?deviceField.value:'')||previousItem.device_id||'';const commandId=(commandField?commandField.value:'')||previousItem.command_id||'';const command=scenarioCommandById(deviceId,commandId);const params=commandSupportsScenarioParams(command)?{...(previousItem.params&&typeof previousItem.params==='object'?previousItem.params:{})}:{};item.querySelectorAll('[data-step-param]').forEach(input=>{const key=input.dataset.stepParam||'';if(!key)return;const typeAttr=(input.getAttribute('type')||'').toLowerCase();if(input.type==='checkbox')params[key]=input.checked;else if(typeAttr==='number')params[key]=Number(input.value)||0;else params[key]=input.value;});if(deviceId==='system_audio'&&commandId==='play'&&params.channel!=='background')params.repeat=false;const out={device_id:deviceId,command_id:commandId};if(Object.keys(params).length)out.params=params;step.commands.push(out);});}
else if(type==='WAIT_DEVICE_EVENT'){
step.device_id=get('device_id')||previous.device_id||'';step.event_id=get('event_id')||previous.event_id||'';
const timeout=get('timeout_ms');step.timeout_ms=timeout!==''?durationSecondsToMs(timeout):0;
step.timeout_message=get('timeout_message');}
else if(type==='WAIT_ANY_DEVICE_EVENT'){
const renderedItems=el.querySelectorAll('[data-event-group-item]');
step.events=[];
if(!renderedItems.length&&Array.isArray(previous.events))step.events=previous.events.map(ev=>({device_id:ev.device_id||'',event_id:ev.event_id||''}));
renderedItems.forEach((item,itemIndex)=>{const deviceField=item.querySelector('[data-event-group-field="device_id"]');const eventField=item.querySelector('[data-event-group-field="event_id"]');const previousItem=Array.isArray(previous.events)?(previous.events[itemIndex]||{}):{};step.events.push({device_id:(deviceField?deviceField.value:'')||previousItem.device_id||'',event_id:(eventField?eventField.value:'')||previousItem.event_id||''});});
}
else if(type==='WAIT_ALL_DEVICE_EVENTS'){
const renderedItems=el.querySelectorAll('[data-event-group-item]');
step.events=[];
if(!renderedItems.length&&Array.isArray(previous.events))step.events=previous.events.map(ev=>({device_id:ev.device_id||'',event_id:ev.event_id||''}));
renderedItems.forEach((item,itemIndex)=>{const deviceField=item.querySelector('[data-event-group-field="device_id"]');const eventField=item.querySelector('[data-event-group-field="event_id"]');const previousItem=Array.isArray(previous.events)?(previous.events[itemIndex]||{}):{};step.events.push({device_id:(deviceField?deviceField.value:'')||previousItem.device_id||'',event_id:(eventField?eventField.value:'')||previousItem.event_id||''});});
}
else if(type==='WAIT_TIME'){
step.duration_ms=get('duration_ms')?durationSecondsToMs(get('duration_ms')):(previous.duration_ms||1000);}
else if(type==='OPERATOR_APPROVAL'){
step.prompt=get('prompt')||previous.prompt||previous.operator_prompt||'';step.approve_label=get('approve_label')||previous.approve_label||previous.operator_approve_label||'Continue';}
else if(type==='SHOW_OPERATOR_MESSAGE'){
step.message=get('message')||previous.message||'';}
else if(type==='SET_FLAG'){
const valueField=el.querySelector(`[data-step-field='value']`);
step.flag_name=get('flag_name')||previous.flag_name||'';
step.value=valueField?(valueField.type==='checkbox'?valueField.checked:valueField.value!=='false'):(previous.value!==false);}
else if(type==='WAIT_FLAGS'){
const renderedItems=el.querySelectorAll('[data-flag-list-item]');
step.flags=[];
if(!renderedItems.length&&Array.isArray(previous.flags))step.flags=previous.flags.map(normalizeScenarioFlagItem);
renderedItems.forEach((item,itemIndex)=>{const nameField=item.querySelector('[data-flag-list-field="flag_name"]');const valueField=item.querySelector('[data-flag-list-field="value"]');const previousItem=Array.isArray(previous.flags)?normalizeScenarioFlagItem(previous.flags[itemIndex]||{}):defaultScenarioFlagItem();step.flags.push({flag_name:(nameField?nameField.value:'')||previousItem.flag_name||'',value:valueField?(valueField.type==='checkbox'?valueField.checked:valueField.value!=='false'):(previousItem.value!==false)});});
const timeout=get('timeout_ms');step.timeout_ms=timeout!==''?durationSecondsToMs(timeout):0;
step.timeout_message=get('timeout_message');
}
if(scenarioStepIsWaitType(type)){
const skipField=el.querySelector(`[data-step-field='allow_operator_skip']`);
step.allow_operator_skip=skipField?skipField.checked:!!previous.allow_operator_skip;
step.operator_skip_label=get('operator_skip_label')||previous.operator_skip_label||'';
if(!step.allow_operator_skip)delete step.operator_skip_label;
}
if(activeBranch)activeBranch.steps.push(step);}
);
if(!scenario.id&&scenario.name)scenario.id=slugifyId(scenario.name,'scenario');
return scenario;
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

function applyReactiveV2Action(action,variantIndex,actionIndex,actionType){
const draft=collectScenarioEditor();
const branch=scenarioActiveBranch(draft);
if(!scenarioIsReactiveV2Branch(branch))return;
ensureReactiveV2Branch(branch);
variantIndex=Number.isFinite(Number(variantIndex))?Number(variantIndex):0;
actionIndex=Number.isFinite(Number(actionIndex))?Number(actionIndex):0;
if(action==='add_guard'){
branch.guard_flags=Array.isArray(branch.guard_flags)?branch.guard_flags:[];
branch.guard_flags.push({flag:'puzzle_done',value:true});
}else if(action==='delete_guard'){
branch.guard_flags=Array.isArray(branch.guard_flags)?branch.guard_flags:[];
branch.guard_flags.splice(actionIndex,1);
}else if(action==='add_variant'){
branch.variants=Array.isArray(branch.variants)?branch.variants:[];
const n=branch.variants.length+1;
const mode=String(branch.policy&&branch.policy.mode||'single');
branch.variants.push({id:`variant_${n}`,label:mode==='escalate'?`Level ${n}`:`Variant ${n}`,actions:[]});
}else if(action==='delete_variant'){
branch.variants=Array.isArray(branch.variants)?branch.variants:[];
if(branch.variants.length>1)branch.variants.splice(variantIndex,1);
}else if(action==='add_action'){
const variant=branch.variants[variantIndex];
if(variant){
variant.actions=Array.isArray(variant.actions)?variant.actions:[];
variant.actions.push(newScenarioStepForType(variant.actions.length,actionType||'DEVICE_COMMAND'));
}
}else if(action==='delete_action'){
const variant=branch.variants[variantIndex];
if(variant){
variant.actions=Array.isArray(variant.actions)?variant.actions:[];
variant.actions.splice(actionIndex,1);
}
}else if(action==='group_add'||action==='group_delete'){
const variant=branch.variants[variantIndex];
const item=variant&&Array.isArray(variant.actions)?variant.actions[actionIndex]:null;
if(item&&scenarioStepTypeValue(item)==='DEVICE_COMMAND_GROUP'){
item.commands=Array.isArray(item.commands)?item.commands:[];
if(action==='group_add'){
item.commands.push(defaultScenarioCommandItem());
}else{
const commandIndex=Number.isFinite(Number(actionType))?Number(actionType):0;
item.commands.splice(commandIndex,1);
if(!item.commands.length)item.commands.push(defaultScenarioCommandItem());
}
}
}
scenarioEditor.draft=draft;
scenarioEditor.dirty=true;
scenarioEditor.validation_report=null;
skipNextScenarioDomSync();
render();
}

function renderScenariosAdminView(){
setPage('Scenarios','Room scenario editor');
const rooms=(gmState&&Array.isArray(gmState.rooms))?gmState.rooms:[];
if(!rooms.length)return `<div class='card empty'>No rooms available</div>`;
if(!scenarioEditor.room_id||!rooms.some(r=>r.room_id===scenarioEditor.room_id)){
scenarioEditor.room_id=rooms[0].room_id;
}
const roomId=scenarioEditor.room_id;
const scenarios=roomScenarios(roomId);
const editing=scenarios.find(s=>s.id===scenarioEditor.scenario_id)||null;
const editorOpen=!!(scenarioEditor.open||editing||scenarioEditor.dirty);
if(editing&&!scenarioEditor.original_scenario){
scenarioEditor.original_scenario=scenarioEditableJson(editing,roomId);
}
const base=editorOpen?scenarioEditorSource():scenarioEditableJson(editing,roomId);
if(!Array.isArray(base.branches)||!base.branches.length)base.branches=normalizeScenarioBranches(base);
const activeBranchIndex=scenarioActiveBranchIndex(base);
scenarioEditor.active_branch=activeBranchIndex;
const activeBranch=scenarioActiveBranch(base);
const activeSteps=scenarioActiveSteps(base);
if(!Number.isFinite(Number(scenarioEditor.expanded_step)))scenarioEditor.expanded_step=-1;
if(scenarioEditor.expanded_step>=activeSteps.length)scenarioEditor.expanded_step=-1;
const json=JSON.stringify(base,null,2);
const issues=editing&&Array.isArray(editing.validation_issues)?editing.validation_issues:[];
const activeIssues=scenarioActiveValidationIssues(issues);
const totalStepCount=scenarioTotalStepCount(base.branches);
const issuesByStep=scenarioIssuesForBranch(activeIssues,base.branches,activeBranchIndex);
const issueHtml=renderScenarioValidationSummary(activeIssues,totalStepCount);
const rows=scenarios.length?scenarios.map(s=>`<div class='row-card'><div class='row-main'><div class='row-title'>${esc(s.name||s.id)} ${s.valid===false?`<span class='badge'>invalid</span>`:''}</div><div class='row-meta'>${esc(s.step_count||0)} steps / ${esc(Array.isArray(s.branches)?s.branches.length:1)} branch${(Array.isArray(s.branches)&&s.branches.length===1)?'':'es'} / ${esc(scenarioValidationText(s))}</div></div><div class='actions'><button data-scenario-edit='${esc(s.id)}'>Edit</button><button data-scenario-mode='${esc(s.id)}'>Create game mode</button><button class='danger' data-scenario-delete='${esc(s.id)}'>Delete</button></div></div>`).join(''):`<div class='card empty'>No scenarios for this room</div>`;
const scenarioIdKey=`scenario:id:${roomId}:${base.id||'new'}`;
const jsonKey=`scenario:json:${roomId}:${base.id||'new'}`;
const emptyStepsText=scenarioBranchTypeValue(activeBranch)==='reactive'?'Add a trigger first. This reaction will listen for it, then run the actions you add after it.':'No steps yet';
const activeBranchIsV2=scenarioIsReactiveV2Branch(activeBranch);
const branchEditorBody=activeBranchIsV2
?renderReactiveV2Editor(activeBranch)
:`<section class='scenario-steps-panel'><h2 class='section-title'>Steps: ${esc(activeBranch&&activeBranch.name||'Branch')}</h2><div>${activeSteps.length?activeSteps.map((step,i)=>renderScenarioStepEditor(step,i,activeSteps.length,Number(scenarioEditor.expanded_step)===i,issuesByStep[i]||[])).join(''):`<div class='empty'>${esc(emptyStepsText)}</div>`}</div></section>`;
const editorHtml=editorOpen?`<div class='card scenario-editor-card' data-scenario-editor='1' data-active-branch-index='${activeBranchIndex}'><div class='scenario-editor-head'><div><h2 class='section-title'>${editing?'Edit scenario':'New scenario'}${scenarioEditor.dirty?' *':''}</h2><input id='scenario_name' placeholder='Scenario name' value='${esc(base.name||'')}'></div><div class='actions'><button data-scenario-validate='1'>Validate</button><button data-scenario-save='1'>Save</button></div></div><details class='scenario-advanced compact-advanced' ${detailsAttrs(scenarioIdKey,false)}><summary>Scenario id</summary><div class='row'><input id='scenario_id' placeholder='Scenario ID' value='${esc(base.id||'')}'></div></details>${issueHtml}${renderScenarioBranchTabs(base,activeBranchIndex)}${renderScenarioBranchSettings(activeBranch,activeBranchIndex,base.branches.length)}<div class='scenario-editor-layout ${activeBranchIsV2?'scenario-editor-layout-v2':''}'>${activeBranchIsV2?'':`<aside class='scenario-add-panel'>${scenarioStepPresetButtons(activeBranch)}</aside>`}${branchEditorBody}</div><details style='margin-top:10px' ${detailsAttrs(jsonKey,false)}><summary class='row-meta'>Debug JSON</summary><textarea id='scenario_json' class='builder-json' readonly>${esc(json)}</textarea></details></div>`:`<div class='card empty'><h2 class='section-title'>Scenario editor</h2><div class='row-meta'>Select a scenario or create a new one.</div></div>`;
return `<div class='scenario-room-bar'><div><span class='row-meta'>Room</span><select class='scenario-select' data-scenario-room-select>${rooms.map(r=>`<option value='${esc(r.room_id)}' ${
r.room_id===roomId?'selected':''}
>${
esc(r.title||r.room_id)}
</option>`).join('')}</select></div><div class='row-meta'>Steps can target devices in any room.</div></div><div class='scenario-admin-layout'><section><div class='card-head'><h2 class='section-title'>Scenarios</h2><div class='actions'><button data-scenario-new='1'>Add scenario</button></div></div><div class='list'>${rows}</div></section><section>${editorHtml}</section></div>`;
}
