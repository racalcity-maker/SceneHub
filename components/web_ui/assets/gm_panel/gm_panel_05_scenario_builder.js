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
const channel=audioChannelValue(params);
const file=audioBaseName(params.file||'');
if(!file)return channel==='background'?'Background audio missing file':'Audio missing file';
return channel==='background'
  ? (params.repeat?`Play background repeat: ${file}`:`Play background: ${file}`)
  : `Play audio: ${file}`;
}
return scenarioCommandName('system_audio',commandId);
}

function scenarioAudioMissingAutoLabel(label){
const text=String(label||'').trim();
return text==='Audio missing file'||text==='Background audio missing file';
}

function scenarioAudioCommandAutoLabel(step){
const commandId=String(step&&step.command_id||'');
if(commandId==='play'){
const params=step&&step.params||{};
const channel=audioChannelValue(params);
const file=audioBaseName(params.file||'');
if(!file)return channel==='background'?'Play background audio':'Play audio';
return channel==='background'
  ? (params.repeat?`Play background repeat: ${file}`:`Play background: ${file}`)
  : `Play audio: ${file}`;
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
if(scenarioEditor.draft&&
   scenarioEditor.draft.room_id===roomId&&
   (scenarioEditor.open||scenarioEditor.dirty))source=JSON.parse(JSON.stringify(scenarioEditor.draft));
else if(scenarioEditor.original_scenario&&
        scenarioEditor.original_scenario.room_id===roomId&&
        String(scenarioEditor.original_scenario.id||'')===String(scenarioEditor.scenario_id||''))source=JSON.parse(JSON.stringify(scenarioEditor.original_scenario));
else{
const detail=roomScenarioDetailById(roomId,scenarioEditor.scenario_id)||null;
if(detail)source=scenarioEditableJson(detail,roomId);
else if(!scenarioEditor.scenario_id)source={id:'',name:'',room_id:roomId,branches:[defaultScenarioBranch(0,[])]};
else return null;
}
return source;
}

function renderScenariosAdminView(){
setPage('Scenarios','Room scenario editor');
const rooms=(gmState&&Array.isArray(gmState.rooms))?gmState.rooms:[];
if(!rooms.length)return `<div class='card empty'>No rooms available</div>`;
if(!scenarioEditor.room_id||!rooms.some(r=>r.room_id===scenarioEditor.room_id)){
scenarioEditor.room_id=rooms[0].room_id;
}
const roomId=scenarioEditor.room_id;
const scenarios=scenarioSummariesByRoom(roomId);
const editing=roomScenarioDetailById(roomId,scenarioEditor.scenario_id)||null;
const editingSummary=roomScenarioSummaryById(roomId,scenarioEditor.scenario_id)||null;
const editorOpen=!!(scenarioEditor.open||scenarioEditor.dirty);
if(editing&&!scenarioEditor.dirty&&(!scenarioEditor.draft||!scenarioEditor.original_scenario)){
scenarioSetLoadedDraft(editing,roomId);
}
const base=editorOpen?scenarioEditorSource():scenarioEditableJson(editing,roomId);
const detailMissing=!!(editorOpen&&scenarioEditor.scenario_id&&!base);
if(detailMissing){
return `<div class='scenario-room-bar'><div><span class='row-meta'>Room</span><select class='scenario-select' data-scenario-room-select>${rooms.map(r=>`<option value='${esc(r.room_id)}' ${
r.room_id===roomId?'selected':''}
>${
esc(r.title||r.room_id)}
</option>`).join('')}</select></div><div class='row-meta'>Steps can target devices in any room.</div></div><div class='scenario-admin-layout'><section><div class='card-head'><h2 class='section-title'>Scenarios</h2><div class='actions'>${uiButton({label:'Add scenario',action:'scenario.new'})}</div></div><div class='list'>${scenarios.length?scenarios.map(s=>{const branchCount=Math.max(1,Number(s&&s.branch_count)||Number(Array.isArray(s&&s.branches)?s.branches.length:0)||1);return `<div class='row-card'><div class='row-main'><div class='row-title'>${esc(s.name||s.id)} ${s.valid===false?`<span class='badge'>invalid</span>`:''}</div><div class='row-meta'>${esc(s.step_count||0)} steps / ${esc(branchCount)} branch${branchCount===1?'':'es'} / ${esc(scenarioValidationText(s))}</div></div><div class='actions'>${uiButton({label:'Edit',action:'scenario.edit',dataset:{'scenario-id':s.id||''}})}${uiButton({label:'Create game mode',action:'scenario.create_game_mode',dataset:{'scenario-id':s.id||''}})}${uiButton({label:'Delete',kind:'danger',action:'scenario.delete',dataset:{'scenario-id':s.id||''},confirm:`Delete scenario ${s.id||''}?`})}</div></div>`;}).join(''):`<div class='card empty'>No scenarios for this room</div>`}</div></section><section><div class='card empty'><h2 class='section-title'>Scenario editor</h2><div class='row-meta'>Scenario detail is still loading. Click Edit again if it does not appear.</div></div></section></div>`;
}
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
const reactiveIssueState=scenarioReactiveIssuesForBranch(activeIssues,base.branches,activeBranchIndex);
const issueHtml=renderScenarioValidationSummary(activeIssues,totalStepCount);
const rows=scenarios.length?scenarios.map(s=>{const branchCount=Math.max(1,Number(s&&s.branch_count)||Number(Array.isArray(s&&s.branches)?s.branches.length:0)||1);return `<div class='row-card'><div class='row-main'><div class='row-title'>${esc(s.name||s.id)} ${s.valid===false?`<span class='badge'>invalid</span>`:''}</div><div class='row-meta'>${esc(s.step_count||0)} steps / ${esc(branchCount)} branch${branchCount===1?'':'es'} / ${esc(scenarioValidationText(s))}</div></div><div class='actions'>${uiButton({label:'Edit',action:'scenario.edit',dataset:{'scenario-id':s.id||''}})}${uiButton({label:'Create game mode',action:'scenario.create_game_mode',dataset:{'scenario-id':s.id||''}})}${uiButton({label:'Delete',kind:'danger',action:'scenario.delete',dataset:{'scenario-id':s.id||''},confirm:`Delete scenario ${s.id||''}?`})}</div></div>`;}).join(''):`<div class='card empty'>No scenarios for this room</div>`;
const scenarioIdKey=`scenario:id:${roomId}:${base.id||'new'}`;
const jsonKey=`scenario:json:${roomId}:${base.id||'new'}`;
const emptyStepsText=scenarioBranchTypeValue(activeBranch)==='reactive'?'Add a trigger first. This reaction will listen for it, then run the actions you add after it.':'No steps yet';
const activeBranchIsV2=scenarioIsReactiveV2Branch(activeBranch);
const branchEditorBody=activeBranchIsV2
?renderReactiveV2Editor(activeBranch,reactiveIssueState)
:`<section class='scenario-steps-panel'><h2 class='section-title'>Steps: ${esc(activeBranch&&activeBranch.name||'Branch')}</h2><div>${activeSteps.length?activeSteps.map((step,i)=>renderScenarioStepEditor(step,i,activeSteps.length,Number(scenarioEditor.expanded_step)===i,issuesByStep[i]||[])).join(''):`<div class='empty'>${esc(emptyStepsText)}</div>`}</div></section>`;
const editorHtml=editorOpen?`<div class='card scenario-editor-card' data-scenario-editor='1' data-active-branch-index='${activeBranchIndex}'><div class='scenario-editor-head'><div><h2 class='section-title'>${(editing||editingSummary||scenarioEditor.scenario_id)?'Edit scenario':'New scenario'}${scenarioEditor.dirty?' *':''}</h2><input id='scenario_name' placeholder='Scenario name' value='${esc(base.name||'')}'></div><div class='actions'>${uiButton({label:'Validate',action:'scenario.validate'})}${uiButton({label:'Save',action:'scenario.save'})}</div></div><details class='scenario-advanced compact-advanced' ${detailsAttrs(scenarioIdKey,false)}><summary>Scenario id</summary><div class='row'><input id='scenario_id' placeholder='Scenario ID' value='${esc(base.id||'')}'></div></details>${issueHtml}${renderScenarioBranchTabs(base,activeBranchIndex)}${renderScenarioBranchSettings(activeBranch,activeBranchIndex,base.branches.length)}<div class='scenario-editor-layout ${activeBranchIsV2?'scenario-editor-layout-v2':''}'>${activeBranchIsV2?'':`<aside class='scenario-add-panel'>${scenarioStepPresetButtons(activeBranch)}</aside>`}${branchEditorBody}</div><details style='margin-top:10px' ${detailsAttrs(jsonKey,false)}><summary class='row-meta'>Debug JSON</summary><textarea id='scenario_json' class='builder-json' readonly>${esc(json)}</textarea></details></div>`:`<div class='card empty'><h2 class='section-title'>Scenario editor</h2><div class='row-meta'>Select a scenario or create a new one.</div></div>`;
return `<div class='scenario-room-bar'><div><span class='row-meta'>Room</span><select class='scenario-select' data-scenario-room-select>${rooms.map(r=>`<option value='${esc(r.room_id)}' ${
r.room_id===roomId?'selected':''}
>${
esc(r.title||r.room_id)}
</option>`).join('')}</select></div><div class='row-meta'>Steps can target devices in any room.</div></div><div class='scenario-admin-layout'><section><div class='card-head'><h2 class='section-title'>Scenarios</h2><div class='actions'>${uiButton({label:'Add scenario',action:'scenario.new'})}</div></div><div class='list'>${rows}</div></section><section>${editorHtml}</section></div>`;
}
