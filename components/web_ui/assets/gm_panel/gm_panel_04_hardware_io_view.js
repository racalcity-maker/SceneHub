// GM panel source part. Edit this file, then rebuild gm_panel.js.
const HARDWARE_IO_RELAY_DEVICE='system_relay';
const HARDWARE_IO_MOSFET_DEVICE='system_mosfet';
const HARDWARE_IO_IO_DEVICE='system_io';

const HARDWARE_IO_SCHEMAS={
relayPulse:[{name:'duration_ms',label:'Pulse ms',type:'number',min:1,step:1,default:1000},{name:'on_ms',label:'On ms',type:'number',min:1,step:1,default:500},{name:'off_ms',label:'Off ms',type:'number',min:1,step:1,default:500},{name:'count',label:'Count',type:'number',min:0,step:1,default:3}],
mosfetSet:[{name:'value',label:'Value 0-255',type:'number',min:0,max:255,step:1,default:255}],
mosfetFade:[{name:'target',label:'Target 0-255',type:'number',min:0,max:255,step:1,default:255},{name:'duration_ms',label:'Fade ms',type:'number',min:1,step:1,default:1000}],
mosfetPulse:[{name:'value',label:'Value 0-255',type:'number',min:0,max:255,step:1,default:255},{name:'duration_ms',label:'Pulse ms',type:'number',min:1,step:1,default:1000}],
ioPulse:[{name:'duration_ms',label:'Pulse ms',type:'number',min:1,step:1,default:1000},{name:'on_ms',label:'On ms',type:'number',min:1,step:1,default:500},{name:'off_ms',label:'Off ms',type:'number',min:1,step:1,default:500},{name:'count',label:'Count',type:'number',min:0,step:1,default:3}],
blink:[{name:'on_ms',label:'On ms',type:'number',min:1,step:1,default:500},{name:'off_ms',label:'Off ms',type:'number',min:1,step:1,default:500},{name:'count',label:'Count',type:'number',min:0,step:1,default:3}],
mosfetBlink:[{name:'value',label:'Value 0-255',type:'number',min:0,max:255,step:1,default:255},{name:'on_ms',label:'On ms',type:'number',min:1,step:1,default:500},{name:'off_ms',label:'Off ms',type:'number',min:1,step:1,default:500},{name:'count',label:'Count',type:'number',min:0,step:1,default:3}],
mosfetBreathe:[{name:'min',label:'Min 0-255',type:'number',min:0,max:255,step:1,default:0},{name:'max',label:'Max 0-255',type:'number',min:0,max:255,step:1,default:255},{name:'fade_ms',label:'Fade ms',type:'number',min:1,step:1,default:1000},{name:'hold_ms',label:'Hold ms',type:'number',min:0,step:1,default:0},{name:'count',label:'Count',type:'number',min:0,step:1,default:3}],
};

async function loadHardwareIoStatus(renderAfter){
if(gmHardwareIo.loading)return;
hardwareIoCaptureForms();
gmHardwareIo.loading=true;
try{
const res=await api.hardwareIo.status();
gmHardwareIo.data=res.ok?await res.json():null;
gmHardwareIo.error=res.ok?'':await gmResponseText(res);
gmHardwareIo.loaded=true;
}
catch(err){
gmHardwareIo.error=err.message||'Hardware IO status failed';
}
gmHardwareIo.loading=false;
if(renderAfter)render();
}

function hardwareIoStatusItem(kind,channel){
const data=gmHardwareIo&&gmHardwareIo.data;
const items=data&&Array.isArray(data[kind])?data[kind]:[];
return items.find(item=>Number(item.channel)===Number(channel))||null;
}

function hardwareIoRelayStatusBadge(channel){
const item=hardwareIoStatusItem('relays',channel);
if(!item)return uiBadge('unknown');
return uiBadge(item.enabled?(item.on?'on':'off'):'disabled',item.enabled?(item.on?'selected-badge':''):'');
}

function hardwareIoMosfetStatusBadge(channel){
const item=hardwareIoStatusItem('mosfets',channel);
if(!item)return uiBadge('unknown');
if(!item.enabled)return uiBadge('disabled');
const mode=item.effect_active?'effect':(item.fade_active?'fade':(item.pulse_active?'pulse':'value'));
return uiBadge(`${mode} ${item.value||0}`,item.value||item.fade_active||item.pulse_active||item.effect_active?'selected-badge':'');
}

function hardwareIoGpioModeText(mode){
const value=Number(mode)||0;
if(value===1)return 'input';
if(value===2)return 'output';
return 'disabled';
}

