// GM panel source part. Edit this file, then rebuild gm_panel.js.
function scenarioActiveValidationIssues(savedIssues){
const report=scenarioDisplayValidationReport(savedIssues);
if(report&&Array.isArray(report.issues))return report.issues;
return Array.isArray(savedIssues)?savedIssues:[];
}

function scenarioIssueIsError(issue){
return String(issue&&issue.level||'error').toLowerCase()==='error';
}

function scenarioIssueBranchId(issue){
return String(issue&&issue.branch_id||'').trim();
}

function scenarioIssueVariantIndex(issue){
const idx=Number(issue&&issue.variant_index);
return Number.isFinite(idx)?idx:-1;
}

function scenarioIssueActionIndex(issue){
const idx=Number(issue&&issue.action_index);
return Number.isFinite(idx)?idx:-1;
}

function scenarioIssueActionKey(issue){
const variantIndex=scenarioIssueVariantIndex(issue);
const actionIndex=scenarioIssueActionIndex(issue);
return variantIndex>=0&&actionIndex>=0?`${variantIndex}:${actionIndex}`:'';
}

function scenarioValidationIssueBuilder(){
const issues=[];
return {
issues,
add(stepIndex,code,message,layer){
issues.push({level:'error',step_index:stepIndex,code,message,layer:layer||'draft'});
},
addReactive(branch,variantIndex,actionIndex,code,message,layer){
issues.push({level:'error',branch_id:String(branch&&branch.id||''),variant_index:variantIndex,action_index:actionIndex,code,message,layer:layer||'draft'});
}
};
}

function scenarioValidateAudioCommand(target,stepIndex,branch,variantIndex,actionIndex,label,builder,layer){
if(String(target&&target.device_id||'')!=='system_audio'||String(target&&target.command_id||'')!=='play')return;
const issue=scenarioAudioValidationIssue(target&&target.params);
if(!issue)return;
if(branch&&Number.isFinite(variantIndex)&&Number.isFinite(actionIndex))builder.addReactive(branch,variantIndex,actionIndex,issue.code,`${label}: ${issue.message}`,layer);
else builder.add(stepIndex,issue.code,`${label}: ${issue.message}`,layer);
}

function scenarioValidateCommandParams(target,stepIndex,branch,variantIndex,actionIndex,label,builder,layer){
const deviceId=String(target&&target.device_id||'').trim();
const commandId=String(target&&target.command_id||'').trim();
if(!deviceId||!commandId)return;
if(deviceId==='system_audio'&&commandId==='play'){
scenarioValidateAudioCommand(target,stepIndex,branch,variantIndex,actionIndex,label,builder,layer);
return;
}
const command=scenarioCommandById(deviceId,commandId);
const schema=Array.isArray(command&&command.args_schema)?command.args_schema:[];
if(!schema.length)return;
const params=target&&target.params&&typeof target.params==='object'?target.params:{};
for(const param of schema){
const key=String(param&&param.key||'').trim();
if(!key||param.optional===true)continue;
const value=params[key];
const missing=value===undefined||value===null||value==='';
if(missing){
const message=`${label}: ${param.label||key} is required.`;
if(branch&&Number.isFinite(variantIndex)&&Number.isFinite(actionIndex))builder.addReactive(branch,variantIndex,actionIndex,'DEVICE_COMMAND_PARAM_REQUIRED',message,layer);
else builder.add(stepIndex,'DEVICE_COMMAND_PARAM_REQUIRED',message,layer);
return;
}
}
}

