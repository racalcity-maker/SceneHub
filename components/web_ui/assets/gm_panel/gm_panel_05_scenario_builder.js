// GM panel source part. Edit this file, then rebuild gm_panel.js.
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

function collectScenarioStepsFromDom(activeBranch,root,previousActiveSteps){
const stepsPanel=root.querySelector('.scenario-steps-panel');
if(!activeBranch||scenarioIsReactiveV2Branch(activeBranch))return;
(stepsPanel?stepsPanel.querySelectorAll('[data-scenario-step]'):[]).forEach((el,index)=>{
const previous=previousActiveSteps[index]?JSON.parse(JSON.stringify(previousActiveSteps[index])):{};
if(!el.querySelector(`[data-step-field='type']`)&&previous.type){
activeBranch.steps.push(previous);
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
activeBranch.steps.push(step);}
);
}

function collectScenarioBranchSettingsFromDom(activeBranch,branchIndex,root){
if(!activeBranch)return;
const branchName=root.querySelector('[data-scenario-branch-field="name"]');
const branchId=root.querySelector('[data-scenario-branch-field="id"]');
const branchType=root.querySelector('[data-scenario-branch-field="type"]');
const branchEnabled=root.querySelector('[data-scenario-branch-field="enabled"]');
const branchRequired=root.querySelector('[data-scenario-branch-field="required_for_completion"]');
const branchCooldown=root.querySelector('[data-scenario-branch-field="cooldown_sec"]');
const branchRunOnce=root.querySelector('[data-scenario-branch-field="run_once"]');
activeBranch.name=(branchName&&branchName.value)||activeBranch.name||`Branch ${branchIndex+1}`;
activeBranch.id=(branchId&&branchId.value)||activeBranch.id||slugifyId(activeBranch.name,`branch_${branchIndex+1}`);
activeBranch.type=branchType?scenarioBranchTypeValue({type:branchType.value}):scenarioBranchTypeValue(activeBranch);
activeBranch.enabled=branchEnabled?branchEnabled.checked:activeBranch.enabled!==false;
activeBranch.required_for_completion=activeBranch.type==='normal'&&(branchRequired?branchRequired.checked:activeBranch.required_for_completion!==false);
activeBranch.cooldown_ms=activeBranch.type==='reactive'?Math.max(0,Math.round(Number(branchCooldown&&branchCooldown.value)||0))*1000:0;
if(activeBranch.type==='reactive'&&activeBranch.policy&&typeof activeBranch.policy==='object')activeBranch.policy.cooldown_ms=activeBranch.cooldown_ms;
activeBranch.run_once=activeBranch.type==='reactive'&&!!(branchRunOnce&&(branchRunOnce.type==='checkbox'?branchRunOnce.checked:String(branchRunOnce.value)==='true'));
activeBranch.steps=[];
if(scenarioIsReactiveV2Branch(activeBranch)){
collectReactiveV2BranchFromDom(activeBranch,root);
}
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
const previousActiveSteps=activeBranch&&Array.isArray(activeBranch.steps)?activeBranch.steps.map(step=>JSON.parse(JSON.stringify(step))):[];
collectScenarioBranchSettingsFromDom(activeBranch,branchIndex,root);
const scenario={
id:(root.querySelector('#scenario_id')||{}).value||'',
name:(root.querySelector('#scenario_name')||{}).value||'',
room_id:scenarioEditor.room_id,
branches}
;

collectScenarioStepsFromDom(activeBranch,root,previousActiveSteps);
if(!scenario.id&&scenario.name)scenario.id=slugifyId(scenario.name,'scenario');
return scenario;
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
const rows=scenarios.length?scenarios.map(s=>`<div class='row-card'><div class='row-main'><div class='row-title'>${esc(s.name||s.id)} ${s.valid===false?`<span class='badge'>invalid</span>`:''}</div><div class='row-meta'>${esc(s.step_count||0)} steps / ${esc(Array.isArray(s.branches)?s.branches.length:1)} branch${(Array.isArray(s.branches)&&s.branches.length===1)?'':'es'} / ${esc(scenarioValidationText(s))}</div></div><div class='actions'>${uiButton({label:'Edit',action:'scenario.edit',dataset:{'scenario-id':s.id||''}})}${uiButton({label:'Create game mode',action:'scenario.create_game_mode',dataset:{'scenario-id':s.id||''}})}${uiButton({label:'Delete',kind:'danger',action:'scenario.delete',dataset:{'scenario-id':s.id||''},confirm:`Delete scenario ${s.id||''}?`})}</div></div>`).join(''):`<div class='card empty'>No scenarios for this room</div>`;
const scenarioIdKey=`scenario:id:${roomId}:${base.id||'new'}`;
const jsonKey=`scenario:json:${roomId}:${base.id||'new'}`;
const emptyStepsText=scenarioBranchTypeValue(activeBranch)==='reactive'?'Add a trigger first. This reaction will listen for it, then run the actions you add after it.':'No steps yet';
const activeBranchIsV2=scenarioIsReactiveV2Branch(activeBranch);
const branchEditorBody=activeBranchIsV2
?renderReactiveV2Editor(activeBranch)
:`<section class='scenario-steps-panel'><h2 class='section-title'>Steps: ${esc(activeBranch&&activeBranch.name||'Branch')}</h2><div>${activeSteps.length?activeSteps.map((step,i)=>renderScenarioStepEditor(step,i,activeSteps.length,Number(scenarioEditor.expanded_step)===i,issuesByStep[i]||[])).join(''):`<div class='empty'>${esc(emptyStepsText)}</div>`}</div></section>`;
const editorHtml=editorOpen?`<div class='card scenario-editor-card' data-scenario-editor='1' data-active-branch-index='${activeBranchIndex}'><div class='scenario-editor-head'><div><h2 class='section-title'>${editing?'Edit scenario':'New scenario'}${scenarioEditor.dirty?' *':''}</h2><input id='scenario_name' placeholder='Scenario name' value='${esc(base.name||'')}'></div><div class='actions'>${uiButton({label:'Validate',action:'scenario.validate'})}${uiButton({label:'Save',action:'scenario.save'})}</div></div><details class='scenario-advanced compact-advanced' ${detailsAttrs(scenarioIdKey,false)}><summary>Scenario id</summary><div class='row'><input id='scenario_id' placeholder='Scenario ID' value='${esc(base.id||'')}'></div></details>${issueHtml}${renderScenarioBranchTabs(base,activeBranchIndex)}${renderScenarioBranchSettings(activeBranch,activeBranchIndex,base.branches.length)}<div class='scenario-editor-layout ${activeBranchIsV2?'scenario-editor-layout-v2':''}'>${activeBranchIsV2?'':`<aside class='scenario-add-panel'>${scenarioStepPresetButtons(activeBranch)}</aside>`}${branchEditorBody}</div><details style='margin-top:10px' ${detailsAttrs(jsonKey,false)}><summary class='row-meta'>Debug JSON</summary><textarea id='scenario_json' class='builder-json' readonly>${esc(json)}</textarea></details></div>`:`<div class='card empty'><h2 class='section-title'>Scenario editor</h2><div class='row-meta'>Select a scenario or create a new one.</div></div>`;
return `<div class='scenario-room-bar'><div><span class='row-meta'>Room</span><select class='scenario-select' data-scenario-room-select>${rooms.map(r=>`<option value='${esc(r.room_id)}' ${
r.room_id===roomId?'selected':''}
>${
esc(r.title||r.room_id)}
</option>`).join('')}</select></div><div class='row-meta'>Steps can target devices in any room.</div></div><div class='scenario-admin-layout'><section><div class='card-head'><h2 class='section-title'>Scenarios</h2><div class='actions'>${uiButton({label:'Add scenario',action:'scenario.new'})}</div></div><div class='list'>${rows}</div></section><section>${editorHtml}</section></div>`;
}