function hardwareIoGpioModeValue(mode){
const text=hardwareIoGpioModeText(mode);
return text==='input'?'input':(text==='output'?'output':'disabled');
}

function hardwareIoGpioModeOptions(mode){
const selected=hardwareIoGpioModeValue(mode);
return ['disabled','input','output'].map(value=>`<option value='${esc(value)}' ${value===selected?'selected':''}>${esc(value)}</option>`).join('');
}

function hardwareIoGpioStatusBadge(channel){
const item=hardwareIoStatusItem('ios',channel);
if(!item)return uiBadge('unknown');
if(!item.enabled)return uiBadge('disabled');
const mode=hardwareIoGpioModeText(item.mode);
if(mode==='output')return uiBadge(item.active?'active':'inactive',item.active?'selected-badge':'');
return uiBadge(item.active?'active':'inactive',item.active?'selected-badge':'');
}

function hardwareIoChannelMeta(kind,channel){
const item=hardwareIoStatusItem(kind,channel);
if(!item)return 'Status not loaded yet';
const parts=[item.enabled?'enabled':'disabled'];
if(kind==='relays')parts.push(item.active_low?'active low':'active high');
if(kind==='mosfets')parts.push(`${item.pwm_freq_hz||0} Hz`);
if(kind==='ios')parts.push(hardwareIoGpioModeText(item.mode),item.active_low?'active low':'active high',item.physical_high?'pin high':'pin low');
return parts.join(' / ');
}

function hardwareIoCommand(deviceId,commandId){
const dev=questDeviceById(deviceId);
return dev&&Array.isArray(dev.commands)?dev.commands.find(cmd=>(cmd.id||'')===commandId):null;
}

function hardwareIoServiceAvailable(){
const data=gmHardwareIo&&gmHardwareIo.data;
return !!(data&&data.service&&data.service.available);
}

function hardwareIoServiceMessage(){
const data=gmHardwareIo&&gmHardwareIo.data;
if(!data||!data.service)return gmHardwareIo.error||'Hardware IO status is not loaded.';
const svc=data.service;
if(svc.available&&!svc.fault)return 'Hardware IO service is available.';
if(svc.error)return svc.error;
if(svc.last_error)return `hardware_io error ${svc.last_error}`;
return 'hardware_io_unavailable';
}

function hardwareIoAvailable(deviceId,commandId){
return hardwareIoServiceAvailable()&&!!hardwareIoCommand(deviceId,commandId);
}

function hardwareIoFormStore(){
if(!gmHardwareIo.forms||typeof gmHardwareIo.forms!=='object')gmHardwareIo.forms={};
return gmHardwareIo.forms;
}

function hardwareIoFormModel(scope,defaults){
return {...(defaults||{}),...(hardwareIoFormStore()[scope]||{})};
}

function hardwareIoCaptureForms(){
if(currentView!=='hardware_io'||typeof document==='undefined')return;
document.querySelectorAll('[data-hardware-form]').forEach(form=>{
const schemaName=form.dataset.hardwareSchema||'';
const scope=form.dataset.hardwareScope||'';
const schema=HARDWARE_IO_SCHEMAS[schemaName]||[];
if(!scope||!schema.length)return;
hardwareIoFormStore()[scope]=collectFormFields(form,schema,scope);
});
}

function hardwareIoParams(el){
let params={};
if(el.dataset.params){
try{
params=JSON.parse(el.dataset.params);
}
catch(err){
throw new Error('Invalid hardware action params');
}
}
if(el.dataset.noForm==='1')return params;
const form=el.closest('[data-hardware-form]');
const schemaName=form&&form.dataset.hardwareSchema||'';
const schema=HARDWARE_IO_SCHEMAS[schemaName]||[];
const scope=form&&form.dataset.hardwareScope||'';
const fields=collectFormFields(form,schema,scope);
if(scope)hardwareIoFormStore()[scope]=fields;
return {...params,...fields};
}

function hardwareIoButton(label,deviceId,commandId,params,opts){
opts=opts||{};
return uiButton({
label,
kind:opts.kind||'',
action:'hardware.command',
dataset:{
'device-id':deviceId,
'command-id':commandId,
params:JSON.stringify(params||{}),
noForm:opts.noForm?'1':undefined,
},
disabled:opts.disabled||!hardwareIoAvailable(deviceId,commandId),
confirm:opts.confirm||'',
});
}

