// GM panel source part. Edit this file, then rebuild gm_panel.js.
function normalizeScenarioEditorStep(step){
step=step||{};
const out={
id:step.id||'',label:step.label||'',enabled:step.enabled!==false,type:step.type||'WAIT_TIME'}
;if(step.allow_operator_skip)out.allow_operator_skip=true;if(step.operator_skip_label)out.operator_skip_label=step.operator_skip_label;if(step.device_id)out.device_id=step.device_id;if(step.scenario_id)out.scenario_id=step.scenario_id;if(step.command_id)out.command_id=step.command_id;if(step.event_id)out.event_id=step.event_id;if(step.params)out.params=step.params;if(step.duration_ms)out.duration_ms=step.duration_ms;if(step.event_type)out.event_type=step.event_type;if(step.source_id)out.source_id=step.source_id;if(step.operator_prompt)out.prompt=step.operator_prompt;if(step.operator_approve_label)out.approve_label=step.operator_approve_label;if(step.prompt)out.prompt=step.prompt;if(step.approve_label)out.approve_label=step.approve_label;if(Array.isArray(step.commands))out.commands=step.commands.map(cmd=>({device_id:cmd.device_id||'',command_id:cmd.command_id||'',params:cmd.params&&typeof cmd.params==='object'?cmd.params:{}}));if(Array.isArray(step.events))out.events=step.events.map(ev=>({device_id:ev.device_id||'',event_id:ev.event_id||''}));if(Array.isArray(step.flags))out.flags=step.flags.map(flag=>({flag_name:flag.flag_name||flag.name||'',value:flag.value!==false}));if(step.message)out.message=step.message;if(step.operator_message)out.message=step.operator_message;if(step.flag_name)out.flag_name=step.flag_name;if(step.flag_value!==undefined)out.value=!!step.flag_value;if(step.value!==undefined)out.value=!!step.value;return out;}
function scenarioBranchTypeValue(branch){
const raw=String(branch&&branch.type||'normal').toLowerCase();
return raw==='reactive'||raw==='reaction'?'reactive':'normal';
}

function defaultScenarioBranch(index,steps,type){
const n=Number(index)||0;
const branchType=type==='reactive'?'reactive':'normal';
return {id:n?`branch_${n+1}`:'main',name:n?(branchType==='reactive'?`Reaction ${n+1}`:`Branch ${n+1}`):'Main',type:branchType,enabled:true,required_for_completion:branchType==='normal',cooldown_ms:0,run_once:false,steps:Array.isArray(steps)?steps:[]};
}

function normalizeScenarioBranch(branch,index){
const base=defaultScenarioBranch(index,[]);
const name=branch&&branch.name||base.name;
const steps=branch&&Array.isArray(branch.steps)?branch.steps.map(normalizeScenarioEditorStep):[];
const type=scenarioBranchTypeValue(branch||base);
return {id:branch&&branch.id||slugifyId(name,`branch_${index+1}`),name,type,enabled:!branch||branch.enabled!==false,required_for_completion:type==='normal'&&(!branch||branch.required_for_completion!==false),cooldown_ms:Number(branch&&branch.cooldown_ms)||0,run_once:!!(branch&&branch.run_once),steps};
}

function normalizeScenarioBranches(obj){
if(obj&&Array.isArray(obj.branches)&&obj.branches.length)return obj.branches.slice(0,8).map(normalizeScenarioBranch);
const steps=obj&&Array.isArray(obj.steps)?obj.steps.map(normalizeScenarioEditorStep):[];
return [defaultScenarioBranch(0,steps)];
}

function scenarioEditableJson(s,roomId){
const obj=s?JSON.parse(JSON.stringify(s)):{
id:'',name:'',room_id:roomId,branches:[defaultScenarioBranch(0,[])]}
;
obj.room_id=roomId;
obj.branches=normalizeScenarioBranches(obj);
delete obj.steps;
delete obj.step_count;
delete obj.valid;
delete obj.validation_issue_count;
delete obj.validation_issues;
return obj;
}

function scenarioActiveBranchIndex(scenario){
const branches=Array.isArray(scenario&&scenario.branches)?scenario.branches:[];
const max=Math.max(0,branches.length-1);
const raw=Number(scenarioEditor.active_branch);
if(!Number.isFinite(raw))return 0;
return Math.max(0,Math.min(max,Math.floor(raw)));
}

function scenarioActiveBranch(scenario){
const branches=Array.isArray(scenario&&scenario.branches)?scenario.branches:[];
if(!branches.length)return null;
return branches[scenarioActiveBranchIndex(scenario)]||branches[0];
}

function scenarioActiveSteps(scenario){
const branch=scenarioActiveBranch(scenario);
if(!branch)return [];
branch.steps=Array.isArray(branch.steps)?branch.steps:[];
return branch.steps;
}

function scenarioBranchStepOffset(branches,branchIndex){
let offset=0;
(Array.isArray(branches)?branches:[]).forEach((branch,index)=>{if(index<branchIndex)offset+=(Array.isArray(branch.steps)?branch.steps.length:0);});
return offset;
}

function scenarioTotalStepCount(branches){
return (Array.isArray(branches)?branches:[]).reduce((sum,branch)=>sum+(Array.isArray(branch.steps)?branch.steps.length:0),0);
}

function scenarioNextStepLocalIndex(steps){
const list=Array.isArray(steps)?steps:[];
let maxNumber=0;
list.forEach(step=>{
const match=String(step&&step.id||'').match(/^step_(\d+)(?:\D|$)/);
if(match)maxNumber=Math.max(maxNumber,Number(match[1])||0);
});
return Math.max(list.length,maxNumber);
}

function scenarioForEachStep(scenario,fn){
(Array.isArray(scenario&&scenario.branches)?scenario.branches:[]).forEach((branch,branchIndex)=>{
(Array.isArray(branch.steps)?branch.steps:[]).forEach((step,stepIndex)=>fn(step,branch,branchIndex,stepIndex));
});
}