function scenarioValidateReactiveTrigger(branch,builder){
if(!branch||scenarioBranchTypeValue(branch)!=='reactive')return;
const trigger=branch.trigger&&typeof branch.trigger==='object'?branch.trigger:null;
if(!trigger){
builder.addReactive(branch,-1,-1,'REACTIVE_TRIGGER_INCOMPLETE','Reaction trigger: choose a trigger type and target.','field');
return;
}
const kind=String(trigger.kind||'device_event');
if(kind==='device_event'){
if(!String(trigger.device_id||'').trim()||!String(trigger.event_id||'').trim()){
builder.addReactive(branch,-1,-1,'REACTIVE_TRIGGER_INCOMPLETE','Reaction trigger: choose a device and event.','field');
}
return;
}
if(kind==='flag_changed'){
if(!String(trigger.flag_name||'').trim()){
builder.addReactive(branch,-1,-1,'REACTIVE_TRIGGER_INCOMPLETE','Reaction trigger: choose a flag name.','field');
}
return;
}
if(kind==='operator_event'){
if(!String(trigger.operator_event||trigger.event_id||'').trim()){
builder.addReactive(branch,-1,-1,'REACTIVE_TRIGGER_INCOMPLETE','Reaction trigger: choose an operator event id.','field');
}
return;
}
if(kind==='runtime_event'){
if(!String(trigger.runtime_event||trigger.event_id||'').trim()){
builder.addReactive(branch,-1,-1,'REACTIVE_TRIGGER_INCOMPLETE','Reaction trigger: choose a runtime event id.','field');
}
return;
}
builder.addReactive(branch,-1,-1,'REACTIVE_TRIGGER_INCOMPLETE','Reaction trigger: choose a valid trigger type.','field');
}

