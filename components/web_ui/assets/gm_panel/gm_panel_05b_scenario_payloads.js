// GM panel source part. Edit this file, then rebuild gm_panel.js.
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

function scenarioNormalizeAudioParams(params){
const out=params&&typeof params==='object'?{...params}:{};
out.channel=audioChannelValue(out);
if(out.channel!=='background')out.repeat=false;
return out;
}

function scenarioAudioValidationIssue(params){
const normalized=scenarioNormalizeAudioParams(params);
const file=String(normalized.file||'').trim();
if(!file)return {code:'device_command_audio_file_missing',message:'Choose an audio file.'};
if(normalized.channel==='background'&&!audioFileIsWav(file))return {code:'device_command_audio_background_requires_wav',message:'Background audio requires a WAV file.'};
if(normalized.channel!=='background'&&!audioFileIsPlayableEffect(file))return {code:'device_command_audio_file_invalid',message:'Effect audio must be a playable file.'};
return null;
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
const selectedAllowed=!selected||(background?audioFileIsWav(selected):audioFileIsPlayableEffect(selected));
const invalidHint=selected&&!selectedAllowed
  ? `<div class='row-meta bad-text'>${background?'Background audio requires a WAV file.':'Selected file is not valid for the current audio channel.'}</div>`
  : '';
const refresh=uiButton({label:gmAudioFiles.loading?'Loading files':'Refresh files',action:'audio.files.refresh',disabled:gmAudioFiles.loading});
if(files.length){
const selectedKnown=files.some(item=>item.path===selected);
const custom=selected&&!selectedKnown?`<option value='${esc(selected)}' selected>${esc(selected)} ${selectedAllowed?'(custom)':'(not allowed for selected channel)'}</option>`:'';
const options=files.map(item=>{
const labelText=`${audioDirName(item.path)} / ${audioBaseName(item.path)}`;
return `<option value='${esc(item.path)}' ${item.path===selected?'selected':''}>${esc(labelText)}</option>`;
}).join('');
return `<div class='row'><select class='scenario-select' data-step-param='${esc(key)}'><option value='' ${selected?'':'selected'}>${esc(label||'Select audio file')}</option>${custom}${options}</select>${refresh}</div>${background?`<div class='row-meta'>Background accepts WAV only. Starting a new background replaces the previous one.</div>`:''}${invalidHint}`;
}
const statusText=gmAudioFiles.error?gmAudioFiles.error:(gmAudioFiles.loading?'Scanning /sdcard for audio files...':(background?'No WAV files loaded yet':'No audio files loaded yet'));
return `<div class='row'><input data-step-param='${esc(key)}' placeholder='${esc(label||'Audio file path')}' value='${esc(selected)}'>${refresh}</div><div class='row-meta'>${esc(statusText)}</div>${invalidHint}`;
}