function scenarioKnownFlagNames(scenario){
const names=new Set();
scenarioForEachStep(scenario||scenarioEditorSource(),step=>{
const type=scenarioStepTypeValue(step);
if(type==='SET_FLAG'&&step.flag_name)names.add(step.flag_name);
if(type==='WAIT_FLAGS'&&Array.isArray(step.flags)){
step.flags.forEach(flag=>{const item=normalizeScenarioFlagItem(flag);if(item.flag_name)names.add(item.flag_name);});
}
});
return Array.from(names).sort((a,b)=>a.localeCompare(b));
}

function renderScenarioFlagInput(value,attr){
const selected=String(value||'');
const flags=scenarioKnownFlagNames();
const input=`<input ${attr||''} placeholder='Flag name, e.g. puzzle_done' value='${esc(selected)}'>`;
if(!flags.length)return input;
const options=[`<option value=''>Use existing flag</option>`].concat(flags.map(name=>`<option value='${esc(name)}' ${name===selected?'selected':''}>${esc(name)}</option>`)).join('');
return `<div class='flag-picker'>${input}<select data-scenario-flag-suggest>${options}</select></div>`;
}

function scenarioStepTypeValue(s){
const raw=String((s&&s.type)||'WAIT_TIME');
const low=raw.toLowerCase();
if(low==='device_command')return 'DEVICE_COMMAND';
if(low==='device_command_group')return 'DEVICE_COMMAND_GROUP';
if(low==='wait_time')return 'WAIT_TIME';
if(low==='wait_device_event')return 'WAIT_DEVICE_EVENT';
if(low==='wait_any_device_event')return 'WAIT_ANY_DEVICE_EVENT';
if(low==='wait_all_device_events')return 'WAIT_ALL_DEVICE_EVENTS';
if(low==='end_game'||low==='finish_game')return 'END_GAME';
if(low==='operator_approval')return 'OPERATOR_APPROVAL';
if(low==='show_operator_message'||low==='operator_message')return 'SHOW_OPERATOR_MESSAGE';
if(low==='set_flag')return 'SET_FLAG';
if(low==='wait_flags')return 'WAIT_FLAGS';
return 'WAIT_TIME';
}

function scenarioStepIsWaitType(type){
type=scenarioStepTypeValue({type});
return type==='WAIT_TIME'||type==='WAIT_DEVICE_EVENT'||type==='WAIT_ANY_DEVICE_EVENT'||type==='WAIT_ALL_DEVICE_EVENTS'||type==='WAIT_FLAGS';
}

function scenarioFallbackStepSchemas(){
const skipFields=[{key:'allow_operator_skip',type:'checkbox',label:'Allow operator skip'},{key:'operator_skip_label',type:'text',label:'Skip label'}];
return [
{type:'DEVICE_COMMAND',label:'Device command',fields:[{key:'device_id',type:'device_select',label:'Device',required:true},{key:'command_id',type:'device_command_select',label:'Command',depends_on:'device_id',required:true},{key:'params',type:'params_object',label:'Parameters',depends_on:'command_id'}]},
{type:'DEVICE_COMMAND_GROUP',label:'Command group',fields:[{key:'commands',type:'command_group',label:'Commands',required:true}]},
{type:'WAIT_DEVICE_EVENT',label:'Wait device event',fields:[{key:'device_id',type:'device_select',label:'Device',required:true},{key:'event_id',type:'device_event_select',label:'Event',depends_on:'device_id',required:true},{key:'timeout_ms',type:'optional_duration_ms',label:'Timeout'},{key:'timeout_message',type:'textarea',label:'Timeout message'},...skipFields]},
{type:'WAIT_ANY_DEVICE_EVENT',label:'Wait any device event',fields:[{key:'events',type:'event_group',label:'Events',required:true},...skipFields]},
{type:'WAIT_ALL_DEVICE_EVENTS',label:'Wait all device events',fields:[{key:'events',type:'event_group',label:'Events',required:true},...skipFields]},
{type:'WAIT_TIME',label:'Wait time',fields:[{key:'duration_ms',type:'duration_ms',label:'Duration',required:true},...skipFields]},
{type:'OPERATOR_APPROVAL',label:'Operator approval',fields:[{key:'prompt',type:'text',label:'Prompt',required:true},{key:'approve_label',type:'text',label:'Button label'}]},
{type:'SHOW_OPERATOR_MESSAGE',label:'Show operator message',fields:[{key:'message',type:'textarea',label:'Message',required:true}]},
{type:'SET_FLAG',label:'Set flag',fields:[{key:'flag_name',type:'text',label:'Flag',required:true},{key:'value',type:'checkbox',label:'Value',required:true}]},
{type:'WAIT_FLAGS',label:'Wait flags',fields:[{key:'flags',type:'flag_list',label:'Flags',required:true},{key:'timeout_ms',type:'optional_duration_ms',label:'Timeout'},{key:'timeout_message',type:'textarea',label:'Timeout message'},...skipFields]},
{type:'END_GAME',label:'End game',fields:[]}
];
}

function scenarioStepSchemas(){
const catalog=scenarioEditorCatalog(scenarioEditor.room_id);
const schemas=Array.isArray(catalog.step_schemas)?catalog.step_schemas:[];
return schemas.length?schemas:scenarioFallbackStepSchemas();
}

function scenarioReactiveTriggerTypes(){
return ['WAIT_DEVICE_EVENT','WAIT_ANY_DEVICE_EVENT','WAIT_ALL_DEVICE_EVENTS','WAIT_FLAGS'];
}

function scenarioReactiveActionTypes(){
return ['DEVICE_COMMAND','DEVICE_COMMAND_GROUP','WAIT_TIME','SHOW_OPERATOR_MESSAGE','SET_FLAG'];
}

function scenarioAllowedStepTypesForBranch(branch){
if(scenarioBranchTypeValue(branch)!=='reactive')return null;
const steps=Array.isArray(branch&&branch.steps)?branch.steps:[];
return steps.length?scenarioReactiveActionTypes():scenarioReactiveTriggerTypes();
}

function scenarioStepSchema(type){
return scenarioStepSchemas().find(s=>s.type===type)||null;
}