function scenarioFieldValidationIssues(scenario){
const builder=scenarioValidationIssueBuilder();
let globalIndex=0;
(Array.isArray(scenario&&scenario.branches)?scenario.branches:[]).forEach(branch=>{
scenarioValidateReactiveTrigger(branch,builder);
const steps=Array.isArray(branch&&branch.steps)?branch.steps:[];
steps.forEach((step,localIndex)=>{
const type=scenarioStepTypeValue(step);
const stepIndex=globalIndex++;
const stepLabel=`Step ${localIndex+1}`;
if(type==='DEVICE_COMMAND'){
if(!String(step&&step.device_id||'').trim()||!String(step&&step.command_id||'').trim())builder.add(stepIndex,'DEVICE_COMMAND_INCOMPLETE',`${stepLabel}: choose a device and command`,'field');
scenarioValidateCommandParams(step,stepIndex,null,null,null,stepLabel,builder,'field');
}
else if(type==='DEVICE_COMMAND_GROUP'){
const commands=Array.isArray(step&&step.commands)?step.commands:[];
commands.forEach((cmd,cmdIndex)=>{
if(!String(cmd&&cmd.device_id||'').trim()||!String(cmd&&cmd.command_id||'').trim())builder.add(stepIndex,'COMMAND_GROUP_INCOMPLETE',`${stepLabel}: command ${cmdIndex+1} needs a device and command`,'field');
scenarioValidateCommandParams(cmd,stepIndex,null,null,null,`${stepLabel}: command ${cmdIndex+1}`,builder,'field');
});
}
else if(type==='WAIT_DEVICE_EVENT'){
if(!String(step&&step.device_id||'').trim()||!String(step&&step.event_id||'').trim())builder.add(stepIndex,'WAIT_DEVICE_EVENT_INCOMPLETE',`${stepLabel}: choose a device and event`,'field');
}
else if(type==='WAIT_ANY_DEVICE_EVENT'||type==='WAIT_ALL_DEVICE_EVENTS'){
const events=Array.isArray(step&&step.events)?step.events:[];
events.forEach((ev,eventIndex)=>{
if(!String(ev&&ev.device_id||'').trim()||!String(ev&&ev.event_id||'').trim())builder.add(stepIndex,'WAIT_EVENT_INCOMPLETE',`${stepLabel}: event ${eventIndex+1} needs a device and event`,'field');
});
}
else if(type==='WAIT_TIME'){
if(!Number.isFinite(Number(step&&step.duration_ms))||Number(step.duration_ms)<=0)builder.add(stepIndex,'WAIT_TIME_INVALID',`${stepLabel}: duration must be greater than zero`,'field');
}
else if(type==='OPERATOR_APPROVAL'){
if(!String(step&&step.prompt||step&&step.operator_prompt||'').trim())builder.add(stepIndex,'OPERATOR_PROMPT_EMPTY',`${stepLabel}: write the operator prompt`,'field');
}
else if(type==='SHOW_OPERATOR_MESSAGE'){
if(!String(step&&step.message||'').trim())builder.add(stepIndex,'OPERATOR_MESSAGE_EMPTY',`${stepLabel}: write the operator message`,'field');
}
else if(type==='SET_FLAG'){
if(!String(step&&step.flag_name||'').trim())builder.add(stepIndex,'FLAG_NAME_EMPTY',`${stepLabel}: choose or type a flag name`,'field');
}
else if(type==='WAIT_FLAGS'){
const flags=Array.isArray(step&&step.flags)?step.flags:[];
flags.forEach((flag,flagIndex)=>{
if(!String(flag&&flag.flag_name||'').trim())builder.add(stepIndex,'FLAG_NAME_EMPTY',`${stepLabel}: flag ${flagIndex+1} needs a name`,'field');
});
}
});
const variants=Array.isArray(branch&&branch.variants)?branch.variants:[];
variants.forEach((variant,variantIndex)=>{
const actions=Array.isArray(variant&&variant.actions)?variant.actions:[];
actions.forEach((action,actionIndex)=>{
const type=scenarioStepTypeValue(action);
const actionLabel=`Reaction action ${actionIndex+1}`;
if(type==='DEVICE_COMMAND'){
if(!String(action&&action.device_id||'').trim()||!String(action&&action.command_id||'').trim())builder.addReactive(branch,variantIndex,actionIndex,'DEVICE_COMMAND_INCOMPLETE',`${actionLabel}: choose a device and command`,'field');
scenarioValidateCommandParams(action,-1,branch,variantIndex,actionIndex,actionLabel,builder,'field');
}
else if(type==='DEVICE_COMMAND_GROUP'){
const commands=Array.isArray(action&&action.commands)?action.commands:[];
commands.forEach((cmd,cmdIndex)=>{
if(!String(cmd&&cmd.device_id||'').trim()||!String(cmd&&cmd.command_id||'').trim())builder.addReactive(branch,variantIndex,actionIndex,'COMMAND_GROUP_INCOMPLETE',`${actionLabel}: command ${cmdIndex+1} needs a device and command`,'field');
scenarioValidateCommandParams(cmd,-1,branch,variantIndex,actionIndex,`${actionLabel}: command ${cmdIndex+1}`,builder,'field');
});
}
else if(type==='WAIT_TIME'){
if(!Number.isFinite(Number(action&&action.duration_ms))||Number(action.duration_ms)<=0)builder.addReactive(branch,variantIndex,actionIndex,'WAIT_TIME_INVALID',`${actionLabel}: duration must be greater than zero`,'field');
}
else if(type==='SHOW_OPERATOR_MESSAGE'){
if(!String(action&&action.message||'').trim())builder.addReactive(branch,variantIndex,actionIndex,'OPERATOR_MESSAGE_EMPTY',`${actionLabel}: write the operator message`,'field');
}
else if(type==='SET_FLAG'){
if(!String(action&&action.flag_name||'').trim())builder.addReactive(branch,variantIndex,actionIndex,'FLAG_NAME_EMPTY',`${actionLabel}: choose or type a flag name`,'field');
}
});
});
});
return builder.issues;
}