function renderCommandParams(command,params){
const schema=command&&Array.isArray(command.args_schema)?command.args_schema:[];
const values=params&&typeof params==='object'?params:{};
if(!schema.length)return '';
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
if(param.type==='resource_channel'&&Array.isArray(param.resource_options)&&param.resource_options.length){
const selected=value===undefined?param.resource_options[0].id:String(value);
return `<div class='row'><select class='scenario-select' data-step-param='${esc(key)}' data-step-param-type='number'>${optionList(param.resource_options,selected,`Select ${label}`)}</select></div>`;
}
if(param.type==='select'&&Array.isArray(param.options)&&param.options.length){
const items=param.options.map(option=>({id:String(option),name:String(option)}));
const selected=value===undefined?items[0].id:String(value);
return `<div class='row'><select class='scenario-select' data-step-param='${esc(key)}'>${optionList(items,selected,`Select ${label}`)}</select></div>`;
}
if(key==='channel'&&command&&Array.isArray(command.channel_options)&&command.channel_options.length){
const selected=value===undefined?command.channel_options[0].id:String(value);
return `<div class='row'><select class='scenario-select' data-step-param='${esc(key)}'>${optionList(command.channel_options,selected,'Select channel')}</select></div>`;
}
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
const command=deviceCommands.find(cmd=>cmd.id===selectedCommand)||deviceCommands[0]||null;
const deviceControl=devices.length?`<select class='scenario-select' data-group-command-field='device_id'>${optionList(devices,selectedDevice,'Select device')}</select>`:`<input data-group-command-field='device_id' placeholder='Device ID' value='${esc(selectedDevice)}'>`;
const commandItems=deviceCommands.map(cmd=>({id:cmd.id,name:cmd.label||cmd.id}));
const commandControl=deviceCommands.length?`<select class='scenario-select' data-group-command-field='command_id'>${optionList(commandItems,selectedCommand,'Select command')}</select>`:`<input data-group-command-field='command_id' placeholder='Command ID' value='${esc(selectedCommand)}'>`;
const paramsHtml=renderCommandParams(command,item.params);
return `<div class='command-group-item' data-command-group-item='${index}'><div class='row compact-row'><span class='row-meta'>${index+1}.</span>${deviceControl}${commandControl}<button class='icon-btn danger' title='Remove command' aria-label='Remove command' data-action='scenario.step' data-op='group_delete' data-command-index='${index}'>&times;</button></div>${paramsHtml}</div>`;
}).join('')}<button data-action='scenario.step' data-op='group_add'>Add command</button></div>`;
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
return `<div class='command-group-item' data-event-group-item='${index}'><div class='row compact-row'><span class='row-meta'>${index+1}.</span>${deviceControl}${eventControl}<button class='icon-btn danger' title='Remove event' aria-label='Remove event' data-action='scenario.step' data-op='event_group_delete' data-event-index='${index}'>&times;</button></div></div>`;
}).join('')}<button data-action='scenario.step' data-op='event_group_add'>Add event</button></div>`;
}

function normalizeScenarioFlagItem(item){
return {flag_name:item&&((item.flag_name!==undefined?item.flag_name:item.name)||'')||'',value:item&&item.value===false?false:true};
}

function defaultScenarioFlagItem(){
return {flag_name:'puzzle_done',value:true};
}

function renderFlagListControl(step){
const flags=Array.isArray(step.flags)&&step.flags.length?step.flags.map(normalizeScenarioFlagItem):[defaultScenarioFlagItem()];
return `<div class='command-group-list'>${flags.map((item,index)=>`<div class='command-group-item' data-flag-list-item='${index}'><div class='row compact-row'><span class='row-meta'>${index+1}.</span>${renderScenarioFlagInput(item.flag_name,`data-flag-list-field='flag_name'`)}<select data-flag-list-field='value'><option value='true' ${item.value!==false?'selected':''}>is true</option><option value='false' ${item.value===false?'selected':''}>is false</option></select><button class='icon-btn danger' title='Remove flag' aria-label='Remove flag' data-action='scenario.step' data-op='flag_list_delete' data-flag-index='${index}'>&times;</button></div></div>`).join('')}<button data-action='scenario.step' data-op='flag_list_add'>Add flag</button></div>`;
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
type=scenarioStepTypeValue({type});
if(type==='DEVICE_COMMAND')return renderDeviceCommandPayload(step);
if(type==='DEVICE_COMMAND_GROUP')return renderCommandGroupControl(step);
if(type==='WAIT_DEVICE_EVENT')return renderWaitDeviceEventPayload(step);
if(type==='WAIT_ANY_DEVICE_EVENT'||type==='WAIT_ALL_DEVICE_EVENTS')return renderEventGroupControl(step);
if(type==='WAIT_FLAGS')return renderFlagListControl(step);
if(type==='SET_FLAG')return renderSetFlagPayload(step);
if(scenarioStepSchema(type))return renderScenarioSchemaPayload(step,type);
if(type==='OPERATOR_APPROVAL')return `<div class='row'><input data-step-field='prompt' placeholder='Operator prompt' value='${esc(step.prompt||step.operator_prompt||'')}'><input data-step-field='approve_label' placeholder='Approve label' value='${esc(step.approve_label||step.operator_approve_label||'Continue')}'></div>`;
if(type==='SHOW_OPERATOR_MESSAGE')return `<textarea class='scenario-textarea' rows='1' data-step-field='message' placeholder='Operator message'>${esc(step.message||'')}</textarea>`;
if(type==='END_GAME')return '';
return `<div class='row'><input data-step-field='duration_ms' type='number' min='1' step='1' placeholder='Duration sec' value='${esc(durationMsToSeconds(step.duration_ms||1000))}'><span class='row-meta'>sec</span></div>`;
}