function scenarioStepHelpText(type){
const normalized=scenarioStepTypeValue({type});
if(normalized==='DEVICE_COMMAND')return `Device command

Use when the scenario must press one saved device action: open a lock, turn on a screen, play audio.

Setup: choose a device, then choose one of its commands.

During game: the command is sent and the scenario immediately goes to the next step. If the command fails, the scenario stops on this step.`;
if(normalized==='DEVICE_COMMAND_GROUP')return `Command group

Use when several commands must happen as one moment: open two drawers and turn on TV.

Setup: add commands in the order they must run.

During game: commands are sent one by one. Any failed command stops the scenario.`;
if(normalized==='WAIT_DEVICE_EVENT')return `Wait device event

Use when players must do one specific thing on one device: solve UID order, press a sensor, finish a local puzzle.

Setup: choose the device and the event that means success.

During game: the scenario waits here until this exact event arrives. Operator Next can force it forward.`;
if(normalized==='WAIT_ANY_DEVICE_EVENT')return `Wait any device event

Use when several different events can continue the game: either keypad success or operator bypass device success.

Setup: add two to four device events.

During game: the first matching event continues the scenario.`;
if(normalized==='WAIT_ALL_DEVICE_EVENTS')return `Wait all device events

Use when several puzzles can be solved in any order, but all of them must be done before the scenario continues.

Example: wait for UID order solved, altar completed, and book placed.

Setup: add every required device event.

During game: each matching event is remembered. The scenario continues only after every listed event has arrived.`;
if(normalized==='WAIT_TIME')return `Wait time

Use for a simple delay between actions: wait 5 seconds after opening a drawer before starting audio.

Setup: enter seconds.

During game: the scenario continues automatically after the delay.`;
if(normalized==='OPERATOR_APPROVAL')return `Operator approval

Use when a human must confirm the next part: players solved a puzzle, room is safe to open, sensor is unreliable today.

Setup: write the text the operator should see and the button label.

During game: the scenario waits until the operator presses the button.`;
if(normalized==='SHOW_OPERATOR_MESSAGE')return `Show operator message

Use to leave a short note for the operator: send players to room 2, prepare actor, watch camera.

Setup: write the message.

During game: the message appears and the scenario continues.`;
if(normalized==='SET_FLAG')return `Set flag

Use to remember progress inside one scenario run.

Example: after a puzzle succeeds, set puzzle_done to true. Later another step can wait for puzzle_done before continuing.

Setup: write a short flag name and choose whether this step sets it to true or false.

During game: the scenario stores the value and immediately continues. Flags reset when the scenario starts again.`;
if(normalized==='WAIT_FLAGS')return `Wait flags

Use when the scenario must wait until earlier steps or branches have marked their work done.

Example: wait until puzzle_done is true and door_ready is true.

Setup: add one or more flag names and the expected value for each.

During game: all listed flags must match. Operator Next can still force the step.`;
if(normalized==='END_GAME')return `End game

Use when this branch reaches the real quest finish.

Setup: no fields are required.

During game: the game timer is finished and the game becomes completed. Audio is not stopped automatically; add a separate Stop audio command if you want silence.`;
return 'This step type does not have a help text yet.';
}

function scenarioStepTypeLabel(type){
const schema=scenarioStepSchema(type);
return schema&&(schema.label||schema.type)||type;
}

function durationMsToSeconds(ms){
const n=Number(ms);
if(!Number.isFinite(n)||n<=0)return 1;
return Math.max(1,Math.round(n/1000));
}

function durationSecondsToMs(seconds){
const n=Number(seconds);
if(!Number.isFinite(n)||n<=0)return 1000;
return Math.max(1,Math.round(n*1000));
}

function waitTimeLabel(ms){
const seconds=durationMsToSeconds(ms);
return `Wait ${seconds} sec`;
}

function scenarioTypeOptions(type){
const schemas=scenarioStepSchemas();
const normal=schemas.map(s=>s.type).filter(Boolean);
const all=normal.includes(type)?normal:[type].concat(normal);
return all.map(t=>`<option value='${esc(t)}' ${type===t?'selected':''}>${esc(scenarioStepTypeLabel(t))}</option>`).join('');
}

function scenarioCatalogDevices(){
const catalog=scenarioEditorCatalog(scenarioEditor.room_id);
const catalogDevices=Array.isArray(catalog.quest_devices)?catalog.quest_devices:[];
if(catalogDevices.length)return catalogDevices;
return questDevices().map(device=>({
id:device.id||'',name:device.name||device.id||'',room_id:device.room_id||'',commands:Array.isArray(device.commands)?device.commands:[],events:Array.isArray(device.events)?device.events:[]}
)).filter(device=>device.id);
}

function firstScenarioDevice(requireCommand){
const devices=scenarioCatalogDevices();
return devices.find(device=>!requireCommand||(Array.isArray(device.commands)&&device.commands.length))||devices[0]||null;
}

function firstCommandForDevice(device){
return device&&Array.isArray(device.commands)&&device.commands.length?device.commands[0]:null;
}

function defaultParamsForCommand(device,command){
const params={};
const deviceId=device&&device.id||'';
const commandId=command&&command.id||'';
if(deviceId==='system_audio'&&commandId==='play'){
params.volume=70;
params.channel='effect';
params.repeat=false;
}
return params;
}

function defaultScenarioCommandItem(){
const device=firstScenarioDevice(true);
const command=firstCommandForDevice(device);
return {device_id:device&&device.id||'',command_id:command&&command.id||''};
}

function firstDeviceWithEvent(){
const devices=scenarioCatalogDevices();
return devices.find(device=>Array.isArray(device.events)&&device.events.length)||devices[0]||null;
}

function firstEventForDevice(device){
return device&&Array.isArray(device.events)&&device.events.length?device.events[0]:null;
}

function defaultScenarioEventItem(){
const device=firstDeviceWithEvent();
const event=firstEventForDevice(device);
return {device_id:device&&device.id||'',event_id:event&&event.id||''};
}

function scenarioDeviceName(device){
return device&&(device.name||device.id)||'Device';
}

function scenarioRoomNameForDevice(device){
return roomName(device&&device.room_id||scenarioEditor.room_id);
}