function scenarioDraftDomainValidationIssues(scenario){
const builder=scenarioValidationIssueBuilder();
let globalIndex=0;
(Array.isArray(scenario&&scenario.branches)?scenario.branches:[]).forEach(branch=>{
const seenStepIds=new Set();
(Array.isArray(branch&&branch.steps)?branch.steps:[]).forEach((step,localIndex)=>{
const type=scenarioStepTypeValue(step);
const stepIndex=globalIndex++;
const stepLabel=`Step ${localIndex+1}`;
const stepId=String(step&&step.id||'').trim();
if(!stepId)builder.add(stepIndex,'STEP_ID_EMPTY',`${stepLabel}: internal step id is empty`,'draft');
else if(seenStepIds.has(stepId))builder.add(stepIndex,'STEP_ID_DUPLICATE',`${stepLabel}: duplicate step id inside this branch`,'draft');
seenStepIds.add(stepId);
if(type==='DEVICE_COMMAND_GROUP'){
const commands=Array.isArray(step&&step.commands)?step.commands:[];
if(!commands.length)builder.add(stepIndex,'COMMAND_GROUP_EMPTY',`${stepLabel}: add at least one command`,'draft');
}
else if(type==='WAIT_ANY_DEVICE_EVENT'||type==='WAIT_ALL_DEVICE_EVENTS'){
const events=Array.isArray(step&&step.events)?step.events:[];
if(!events.length)builder.add(stepIndex,'WAIT_EVENTS_EMPTY',`${stepLabel}: add at least one device event`,'draft');
}
else if(type==='WAIT_FLAGS'){
const flags=Array.isArray(step&&step.flags)?step.flags:[];
if(!flags.length)builder.add(stepIndex,'WAIT_FLAGS_EMPTY',`${stepLabel}: add at least one flag`,'draft');
}
});
const variants=Array.isArray(branch&&branch.variants)?branch.variants:[];
variants.forEach((variant,variantIndex)=>{
const actions=Array.isArray(variant&&variant.actions)?variant.actions:[];
actions.forEach((action,actionIndex)=>{
const type=scenarioStepTypeValue(action);
const actionLabel=`Reaction action ${actionIndex+1}`;
if(type==='DEVICE_COMMAND_GROUP'){
const commands=Array.isArray(action&&action.commands)?action.commands:[];
if(!commands.length)builder.addReactive(branch,variantIndex,actionIndex,'COMMAND_GROUP_EMPTY',`${actionLabel}: add at least one command`,'draft');
}
});
});
});
return builder.issues;
}

function scenarioClientValidationReport(scenario){
const fieldIssues=scenarioFieldValidationIssues(scenario);
const draftIssues=scenarioDraftDomainValidationIssues(scenario);
const issues=fieldIssues.concat(draftIssues);
return {
ok:true,
valid:!issues.length,
issue_count:issues.length,
error_count:issues.length,
warning_count:0,
issues,
_layers:{
field:{issue_count:fieldIssues.length,error_count:fieldIssues.length,warning_count:0},
draft:{issue_count:draftIssues.length,error_count:draftIssues.length,warning_count:0}
}
};
}

function scenarioIssueIsStepSpecific(issue,stepCount){
if(scenarioIssueBranchId(issue))return false;
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
return (Array.isArray(issues)?issues:[]).filter(issue=>!scenarioIssueBranchId(issue)&&!scenarioIssueIsStepSpecific(issue,stepCount));
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
if(scenarioIssueBranchId(issue))return;
const idx=Number(issue&&issue.step_index);
if(!Number.isFinite(idx)||idx<offset||idx>=offset+stepCount)return;
const local={...issue,step_index:idx-offset};
if(!scenarioIssueIsStepSpecific(local,stepCount))return;
out[local.step_index]=out[local.step_index]||[];
out[local.step_index].push(local);
});
return out;
}

function scenarioReactiveIssuesForBranch(issues,branches,branchIndex){
const branch=(Array.isArray(branches)?branches:[])[branchIndex]||null;
const branchId=String(branch&&branch.id||'').trim();
const out={branchIssues:[],actionIssues:{}};
if(!branchId)return out;
 (Array.isArray(issues)?issues:[]).forEach(issue=>{
const issueBranchId=scenarioIssueBranchId(issue);
if(!issueBranchId||issueBranchId!==branchId)return;
const key=scenarioIssueActionKey(issue);
if(key){
out.actionIssues[key]=out.actionIssues[key]||[];
out.actionIssues[key].push(issue);
return;
}
out.branchIssues.push(issue);
});
return out;
}
