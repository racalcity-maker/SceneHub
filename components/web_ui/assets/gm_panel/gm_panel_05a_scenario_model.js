// GM panel source part. Edit this file, then rebuild gm_panel.js.
function inferScenarioEditorStepType(step){
const raw=String(step&&step.type||'').trim();
const low=raw.toLowerCase();
if(low==='device_command')return 'DEVICE_COMMAND';
if(low==='device_command_group')return 'DEVICE_COMMAND_GROUP';
if(low==='wait_device_event')return 'WAIT_DEVICE_EVENT';
if(low==='wait_any_device_event')return 'WAIT_ANY_DEVICE_EVENT';
if(low==='wait_all_device_events')return 'WAIT_ALL_DEVICE_EVENTS';
if(low==='operator_approval')return 'OPERATOR_APPROVAL';
if(low==='show_operator_message'||low==='operator_message')return 'SHOW_OPERATOR_MESSAGE';
if(low==='set_flag')return 'SET_FLAG';
if(low==='wait_flags')return 'WAIT_FLAGS';
if(low==='end_game'||low==='finish_game')return 'END_GAME';
if(step&&step.device_id&&step.event_id)return 'WAIT_DEVICE_EVENT';
if(step&&Array.isArray(step.events)&&step.events.length)return 'WAIT_ANY_DEVICE_EVENT';
if(step&&(step.prompt||step.operator_prompt||step.approve_label||step.operator_approve_label))return 'OPERATOR_APPROVAL';
if(step&&(step.message||step.operator_message))return 'SHOW_OPERATOR_MESSAGE';
if(step&&Array.isArray(step.commands)&&step.commands.length)return 'DEVICE_COMMAND_GROUP';
if(step&&step.device_id&&step.command_id)return 'DEVICE_COMMAND';
if(step&&Array.isArray(step.flags)&&step.flags.length)return 'WAIT_FLAGS';
if(step&&step.flag_name)return 'SET_FLAG';
if(low==='wait_time')return 'WAIT_TIME';
return 'WAIT_TIME';
}

function normalizeScenarioEditorStep(step){
step=step||{};
const type=inferScenarioEditorStepType(step);
const out={
id:step.id||'',label:step.label||'',enabled:step.enabled!==false,type}
;if(step.allow_operator_skip)out.allow_operator_skip=true;if(step.operator_skip_label)out.operator_skip_label=step.operator_skip_label;if(step.device_id)out.device_id=step.device_id;if(step.scenario_id)out.scenario_id=step.scenario_id;if(step.command_id)out.command_id=step.command_id;if(step.event_id)out.event_id=step.event_id;if(step.params)out.params=step.params;if(step.duration_ms!==undefined&&step.duration_ms!==null)out.duration_ms=Number(step.duration_ms)||0;if(step.event_type)out.event_type=step.event_type;if(step.source_id)out.source_id=step.source_id;if(step.operator_prompt)out.prompt=step.operator_prompt;if(step.operator_approve_label)out.approve_label=step.operator_approve_label;if(step.prompt)out.prompt=step.prompt;if(step.approve_label)out.approve_label=step.approve_label;if(Array.isArray(step.commands))out.commands=step.commands.map(cmd=>({device_id:cmd.device_id||'',command_id:cmd.command_id||'',params:scenarioHydrateCommandParams(cmd.device_id||'',cmd.command_id||'',cmd.params&&typeof cmd.params==='object'?cmd.params:null)}));if(Array.isArray(step.events))out.events=step.events.map(ev=>({device_id:ev.device_id||'',event_id:ev.event_id||''}));if(Array.isArray(step.flags))out.flags=step.flags.map(flag=>({flag_name:flag.flag_name||flag.name||'',value:flag.value!==false}));if(step.message)out.message=step.message;if(step.operator_message)out.message=step.operator_message;if(step.flag_name)out.flag_name=step.flag_name;if(step.flag_value!==undefined)out.value=!!step.flag_value;if(step.value!==undefined)out.value=!!step.value;if(type==='WAIT_TIME'&&(!Number.isFinite(Number(out.duration_ms))||Number(out.duration_ms)<=0))out.duration_ms=1000;if(out.device_id&&out.command_id)out.params=scenarioHydrateCommandParams(out.device_id,out.command_id,out.params&&typeof out.params==='object'?out.params:null);return out;}
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
if(type==='reactive')return ensureReactiveV2Branch(out);
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