function newScenarioStep(index,kind){
const n=index+1;
if(kind==='device_command'){
const device=firstScenarioDevice(true);
const command=firstCommandForDevice(device);
const room=scenarioRoomNameForDevice(device);
const devName=scenarioDeviceName(device);
const commandName=command&&(command.label||command.id)||'command';
return {id:`step_${n}`,label:`${room}: ${devName} - ${commandName}`,enabled:true,type:'DEVICE_COMMAND',device_id:device&&device.id||'',command_id:command&&command.id||'',params:defaultParamsForCommand(device,command)};
}
if(kind==='device_command_group'){
return {id:`step_${n}`,label:'Command group',enabled:true,type:'DEVICE_COMMAND_GROUP',commands:[defaultScenarioCommandItem()]};
}
if(kind==='wait_device_event'){
const device=firstDeviceWithEvent();
const event=firstEventForDevice(device);
const room=scenarioRoomNameForDevice(device);
const devName=scenarioDeviceName(device);
const eventName=event&&(event.label||event.id)||'event';
return {id:`step_${n}`,label:`${room}: wait ${devName} - ${eventName}`,enabled:true,type:'WAIT_DEVICE_EVENT',device_id:device&&device.id||'',event_id:event&&event.id||''};
}
if(kind==='wait_any_device_event'){
return {id:`step_${n}`,label:'Wait any device event',enabled:true,type:'WAIT_ANY_DEVICE_EVENT',events:[defaultScenarioEventItem()]};
}
if(kind==='wait_all_device_events'){
return {id:`step_${n}`,label:'Wait all device events',enabled:true,type:'WAIT_ALL_DEVICE_EVENTS',events:[defaultScenarioEventItem()]};
}
if(kind==='operator'){
return {id:`step_${n}`,label:'Operator approval',enabled:true,type:'OPERATOR_APPROVAL',prompt:'Continue?',approve_label:'Continue'};
}
if(kind==='operator_message'){
return {id:`step_${n}`,label:'Show operator message',enabled:true,type:'SHOW_OPERATOR_MESSAGE',message:'Check the room before continuing.'};
}
if(kind==='set_flag'){
return {id:`step_${n}`,label:'Set flag',enabled:true,type:'SET_FLAG',flag_name:'puzzle_done',value:true};
}
if(kind==='wait_flags'){
return {id:`step_${n}`,label:'Wait flags',enabled:true,type:'WAIT_FLAGS',flags:[{flag_name:'puzzle_done',value:true}]};
}
if(kind==='end_game'){
return {id:`step_${n}`,label:'End game',enabled:true,type:'END_GAME'};
}
return {id:`step_${n}`,label:waitTimeLabel(1000),enabled:true,type:'WAIT_TIME',duration_ms:1000};
}

function newScenarioStepForType(index,type){
const normalized=scenarioStepTypeValue({type});
if(normalized==='DEVICE_COMMAND')return newScenarioStep(index,'device_command');
if(normalized==='DEVICE_COMMAND_GROUP')return newScenarioStep(index,'device_command_group');
if(normalized==='WAIT_DEVICE_EVENT')return newScenarioStep(index,'wait_device_event');
if(normalized==='WAIT_ANY_DEVICE_EVENT')return newScenarioStep(index,'wait_any_device_event');
if(normalized==='WAIT_ALL_DEVICE_EVENTS')return newScenarioStep(index,'wait_all_device_events');
if(normalized==='OPERATOR_APPROVAL')return newScenarioStep(index,'operator');
if(normalized==='SHOW_OPERATOR_MESSAGE')return newScenarioStep(index,'operator_message');
if(normalized==='SET_FLAG')return newScenarioStep(index,'set_flag');
if(normalized==='WAIT_FLAGS')return newScenarioStep(index,'wait_flags');
if(normalized==='END_GAME')return newScenarioStep(index,'end_game');
return newScenarioStep(index,'wait_time');
}

