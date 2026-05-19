// GM panel source part. Edit this file, then rebuild gm_panel.js.
function scenarioIsReactiveV2Branch(branch){
return scenarioBranchTypeValue(branch)==='reactive';
}

function reactiveV2ActionTypes(){
return ['DEVICE_COMMAND','DEVICE_COMMAND_GROUP','WAIT_TIME','SET_FLAG','SHOW_OPERATOR_MESSAGE'];
}

function reactiveV2ActionTypeOptions(type){
const selected=scenarioStepTypeValue({type});
const types=reactiveV2ActionTypes();
const all=types.includes(selected)?types:[selected].concat(types);
return all.map(t=>`<option value='${esc(t)}' ${selected===t?'selected':''}>${esc(scenarioStepTypeLabel(t))}</option>`).join('');
}

function defaultReactiveV2Trigger(){
return {kind:'device_event',device_id:'',event_id:''};
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
const triggerKind=String(branch.trigger.kind||'device_event');
if((triggerKind==='operator_event'||triggerKind==='runtime_event')&&!branch.trigger.event_id){
branch.trigger.event_id=branch.trigger.operator_event||branch.trigger.runtime_event||'';
}
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
branch.variants=branch.variants.map((variant,index)=>({
id:variant&&variant.id||`variant_${index+1}`,
label:variant&&variant.label||variant&&variant.name||(index===0?'Actions':`Variant ${index+1}`),
actions:Array.isArray(variant&&variant.actions)?variant.actions.map(normalizeScenarioEditorStep):[]
}));
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
let selectedDevice=trigger.device_id||'';
const device=scenarioDeviceById(selectedDevice)||null;
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
body=`<div class='scenario-v2-inline-fields'><input data-v2-trigger-field='event_id' placeholder='Operator event' value='${esc(trigger.event_id||trigger.operator_event||'')}'></div>`;
}else{
body=`<div class='scenario-v2-inline-fields'><input data-v2-trigger-field='event_id' placeholder='Runtime event' value='${esc(trigger.event_id||trigger.runtime_event||'')}'></div>`;
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

function renderReactiveV2Action(action,variantIndex,actionIndex,issues){
const type=scenarioStepTypeValue(action);
const summary=scenarioStepSummaryText(action);
const key=reactiveV2ActionKey(variantIndex,actionIndex);
const expanded=scenarioEditor.expanded_v2_action===key;
const actionIssues=Array.isArray(issues)?issues:[];
const hasErrors=actionIssues.some(scenarioIssueIsError);
const validationClass=actionIssues.length?(hasErrors?'scenario-step-invalid':'scenario-step-warning'):'';
const issueBadge=actionIssues.length?`<span class='badge'>${esc(`Error ${actionIssues.length}`)}</span>`:'';
const controls=`<button class='icon-btn' data-action='scenario.reactive_v2' data-op='edit_action' data-variant-index='${variantIndex}' data-action-index='${actionIndex}' title='Edit'>${expanded?'&times;':'&#9998;'}</button><button class='icon-btn danger' data-action='scenario.reactive_v2' data-op='delete_action' data-variant-index='${variantIndex}' data-action-index='${actionIndex}'>&times;</button>`;
const body=expanded?`<div class='scenario-step-edit'><div class='scenario-v2-action-fields'><input data-step-field='label' placeholder='Action label' value='${esc(action.label||'')}'><select data-step-field='type'>${reactiveV2ActionTypeOptions(type)}</select></div>${renderScenarioStepPayload(action,type)}</div>`:'';
return `<div class='builder-step scenario-step-row scenario-step-${scenarioStepVisualType(action)} compact-step scenario-v2-action ${validationClass} ${expanded?'scenario-step-expanded':''}' data-v2-action='${actionIndex}' data-variant-index='${variantIndex}'><div class='scenario-step-line'><div class='scenario-step-line-main'><span class='scenario-step-number'>${actionIndex+1}.</span><span class='scenario-step-icon'>${scenarioStepIcon(action)}</span><span class='scenario-step-summary'>${esc(summary)}</span><span class='badge scenario-type-badge'>${esc(scenarioStepBadgeLabel(action))}</span>${issueBadge}</div><div class='actions compact-actions'>${controls}</div></div>${renderScenarioInlineIssues(actionIssues)}${body}</div>`;
}

function reactiveV2ActionKey(variantIndex,actionIndex){
return `${Number(variantIndex)||0}:${Number(actionIndex)||0}`;
}

function renderReactiveV2ActionAddButtons(variantIndex){
return `<div class='scenario-v2-action-add'>${uiButton({label:'Add device command',action:'scenario.reactive_v2',dataset:{op:'add_action','action-type':'DEVICE_COMMAND','variant-index':variantIndex}})}${uiButton({label:'Add command group',action:'scenario.reactive_v2',dataset:{op:'add_action','action-type':'DEVICE_COMMAND_GROUP','variant-index':variantIndex}})}${uiButton({label:'Add wait',action:'scenario.reactive_v2',dataset:{op:'add_action','action-type':'WAIT_TIME','variant-index':variantIndex}})}${uiButton({label:'Add flag',action:'scenario.reactive_v2',dataset:{op:'add_action','action-type':'SET_FLAG','variant-index':variantIndex}})}${uiButton({label:'Add message',action:'scenario.reactive_v2',dataset:{op:'add_action','action-type':'SHOW_OPERATOR_MESSAGE','variant-index':variantIndex}})}</div>`;
}

function renderReactiveV2Variants(branch,issueState){
const variants=Array.isArray(branch.variants)?branch.variants:[];
const mode=String(branch.policy&&branch.policy.mode||'single');
const isSingle=mode==='single';
const isEscalate=mode==='escalate';
const title=isEscalate?'Escalation levels':(isSingle?'Actions':'Variants');
const addLabel=isEscalate?'Add level':'Add variant';
const shown=isSingle?(variants.length?[variants[0]]:[{id:'variant_1',label:'Actions',actions:[]}]):variants;
const actionIssues=issueState&&issueState.actionIssues&&typeof issueState.actionIssues==='object'?issueState.actionIssues:{};
const maxFireValue=Number(branch.policy&&branch.policy.max_fire_count)||Number(branch.max_fire_count)||shown.length||1;
const escalateControls=isEscalate?`<label class='scenario-v2-max-fire'><span>Stop after level</span><input data-v2-policy-field='max_fire_count' type='number' min='0' step='1' value='${esc(maxFireValue)}'></label>`:'';
return `<section class='scenario-v2-rule scenario-v2-then'><div class='scenario-v2-rule-label'>Then</div><div class='scenario-v2-rule-body'><div class='scenario-v2-subtitle-row'><div class='scenario-v2-subtitle'>${esc(title)}</div>${escalateControls}</div><div class='scenario-v2-variant-list'>${shown.map((variant,index)=>{const originalIndex=isSingle?0:index;const label=isEscalate?`Level ${index+1}`:(isSingle?'Actions':`Variant ${index+1}`);const nameValue=isSingle?'Actions':(variant.label||variant.name||label);return `<div class='scenario-v2-variant' data-v2-variant='${originalIndex}'><div class='scenario-v2-variant-head'><label class='field-stack'><span>${esc(label)}</span><input data-v2-variant-field='label' placeholder='${esc(label)} label' value='${esc(nameValue)}' ${isSingle?'readonly':''}></label><div class='actions'>${isSingle?'':uiButton({label:`Delete ${isEscalate?'level':'variant'}`,kind:'danger',action:'scenario.reactive_v2',dataset:{op:'delete_variant','variant-index':originalIndex},disabled:variants.length<=1})}</div></div><details class='scenario-advanced compact-advanced scenario-v2-variant-id'><summary>${esc(isEscalate?'Level id':'Variant id')}</summary><div class='row'><input data-v2-variant-field='id' placeholder='Variant ID' value='${esc(variant.id||'')}'></div></details><div class='scenario-v2-action-list'>${(Array.isArray(variant.actions)?variant.actions:[]).map((action,actionIndex)=>renderReactiveV2Action(action,originalIndex,actionIndex,actionIssues[reactiveV2ActionKey(originalIndex,actionIndex)]||[])).join('')||`<div class='empty'>No actions yet. Add one or more actions below.</div>`}</div>${renderReactiveV2ActionAddButtons(originalIndex)}</div>`;}).join('')}</div>${isSingle?'':uiButton({label:addLabel,action:'scenario.reactive_v2',dataset:{op:'add_variant'}})}</div></section>`;
}

function renderReactiveV2Editor(branch,issueState){
ensureReactiveV2Branch(branch);
const branchIssues=issueState&&Array.isArray(issueState.branchIssues)?issueState.branchIssues:[];
return `<div class='scenario-v2-editor'>${renderScenarioInlineIssues(branchIssues)}${renderReactiveV2Type(branch)}${renderReactiveV2Trigger(branch)}${renderReactiveV2Guards(branch)}${renderReactiveV2Variants(branch,issueState)}${renderReactiveV2Policy(branch)}</div>`;
}

function applyReactiveV2Action(action,variantIndex,actionIndex,actionType){
const draft=scenarioWorkingDraft();
if(!draft)return;
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
skipNextScenarioDomSync();
render();
return;
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
scenarioCommitDraft(draft);
skipNextScenarioDomSync();
render();
}
