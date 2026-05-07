// GM panel source part. Edit this file, then rebuild gm_panel.js.
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

function normalizeReactiveV2RepeatPolicy(branch){
if(!branch)return branch;
branch.policy=branch.policy&&typeof branch.policy==='object'?branch.policy:{};
branch.policy.mode=branch.policy.mode||'single';
if(branch.policy.mode==='single'){
branch.run_once=!!branch.run_once;
branch.max_fire_count=branch.run_once?1:0;
branch.policy.max_fire_count=branch.max_fire_count;
}
return branch;
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
return normalizeReactiveV2RepeatPolicy(branch);
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
const repeat=mode==='single'?`<div class='scenario-v2-repeat-choice'><label class='field-stack'><span>Trigger behavior</span><select data-scenario-branch-field='run_once'><option value='false' ${branch.run_once?'':'selected'}>Can repeat</option><option value='true' ${branch.run_once?'selected':''}>Run once</option></select></label></div>`:'';
return `<section class='scenario-v2-type'><div class='scenario-v2-type-title'>Reaction type</div><div class='scenario-v2-type-grid'>${item('single','Same actions','Run the same action list on every trigger.')}${item('escalate','Escalate','Run level 1, then level 2, then the next levels.')}${item('rotate','Rotate','Cycle through variants on each trigger.')}${item('random','Random','Pick one variant randomly.')}</div>${repeat}</section>`;
}

function renderReactiveV2Policy(branch){
const policy=branch.policy||{};
const reentry=branch.reentry||{};
const result=branch.result_policy||{};
const reentryMode=String(reentry.mode||'ignore');
const mode=String(policy.mode||'single');
const isSingle=mode==='single';
const isEscalate=mode==='escalate';
const resultAction=value=>['continue','set_flag','fail_reaction','fail_scenario','retry'].map(item=>`<option value='${item}' ${String(value||'')===item?'selected':''}>${item}</option>`).join('');
const runOnceAdvanced=isSingle?'':`<label class='row-meta branch-toggle'><input data-scenario-branch-field='run_once' type='checkbox' ${branch.run_once?'checked':''}> Run once</label>`;
return `<details class='scenario-advanced scenario-v2-settings'><summary>Advanced reaction settings</summary><div class='scenario-v2-settings-grid'><label class='field-stack'><span>Cooldown, sec</span><input data-scenario-branch-field='cooldown_sec' type='number' min='0' step='1' value='${esc(Math.round((Number(branch.cooldown_ms)||0)/1000))}'></label>${runOnceAdvanced}<label class='field-stack'><span>Reentry while running</span><select data-v2-reentry-field='mode'><option value='ignore' ${reentryMode==='ignore'?'selected':''}>ignore</option><option value='queue_one' ${reentryMode==='queue_one'?'selected':''}>queue_one</option></select></label>${isEscalate||isSingle?'':`<label class='field-stack'><span>Max fires</span><input data-v2-policy-field='max_fire_count' type='number' min='0' step='1' value='${esc(Number(policy.max_fire_count)||Number(branch.max_fire_count)||0)}'></label>`}<label class='field-stack'><span>Priority</span><input data-v2-branch-field='priority' type='number' step='1' value='${esc(Number(branch.priority)||0)}'></label><label class='field-stack'><span>On done</span><select data-v2-result-field='on_done'>${resultAction(result.on_done||'continue')}</select></label><label class='field-stack'><span>On fail</span><select data-v2-result-field='on_fail'>${resultAction(result.on_fail||'fail_reaction')}</select></label><label class='field-stack'><span>On timeout</span><select data-v2-result-field='on_timeout'>${resultAction(result.on_timeout||'fail_reaction')}</select></label><label class='field-stack'><span>Result flag</span>${renderScenarioFlagInput(result.timeout_flag||result.flag||'',`data-v2-result-field='timeout_flag'`)}</label></div></details>`;
}