function hardwareIoRelayChannel(channel){
const scope=`relay_${channel}`;
const pulseSchema=HARDWARE_IO_SCHEMAS.relayPulse;
const channelStatus=hardwareIoStatusItem('relays',channel);
const disabled=channelStatus?channelStatus.enabled===false:false;
return `<section class='builder-step' data-hardware-form='relay' data-hardware-schema='relayPulse' data-hardware-scope='${esc(scope)}'><div class='builder-step-head'><div><div class='builder-step-title'>Relay ${esc(channel)}</div><div class='row-meta'>${esc(hardwareIoChannelMeta('relays',channel))}</div></div>${hardwareIoRelayStatusBadge(channel)}</div><div class='field-grid'>${renderFormFields(pulseSchema,hardwareIoFormModel(scope,{duration_ms:1000}),scope)}</div>${uiActions([
hardwareIoButton('On',HARDWARE_IO_RELAY_DEVICE,'set',{channel,on:true},{kind:'approve',disabled,noForm:true}),
hardwareIoButton('Off',HARDWARE_IO_RELAY_DEVICE,'set',{channel,on:false},{disabled,noForm:true}),
hardwareIoButton('Pulse',HARDWARE_IO_RELAY_DEVICE,'pulse',{channel},{kind:'approve',disabled}),
hardwareIoButton('Blink',HARDWARE_IO_RELAY_DEVICE,'blink',{channel},{kind:'approve',disabled}),
hardwareIoButton('Toggle',HARDWARE_IO_RELAY_DEVICE,'toggle',{channel},{disabled,noForm:true}),
])}</section>`;
}

function hardwareIoMosfetChannel(channel){
const scope=`mosfet_${channel}`;
const effectsKey=`hardware-io:mosfet:${channel}:effects`;
const channelStatus=hardwareIoStatusItem('mosfets',channel);
const disabled=channelStatus?channelStatus.enabled===false:false;
return `<section class='builder-step compact-step' data-hardware-channel='mosfet' data-hardware-scope='${esc(scope)}'><div class='builder-step-head'><div><div class='builder-step-title'>MOSFET ${esc(channel)}</div><div class='row-meta'>${esc(hardwareIoChannelMeta('mosfets',channel))}</div></div>${hardwareIoMosfetStatusBadge(channel)}</div><div class='grid cols-3'><div data-hardware-form='mosfet-set' data-hardware-schema='mosfetSet' data-hardware-scope='${esc(scope)}_set'>${renderFormFields(HARDWARE_IO_SCHEMAS.mosfetSet,hardwareIoFormModel(`${scope}_set`,{value:255}),`${scope}_set`)}${uiActions([hardwareIoButton('Set',HARDWARE_IO_MOSFET_DEVICE,'set',{channel},{kind:'approve',disabled}),hardwareIoButton('Off',HARDWARE_IO_MOSFET_DEVICE,'set',{channel,value:0},{kind:'danger',disabled,noForm:true})])}</div><div data-hardware-form='mosfet-fade' data-hardware-schema='mosfetFade' data-hardware-scope='${esc(scope)}_fade'>${renderFormFields(HARDWARE_IO_SCHEMAS.mosfetFade,hardwareIoFormModel(`${scope}_fade`,{target:255,duration_ms:1000}),`${scope}_fade`)}${uiActions([hardwareIoButton('Fade',HARDWARE_IO_MOSFET_DEVICE,'fade',{channel},{disabled})])}</div><div data-hardware-form='mosfet-pulse' data-hardware-schema='mosfetPulse' data-hardware-scope='${esc(scope)}_pulse'>${renderFormFields(HARDWARE_IO_SCHEMAS.mosfetPulse,hardwareIoFormModel(`${scope}_pulse`,{value:255,duration_ms:1000}),`${scope}_pulse`)}${uiActions([hardwareIoButton('Pulse',HARDWARE_IO_MOSFET_DEVICE,'pulse',{channel},{disabled})])}</div></div><details class='scenario-advanced compact-advanced' ${detailsAttrs(effectsKey,false)}><summary>Effects</summary><div class='grid cols-2'><div data-hardware-form='mosfet-blink' data-hardware-schema='mosfetBlink' data-hardware-scope='${esc(scope)}_blink'>${renderFormFields(HARDWARE_IO_SCHEMAS.mosfetBlink,hardwareIoFormModel(`${scope}_blink`,{value:255,on_ms:500,off_ms:500,count:3}),`${scope}_blink`)}${uiActions([hardwareIoButton('Blink',HARDWARE_IO_MOSFET_DEVICE,'blink',{channel,final_value:0},{disabled})])}</div><div data-hardware-form='mosfet-breathe' data-hardware-schema='mosfetBreathe' data-hardware-scope='${esc(scope)}_breathe'>${renderFormFields(HARDWARE_IO_SCHEMAS.mosfetBreathe,hardwareIoFormModel(`${scope}_breathe`,{min:0,max:255,fade_ms:1000,hold_ms:0,count:3}),`${scope}_breathe`)}${uiActions([hardwareIoButton('Breathe',HARDWARE_IO_MOSFET_DEVICE,'breathe',{channel,final_value:0},{disabled})])}</div></div><div class='row-meta'>Count 0 repeats until Set, Off, Fade, Pulse, All off, Stop game or Reset game.</div></details></section>`;
}

