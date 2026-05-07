// GM panel source part. Edit this file, then rebuild gm_panel.js.
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