function renderReactiveV2Guards(branch){
const guards=Array.isArray(branch.guard_flags)?branch.guard_flags:[];
return `<section class='scenario-v2-rule'><div class='scenario-v2-rule-label'>If</div><div class='scenario-v2-rule-body'><div class='scenario-v2-guard-list'>${guards.length?guards.map((guard,index)=>`<div class='scenario-v2-guard' data-v2-guard-item='${index}'><span class='row-meta'>Flag</span>${renderScenarioFlagInput(guard.flag||guard.flag_name||guard.name||'',`data-v2-guard-field='flag'`)}<select data-v2-guard-field='value'><option value='true' ${guard.value!==false?'selected':''}>is true</option><option value='false' ${guard.value===false?'selected':''}>is false</option></select><button class='icon-btn danger' data-action='scenario.reactive_v2' data-op='delete_guard' data-guard-index='${index}'>&times;</button></div>`).join(''):`<div class='empty compact-empty'>No guard flags. The reaction can run whenever the trigger arrives.</div>`}</div>${uiButton({label:'Add condition',action:'scenario.reactive_v2',dataset:{op:'add_guard'}})}</div></section>`;
}

function renderReactiveV2Action(action,variantIndex,actionIndex){
const type=scenarioStepTypeValue(action);
const summary=scenarioStepSummaryText(action);
const key=reactiveV2ActionKey(variantIndex,actionIndex);
const expanded=scenarioEditor.expanded_v2_action===key;
const controls=`<button class='icon-btn' data-action='scenario.reactive_v2' data-op='edit_action' data-variant-index='${variantIndex}' data-action-index='${actionIndex}' title='Edit'>${expanded?'&times;':'&#9998;'}</button><button class='icon-btn danger' data-action='scenario.reactive_v2' data-op='delete_action' data-variant-index='${variantIndex}' data-action-index='${actionIndex}'>&times;</button>`;
const body=expanded?`<div class='scenario-step-edit'><div class='scenario-v2-action-fields'><input data-step-field='label' placeholder='Action label' value='${esc(action.label||'')}'><select data-step-field='type'>${reactiveV2ActionTypeOptions(type)}</select></div>${renderScenarioStepPayload(action,type)}</div>`:'';
return `<div class='builder-step scenario-step-row scenario-step-${scenarioStepVisualType(action)} compact-step scenario-v2-action ${expanded?'scenario-step-expanded':''}' data-v2-action='${actionIndex}' data-variant-index='${variantIndex}'><div class='scenario-step-line'><div class='scenario-step-line-main'><span class='scenario-step-number'>${actionIndex+1}.</span><span class='scenario-step-icon'>${scenarioStepIcon(action)}</span><span class='scenario-step-summary'>${esc(summary)}</span><span class='badge scenario-type-badge'>${esc(scenarioStepBadgeLabel(action))}</span></div><div class='actions compact-actions'>${controls}</div></div>${body}</div>`;
}

function reactiveV2ActionKey(variantIndex,actionIndex){
return `${Number(variantIndex)||0}:${Number(actionIndex)||0}`;
}