function hardwareIoGpioChannel(channel){
const scope=`io_${channel}`;
const item=hardwareIoStatusItem('ios',channel);
const mode=hardwareIoGpioModeText(item&&item.mode);
const disabled=!item||!item.enabled||mode!=='output';
const modeDisabled=!item||Number(item.gpio)<0||!hardwareIoServiceAvailable();
const events=['changed','active','inactive','high','low'].map(name=>`ch${channel}_${name}`).join(', ');
const modeControls=`<div class='field-grid'><label><span>Mode</span><select data-hardware-io-mode='${esc(channel)}' ${modeDisabled?'disabled':''}>${hardwareIoGpioModeOptions(item&&item.mode)}</select></label></div>${uiActions([uiButton({label:'Apply mode',action:'hardware.io.mode',dataset:{channel},disabled:modeDisabled})])}`;
const outputControls=mode==='output'?`<div data-hardware-form='io' data-hardware-schema='ioPulse' data-hardware-scope='${esc(scope)}'><div class='field-grid'>${renderFormFields(HARDWARE_IO_SCHEMAS.ioPulse,hardwareIoFormModel(scope,{duration_ms:1000}),scope)}</div>${uiActions([
hardwareIoButton('Active',HARDWARE_IO_IO_DEVICE,'set',{channel,active:true},{kind:'approve',disabled,noForm:true}),
hardwareIoButton('Inactive',HARDWARE_IO_IO_DEVICE,'set',{channel,active:false},{disabled,noForm:true}),
hardwareIoButton('Pulse active',HARDWARE_IO_IO_DEVICE,'pulse',{channel,active:true},{kind:'approve',disabled}),
hardwareIoButton('Blink',HARDWARE_IO_IO_DEVICE,'blink',{channel},{kind:'approve',disabled}),
hardwareIoButton('Toggle',HARDWARE_IO_IO_DEVICE,'toggle',{channel},{disabled,noForm:true}),
])}</div>`:'';
const inputHint=modeDisabled?`<div class='row-meta'>Board channel is not assigned.</div>`:(mode==='input'?`<div class='row-meta'>Main scenario events: ${esc(`ch${channel}_active, ch${channel}_inactive`)}. All input events: ${esc(events)}</div>`:`<div class='row-meta'>Switch to input to use ${esc(`ch${channel}_active`)} / ${esc(`ch${channel}_inactive`)} in scenarios.</div>`);
return `<section class='builder-step'><div class='builder-step-head'><div><div class='builder-step-title'>IO ${esc(channel)}</div><div class='row-meta'>${esc(hardwareIoChannelMeta('ios',channel))}</div></div>${hardwareIoGpioStatusBadge(channel)}</div>${modeControls}<div class='kvs'><div class='kv'><span class='k'>Mode</span><span class='v'>${esc(mode)}</span></div><div class='kv'><span class='k'>Physical</span><span class='v'>${esc(item&&item.physical_high?'HIGH':'LOW')}</span></div></div>${outputControls}${inputHint}</section>`;
}

function renderHardwareIoHeader(){
const available=hardwareIoServiceAvailable();
const statusText=gmHardwareIo.loading?'Loading hardware status...':(gmHardwareIo.error?`Status error: ${gmHardwareIo.error}`:hardwareIoServiceMessage());
const content=!gmHardwareIo.loaded?'':(available?'':`<div class='row-meta bad-text'>Hardware controls are disabled because ${esc(hardwareIoServiceMessage())}.</div>`);
return uiCard({
title:'Hardware status',
subtitle:statusText,
status:gmHardwareIo.loaded?(available?status('ok'):status('fault')):'',
actions:[uiButton({label:'Refresh status',action:'hardware.status.refresh'})],
content,
});
}

