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
const policy=branch&&branch.policy&&typeof branch.policy==='object'?branch.policy:null;
const cooldownMs=Number(branch&&branch.cooldown_ms)||Number(policy&&policy.cooldown_ms)||0;
const out={id:branch&&branch.id||slugifyId(name,`branch_${index+1}`),name,type,enabled:!branch||branch.enabled!==false,required_for_completion:type==='normal'&&(!branch||branch.required_for_completion!==false),cooldown_ms:cooldownMs,run_once:!!(branch&&branch.run_once),steps};
if(branch&&typeof branch==='object'){
['priority','max_fire_count','trigger','guard_flags','policy','reentry','variants','result_policy','on_complete'].forEach(key=>{
if(branch[key]!==undefined)out[key]=JSON.parse(JSON.stringify(branch[key]));
});
}
return out;
}

function uniqueScenarioBranchId(branches,type){
const used=new Set((Array.isArray(branches)?branches:[]).map(branch=>String(branch&&branch.id||'')));
const reactive=type==='reactive';

if(!reactive&&!used.has('main'))return 'main';

const prefix=reactive?'reaction':'branch';
let n=reactive?1:2;

while(used.has(`${prefix}_${n}`))n++;

return `${prefix}_${n}`;
}

function scenarioBranchNumberFromId(id,type){
const reactive=type==='reactive';
if(!reactive&&id==='main')return 1;
const prefix=reactive?'reaction_':'branch_';
const raw=String(id||'');
if(raw.indexOf(prefix)!==0)return 1;
const n=Number(raw.slice(prefix.length));
return Number.isFinite(n)&&n>0?n:1;
}

function defaultScenarioBranchName(id,type){
const reactive=type==='reactive';
const n=scenarioBranchNumberFromId(id,type);
if(!reactive&&id==='main')return 'Main';
return reactive?`Reaction ${n}`:`Branch ${n}`;
}

function normalizeScenarioBranches(obj){
if(obj&&Array.isArray(obj.branches)&&obj.branches.length){
const normalized=obj.branches.slice(0,8).map(normalizeScenarioBranch);
const used=new Set();
normalized.forEach((branch,index)=>{
const type=scenarioBranchTypeValue(branch);
let id=String(branch&&branch.id||'').trim();
if(!id||used.has(id)){
id=uniqueScenarioBranchId(normalized.slice(0,index),type);
branch.id=id;
}
used.add(id);
if(!branch.name){
branch.name=defaultScenarioBranchName(branch.id,type);
}
});
return normalized;
}
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

function scenarioClone(obj){
return obj?JSON.parse(JSON.stringify(obj)):obj;
}

function scenarioRestoreMissingOriginalBranches(source){
if(!source||!scenarioEditor.original_scenario)return source;
const original=scenarioEditor.original_scenario;
if((source.id||'')!==(original.id||'')||(source.room_id||'')!==(original.room_id||''))return source;
if(!Array.isArray(source.branches)||!Array.isArray(original.branches))return source;
const shrinkFloor=Number(scenarioEditor.branch_count_shrink_floor)||0;
if(scenarioEditor.branch_count_shrink_allowed&&shrinkFloor>0&&source.branches.length>=shrinkFloor)return source;
if(scenarioEditor.branch_count_shrink_allowed&&shrinkFloor<=0)return source;
if(source.branches.length>=original.branches.length)return source;
const currentById=new Map(source.branches.map(branch=>[String(branch&&branch.id||''),branch]));
const originalIds=new Set(original.branches.map(branch=>String(branch&&branch.id||'')));
const merged=[];
original.branches.forEach(branch=>{
const id=String(branch&&branch.id||'');
merged.push(currentById.has(id)?currentById.get(id):scenarioClone(branch));
});
source.branches.forEach(branch=>{
const id=String(branch&&branch.id||'');
if(!originalIds.has(id))merged.push(branch);
});
source.branches=merged;
return source;
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
if(!flags.length)return `<input ${attr||''} placeholder='Flag name, e.g. puzzle_done' value='${esc(selected)}'>`;
const listId=`scenario_flag_names_${++gmFlagDatalistSeq}`;
const options=flags.map(name=>`<option value='${esc(name)}'></option>`).join('');
return `<input ${attr||''} list='${esc(listId)}' placeholder='Flag name, e.g. puzzle_done' value='${esc(selected)}'><datalist id='${esc(listId)}'>${options}</datalist>`;
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
return {device_id:device&&device.id||'',command_id:command&&command.id||'',params:defaultParamsForCommand(device,command)};
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