function renderReactiveV2ActionAddButtons(variantIndex){
return `<div class='scenario-v2-action-add'>${uiButton({label:'Add device command',action:'scenario.reactive_v2',dataset:{op:'add_action','action-type':'DEVICE_COMMAND','variant-index':variantIndex}})}${uiButton({label:'Add wait',action:'scenario.reactive_v2',dataset:{op:'add_action','action-type':'WAIT_TIME','variant-index':variantIndex}})}${uiButton({label:'Add flag',action:'scenario.reactive_v2',dataset:{op:'add_action','action-type':'SET_FLAG','variant-index':variantIndex}})}${uiButton({label:'Add message',action:'scenario.reactive_v2',dataset:{op:'add_action','action-type':'SHOW_OPERATOR_MESSAGE','variant-index':variantIndex}})}</div>`;
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
return `<section class='scenario-v2-rule scenario-v2-then'><div class='scenario-v2-rule-label'>Then</div><div class='scenario-v2-rule-body'><div class='scenario-v2-subtitle-row'><div class='scenario-v2-subtitle'>${esc(title)}</div>${escalateControls}</div><div class='scenario-v2-variant-list'>${shown.map((variant,index)=>{const originalIndex=isSingle?0:index;const label=isEscalate?`Level ${index+1}`:(isSingle?'Actions':`Variant ${index+1}`);const nameValue=isSingle?'Actions':(variant.label||variant.name||label);return `<div class='scenario-v2-variant' data-v2-variant='${originalIndex}'><div class='scenario-v2-variant-head'><label class='field-stack'><span>${esc(label)}</span><input data-v2-variant-field='label' placeholder='${esc(label)} label' value='${esc(nameValue)}' ${isSingle?'readonly':''}></label><div class='actions'>${isSingle?'':uiButton({label:`Delete ${isEscalate?'level':'variant'}`,kind:'danger',action:'scenario.reactive_v2',dataset:{op:'delete_variant','variant-index':originalIndex},disabled:variants.length<=1})}</div></div><details class='scenario-advanced compact-advanced scenario-v2-variant-id'><summary>${esc(isEscalate?'Level id':'Variant id')}</summary><div class='row'><input data-v2-variant-field='id' placeholder='Variant ID' value='${esc(variant.id||'')}'></div></details><div class='scenario-v2-action-list'>${(Array.isArray(variant.actions)?variant.actions:[]).map((action,actionIndex)=>renderReactiveV2Action(action,originalIndex,actionIndex)).join('')||`<div class='empty'>No actions yet. Add one or more actions below.</div>`}</div>${renderReactiveV2ActionAddButtons(originalIndex)}</div>`;}).join('')}</div>${isSingle?'':uiButton({label:addLabel,action:'scenario.reactive_v2',dataset:{op:'add_variant'}})}</div></section>`;
}

function renderReactiveV2Editor(branch){
ensureReactiveV2Branch(branch);
return `<div class='scenario-v2-editor'>${renderReactiveV2Type(branch)}${renderReactiveV2Trigger(branch)}${renderReactiveV2Guards(branch)}${renderReactiveV2Variants(branch)}${renderReactiveV2Policy(branch)}</div>`;
}

function collectReactiveActionFromElement(el,previous,index){
if(!el.querySelector(`[data-step-field='type']`)&&previous&&previous.type)return JSON.parse(JSON.stringify(previous));
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
const runOnceField=root.querySelector('[data-scenario-branch-field="run_once"]');
branch.priority=Number(value('[data-v2-branch-field="priority"]',branch.priority))||0;
branch.policy=branch.policy&&typeof branch.policy==='object'?branch.policy:{};
branch.policy.mode=value('[data-v2-policy-field="mode"]',branch.policy.mode||'single')||'single';
if(branch.policy.mode==='single'&&runOnceField){
branch.run_once=runOnceField.type==='checkbox'?!!runOnceField.checked:String(runOnceField.value)==='true';
}
branch.policy.cooldown_ms=Number(branch.cooldown_ms)||Number(branch.policy.cooldown_ms)||0;
branch.policy.max_fire_count=Math.max(0,Math.round(Number(value('[data-v2-policy-field="max_fire_count"]',branch.policy.max_fire_count||0))||0));
branch.max_fire_count=branch.policy.max_fire_count;
normalizeReactiveV2RepeatPolicy(branch);
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
return normalizeReactiveV2RepeatPolicy(branch);
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
scenarioEditor.expanded_v2_action='';
}else if(action==='add_action'){
const variant=branch.variants[variantIndex];
if(variant){
variant.actions=Array.isArray(variant.actions)?variant.actions:[];
variant.actions.push(newScenarioStepForType(variant.actions.length,actionType||'DEVICE_COMMAND'));
scenarioEditor.expanded_v2_action=reactiveV2ActionKey(variantIndex,variant.actions.length-1);
}
}else if(action==='delete_action'){
const variant=branch.variants[variantIndex];
if(variant){
variant.actions=Array.isArray(variant.actions)?variant.actions:[];
variant.actions.splice(actionIndex,1);
scenarioEditor.expanded_v2_action='';
}
}else if(action==='edit_action'){
const key=reactiveV2ActionKey(variantIndex,actionIndex);
scenarioEditor.expanded_v2_action=scenarioEditor.expanded_v2_action===key?'':key;
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