function scenarioWorkingDraft(){
const source=scenarioEditorSource();
return source?scenarioClone(source):null;
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

function scenarioStepSchemas(){
const catalog=scenarioEditorCatalog(scenarioEditor.room_id);
const schemas=Array.isArray(catalog.step_schemas)?catalog.step_schemas:[];
return schemas;
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
if(!gmHardwareIo.loaded&&!gmHardwareIo.loading&&typeof loadHardwareIoStatus==='function'){
setTimeout(()=>loadHardwareIoStatus(false),0);
}
const catalog=scenarioEditorCatalog(scenarioEditor.room_id);
const catalogDevices=Array.isArray(catalog.quest_devices)?catalog.quest_devices:[];
const base=catalogDevices.length?catalogDevices:questDevices().map(device=>({
id:device.id||'',name:device.name||device.id||'',room_id:device.room_id||'',device_description:device.device_description,commands:Array.isArray(device.commands)?device.commands:[],events:Array.isArray(device.events)?device.events:[]}
)).filter(device=>device.id);
return base.map(scenarioNormalizeHardwareDevice).filter(device=>device.id&&(Array.isArray(device.commands)&&device.commands.length||Array.isArray(device.events)&&device.events.length||device.id!=='system_io'));
}

function compactManifest(device){
const manifest=device&&device.device_description;
return manifest&&Number(manifest.manifest_version)===2&&manifest.format==='compact_resources'&&manifest.node_kind&&manifest.capability_contract==='scenehub.node.compact.v1'?manifest:null;
}

function compactResourceId(item,target){
if(target==='led_strips')return String(item&&item.strip!==undefined?item.strip:'');
return String(item&&item.channel!==undefined?item.channel:'');
}

function compactResourceLabel(item,target){
const id=compactResourceId(item,target);
const fallback=target==='led_strips'?`LED Strip ${id}`:target==='mosfets'?`MOSFET ${id}`:target==='outputs'?`Output ${id}`:target==='inputs'?`Input ${id}`:`Relay ${id}`;
return item&&(item.label||fallback)||fallback;
}

function compactResourceOptions(manifest,target){
const resources=manifest&&manifest.resources&&typeof manifest.resources==='object'?manifest.resources:{};
return (Array.isArray(resources[target])?resources[target]:[]).map(item=>({id:compactResourceId(item,target),name:compactResourceLabel(item,target)})).filter(item=>item.id);
}

function compactSchemaForTemplate(manifest,template){
const schemas=manifest&&manifest.schemas&&typeof manifest.schemas==='object'?manifest.schemas:{};
const ref=template&&template.args_schema_ref||'';
const schema=Array.isArray(schemas[ref])?schemas[ref]:[];
const target=template&&template.target||'';
const resourceOptions=compactResourceOptions(manifest,target);
return schema.map(param=>{
const out={...param};
if(out.type==='resource_channel'){
out.resource_target=target;
out.resource_options=resourceOptions;
}
return out;
});
}

function compactPolicy(policy){
policy=policy&&typeof policy==='object'?policy:{};
return {
manual_allowed:policy.manual_allowed===false?false:true,
scenario_allowed:policy.scenario_allowed===false?false:true,
requires_confirmation:!!policy.requires_confirmation,
result_required:policy.result_required===false?false:true,
timeout_ms:Number(policy.timeout_ms)||3000,
danger_level:String(policy.danger_level||'normal')
};
}

function compactCommandsForDevice(device){
const manifest=compactManifest(device);
if(!manifest)return Array.isArray(device&&device.commands)?device.commands:[];
return (Array.isArray(manifest.command_templates)?manifest.command_templates:[]).map(template=>{
const commandName=String(template&&template.command||'');
return {
id:String(template&&template.id||commandName),
label:String(template&&template.label||template&&template.id||commandName),
capability:String(template&&template.capability||template&&template.target||commandName.split('.')[0]||'node'),
command:commandName,
target:template&&template.target||'',
default_args:template&&template.default_args&&typeof template.default_args==='object'?template.default_args:undefined,
policy:compactPolicy(template&&template.policy),
manual_allowed:!template||!template.policy||template.policy.manual_allowed!==false,
scenario_allowed:!template||!template.policy||template.policy.scenario_allowed!==false,
args_schema:compactSchemaForTemplate(manifest,template)
};
}).filter(command=>command.id&&command.command);
}

function compactEventsForDevice(device){
const manifest=compactManifest(device);
if(!manifest)return Array.isArray(device&&device.events)?device.events:[];
return (Array.isArray(manifest.event_templates)?manifest.event_templates:[]).map(template=>{
const eventName=String(template&&template.event||'');
return {
id:String(template&&template.id||eventName),
label:String(template&&template.label||template&&template.id||eventName),
capability:String(template&&template.capability||template&&template.source||eventName.split('.')[0]||'node'),
event:eventName,
source:template&&template.source||''
};
}).filter(event=>event.id&&event.event);
}

function scenarioIoModeText(mode){
const value=Number(mode)||0;
if(value===1)return 'input';
if(value===2)return 'output';
return 'disabled';
}

function scenarioIoChannelFromId(id){
const match=String(id||'').match(/^ch([1-4])_/);
return match?Number(match[1]):0;
}

function scenarioHardwareStatusItems(key){
const data=gmHardwareIo&&gmHardwareIo.data;
return data&&Array.isArray(data[key])?data[key]:[];
}

function scenarioEnabledChannels(key){
return new Set(scenarioHardwareStatusItems(key).filter(item=>item&&item.enabled).map(item=>Number(item.channel)||0));
}

function scenarioNormalizeChannelCommand(command,channelLabelPrefix,channels){
const cmd={...command};
cmd.label=cmd.label||cmd.id||'Command';
cmd.channel_options=Array.from(channels||[]).filter(Boolean).sort((a,b)=>a-b).map(channel=>({id:String(channel),name:`${channelLabelPrefix} ${channel}`}));
cmd.args_schema=Array.isArray(cmd.args_schema)?cmd.args_schema:[];
cmd.args_schema=cmd.args_schema.map(param=>param&&param.key==='channel'?{...param,label:'Channel'}:param);
return cmd;
}

function scenarioNormalizeHardwareDevice(device){
if(!device||!device.id)return device;
const out={...device};
out.commands=compactCommandsForDevice(device).slice();
out.events=compactEventsForDevice(device).slice();
if(device.id==='system_relay'){
const channels=scenarioEnabledChannels('relays');
out.name='Relay channels';
out.commands=out.commands.filter(cmd=>cmd.id!=='toggle'||cmd.manual_allowed!==false).map(cmd=>scenarioNormalizeChannelCommand(cmd,'Relay',channels));
if(gmHardwareIo.loaded&&!channels.size)out.commands=[];
}
else if(device.id==='system_mosfet'){
const channels=scenarioEnabledChannels('mosfets');
out.name='MOSFET channels';
out.commands=out.commands.map(cmd=>scenarioNormalizeChannelCommand(cmd,'MOSFET',channels));
if(gmHardwareIo.loaded&&!channels.size)out.commands=out.commands.filter(cmd=>cmd.id==='all_off');
}
else if(device.id==='system_io'){
const items=scenarioHardwareStatusItems('ios');
const inputChannels=new Set(items.filter(item=>item&&item.enabled&&scenarioIoModeText(item.mode)==='input').map(item=>Number(item.channel)||0));
const outputChannels=new Set(items.filter(item=>item&&item.enabled&&scenarioIoModeText(item.mode)==='output').map(item=>Number(item.channel)||0));
out.name='IO channels';
out.commands=out.commands.filter(cmd=>cmd.id==='get_state'?inputChannels.size||outputChannels.size:outputChannels.size).map(cmd=>scenarioNormalizeChannelCommand(cmd,'IO',cmd.id==='get_state'?new Set([...inputChannels,...outputChannels]):outputChannels));
out.events=out.events.filter(event=>inputChannels.has(scenarioIoChannelFromId(event.id)));
}
return out;
}

function firstScenarioDevice(requireCommand){
const devices=scenarioCatalogDevices();
return devices.find(device=>!requireCommand||(Array.isArray(device.commands)&&device.commands.length))||devices[0]||null;
}

function firstCommandForDevice(device){
return device&&Array.isArray(device.commands)&&device.commands.length?device.commands[0]:null;
}

function defaultParamsForCommand(device,command){
if(command&&command.default_args&&typeof command.default_args==='object'){
return JSON.parse(JSON.stringify(command.default_args));
}
const params={};
const deviceId=device&&device.id||'';
const commandId=command&&command.id||'';
if(deviceId==='system_audio'&&commandId==='play'){
params.volume=70;
params.channel='effect';
params.repeat=false;
}
const schema=command&&Array.isArray(command.args_schema)?command.args_schema:[];
schema.forEach(param=>{
const key=param&&param.key||'';
if(!key||params[key]!==undefined)return;
if(param.type==='resource_channel'&&Array.isArray(param.resource_options)&&param.resource_options.length){
const raw=param.resource_options[0].id;
const num=Number(raw);
params[key]=Number.isFinite(num)?num:String(raw);
}
else if(param.type==='select'&&Array.isArray(param.options)&&param.options.length){
params[key]=String(param.options[0]);
}
else if(param.type==='checkbox'&&param.optional!==true){
params[key]=false;
}
});
return deviceId==='system_audio'&&commandId==='play'?scenarioNormalizeAudioParams(params):params;
}

function scenarioHydrateCommandParams(deviceId,commandId,params,device,command){
const targetDevice=device||scenarioDeviceById(deviceId);
const targetCommand=command||scenarioCommandById(deviceId,commandId);
const defaults=defaultParamsForCommand(targetDevice,targetCommand);
const existing=params&&typeof params==='object'?params:{};
const merged={...(defaults&&typeof defaults==='object'?defaults:{}),...existing};
if(String(deviceId||'')==='system_audio'&&String(commandId||'')==='play'){
const normalized=scenarioNormalizeAudioParams(merged);
return Object.keys(normalized).length?normalized:undefined;
}
return Object.keys(merged).length?merged:undefined;
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

function scenarioStepAutoLabel(step){
const type=scenarioStepTypeValue(step);
if(type==='DEVICE_COMMAND'){
if(String(step&&step.device_id||'')==='system_audio')return scenarioAudioCommandAutoLabel(step);
const device=scenarioDeviceById(step&&step.device_id||'');
return `${scenarioRoomNameForDevice(device)}: ${scenarioDeviceName(device)} - ${scenarioCommandName(step&&step.device_id||'',step&&step.command_id||'')}`;
}
if(type==='DEVICE_COMMAND_GROUP')return `Command group (${(Array.isArray(step&&step.commands)?step.commands:[]).length||1})`;
if(type==='WAIT_DEVICE_EVENT'){
const device=scenarioDeviceById(step&&step.device_id||'');
return `${scenarioRoomNameForDevice(device)}: wait ${scenarioDeviceName(device)} - ${scenarioDeviceEventName(step&&step.device_id||'',step&&step.event_id||'')}`;
}
if(type==='WAIT_ANY_DEVICE_EVENT')return `Wait any device event (${(Array.isArray(step&&step.events)?step.events:[]).length||1})`;
if(type==='WAIT_ALL_DEVICE_EVENTS')return `Wait all device events (${(Array.isArray(step&&step.events)?step.events:[]).length||1})`;
if(type==='WAIT_TIME')return waitTimeLabel(step&&step.duration_ms);
if(type==='OPERATOR_APPROVAL')return `Operator approval: ${step&&step.prompt||step&&step.operator_prompt||'approval'}`;
if(type==='SHOW_OPERATOR_MESSAGE')return `Show operator: ${step&&step.message||'message'}`;
if(type==='SET_FLAG')return `Set ${(step&&step.flag_name)||'flag'} = ${(step&&step.value)===false?'false':'true'}`;
if(type==='WAIT_FLAGS')return `Wait flags (${(Array.isArray(step&&step.flags)?step.flags:[]).length||1})`;
if(type==='END_GAME')return 'End game';
return step&&step.label||type;
}

function scenarioRefreshAutoLabel(step,previous){
if(!step)return step;
const current=String(step.label||'').trim();
const prevLabel=String(previous&&previous.label||'').trim();
const prevAuto=previous?String(scenarioStepAutoLabel(previous)||'').trim():'';
const legacyAudioAuto=String(step.device_id||'')==='system_audio'&&
  String(step.command_id||'')==='play'&&
  scenarioAudioMissingAutoLabel(current);
if(!current||current===prevLabel||current===prevAuto||legacyAudioAuto){
step.label=scenarioStepAutoLabel(step);
}
return step;
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