function scenarioStepPresetButtons(branch){
const allowed=scenarioAllowedStepTypesForBranch(branch);
const allowedSet=allowed?new Set(allowed):null;
const schemas=scenarioStepSchemas().filter(schema=>!allowedSet||allowedSet.has(schema.type||''));
const title=allowedSet?(Array.isArray(branch&&branch.steps)&&branch.steps.length?'Add action':'Add trigger'):'Add step';
const hasSteps=Array.isArray(branch&&branch.steps)&&branch.steps.length>0;
const hint=allowedSet&&!hasSteps?`<div class='row-meta scenario-reaction-hint'>Add one trigger first. Actions become available after the trigger.</div>`:'';
return `<h2 class='section-title'>${esc(title)}</h2>${hint}<div class='scenario-step-presets'>${schemas.map(schema=>`<div class='scenario-step-preset-row'><button data-scenario-step-action='add_schema' data-scenario-step-type='${esc(schema.type||'WAIT_TIME')}'>${esc(schema.label||schema.type)}</button><button class='icon-btn' title='Show example' aria-label='Show step example' data-scenario-step-help='${esc(schema.type||'WAIT_TIME')}'>?</button></div>`).join('')}</div>`;
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

function audioFileIsWav(path){
return /\.wav$/i.test(String(path||''));
}

function audioFileIsPlayableEffect(path){
return /\.(wav|mp3)$/i.test(String(path||''));
}

function audioChannelValue(values){
const raw=String(values&&values.channel||'effect').toLowerCase();
return raw==='background'||raw==='bg'||raw==='music'?'background':'effect';
}

function renderAudioChannelParam(key,label,value){
const selected=audioChannelValue({channel:value});
return `<div class='row'><select class='scenario-select' data-step-param='${esc(key)}'><option value='effect' ${selected==='effect'?'selected':''}>Effect / one-shot</option><option value='background' ${selected==='background'?'selected':''}>Background / music bed (WAV only)</option></select></div>`;
}

function renderAudioFileParam(key,label,value,channel){
scheduleGMAudioFilesLoad();
const selected=value===undefined?'':String(value||'');
const background=String(channel||'effect')==='background';
const files=gmAudioFileItems().filter(item=>background?audioFileIsWav(item.path):audioFileIsPlayableEffect(item.path));
const refresh=`<button data-audio-files-refresh='1' ${gmAudioFiles.loading?'disabled':''}>${gmAudioFiles.loading?'Loading files':'Refresh files'}</button>`;
if(files.length){
const selectedKnown=files.some(item=>item.path===selected);
const selectedAllowed=!selected||(background?audioFileIsWav(selected):audioFileIsPlayableEffect(selected));
const custom=selected&&!selectedKnown?`<option value='${esc(selected)}' selected>${esc(selected)} ${selectedAllowed?'(custom)':'(not allowed for selected channel)'}</option>`:'';
const options=files.map(item=>{
const labelText=`${audioDirName(item.path)} / ${audioBaseName(item.path)}`;
return `<option value='${esc(item.path)}' ${item.path===selected?'selected':''}>${esc(labelText)}</option>`;
}).join('');
return `<div class='row'><select class='scenario-select' data-step-param='${esc(key)}'><option value='' ${selected?'':'selected'}>${esc(label||'Select audio file')}</option>${custom}${options}</select>${refresh}</div>${background?`<div class='row-meta'>Background accepts WAV only. Starting a new background replaces the previous one.</div>`:''}`;
}
const statusText=gmAudioFiles.error?gmAudioFiles.error:(gmAudioFiles.loading?'Scanning /sdcard for audio files...':(background?'No WAV files loaded yet':'No audio files loaded yet'));
return `<div class='row'><input data-step-param='${esc(key)}' placeholder='${esc(label||'Audio file path')}' value='${esc(selected)}'>${refresh}</div><div class='row-meta'>${esc(statusText)}</div>`;
}

function renderCommandParams(command,params){
const schema=command&&Array.isArray(command.params_schema)?command.params_schema:[];
const values=params&&typeof params==='object'?params:{};
if(!schema.length)return '';
if(!commandSupportsScenarioParams(command)){
return `<div class='row-meta warn-text'>Parameters are not applied to MQTT payload yet. This command publishes its saved static payload.</div>`;
}
return `<div class='builder-param-list'>${schema.map(param=>{
const key=param.key||'';
const label=param.label||key;
let value=values[key];
if(value===undefined&&command&&command.id==='play'&&key==='volume')value=70;
if(value===undefined&&command&&command.id==='play'&&key==='channel')value='effect';
if(value===undefined&&command&&command.id==='play'&&key==='repeat')value=false;
if(command&&command.id==='play'&&key==='repeat'){
return audioChannelValue(values)==='background'?`<label class='row-meta'><input data-step-param='${esc(key)}' type='checkbox' ${value?'checked':''} style='min-width:auto'> Repeat background track</label>`:'';
}
if(param.type==='checkbox')return `<label class='row-meta'><input data-step-param='${esc(key)}' type='checkbox' ${value?'checked':''} style='min-width:auto'> ${esc(label)}</label>`;
if(command&&command.id==='play'&&key==='channel')return renderAudioChannelParam(key,label,value);
if(param.type==='audio_file_select')return renderAudioFileParam(key,label,value,audioChannelValue(values));
const inputType=param.type==='number'?'number':'text';
return `<div class='row'><input data-step-param='${esc(key)}' type='${inputType}' placeholder='${esc(label)}' value='${esc(value===undefined?'':value)}'></div>`;
}).join('')}</div>`;
}

function renderDeviceCommandPayload(step){
const devices=scenarioCatalogDevices().filter(device=>Array.isArray(device.commands)&&device.commands.length);
let selectedDevice=step.device_id||((devices[0]&&devices[0].id)||'');
const device=scenarioDeviceById(selectedDevice)||devices[0]||null;
if(device&&!selectedDevice)selectedDevice=device.id||'';
const commands=device&&Array.isArray(device.commands)?device.commands:[];
let selectedCommand=scenarioValidCommandId(device,step.command_id);
const command=commands.find(cmd=>cmd.id===selectedCommand)||commands[0]||null;
if(command&&!selectedCommand)selectedCommand=command.id||'';
const deviceControl=devices.length?`<select class='scenario-select' data-step-field='device_id'>${optionList(devices,selectedDevice,'Select device')}</select>`:`<input data-step-field='device_id' placeholder='Device ID' value='${esc(selectedDevice)}'>`;
const commandControl=commands.length?`<select class='scenario-select' data-step-field='command_id'>${optionList(commands,selectedCommand,'Select command')}</select>`:`<input data-step-field='command_id' placeholder='Command ID' value='${esc(selectedCommand)}'>`;
return `<div class='row'>${deviceControl}${commandControl}</div>${renderCommandParams(command,step.params)}`;
}

function renderCommandGroupControl(step){
const commands=Array.isArray(step.commands)&&step.commands.length?step.commands:[defaultScenarioCommandItem()];
const devices=scenarioCatalogDevices().filter(device=>Array.isArray(device.commands)&&device.commands.length);
return `<div class='command-group-list'>${commands.map((item,index)=>{
let selectedDevice=item.device_id||((devices[0]&&devices[0].id)||'');
const device=scenarioDeviceById(selectedDevice)||devices[0]||null;
if(device&&!selectedDevice)selectedDevice=device.id||'';
const deviceCommands=device&&Array.isArray(device.commands)?device.commands:[];
const selectedCommand=scenarioValidCommandId(device,item.command_id);
const deviceControl=devices.length?`<select class='scenario-select' data-group-command-field='device_id'>${optionList(devices,selectedDevice,'Select device')}</select>`:`<input data-group-command-field='device_id' placeholder='Device ID' value='${esc(selectedDevice)}'>`;
const commandItems=deviceCommands.map(cmd=>({id:cmd.id,name:cmd.label||cmd.id}));
const commandControl=deviceCommands.length?`<select class='scenario-select' data-group-command-field='command_id'>${optionList(commandItems,selectedCommand,'Select command')}</select>`:`<input data-group-command-field='command_id' placeholder='Command ID' value='${esc(selectedCommand)}'>`;
return `<div class='command-group-item' data-command-group-item='${index}'><div class='row compact-row'><span class='row-meta'>${index+1}.</span>${deviceControl}${commandControl}<button class='icon-btn danger' title='Remove command' aria-label='Remove command' data-scenario-step-action='group_delete' data-command-index='${index}'>&times;</button></div></div>`;
}).join('')}<button data-scenario-step-action='group_add'>Add command</button></div>`;
}

function renderEventGroupControl(step){
const events=Array.isArray(step.events)&&step.events.length?step.events:[defaultScenarioEventItem()];
const devices=scenarioCatalogDevices().filter(device=>Array.isArray(device.events)&&device.events.length);
return `<div class='command-group-list'>${events.map((item,index)=>{
let selectedDevice=item.device_id||((devices[0]&&devices[0].id)||'');
const device=scenarioDeviceById(selectedDevice)||devices[0]||null;
if(device&&!selectedDevice)selectedDevice=device.id||'';
const deviceEvents=device&&Array.isArray(device.events)?device.events:[];
const selectedEvent=scenarioValidEventId(device,item.event_id);
const deviceControl=devices.length?`<select class='scenario-select' data-event-group-field='device_id'>${optionList(devices,selectedDevice,'Select device')}</select>`:`<input data-event-group-field='device_id' placeholder='Device ID' value='${esc(selectedDevice)}'>`;
const eventItems=deviceEvents.map(event=>({id:event.id,name:event.label||event.id}));
const eventControl=deviceEvents.length?`<select class='scenario-select' data-event-group-field='event_id'>${optionList(eventItems,selectedEvent,'Select event')}</select>`:`<input data-event-group-field='event_id' placeholder='Event ID' value='${esc(selectedEvent)}'>`;
return `<div class='command-group-item' data-event-group-item='${index}'><div class='row compact-row'><span class='row-meta'>${index+1}.</span>${deviceControl}${eventControl}<button class='icon-btn danger' title='Remove event' aria-label='Remove event' data-scenario-step-action='event_group_delete' data-event-index='${index}'>&times;</button></div></div>`;
}).join('')}<button data-scenario-step-action='event_group_add'>Add event</button></div>`;
}

function normalizeScenarioFlagItem(item){
return {flag_name:item&&((item.flag_name!==undefined?item.flag_name:item.name)||'')||'',value:item&&item.value===false?false:true};
}

function defaultScenarioFlagItem(){
return {flag_name:'puzzle_done',value:true};
}

function renderFlagListControl(step){
const flags=Array.isArray(step.flags)&&step.flags.length?step.flags.map(normalizeScenarioFlagItem):[defaultScenarioFlagItem()];
return `<div class='command-group-list'>${flags.map((item,index)=>`<div class='command-group-item' data-flag-list-item='${index}'><div class='row compact-row'><span class='row-meta'>${index+1}.</span>${renderScenarioFlagInput(item.flag_name,`data-flag-list-field='flag_name'`)}<select data-flag-list-field='value'><option value='true' ${item.value!==false?'selected':''}>is true</option><option value='false' ${item.value===false?'selected':''}>is false</option></select><button class='icon-btn danger' title='Remove flag' aria-label='Remove flag' data-scenario-step-action='flag_list_delete' data-flag-index='${index}'>&times;</button></div></div>`).join('')}<button data-scenario-step-action='flag_list_add'>Add flag</button></div>`;
}

function renderSetFlagPayload(step){
const value=step.value===false?false:true;
return `<div class='row compact-row'><div class='field-stack'><span>Flag name</span>${renderScenarioFlagInput(step.flag_name||'',`data-step-field='flag_name'`)}</div><label class='field-stack'><span>Set value</span><select data-step-field='value'><option value='true' ${value?'selected':''}>true / completed</option><option value='false' ${!value?'selected':''}>false / reset</option></select></label></div><div class='row-meta'>Use the same flag name in Wait flags. Flags are temporary and reset when this scenario starts again.</div>`;
}

function renderWaitDeviceEventPayload(step){
const devices=scenarioCatalogDevices().filter(device=>Array.isArray(device.events)&&device.events.length);
let selectedDevice=step.device_id||((devices[0]&&devices[0].id)||'');
const device=scenarioDeviceById(selectedDevice)||devices[0]||null;
if(device&&!selectedDevice)selectedDevice=device.id||'';
const events=device&&Array.isArray(device.events)?device.events:[];
let selectedEvent=scenarioValidEventId(device,step.event_id);
const eventControl=events.length?`<select class='scenario-select' data-step-field='event_id'>${optionList(events,selectedEvent,'Select event')}</select>`:`<input data-step-field='event_id' placeholder='Event ID' value='${esc(selectedEvent)}'>`;
const deviceControl=devices.length?`<select class='scenario-select' data-step-field='device_id'>${optionList(devices,selectedDevice,'Select device')}</select>`:`<input data-step-field='device_id' placeholder='Device ID' value='${esc(selectedDevice)}'>`;
return `<div class='row'>${deviceControl}${eventControl}</div>`;
}

function scenarioDevicesForStepType(type){
const devices=scenarioCatalogDevices();
if(type==='DEVICE_COMMAND'||type==='DEVICE_COMMAND_GROUP')return devices.filter(device=>Array.isArray(device.commands)&&device.commands.length);
if(type==='WAIT_DEVICE_EVENT'||type==='WAIT_ANY_DEVICE_EVENT'||type==='WAIT_ALL_DEVICE_EVENTS')return devices.filter(device=>Array.isArray(device.events)&&device.events.length);
return devices;
}

function scenarioSelectedDeviceForStep(type,step){
const devices=scenarioDevicesForStepType(type);
return scenarioDeviceById(step.device_id)||devices[0]||null;
}

function renderSchemaFieldControl(schema,field,step){
const type=schema&&schema.type||scenarioStepTypeValue(step);
const key=field.key||'';
const label=field.label||key;
const fieldType=field.type||'text';
const selectedDevice=scenarioSelectedDeviceForStep(type,step);
if(fieldType==='device_select'){
const devices=scenarioDevicesForStepType(type);
const selected=step.device_id||((selectedDevice&&selectedDevice.id)||'');
return devices.length?`<select class='scenario-select' data-step-field='${esc(key)}'>${optionList(devices,selected,'Select device')}</select>`:`<input data-step-field='${esc(key)}' placeholder='Device ID' value='${esc(selected)}'>`;
}
if(fieldType==='device_command_select'){
const commands=selectedDevice&&Array.isArray(selectedDevice.commands)?selectedDevice.commands:[];
const selected=scenarioValidCommandId(selectedDevice,step.command_id);
const items=commands.map(cmd=>({id:cmd.id,name:cmd.label||cmd.id}));
return commands.length?`<select class='scenario-select' data-step-field='${esc(key)}'>${optionList(items,selected,'Select command')}</select>`:`<input data-step-field='${esc(key)}' placeholder='Command ID' value='${esc(selected)}'>`;
}
if(fieldType==='device_event_select'){
const events=selectedDevice&&Array.isArray(selectedDevice.events)?selectedDevice.events:[];
const selected=scenarioValidEventId(selectedDevice,step.event_id);
const items=events.map(event=>({id:event.id,name:event.label||event.id}));
return events.length?`<select class='scenario-select' data-step-field='${esc(key)}'>${optionList(items,selected,'Select event')}</select>`:`<input data-step-field='${esc(key)}' placeholder='Event ID' value='${esc(selected)}'>`;
}
if(fieldType==='params_object'){
const commands=selectedDevice&&Array.isArray(selectedDevice.commands)?selectedDevice.commands:[];
const commandId=scenarioValidCommandId(selectedDevice,step.command_id);
const command=scenarioCommandById(selectedDevice&&selectedDevice.id,commandId);
return renderCommandParams(command,step.params);
}
if(fieldType==='command_group'){
return renderCommandGroupControl(step);
}
if(fieldType==='event_group'){
return renderEventGroupControl(step);
}
if(fieldType==='flag_list'){
return renderFlagListControl(step);
}
if(fieldType==='duration_ms'){
return `<input data-step-field='${esc(key)}' type='number' min='1' step='1' placeholder='${esc(label)} sec' value='${esc(durationMsToSeconds(step[key]||1000))}'><span class='row-meta'>sec</span>`;
}
if(fieldType==='optional_duration_ms'){
return `<input data-step-field='${esc(key)}' type='number' min='0' step='1' placeholder='${esc(label)} sec, 0 = no timeout' value='${esc(step[key]?durationMsToSeconds(step[key]):'')}'><span class='row-meta'>sec timeout</span>`;
}
if(fieldType==='checkbox'){
return `<label class='row-meta'><input data-step-field='${esc(key)}' type='checkbox' ${step[key]?'checked':''} style='min-width:auto'> ${esc(label)}</label>`;
}
if(fieldType==='textarea'){
return `<textarea class='scenario-textarea' rows='1' data-step-field='${esc(key)}' placeholder='${esc(label)}'>${esc(step[key]||'')}</textarea>`;
}
const inputType=fieldType==='number'?'number':'text';
return `<input data-step-field='${esc(key)}' type='${inputType}' placeholder='${esc(label)}' value='${esc(step[key]||'')}'>`;
}

function renderScenarioSchemaPayload(step,type){
const schema=scenarioStepSchema(type);
const fields=schema&&Array.isArray(schema.fields)?schema.fields:[];
if(!fields.length)return '';
let row=[];
const flush=()=>{
if(!row.length)return '';
const html=`<div class='row'>${row.join('')}</div>`;
row=[];
return html;
};
let out='';
fields.forEach(field=>{
const control=renderSchemaFieldControl(schema,field,step);
if(!control)return;
if((field.type||'')==='params_object'||(field.type||'')==='command_group'||(field.type||'')==='event_group'||(field.type||'')==='flag_list'){
out+=flush()+control;
}
else if((field.type||'')==='checkbox'||(field.type||'')==='textarea'){
out+=flush()+control;
}
else{
row.push(control);
if(row.length>=2)out+=flush();
}
});
out+=flush();
return out;
}

function renderScenarioStepPayload(step,type){
if(type==='SET_FLAG')return renderSetFlagPayload(step);
if(scenarioStepSchema(type))return renderScenarioSchemaPayload(step,type);
if(type==='OPERATOR_APPROVAL')return `<div class='row'><input data-step-field='prompt' placeholder='Operator prompt' value='${esc(step.prompt||step.operator_prompt||'')}'><input data-step-field='approve_label' placeholder='Approve label' value='${esc(step.approve_label||step.operator_approve_label||'Continue')}'></div>`;
return `<div class='row'><input data-step-field='duration_ms' type='number' min='1' step='1' placeholder='Duration sec' value='${esc(durationMsToSeconds(step.duration_ms||1000))}'><span class='row-meta'>sec</span></div>`;
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
const tab=item=>`<button class='scenario-branch-tab ${item.index===activeIndex?'active':''}' data-scenario-branch-action='select' data-branch-index='${item.index}'><span>${esc(item.branch.name||item.branch.id||`Branch ${item.index+1}`)}</span><em>${esc((Array.isArray(item.branch.steps)?item.branch.steps.length:0))}</em></button>`;
return `<div class='scenario-branch-tabs grouped'><div class='scenario-branch-tab-group'><span class='row-meta'>Scenario flow</span>${flow.map(tab).join('')}<button class='scenario-branch-add' data-scenario-branch-action='add'>+ Branch</button></div><div class='scenario-branch-tab-group'><span class='row-meta'>Reactions</span>${reactions.map(tab).join('')}<button class='scenario-branch-add' data-scenario-branch-action='add_reactive'>+ Reaction</button></div></div>`;
}

function renderScenarioBranchSettings(branch,index,total){
if(!branch)return '';
const branchIdKey=`scenario:branch:${scenarioEditor.room_id}:${branch.id||index}`;
const type=scenarioBranchTypeValue(branch);
const typeField=type==='normal'?`<div class='field-stack'><span>Type</span><select data-scenario-branch-field='type'><option value='normal' selected>Scenario flow</option><option value='reactive'>Reaction</option></select></div>`:`<input type='hidden' data-scenario-branch-field='type' value='reactive'>`;
const controls=type==='normal'?`<label class='row-meta branch-toggle'><input data-scenario-branch-field='required_for_completion' type='checkbox' ${branch.required_for_completion!==false?'checked':''}> Required for finish</label>`:`<label class='row-meta branch-toggle'><input data-scenario-branch-field='run_once' type='checkbox' ${branch.run_once?'checked':''}> Run once</label><div class='field-stack compact-field'><span>Cooldown, sec</span><input data-scenario-branch-field='cooldown_sec' type='number' min='0' step='1' value='${esc(Math.round((Number(branch.cooldown_ms)||0)/1000))}'></div>`;
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
draft.branches.push(defaultScenarioBranch(nextIndex,[],branchType));
scenarioEditor.active_branch=nextIndex;
scenarioEditor.expanded_step=-1;
}
else if(action==='delete'){
const removeIndex=Number.isFinite(index)?index:scenarioActiveBranchIndex(draft);
if(draft.branches.length<=1)return;
if(!confirm('Delete this scenario branch?'))return;
draft.branches.splice(removeIndex,1);
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

function scenarioEditorSource(){
const roomId=scenarioEditor.room_id;
if(scenarioEditor.draft&&scenarioEditor.draft.room_id===roomId)return JSON.parse(JSON.stringify(scenarioEditor.draft));
const editing=roomScenarios(roomId).find(s=>s.id===scenarioEditor.scenario_id)||null;
return scenarioEditableJson(editing,roomId);
}

function collectScenarioEditor(){
const source=scenarioEditorSource();
if(!Array.isArray(source.branches)||!source.branches.length)source.branches=normalizeScenarioBranches(source);
const branchIndex=scenarioActiveBranchIndex(source);
const branches=source.branches.map((branch,index)=>({
...normalizeScenarioBranch(branch,index),
steps:Array.isArray(branch.steps)?branch.steps.map(step=>JSON.parse(JSON.stringify(step))):[]}
));
const activeBranch=branches[branchIndex]||branches[0];
const branchName=document.querySelector('[data-scenario-branch-field="name"]');
const branchId=document.querySelector('[data-scenario-branch-field="id"]');
const branchType=document.querySelector('[data-scenario-branch-field="type"]');
const branchEnabled=document.querySelector('[data-scenario-branch-field="enabled"]');
const branchRequired=document.querySelector('[data-scenario-branch-field="required_for_completion"]');
const branchCooldown=document.querySelector('[data-scenario-branch-field="cooldown_sec"]');
const branchRunOnce=document.querySelector('[data-scenario-branch-field="run_once"]');
const previousActiveSteps=activeBranch&&Array.isArray(activeBranch.steps)?activeBranch.steps.map(step=>JSON.parse(JSON.stringify(step))):[];
if(activeBranch){
activeBranch.name=(branchName&&branchName.value)||activeBranch.name||`Branch ${branchIndex+1}`;
activeBranch.id=(branchId&&branchId.value)||activeBranch.id||slugifyId(activeBranch.name,`branch_${branchIndex+1}`);
activeBranch.type=branchType?scenarioBranchTypeValue({type:branchType.value}):scenarioBranchTypeValue(activeBranch);
activeBranch.enabled=branchEnabled?branchEnabled.checked:activeBranch.enabled!==false;
activeBranch.required_for_completion=activeBranch.type==='normal'&&(branchRequired?branchRequired.checked:activeBranch.required_for_completion!==false);
activeBranch.cooldown_ms=activeBranch.type==='reactive'?Math.max(0,Math.round(Number(branchCooldown&&branchCooldown.value)||0))*1000:0;
activeBranch.run_once=activeBranch.type==='reactive'&&!!(branchRunOnce&&branchRunOnce.checked);
activeBranch.steps=[];
}
const scenario={
id:(document.getElementById('scenario_id')||{
}
).value||'',name:(document.getElementById('scenario_name')||{
}
).value||'',room_id:scenarioEditor.room_id,branches}
;

document.querySelectorAll('[data-scenario-step]').forEach((el,index)=>{
const previous=previousActiveSteps[index]?JSON.parse(JSON.stringify(previousActiveSteps[index])):{};
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
renderedItems.forEach((item,itemIndex)=>{const deviceField=item.querySelector('[data-group-command-field="device_id"]');const commandField=item.querySelector('[data-group-command-field="command_id"]');const previousItem=Array.isArray(previous.commands)?(previous.commands[itemIndex]||{}):{};step.commands.push({device_id:(deviceField?deviceField.value:'')||previousItem.device_id||'',command_id:(commandField?commandField.value:'')||previousItem.command_id||''});});}
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
const base=(scenarioEditor.draft&&scenarioEditor.draft.room_id===roomId)?scenarioEditor.draft:scenarioEditableJson(editing,roomId);
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
const editorHtml=editorOpen?`<div class='card scenario-editor-card'><div class='scenario-editor-head'><div><h2 class='section-title'>${editing?'Edit scenario':'New scenario'}${scenarioEditor.dirty?' *':''}</h2><input id='scenario_name' placeholder='Scenario name' value='${esc(base.name||'')}'></div><div class='actions'><button data-scenario-validate='1'>Validate</button><button data-scenario-save='1'>Save</button></div></div><details class='scenario-advanced compact-advanced' ${detailsAttrs(scenarioIdKey,false)}><summary>Scenario id</summary><div class='row'><input id='scenario_id' placeholder='Scenario ID' value='${esc(base.id||'')}'></div></details>${issueHtml}${renderScenarioBranchTabs(base,activeBranchIndex)}${renderScenarioBranchSettings(activeBranch,activeBranchIndex,base.branches.length)}<div class='scenario-editor-layout'><aside class='scenario-add-panel'>${scenarioStepPresetButtons(activeBranch)}</aside><section class='scenario-steps-panel'><h2 class='section-title'>Steps: ${esc(activeBranch&&activeBranch.name||'Branch')}</h2><div>${activeSteps.length?activeSteps.map((step,i)=>renderScenarioStepEditor(step,i,activeSteps.length,Number(scenarioEditor.expanded_step)===i,issuesByStep[i]||[])).join(''):`<div class='empty'>${esc(emptyStepsText)}</div>`}</div></section></div><details style='margin-top:10px' ${detailsAttrs(jsonKey,false)}><summary class='row-meta'>Debug JSON</summary><textarea id='scenario_json' class='builder-json' readonly>${esc(json)}</textarea></details></div>`:`<div class='card empty'><h2 class='section-title'>Scenario editor</h2><div class='row-meta'>Select a scenario or create a new one.</div></div>`;
return `<div class='scenario-room-bar'><div><span class='row-meta'>Room</span><select class='scenario-select' data-scenario-room-select>${rooms.map(r=>`<option value='${esc(r.room_id)}' ${
r.room_id===roomId?'selected':''}
>${
esc(r.title||r.room_id)}
</option>`).join('')}</select></div><div class='row-meta'>Steps can target devices in any room.</div></div><div class='scenario-admin-layout'><section><div class='card-head'><h2 class='section-title'>Scenarios</h2><div class='actions'><button data-scenario-new='1'>Add scenario</button></div></div><div class='list'>${rows}</div></section><section>${editorHtml}</section></div>`;
}