function renderHardwareIoDeviceSummary(deviceId,title,subtitle,statusHtml){
const dev=questDeviceById(deviceId);
const missingNotice=dev?'':`<div class='row-meta bad-text'>${esc(title)} quest device is missing. Hardware status is visible, but commands are unavailable.</div>`;
const content=`<div class='kvs'><div class='kv'><span class='k'>Device</span><span class='v'>${esc(deviceId)}</span></div><div class='kv'><span class='k'>Commands</span><span class='v'>${esc(dev&&Array.isArray(dev.commands)?dev.commands.length:0)}</span></div></div>${missingNotice}`;
return uiCard({title,subtitle,status:statusHtml,content});
}

function renderHardwareIoSummaryCards(){
const relay=questDeviceById(HARDWARE_IO_RELAY_DEVICE);
const mosfet=questDeviceById(HARDWARE_IO_MOSFET_DEVICE);
const io=questDeviceById(HARDWARE_IO_IO_DEVICE);
const relayStatus=relay?status(questDeviceHealth(relay)):`<span class='status state-fault'>missing</span>`;
const mosfetStatus=mosfet?status(questDeviceHealth(mosfet)):`<span class='status state-fault'>missing</span>`;
const ioStatus=io?status(questDeviceHealth(io)):`<span class='status state-fault'>missing</span>`;
return `<div class='grid cols-2'>${renderHardwareIoDeviceSummary(HARDWARE_IO_RELAY_DEVICE,'Relay channels','Relay 1-4 outputs.',relayStatus)}${renderHardwareIoDeviceSummary(HARDWARE_IO_MOSFET_DEVICE,'MOSFET channels','MOSFET 1-4 PWM outputs.',mosfetStatus)}${renderHardwareIoDeviceSummary(HARDWARE_IO_IO_DEVICE,'IO channels','IO 1-4 configurable input/output channels.',ioStatus)}</div>`;
}

function renderHardwareRelaySection(){
return `<section><h2 class='section-title'>Relays</h2><div class='grid cols-2'>${[1,2,3,4].map(hardwareIoRelayChannel).join('')}</div></section>`;
}

function renderHardwareMosfetSection(){
return `<section><div class='card-head'><h2 class='section-title'>MOSFET PWM</h2><div class='actions'>${hardwareIoButton('All off',HARDWARE_IO_MOSFET_DEVICE,'all_off',{}, {kind:'danger'})}</div></div><div class='list'>${[1,2,3,4].map(hardwareIoMosfetChannel).join('')}</div></section>`;
}

function renderHardwareGpioSection(){
return `<section><h2 class='section-title'>IO channels</h2><div class='grid cols-2'>${[1,2,3,4].map(hardwareIoGpioChannel).join('')}</div></section>`;
}

function renderHardwareIoView(){
setPage('Hardware IO','Relay, MOSFET and IO channels');
if(!gmHardwareIo.loaded&&!gmHardwareIo.loading){
setTimeout(()=>loadHardwareIoStatus(true),0);
}
return [
renderHardwareIoHeader(),
renderHardwareIoSummaryCards(),
renderHardwareRelaySection(),
renderHardwareMosfetSection(),
renderHardwareGpioSection(),
].join('<div style="height:14px"></div>');
}

gmRegisterAction('hardware.command',async el=>{
const deviceId=el.dataset.deviceId||'';
const commandId=el.dataset.commandId||'';
if(!deviceId||!commandId)throw new Error('Hardware command is incomplete');
const params=hardwareIoParams(el);
setGMStatus('Sending hardware command...');
const res=await api.device.runCommand(deviceId,commandId,params);
await gmExpectOk(res);
await loadHardwareIoStatus(true);
setGMStatus('Hardware command sent','gm-ok');
});

gmRegisterAction('hardware.io.mode',async el=>{
const channel=Number(el.dataset.channel)||0;
const select=typeof document!=='undefined'?document.querySelector(`[data-hardware-io-mode="${channel}"]`):null;
const mode=select&&select.value?select.value:'disabled';
if(!channel)throw new Error('IO channel is incomplete');
setGMStatus('Updating IO mode...');
const res=await api.hardwareIo.setIoMode({channel,mode});
await gmExpectOk(res);
await loadHardwareIoStatus(true);
setGMStatus('IO mode updated','gm-ok');
});

gmRegisterAction('hardware.status.refresh',async()=>{
await loadHardwareIoStatus(true);
setGMStatus('Hardware status updated','gm-ok');
});
