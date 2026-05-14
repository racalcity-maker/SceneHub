// GM panel source part. Edit this file, then rebuild gm_panel.js.
function esc(v){
const value=(v===undefined||v===null)?'':v;
return String(value).replace(/[&<>"']/g,m=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[m]));
}

function enc(value){
return encodeURIComponent(value);
}

function kebab(value){
return String(value||'').replace(/([a-z0-9])([A-Z])/g,'$1-$2').replace(/[_\s]+/g,'-').toLowerCase();
}

function boolAttr(name,value){
return value?` ${esc(name)}`:'';
}

function jsonAttr(value){
return esc(JSON.stringify(value===undefined?null:value));
}

function uiAttrs(attrs){
return Object.entries(attrs||{}).filter(([,value])=>value!==false&&value!==undefined&&value!==null).map(([key,value])=>value===true?esc(key):`${esc(key)}='${esc(value)}'`).join(' ');
}

function uiDataset(dataset){
const attrs={};
Object.entries(dataset||{}).forEach(([key,value])=>{
if(value===undefined||value===null)return;
attrs[`data-${kebab(key)}`]=value;
});
return uiAttrs(attrs);
}
function uiButton(opts){
opts=opts||{};
const classes=['ui-btn',opts.kind||'',opts.size?`ui-btn-${opts.size}`:'',opts.className||''].filter(Boolean).join(' ');
const cls=` class='${esc(classes)}'`;
const action=opts.action?` data-action='${esc(opts.action)}'`:'';
const dataset=uiDataset(opts.dataset);
const confirm=opts.confirm?` data-confirm='${esc(opts.confirm)}'`:'';
const title=opts.title?` title='${esc(opts.title)}'`:'';
const disabled=opts.disabled?' disabled':'';
const attrs=[action,dataset,confirm,title,disabled].filter(Boolean).join(' ');
return `<button${cls}${attrs?` ${attrs}`:''}>${esc(opts.label||'')}</button>`;
}

function uiIconButton(opts){
opts=opts||{};
return uiButton({...opts,kind:`icon-btn${opts.kind?` ${opts.kind}`:''}`.trim()});
}

function uiActions(buttons){
return `<div class='actions'>${(Array.isArray(buttons)?buttons:[]).join('')}</div>`;
}

function uiCard(opts){
opts=opts||{};
const cls=['card',opts.kind||'',opts.className||''].filter(Boolean).join(' ');
const dataset=opts.dataset?` ${uiDataset(opts.dataset)}`:'';
const actions=Array.isArray(opts.actions)?opts.actions.join(''):(opts.actions||'');
const header=(opts.title||opts.subtitle||opts.status||actions)?`<div class='card-head'><div>${opts.title?`<h2 class='section-title'>${esc(opts.title)}</h2>`:''}${opts.subtitle?`<div class='card-sub'>${esc(opts.subtitle)}</div>`:''}</div>${opts.status||actions?`<div class='actions'>${opts.status||''}${actions||''}</div>`:''}</div>`:'';
const footer=opts.footer?`<div class='card-footer'>${opts.footer}</div>`:'';
return `<section class='${esc(cls)}'${dataset}>${header}${opts.content||''}${footer}</section>`;
}

function uiSection(opts){
opts=opts||{};
return `<section${opts.kind?` class='${esc(opts.kind)}'`:''}>${opts.title?`<h2 class='section-title'>${esc(opts.title)}</h2>`:''}${opts.content||''}</section>`;
}

function uiField(opts){
opts=opts||{};
return `<label class='field-stack'>${opts.label?`<span>${esc(opts.label)}</span>`:''}${opts.content||''}</label>`;
}

function uiInput(opts){
opts=opts||{};
const attrs=uiAttrs({
type:opts.type||'text',
value:opts.value!==undefined?opts.value:'',
placeholder:opts.placeholder||undefined,
min:opts.min,
max:opts.max,
step:opts.step,
disabled:!!opts.disabled,
});
return `<input ${attrs}${opts.dataset?` ${uiDataset(opts.dataset)}`:''}>`;
}

function uiSelect(opts){
opts=opts||{};
const selected=String(opts.value!==undefined?opts.value:'');
const options=(Array.isArray(opts.options)?opts.options:[]).map(option=>{
const value=String(option&&typeof option==='object'?option.value:option);
const label=option&&typeof option==='object'?(option.label!==undefined?option.label:value):value;
return `<option value='${esc(value)}' ${value===selected?'selected':''}>${esc(label)}</option>`;
}).join('');
return `<select${opts.dataset?` ${uiDataset(opts.dataset)}`:''}${opts.disabled?' disabled':''}>${options}</select>`;
}

function uiCheckbox(opts){
opts=opts||{};
return `<label class='row-meta'><input type='checkbox' ${opts.checked?'checked':''}${opts.disabled?' disabled':''}${opts.dataset?` ${uiDataset(opts.dataset)}`:''} style='min-width:auto'> ${esc(opts.label||'')}</label>`;
}

function uiBadge(text,kind){
return `<span class='badge${kind?` ${esc(kind)}`:''}'>${esc(text)}</span>`;
}

function uiEmpty(text){
return `<div class='manual-empty'>${esc(text||'')}</div>`;
}

function uiDetails(opts){
opts=opts||{};
return `<details class='scenario-advanced' ${opts.open?'open':''}><summary>${esc(opts.summary||'Details')}</summary>${opts.content||''}</details>`;
}
async function gmResponseText(res){
return (await res.text().catch(()=>''))||(`HTTP ${res.status}`);
}

async function gmJsonOrNull(res){
return res.ok?await res.json():null;
}

async function gmReadJson(res){
if(!res.ok)throw new Error(await gmResponseText(res));
return await res.json();
}

async function gmExpectOk(res){
if(!res.ok)throw new Error(await gmResponseText(res));
return res;
}

async function gmGet(url){
return gmFetch(url);
}

async function gmGetJson(url){
return gmReadJson(await gmGet(url));
}

async function gmPost(url){
return gmFetch(url,{method:'POST'});
}

async function gmPostJson(url,body){
return gmFetch(url,{
method:'POST',
headers:{'Content-Type':'application/json'},
body:JSON.stringify(body||{})}
);
}

async function gmPostJsonResult(url,body){
return gmReadJson(await gmPostJson(url,body));
}

async function gmDeleteJson(url,body){
return gmFetch(url,{
method:'DELETE',
headers:{'Content-Type':'application/json'},
body:JSON.stringify(body||{})}
);
}

const api={
session:{
info:()=>gmGet('/api/session/info'),
},
gm:{
state:()=>gmGet('/api/gm/state'),
stateJson:()=>gmGetJson('/api/gm/state'),
systemSummary:()=>gmGet('/api/gm/system/summary'),
systemSummaryJson:()=>gmGetJson('/api/gm/system/summary'),
versions:()=>gmGet('/api/gm/versions'),
versionsJson:()=>gmGetJson('/api/gm/versions'),
},
orchestrator:{
controlDevices:()=>gmGet('/api/orchestrator/control/devices'),
auditRecent:()=>gmGet('/api/orchestrator/audit/recent'),
timelineRecent:()=>gmGet('/api/orchestrator/timeline/recent'),
},
files:{
list:path=>gmGet(`/api/files?path=${enc(path)}`),
},
hardwareIo:{
status:()=>gmGet('/api/hardware-io/status'),
setIoMode:body=>gmPostJson('/api/hardware-io/io-mode',body),
},
device:{
list:(includeSystem=true)=>gmGet(`/api/gm/devices?include_system=${includeSystem?'1':'0'}`),
save:device=>gmPostJson('/api/gm/device/save',{device}),
delete:deviceId=>gmPostJson('/api/gm/device/delete',{device_id:deviceId}),
describeInterface:clientId=>gmPostJson('/api/gm/device/describe-interface',{client_id:clientId}),
runCommand:(deviceId,commandId,params)=>gmPostJson('/api/gm/device/command/run',{
device_id:deviceId,
command_id:commandId,
...(params&&typeof params==='object'?{params}:{}),
}),
},
room:{
runtime:(roomId,detail='detail')=>gmGet(`/api/gm/room/runtime?room_id=${enc(roomId)}${detail&&detail!=='detail'?`&detail=${enc(detail)}`:''}`),
runtimeJson:(roomId,detail='detail')=>gmGetJson(`/api/gm/room/runtime?room_id=${enc(roomId)}${detail&&detail!=='detail'?`&detail=${enc(detail)}`:''}`),
scenarios:roomId=>gmGet(`/api/gm/room/scenarios?room_id=${enc(roomId)}`),
profiles:roomId=>gmGet(`/api/gm/room/profiles?room_id=${enc(roomId)}`),
scenarioEditorCatalog:roomId=>gmGet(`/api/gm/room/scenario-editor/catalog?room_id=${enc(roomId)}`),
save:body=>gmPostJson('/api/gm/room/save',body),
delete:body=>gmPostJson('/api/gm/room/delete',body),
hintSend:body=>gmPostJson('/api/gm/room/hint/send',body),
hintClear:roomId=>gmPost(`/api/gm/room/hint/clear?room_id=${enc(roomId)}`),
profileSelect:body=>gmPostJson('/api/gm/room/profile/select',body),
profileSave:body=>gmPostJson('/api/gm/room/profile/save',body),
profileDelete:profileId=>gmPostJson('/api/gm/room/profile/delete',{profile_id:profileId}),
scenarioSelect:body=>gmPostJson('/api/gm/room/scenario/select',body),
scenarioValidate:scenario=>gmPostJson('/api/gm/room/scenario/validate',{scenario}),
scenarioSave:scenario=>gmPostJson('/api/gm/room/scenario/save',{scenario}),
scenarioDelete:scenarioId=>gmPostJson('/api/gm/room/scenario/delete',{scenario_id:scenarioId}),
game:(roomId,action)=>gmPost(`/api/gm/room/game/${enc(action)}?room_id=${enc(roomId)}`),
timerStart:(roomId,durationMs)=>gmPost(`/api/gm/room/timer/start?room_id=${enc(roomId)}&duration_ms=${enc(durationMs)}`),
timer:(roomId,action)=>gmPost(`/api/gm/room/timer/${enc(action)}?room_id=${enc(roomId)}`),
timerAdd:(roomId,deltaMs)=>gmPost(`/api/gm/room/timer/add?room_id=${enc(roomId)}&delta_ms=${enc(deltaMs)}`),
sessionFinish:roomId=>gmPost(`/api/gm/room/session/finish?room_id=${enc(roomId)}`),
scenarioRuntime:(roomId,action,branchId)=>{
let url=`/api/gm/room/scenario/${enc(action)}?room_id=${enc(roomId)}`;
if(branchId)url+=`&branch_id=${enc(branchId)}`;
return gmPost(url);
},
},
storage:{
post:url=>gmPost(url),
importJson:(url,text)=>gmFetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:text}),
exportUrl:kind=>kind==='device'?'/api/gm/devices/export':(kind==='scenario'?'/api/gm/room/scenarios/export':'/api/gm/profiles/export'),
commandUrl:(kind,op)=>{
if(kind==='device')return `/api/gm/devices/${op}`;
if(kind==='scenario')return `/api/gm/room/scenarios/${op}`;
return `/api/gm/profiles/${op}`;
},
run:(kind,op)=>gmPost(kind==='device'?`/api/gm/devices/${op}`:(kind==='scenario'?`/api/gm/room/scenarios/${op}`:`/api/gm/profiles/${op}`)),
},
};
// GM panel source part. Edit this file, then rebuild gm_panel.js.
function formFieldId(scope,name){
return `${scope||'form'}_${name||'field'}`.replace(/[^a-zA-Z0-9_:-]/g,'_');
}

function renderFormField(field,model,scope){
field=field||{};
const name=field.name||field.field||'';
const type=field.type||'text';
const value=model&&Object.prototype.hasOwnProperty.call(model,name)?model[name]:field.default;
const dataset={field:name,scope:scope||''};
let content='';
if(type==='checkbox'){
content=uiCheckbox({label:field.checkbox_label||'',checked:!!value,disabled:!!field.disabled,dataset});
return field.label?`<div class='field-stack'><span>${esc(field.label)}</span>${content}</div>`:content;
}
if(type==='select'){
content=uiSelect({value:value!==undefined?value:'',options:field.options||[],disabled:!!field.disabled,dataset});
}
else if(type==='textarea'){
content=`<textarea ${uiDataset(dataset)}${field.disabled?' disabled':''} placeholder='${esc(field.placeholder||'')}'>${esc(value!==undefined?value:'')}</textarea>`;
}
else if(type==='json'){
const text=value===undefined||value===null?'':(typeof value==='string'?value:JSON.stringify(value));
content=`<textarea ${uiDataset(dataset)}${field.disabled?' disabled':''} placeholder='${esc(field.placeholder||'{}')}'>${esc(text)}</textarea>`;
}
else{
content=uiInput({
type:type==='number'||type==='duration_ms'?'number':'text',
value:value!==undefined?value:'',
placeholder:field.placeholder||'',
min:field.min,
max:field.max,
step:field.step||(type==='number'||type==='duration_ms'?1:undefined),
disabled:!!field.disabled,
dataset,
});
}
return uiField({label:field.label||name,content});
}

function renderFormFields(schema,model,scope){
return (Array.isArray(schema)?schema:[]).map(field=>renderFormField(field,model||{},scope||'')).join('');
}

function collectFormFields(root,schema,scope){
const out={};
(Array.isArray(schema)?schema:[]).forEach(field=>{
field=field||{};
const name=field.name||field.field||'';
if(!name)return;
const candidates=root&&root.querySelectorAll?Array.from(root.querySelectorAll('[data-field]')):[];
const el=candidates.find(item=>(item.dataset.field||'')===name&&(!scope||(item.dataset.scope||'')===scope));
if(!el)return;
if(field.type==='checkbox'){
out[name]=!!el.checked;
}
else if(field.type==='number'||field.type==='duration_ms'){
const num=Number(el.value);
out[name]=Number.isFinite(num)?num:0;
}
else if(field.type==='json'){
const raw=(el.value||'').trim();
if(!raw){
out[name]=field.default!==undefined?field.default:null;
}
else{
try{
out[name]=JSON.parse(raw);
}
catch(err){
out[name]=raw;
}
}
}
else{
out[name]=el.value;
}
});
return out;
}

function validateFormFields(model,schema){
const errors=[];
(Array.isArray(schema)?schema:[]).forEach(field=>{
field=field||{};
const name=field.name||field.field||'';
if(!name)return;
const value=model?model[name]:undefined;
if(field.required&&(value===undefined||value===null||value==='')){
errors.push(`${field.label||name} is required`);
}
if((field.type==='number'||field.type==='duration_ms')&&value!==undefined){
if(field.min!==undefined&&value<field.min)errors.push(`${field.label||name} is below minimum`);
if(field.max!==undefined&&value>field.max)errors.push(`${field.label||name} is above maximum`);
}
if(field.type==='json'&&typeof value==='string'&&value.trim()){
try{
JSON.parse(value);
}
catch(err){
errors.push(`${field.label||name} must be valid JSON`);
}
}
});
return errors;
}
// GM panel source part. Edit this file, then rebuild gm_panel.js.
const GM_ACTIONS={
}
;

function gmRegisterAction(name,handler){
if(!name||typeof handler!=='function')return;
GM_ACTIONS[name]=handler;
}

async function gmHandleActionClick(e){
const el=e.target.closest('[data-action]');
if(!el||el.disabled)return false;
const action=el.dataset.action||'';
const handler=GM_ACTIONS[action];
if(!handler){
setStatus(`Unknown UI action: ${action}`,'state-fault');
return true;
}
try{
if(el.dataset.confirm&&!confirm(el.dataset.confirm))return true;
await handler(el,e);
}
catch(err){
setStatus(err.message||'command failed','state-fault');
}
return true;
}

gmRegisterAction('manual.device.command',async el=>{
await runManualDeviceCommand(el.dataset.deviceId||'',el.dataset.commandId||'');
});

gmRegisterAction('room.game',async el=>{
await runRoomGame(el.dataset.op||'',el.dataset.roomId||'',true);
});

gmRegisterAction('room.timer',async el=>{
await runRoomTimer(el.dataset.op||'',el.dataset.roomId||'');
});

gmRegisterAction('room.hint',async el=>{
await runRoomHint(el.dataset.op||'',el.dataset.roomId||'');
});

gmRegisterAction('room.create',async()=>{
if(!confirmDiscardEditorChanges())return;
await createRoomFromPrompt();
});

gmRegisterAction('room.delete',async el=>{
if(!confirmDiscardEditorChanges())return;
await deleteRoom(el.dataset.roomId||'',true);
});

gmRegisterAction('admin.open',async el=>{
if(!confirmDiscardEditorChanges())return;
const roomId=el.dataset.roomId||'';
if(roomId){
profileEditor.room_id=roomId;
scenarioEditor.room_id=roomId;
}
currentView=el.dataset.view||'profiles';
if(currentView==='profiles')profileEditor.open=true;
if(currentView==='scenarios')scenarioEditor.open=true;
await loadGMViewData(false);
render();
});

gmRegisterAction('room.open',async el=>{
const nextRoomId=el.dataset.roomId||'';
if((currentView!=='room'||currentRoomId!==nextRoomId)&&!confirmDiscardEditorChanges())return;
currentRoomId=nextRoomId;
currentView='room';
roomTab='control';
await loadGMViewData(false);
await loadGMRuntimeOnly(currentRoomId,false);
});

gmRegisterAction('device.setup.open',async el=>{
if(!isAdmin())return;
if(!confirmDiscardEditorChanges())return;
currentView='device_setup';
const setupTarget=el.dataset.deviceId||'';
if(setupTarget==='new'){
questDeviceEditor.device_id='';
questDeviceEditor.open=true;
questDeviceEditor.draft=newQuestDeviceDraft();
clearQuestDeviceDirty();
}
else if(setupTarget&&setupTarget!=='1'){
questDeviceEditor.device_id=setupTarget;
questDeviceEditor.open=true;
questDeviceEditor.draft=null;
clearQuestDeviceDirty();
}
await loadGMViewData(false);
render();
});

gmRegisterAction('room.tab',async el=>{
if(!confirmDiscardEditorChanges())return;
if((el.dataset.scope||'')==='room')roomTab=el.dataset.tab||'overview';
render();
});

gmRegisterAction('room.scenario.runtime',async el=>{
await runRoomScenarioRuntime(el.dataset.op||'',el.dataset.roomId||'',el.dataset.branchId||'',true);
});

gmRegisterAction('storage.run',async el=>{
if(!confirmDiscardEditorChanges())return;
await runStorageAction(el.dataset.op||'');
});

gmRegisterAction('audio.files.refresh',async()=>{
await loadGMAudioFiles(true);
});

gmRegisterAction('scenario.edit',async el=>{
if(!confirmDiscardScenario())return;
scenarioEditor.scenario_id=el.dataset.scenarioId||'';
scenarioEditor.open=true;
scenarioEditor.expanded_step=-1;
scenarioEditor.expanded_v2_action='';
scenarioEditor.active_branch=0;
clearScenarioDirty();
const original=roomScenarios(scenarioEditor.room_id).find(s=>s.id===scenarioEditor.scenario_id)||null;
scenarioEditor.original_scenario=original?scenarioEditableJson(original,scenarioEditor.room_id):null;
render();
});

gmRegisterAction('scenario.new',async()=>{
if(!confirmDiscardScenario())return;
scenarioEditor.scenario_id='';
scenarioEditor.open=true;
scenarioEditor.expanded_step=-1;
scenarioEditor.expanded_v2_action='';
scenarioEditor.active_branch=0;
clearScenarioDirty();
scenarioEditor.draft={id:'',name:'',room_id:scenarioEditor.room_id,branches:[defaultScenarioBranch(0,[])]};
skipNextScenarioDomSync();
render();
});

gmRegisterAction('scenario.delete',async el=>{
if(!confirmDiscardScenario())return;
await deleteScenarioEditor(el.dataset.scenarioId||'',true);
});

gmRegisterAction('scenario.validate',async()=>{
await validateScenarioEditor();
});

gmRegisterAction('scenario.save',async()=>{
await saveScenarioEditor();
});

gmRegisterAction('scenario.create_game_mode',async el=>{
if(!confirmDiscardEditorChanges())return;
const scenarioId=el.dataset.scenarioId||'';
const scenario=roomScenarios(scenarioEditor.room_id).find(s=>s.id===scenarioId)||null;
profileEditor.room_id=scenarioEditor.room_id;
profileEditor.profile_id='';
profileEditor.open=true;
clearProfileDirty();
profileEditor.prefill={
room_id:scenarioEditor.room_id,
scenario_id:scenarioId,
name:scenario&&(scenario.name||scenario.id)||'New mode',
id:'',
duration_ms:3600000,
hint_pack_id:'',
audio_pack_id:''};
currentView='profiles';
await loadGMViewData(false);
render();
});

gmRegisterAction('scenario.branch',async el=>{
const index=Number(el.dataset.branchIndex);
applyScenarioBranchAction(el.dataset.op||'',Number.isFinite(index)?index:0);
});

gmRegisterAction('scenario.reactive_v2',async el=>{
applyReactiveV2Action(el.dataset.op||'',el.dataset.variantIndex,el.dataset.actionIndex||el.dataset.guardIndex,el.dataset.actionType||'');
});

gmRegisterAction('scenario.step.help',async el=>{
alert(scenarioStepHelpText(el.dataset.stepType||''));
});

gmRegisterAction('scenario.step',async el=>{
const v2ActionEl=el.closest('[data-v2-action]');
if(v2ActionEl){
applyReactiveV2Action(el.dataset.op||'',v2ActionEl.dataset.variantIndex,v2ActionEl.dataset.v2Action,el.dataset.commandIndex||'');
return;
}
const stepEl=el.closest('[data-scenario-step]');
const fallbackIndex=Number(stepEl&&stepEl.dataset.scenarioStep);
const index=Number.isFinite(Number(el.dataset.stepIndex))?Number(el.dataset.stepIndex):fallbackIndex;
applyScenarioStepAction(el.dataset.op||'',index,el.dataset.stepType||el.dataset.commandIndex||el.dataset.eventIndex||el.dataset.flagIndex||'');
});

gmRegisterAction('profile.edit',async el=>{
if(!confirmDiscardProfile())return;
profileEditor.profile_id=el.dataset.profileId||'';
profileEditor.open=true;
clearProfileDirty();
render();
});

gmRegisterAction('profile.new',async()=>{
if(!confirmDiscardProfile())return;
profileEditor.profile_id='';
profileEditor.open=true;
profileEditor.prefill=null;
clearProfileDirty();
render();
});

gmRegisterAction('profile.delete',async el=>{
if(!confirmDiscardProfile())return;
await deleteProfileEditor(el.dataset.profileId||'',true);
});

gmRegisterAction('profile.save',async()=>{
await saveProfileEditor();
});

gmRegisterAction('profile.select',async el=>{
if(!confirmDiscardProfile())return;
await selectRoomProfile(profileEditor.room_id,el.dataset.profileId||'');
});

gmRegisterAction('quest.device.edit',async el=>{
if(!confirmDiscardQuestDevice())return;
questDeviceEditor.device_id=el.dataset.deviceId||'';
questDeviceEditor.open=true;
questDeviceEditor.draft=null;
clearQuestDeviceDirty();
render();
});

gmRegisterAction('quest.device.new',async()=>{
if(!confirmDiscardQuestDevice())return;
questDeviceEditor.device_id='';
questDeviceEditor.open=true;
questDeviceEditor.draft=newQuestDeviceDraft();
clearQuestDeviceDirty();
render();
});

gmRegisterAction('quest.device.discover',async()=>{
await discoverQuestDeviceInterface();
});

gmRegisterAction('quest.discovery.apply',async()=>{
applyQuestDeviceDiscovery();
});

gmRegisterAction('quest.discovery.discard',async()=>{
discardQuestDeviceDiscovery();
});

gmRegisterAction('quest.device.save',async()=>{
await saveQuestDeviceEditor();
});

gmRegisterAction('quest.device.delete',async el=>{
if(!confirmDiscardQuestDevice())return;
await deleteQuestDeviceEditor(el.dataset.deviceId||'',true);
});

gmRegisterAction('quest.command.add',async()=>{
addQuestDeviceCommand();
});

gmRegisterAction('quest.command.delete',async el=>{
deleteQuestDeviceCommand(Number(el.dataset.index));
});

gmRegisterAction('quest.event.add',async()=>{
addQuestDeviceEvent();
});

gmRegisterAction('quest.event.delete',async el=>{
deleteQuestDeviceEvent(Number(el.dataset.index));
});
// GM panel source part. Edit this file, then rebuild gm_panel.js.
const GM={
data:{
state:null,
observed:null,
audit:null,
timeline:null,
roomScenarios:{},
roomProfiles:{},
scenarioEditorCatalogs:{},
deviceConfig:null,
questDevices:null,
hardwareIo:{loaded:false,loading:false,error:'',data:null},
audioFiles:{loaded:false,loading:false,scheduled:false,error:'',items:[]}
},
ui:{
currentRoomScenarioId:{},
currentRoomProfileId:{},
deviceFilterRoom:'',
observedFilter:'all',
currentView:'dashboard',
currentRoomId:'',
roomTab:'overview',
inputDirty:false,
autoRenderDeferred:false,
initialRouteApplied:false,
skipScenarioDomSync:false,
openDetails:{},
flagDatalistSeq:0
},
editors:{
profile:{room_id:'',profile_id:'',dirty:false,open:false,prefill:null},
scenario:{room_id:'',scenario_id:'',dirty:false,open:false,draft:null,original_scenario:null,validation_report:null,expanded_step:-1,expanded_v2_action:'',active_branch:0,branch_count_shrink_allowed:false,branch_count_shrink_floor:0},
questDevice:{device_id:'',dirty:false,open:false,draft:null,discovery:null}
},
session:{
current:{role:'user',username:''}
}
};

Object.defineProperties(globalThis,{
gmState:{get(){return GM.data.state;},set(v){GM.data.state=v;}},
gmObserved:{get(){return GM.data.observed;},set(v){GM.data.observed=v;}},
gmAudit:{get(){return GM.data.audit;},set(v){GM.data.audit=v;}},
gmTimeline:{get(){return GM.data.timeline;},set(v){GM.data.timeline=v;}},
gmRoomScenarios:{get(){return GM.data.roomScenarios;},set(v){GM.data.roomScenarios=v||{};}},
gmRoomProfiles:{get(){return GM.data.roomProfiles;},set(v){GM.data.roomProfiles=v||{};}},
gmScenarioEditorCatalogs:{get(){return GM.data.scenarioEditorCatalogs;},set(v){GM.data.scenarioEditorCatalogs=v||{};}},
gmDeviceConfig:{get(){return GM.data.deviceConfig;},set(v){GM.data.deviceConfig=v;}},
gmQuestDevices:{get(){return GM.data.questDevices;},set(v){GM.data.questDevices=v;}},
gmHardwareIo:{get(){return GM.data.hardwareIo;},set(v){GM.data.hardwareIo=v||{loaded:false,loading:false,error:'',data:null};}},
gmAudioFiles:{get(){return GM.data.audioFiles;},set(v){GM.data.audioFiles=v||{loaded:false,loading:false,scheduled:false,error:'',items:[]};}},
gmSession:{get(){return GM.session.current;},set(v){GM.session.current=v||{role:'user',username:''};}},
currentRoomScenarioId:{get(){return GM.ui.currentRoomScenarioId;},set(v){GM.ui.currentRoomScenarioId=v||{};}},
currentRoomProfileId:{get(){return GM.ui.currentRoomProfileId;},set(v){GM.ui.currentRoomProfileId=v||{};}},
profileEditor:{get(){return GM.editors.profile;},set(v){GM.editors.profile=v||{};}},
scenarioEditor:{get(){return GM.editors.scenario;},set(v){GM.editors.scenario=v||{};}},
questDeviceEditor:{get(){return GM.editors.questDevice;},set(v){GM.editors.questDevice=v||{};}},
deviceFilterRoom:{get(){return GM.ui.deviceFilterRoom;},set(v){GM.ui.deviceFilterRoom=v||'';}},
observedFilter:{get(){return GM.ui.observedFilter;},set(v){GM.ui.observedFilter=v||'all';}},
currentView:{get(){return GM.ui.currentView;},set(v){GM.ui.currentView=v||'dashboard';}},
currentRoomId:{get(){return GM.ui.currentRoomId;},set(v){GM.ui.currentRoomId=v||'';}},
roomTab:{get(){return GM.ui.roomTab;},set(v){GM.ui.roomTab=v||'overview';}},
gmInputDirty:{get(){return GM.ui.inputDirty;},set(v){GM.ui.inputDirty=!!v;}},
gmAutoRenderDeferred:{get(){return GM.ui.autoRenderDeferred;},set(v){GM.ui.autoRenderDeferred=!!v;}},
gmInitialRouteApplied:{get(){return GM.ui.initialRouteApplied;},set(v){GM.ui.initialRouteApplied=!!v;}},
gmSkipScenarioDomSync:{get(){return GM.ui.skipScenarioDomSync;},set(v){GM.ui.skipScenarioDomSync=!!v;}},
gmOpenDetails:{get(){return GM.ui.openDetails;},set(v){GM.ui.openDetails=v||{};}},
gmFlagDatalistSeq:{get(){return GM.ui.flagDatalistSeq;},set(v){GM.ui.flagDatalistSeq=Number(v)||0;}}
});

function setStatus(text,cls){const el=document.getElementById('system_status');if(!el)return;el.textContent=text||'';el.className='status '+(cls||'state-unknown');}
async function gmFetch(url,options){const res=await fetch(url,options);if(res.status===401){window.location='/login';throw new Error('Unauthorized');}return res;}
function isAdmin(){return gmSession&&gmSession.role==='admin';}
function canOpenView(view){return !['profiles','scenarios','device_setup','hardware_io','observed','storage'].includes(view)||isAdmin();}
function ensureAllowedView(){if(!canOpenView(currentView)){currentView='dashboard';}}
function applyGMRoleLayout(){const admin=isAdmin();
document.body.classList.toggle('role-admin',admin);const badge=document.getElementById('gm_role_badge');if(badge)badge.textContent=admin?'admin':'operator';
document.querySelectorAll('[data-view]').forEach(el=>{if(['profiles','scenarios','device_setup','observed','storage'].includes(el.dataset.view||'')){el.style.display=admin?'':'none';}});ensureAllowedView();}
async function loadGMSession(){try{const res=await api.session.info();if(res.ok){gmSession=await res.json();}}catch(err){gmSession={role:'user',username:''};}window.__WEB_SESSION=gmSession;applyGMRoleLayout();return gmSession;}
function metric(label,value){return `<div class='card metric'><div class='label'>${esc(label)}</div><div class='value'>${esc(value)}</div></div>`;}
function status(v){return `<span class='status ${stateClass(v)}'>${esc(healthLabel(v))}</span>`;}
function roomCard(r){const derived=roomDerivedHealth(r);const issueCount=Number(r&&r.issue_count)||0;const deviceCount=Number(r&&r.scenario_device_count)||Number(r&&r.device_count)||0;return `<article class='card clickable' data-action='room.open' data-room-id='${esc(r.room_id)}'><div class='card-head'><div><div class='card-title'>${esc(r.title||r.name||r.room_id)}</div><div class='card-sub'>Room</div></div>${status(derived)}</div><div class='kvs'><div class='kv'><span class='k'>Devices</span><span class='v'>${esc(deviceCount)}</span></div><div class='kv'><span class='k'>Issues</span><span class='v'>${esc(issueCount)}</span></div><div class='kv'><span class='k'>Timer</span>${roomClockHtml(r,'span','v')}</div></div></article>`;}
function issueRow(i){const subject=i.device_id?deviceDisplayName(i.device_id):(i.room_id?roomName(i.room_id):i.scope);return `<div class='row-card'><div class='row-main'><div class='row-title'>${esc(subject)} - ${esc(i.title||i.code)}</div><div class='row-meta'>${esc(i.details||'')}</div></div>${status(i.severity)}</div>`;}
function noProfilesHtml(roomId){return isAdmin()?`<div class='empty'>No game modes for this room</div><div class='actions'>${uiButton({label:'Create game mode',action:'admin.open',dataset:{view:'profiles','room-id':roomId||''}})}</div>`:`<div class='empty'>No game modes available. Ask admin.</div>`;}
function noScenariosHtml(roomId){return isAdmin()?`<div class='empty'>No room scenarios</div><div class='actions'>${uiButton({label:'Create scenario',action:'admin.open',dataset:{view:'scenarios','room-id':roomId||''}})}</div>`:`<div class='empty'>No room scenarios</div>`;}
function applyInitialOperatorRoute(){if(gmInitialRouteApplied)return;gmInitialRouteApplied=true;if(isAdmin())return;const rooms=(gmState&&Array.isArray(gmState.rooms))?gmState.rooms:[];if(!rooms.length)return;if(currentView==='dashboard'||currentView==='rooms'){currentView='room';currentRoomId=currentRoomId||rooms[0].room_id;roomTab='control';}}
// GM panel source part. Edit this file, then rebuild gm_panel.js.
function scenarioById(roomId,scenarioId){return roomScenarios(roomId).find(s=>s.id===scenarioId)||null;}
function roomSelectedScenarioObject(room){if(!room)return null;const profiles=roomProfiles(room.room_id);const profileId=roomSelectedProfileId(room.room_id)||room.selected_profile_id||'';const profile=profiles.find(p=>p.id===profileId)||null;const preferred=room.running_scenario_id||room.selected_profile_scenario_id||(profile&&profile.scenario_id)||room.selected_scenario_id||'';return scenarioById(room.room_id,preferred)||scenarioById(room.room_id,room.selected_scenario_id)||null;}
function scenarioBranchDisplaySteps(branch){
if(!branch)return [];
const steps=Array.isArray(branch.steps)?branch.steps:[];
if(steps.length)return steps;
const variants=Array.isArray(branch.variants)?branch.variants:[];
if(!variants.length)return [];
const mode=String(branch.policy&&branch.policy.mode||'single').toLowerCase();
const multi=mode!=='single'&&variants.length>1;
const out=[];
variants.forEach((variant,variantIndex)=>{
const actions=Array.isArray(variant&&variant.actions)?variant.actions:[];
actions.forEach((action,actionIndex)=>{
const copy=action?JSON.parse(JSON.stringify(action)):{type:'WAIT_TIME',duration_ms:1000};
if(!copy.id)copy.id=`variant_${variantIndex+1}_action_${actionIndex+1}`;
if(multi){
const prefix=mode==='escalate'?`Level ${variantIndex+1}`:`Variant ${variantIndex+1}`;
copy.label=copy.label?`${prefix}: ${copy.label}`:`${prefix}: ${scenarioStepText(copy)}`;
}
out.push(copy);
});
});
return out;
}
function roomCurrentScenarioText(room){return room&&room.scenario_current_step_text||'';}
function roomScenarioDeviceIds(room){return Array.isArray(room&&room.scenario_device_ids)?room.scenario_device_ids.filter(Boolean):[];}
function roomQuestDeviceIssues(room){return [];}
function roomDerivedHealth(room){return room&&room.health||'unknown';}
function scenarioRuntimeBranches(room){return Array.isArray(room&&room.scenario_branches)?room.scenario_branches:[];}
function scenarioFlowRuntimeBranches(room){const branches=scenarioRuntimeBranches(room);const flow=branches.filter(branch=>String(branch&&branch.type||'normal').toLowerCase()!=='reactive');return flow.length?flow:branches;}
function scenarioTotalStepCount(room){return Math.max(0,Number(room&&room.scenario_total_steps)||0);}
function scenarioDoneStepCount(room){return Math.max(0,Number(room&&room.scenario_done_steps)||0);}
function scenarioAudioStepText(s){const params=s&&s.params&&typeof s.params==='object'?s.params:{};const command=String(s&&s.command_id||'');const channel=String(params.channel||'effect').toLowerCase();const file=compactText(audioBaseName(params.file||''),34);if(command==='play'){const kind=channel==='background'||channel==='bg'||channel==='music'?'bg':'sfx';return file?`Play ${kind}: ${file}`:`Play ${kind}`;}if(command==='stop')return `Stop audio${params.channel?`: ${params.channel}`:''}`;if(command==='pause')return 'Pause audio';if(command==='resume')return 'Resume audio';if(command==='set_volume')return `Set volume: ${params.volume??''}`;return `Audio: ${questDeviceCommandName('system_audio',command)}`;}
function scenarioStepText(s){if(!s)return '';const type=String(s.type||'').toLowerCase();if(type==='device_command'){if(String(s.device_id||'')==='system_audio')return scenarioAudioStepText(s);return `${deviceDisplayName(s.device_id)} -> ${questDeviceCommandName(s.device_id,s.command_id)}`;}if(type==='wait_device_event')return `Wait ${deviceDisplayName(s.device_id)}: ${questDeviceEventName(s.device_id,s.event_id)}`;if(type==='wait_time')return `Wait ${Math.max(1,Math.round((Number(s.duration_ms)||1000)/1000))} sec`;if(type==='operator_approval')return `Operator approval: ${s.operator_prompt||s.prompt||s.label||'Confirm'}`;return s.label||s.id||s.type||'Step';}
function scenarioStepLabel(room){const runtime=String(room&&room.scenario_runtime_state||'idle').toLowerCase();const totalSteps=scenarioTotalStepCount(room);const doneSteps=scenarioDoneStepCount(room);if(!totalSteps)return '0 / 0';if(runtime==='idle'||runtime==='stopped')return `0 / ${totalSteps}`;if(runtime==='done')return `${totalSteps} / ${totalSteps}`;return `${Math.min(totalSteps,doneSteps+1)} / ${totalSteps}`;}
function scenarioWaitText(room){return room&&room.scenario_wait_summary||'none';}
function scenarioProgressBranchStepState(branchRuntime,localIndex){const steps=Array.isArray(branchRuntime&&branchRuntime.steps)?branchRuntime.steps:[];const step=steps.find(item=>Number(item&&item.index)===localIndex);const state=String(step&&step.state||'').toLowerCase();return state||null;}
function scenarioProgressBranchState(room,branchRuntime,localIndex,globalIndex){return scenarioProgressBranchStepState(branchRuntime,localIndex)||'pending';}
function scenarioProgressIcon(state){if(state==='done')return '&#10003;';if(state==='current')return '&rarr;';if(state==='error')return '!';return '';}
function scenarioProgressBranches(room,scenarioOrSteps){if(room&&Array.isArray(room.scenario_branches)&&room.scenario_branches.some(branch=>Array.isArray(branch&&branch.steps)&&branch.steps.length))return room.scenario_branches.map((branch,index)=>({id:branch.id||`branch_${index+1}`,name:branch.name||`Branch ${index+1}`,type:String(branch.type||'normal').toLowerCase()==='reactive'?'reactive':'normal',enabled:branch.active!==false,required_for_completion:branch.required_for_completion!==false,trigger:branch.trigger||null,current_step_text:branch.current_step_text||'',wait_summary:branch.wait_summary||'',steps:Array.isArray(branch.steps)?branch.steps:[]}));if(scenarioOrSteps&&Array.isArray(scenarioOrSteps.branches)&&scenarioOrSteps.branches.length)return scenarioOrSteps.branches.map((branch,index)=>{const type=String(branch.type||'normal').toLowerCase()==='reactive'?'reactive':'normal';return {id:branch.id||`branch_${index+1}`,name:branch.name||`Branch ${index+1}`,type,enabled:branch.enabled!==false,required_for_completion:type==='normal'&&branch.required_for_completion!==false,trigger:branch.trigger||null,steps:scenarioBranchDisplaySteps(branch)};});const steps=Array.isArray(scenarioOrSteps)?scenarioOrSteps:(scenarioOrSteps&&Array.isArray(scenarioOrSteps.steps)?scenarioOrSteps.steps:[]);return steps.length?[{id:'main',name:'Main',type:'normal',enabled:true,required_for_completion:true,steps}]:[];}
function scenarioProgressBranchRuntime(room,branch,index){const runtimes=Array.isArray(room&&room.scenario_branches)?room.scenario_branches:[];const byIndex=runtimes.find(item=>Number(item.index)===index);if(byIndex)return byIndex;const branchId=branch&&branch.id||'';if(branchId)return runtimes.find(item=>(item.id||'')===branchId)||null;return null;}
function renderScenarioProgressStep(room,step,index,globalIndex,branchRuntime){const disabled=step&&step.enabled===false;const state=disabled?'disabled':scenarioProgressBranchState(room,branchRuntime,index,globalIndex);const text=step&&step.text||scenarioStepText(step);return `<div class='scenario-progress-step ${state}'><span class='scenario-progress-icon'>${scenarioProgressIcon(state)}</span><span class='scenario-progress-index'>${esc(index+1)}.</span><span class='scenario-progress-text'>${esc(text)}</span>${disabled?`<span class='badge'>disabled</span>`:''}</div>`;}
function scenarioBranchDoneCount(room,branch,branchRuntime,globalStart){
const steps=Array.isArray(branch&&branch.steps)?branch.steps:[];
const total=steps.length;
if(!total)return 0;
return Math.min(total,Math.max(0,Number(branchRuntime&&((branchRuntime.done_steps??branchRuntime.completed_step_count)||0))||0));
}
function scenarioBranchCurrentStep(branch,branchRuntime){
const steps=Array.isArray(branch&&branch.steps)?branch.steps:[];
if(!steps.length)return branch&&branch.type==='reactive'?'No actions':'No steps';
if(branch&&branch.current_step_text)return branch.current_step_text;
return '';
}
function scenarioProgressBar(done,total){const pct=total?Math.max(0,Math.min(100,Math.round(done*100/total))):0;return `<div class='scenario-progress-bar' title='${esc(done)} / ${esc(total)}'><span style='width:${pct}%'></span></div>`;}
function scenarioProgressTypeLabel(branch){return branch.type==='reactive'?'reaction':(branch.required_for_completion?'required':'optional');}
function scenarioBranchWaitText(branchRuntime,branch){
return branch&&branch.wait_summary||'none';
}
function renderScenarioBranchSkipButton(room,branch,branchRuntime){
if(!room||!branchRuntime||branchRuntime.state!=='waiting'||!branchRuntime.wait_operator_skip_allowed)return '';
const label=branchRuntime.wait_operator_skip_label||'Skip wait';
const branchId=branchRuntime.id||branch.id||'';
return uiButton({
label,
action:'room.scenario.runtime',
dataset:{op:'next','room-id':room.room_id||'','branch-id':branchId},
confirm:'Force complete this branch wait?',
});
}
function renderScenarioActiveWaits(room,items){
const waits=items.filter(item=>item.runtime&&item.runtime.state==='waiting'&&item.runtime.wait_operator_skip_allowed);
if(!waits.length)return '';
return `<div class='scenario-active-waits'>${waits.map(item=>{
const skip=renderScenarioBranchSkipButton(room,item.branch,item.runtime);
return `<div class='scenario-active-wait'><div><span class='badge'>operator skip</span> <strong>${esc(item.branch.name||item.branch.id)}</strong><div class='row-meta'>${esc(scenarioBranchWaitText(item.runtime,item.branch))}</div></div>${skip?`<div class='branch-runtime-actions'>${skip}</div>`:''}</div>`;
}).join('')}</div>`;
}
function renderScenarioProgressBranch(room,item){
const branch=item.branch;
const steps=Array.isArray(branch.steps)?branch.steps:[];
const branchRuntime=item.runtime;
const state=(branchRuntime&&branchRuntime.state)||(!branch.enabled?'disabled':'idle');
const waitType=(branchRuntime&&branchRuntime.wait_type)||'none';
const waitText=scenarioBranchWaitText(branchRuntime,branch);
const done=scenarioBranchDoneCount(room,branch,branchRuntime,item.start);
const current=scenarioBranchCurrentStep(branch,branchRuntime);
const detailsKey=`room-progress-steps:${room&&room.room_id||'room'}:${branch.id||item.index}`;
const skip=renderScenarioBranchSkipButton(room,branch,branchRuntime);
const unit=branch.type==='reactive'?'actions':'steps';
const meta=branch.type==='reactive'?`${esc(done)} / ${esc(steps.length)} ${esc(unit)} / ${esc(scenarioProgressTypeLabel(branch))}`:`${esc(done)} / ${esc(steps.length)} ${esc(unit)} / ${esc(scenarioProgressTypeLabel(branch))}${waitType&&waitType!=='none'?` / waiting ${esc(waitText)}`:''}`;
const actionRuntime=branch.type==='reactive'&&state==='waiting'&&(!waitType||waitType==='none')?null:branchRuntime;
return `<section class='scenario-progress-branch ${!branch.enabled?'disabled':''} ${branch.type==='reactive'?'reactive':''} ${state}'><div class='scenario-progress-branch-head'><div class='scenario-progress-branch-main'><div class='scenario-progress-title-row'><div class='scenario-progress-branch-title'>${esc(branch.name||branch.id||`Branch ${item.index+1}`)}</div><span class='badge'>${esc(state)}</span></div><div class='row-meta'>${meta}</div><div class='scenario-progress-current'>${esc(current)}</div>${scenarioProgressBar(done,steps.length)}</div>${skip?`<div class='branch-runtime-actions'>${skip}</div>`:''}</div><details class='scenario-progress-step-details' ${detailsAttrs(detailsKey,false)}><summary>Show ${esc(unit)}</summary><div class='scenario-progress'>${steps.length?steps.map((step,stepIndex)=>renderScenarioProgressStep(room,step,stepIndex,item.start+stepIndex,actionRuntime)).join(''):`<div class='empty'>No ${esc(unit)}</div>`}</div></details></section>`;
}
function renderScenarioProgressSection(title,items,mode){
if(!items.length)return '';
return `<div class='scenario-progress-section'><div class='scenario-progress-section-title'>${esc(title)}</div><div class='scenario-progress-branches ${esc(mode||'flow')}'>${items.map(item=>renderScenarioProgressBranch(item.room,item)).join('')}</div></div>`;
}
function renderScenarioProgress(room,scenarioOrSteps){
const branches=scenarioProgressBranches(room,scenarioOrSteps);
if(!branches.length)return `<div class='scenario-progress empty'>No scenario steps</div>`;
let offset=0;
const items=branches.map((branch,index)=>{
const steps=Array.isArray(branch.steps)?branch.steps:[];
const branchRuntime=scenarioProgressBranchRuntime(room,branch,index);
const start=offset;
offset+=steps.length;
return {room,branch,index,runtime:branchRuntime,start};
});
const flow=items.filter(item=>item.branch.type!=='reactive');
const reactions=items.filter(item=>item.branch.type==='reactive');
const progressItems=flow.length?flow:items;
const total=progressItems.reduce((sum,item)=>sum+(item.branch.steps||[]).length,0);
const done=progressItems.reduce((sum,item)=>sum+scenarioBranchDoneCount(room,item.branch,item.runtime,item.start),0);
const active=items.find(item=>item.runtime&&(item.runtime.state==='waiting'||item.runtime.state==='running'||item.runtime.state==='error'));
const activeText=active?`${active.branch.name||active.branch.id}: ${scenarioBranchCurrentStep(active.branch,active.runtime)}`:'No active branch';
return `<div class='scenario-progress-wrap'><div class='scenario-progress-overview'><div><div class='scenario-progress-overview-title'>${esc(done)} / ${esc(total)} steps</div><div class='row-meta'>Current: ${esc(activeText)}</div></div>${scenarioProgressBar(done,total)}</div>${renderScenarioActiveWaits(room,items)}${renderScenarioProgressSection('Flow branches',flow,'flow')}${renderScenarioProgressSection('Reaction branches',reactions,'reactions')}</div>`;
}
function scenarioValidationText(s){if(!s)return 'No scenario selected';const n=Number(s.validation_issue_count)||0;if(s.valid===false)return `${n||1} validation issue${n===1?'':'s'}`;return n?`Valid, ${n} warning${n===1?'':'s'}`:'Valid';}
function scenarioIssueHtml(issues){return Array.isArray(issues)&&issues.length?`<div class='validation-list'>${issues.map(i=>`<div class='validation-item'>${esc(i.level||'error')} step ${esc(i.step_index||0)} / ${esc(i.code||'VALIDATION')}: ${esc(i.message||'')}</div>`).join('')}</div>`:'';}
function scenarioDraftValidationHtml(){const r=scenarioEditor.validation_report;if(!r)return '';const errors=Number(r.error_count)||0;const warnings=Number(r.warning_count)||0;const summary=errors?`${errors} error${errors===1?'':'s'}, ${warnings} warning${warnings===1?'':'s'}`:(warnings?`${warnings} warning${warnings===1?'':'s'}`:'valid');return `<div class='row-meta ${errors?'bad-text':''}'>Draft validation: ${esc(summary)}</div>${scenarioIssueHtml(r.issues)}`;}
// GM panel source part. Edit this file, then rebuild gm_panel.js.
function isEditableControl(el){return !!(el&&el.closest&&el.closest('#gm_content')&&el.matches('input,select,textarea'));}
function dirtyLockControl(el){
if(!isEditableControl(el)||el.disabled||el.readOnly)return false;
return !!el.closest('#profile_id,#profile_name,#profile_duration,#profile_hint_pack,#profile_audio_pack,#profile_scenario,#profile_enabled,#scenario_id,#scenario_name,[data-scenario-branch-field],[data-step-field],[data-step-param],[data-group-command-field],[data-event-group-field],[data-flag-list-field],[data-quest-device-field],[data-quest-command-field],[data-quest-event-field],#gm_timer_minutes,#gm_hint_input,#storage_devices_file,#storage_scenarios_file,#storage_profiles_file');
}
function markControlEditing(el){if(!isEditableControl(el))return;el.classList.add('gm-field-editing');}
function unmarkControlEditing(el){if(!isEditableControl(el))return;el.classList.remove('gm-field-editing');}
function markControlDirty(el){if(!dirtyLockControl(el))return;gmInputDirty=true;el.classList.add('gm-field-dirty');markControlEditing(el);}
function clearTransientFieldDirty(){gmInputDirty=false;document.querySelectorAll('#gm_content .gm-field-dirty,#gm_content .gm-field-editing').forEach(el=>{el.classList.remove('gm-field-dirty','gm-field-editing');});}
function hasFocusedEditableControl(){const active=document.activeElement;return isEditableControl(active);}
function hasDirtyEditableControls(){return gmInputDirty||!!document.querySelector('#gm_content .gm-field-dirty');}
function shouldDeferAutoRender(){return !!(hasUnsavedEditorChanges()||hasFocusedEditableControl()||hasDirtyEditableControls());}
function hasTransientFieldChanges(){return hasDirtyEditableControls();}
function confirmDiscardTransientFields(){if(!hasTransientFieldChanges())return true;if(!confirm('Discard unsent field changes?'))return false;clearTransientFieldDirty();return true;}
function hasUnsavedEditorChanges(){return !!(profileEditor.dirty||scenarioEditor.dirty||questDeviceEditor.dirty);}
function confirmDiscardProfile(){if(!profileEditor.dirty)return true;if(!confirm('Discard unsaved game mode changes?'))return false;clearProfileDirty();return true;}
function confirmDiscardScenario(){if(!scenarioEditor.dirty)return true;if(!confirm('Discard unsaved scenario changes?'))return false;clearScenarioDirty();return true;}
function confirmDiscardQuestDevice(){if(!questDeviceEditor.dirty)return true;if(!confirm('Discard unsaved device changes?'))return false;clearQuestDeviceDirty();return true;}
function confirmDiscardEditorChanges(){if(!confirmDiscardScenario())return false;if(!confirmDiscardProfile())return false;if(!confirmDiscardQuestDevice())return false;if(!confirmDiscardTransientFields())return false;return true;}
function clearProfileDirty(){profileEditor.dirty=false;profileEditor.prefill=null;clearTransientFieldDirty();}
function clearScenarioDirty(){scenarioEditor.dirty=false;scenarioEditor.draft=null;scenarioEditor.original_scenario=null;scenarioEditor.validation_report=null;scenarioEditor.branch_count_shrink_allowed=false;scenarioEditor.branch_count_shrink_floor=0;clearTransientFieldDirty();}
function clearQuestDeviceDirty(){questDeviceEditor.dirty=false;questDeviceEditor.draft=null;questDeviceEditor.discovery=null;clearTransientFieldDirty();}
function markProfileDirty(){profileEditor.dirty=true;}
function markScenarioDirty(){scenarioEditor.dirty=true;scenarioEditor.validation_report=null;if(document.getElementById('scenario_id'))scenarioEditor.draft=collectScenarioEditor();}
function syncScenarioDraftFromDom(){if(!scenarioEditor.dirty||currentView!=='scenarios'||!document.getElementById('scenario_id'))return;try{scenarioEditor.draft=collectScenarioEditor();}catch(err){}}
function skipNextScenarioDomSync(){gmSkipScenarioDomSync=true;}
function markQuestDeviceDirty(){questDeviceEditor.dirty=true;if(document.querySelector('[data-quest-device-editor]'))questDeviceEditor.draft=collectQuestDeviceEditor(false);}
// GM panel source part. Edit this file, then rebuild gm_panel.js.
function questDeviceDisplayName(dev){return dev&&(dev.name||dev.display_name||dev.id)||'Device';}
function observedDisplayName(item){if(!item)return 'Device';const reg=observedRegistration(item.device_id);return reg&&(reg.name||reg.device_id)||item.name||item.display_name||item.device_id||'Device';}
function questDeviceHealth(dev){return dev&&(dev.health||'unknown')||'unknown';}
function questDeviceStatusText(dev){return dev&&(dev.status_text||dev.state_text||'unknown')||'missing device';}
function questDeviceMonitorRow(dev){const observed=observedByClientId(dev.client_id||dev.id);const health=questDeviceHealth(dev);const meta=[`${(dev.commands||[]).length} commands`,`${(dev.events||[]).length} events`,dev.enabled===false?'disabled':'enabled'].join(' / ');const setup=isAdmin()?uiButton({label:'Device Setup',action:'device.setup.open',dataset:{'device-id':dev.id||'1'}}):'';const debug=isAdmin()?`<details class='scenario-advanced'><summary>Debug ids</summary><div class='row-meta'>Device ID: ${esc(dev.id||'')}</div><div class='row-meta'>Client: ${esc(dev.client_id||'none')}</div></details>`:'';return `<div class='row-card'><div class='row-main'><div class='row-title'>${esc(questDeviceDisplayName(dev))} ${dev.enabled===false?`<span class='badge'>disabled</span>`:''}</div><div class='row-meta'>${esc(meta)}</div><div class='row-meta'>${observed?`${esc(observed.connectivity||'unknown')} / fw ${esc(observed.fw_version||'n/a')}`:'not observed'}</div>${debug}</div><div>${status(health)}<div class='row-meta'>${esc(questDeviceStatusText(dev))}</div></div><div class='actions'>${setup}</div></div>`;}
function commandPolicy(cmd){return cmd&&cmd.policy&&typeof cmd.policy==='object'?cmd.policy:{};}
function commandRequiresConfirmation(cmd){const p=commandPolicy(cmd);return !!p.requires_confirmation||(p.danger_level&&p.danger_level!=='normal');}
function manualButtonGroups(){return questDevices().map(dev=>{const id=dev.id||'';const commands=(Array.isArray(dev.commands)?dev.commands:[]).filter(cmd=>cmd&&cmd.id&&commandPolicy(cmd).manual_allowed!==false);if(!id||!commands.length)return null;return {id,name:dev.name||id,room_id:'',health:questDeviceHealth(dev),commands};}).filter(Boolean);}
let gmRightSidebarRenderKey='';
function rightSidebarRenderKey(groups){
return JSON.stringify({
admin:isAdmin(),
groups:groups.map(g=>({
id:g.id,
name:g.name,
room_id:g.room_id||'',
health:g.health,
commands:g.commands.map(cmd=>({
id:cmd.id||'',
label:cmd.label||cmd.id||'',
danger:commandRequiresConfirmation(cmd)
}))
}))
});
}
function renderRightSidebar(force){const root=document.getElementById('gm_right_sidebar');if(!root)return;const groups=manualButtonGroups();const key=rightSidebarRenderKey(groups);if(!force&&gmRightSidebarRenderKey===key)return;gmRightSidebarRenderKey=key;root.innerHTML=`<div class='right-brand'><h2>Manual buttons</h2><p>Single-device controls</p></div><div class='manual-groups'>${groups.length?groups.map(g=>`<section class='manual-group'><div class='manual-group-head'><div><div class='manual-title'>${esc(g.name)}</div><div class='manual-meta'>${g.room_id?esc(roomName(g.room_id)):'Quest device'}</div></div>${status(g.health)}</div><div class='manual-buttons'>${g.commands.map(cmd=>uiButton({label:cmd.label||cmd.id,action:'manual.device.command',kind:commandRequiresConfirmation(cmd)?'danger':'',dataset:{'device-id':g.id,'command-id':cmd.id},confirm:commandRequiresConfirmation(cmd)?'Run this manual command?':''})).join('')}</div>${isAdmin()?uiDetails({summary:'Debug ids',content:`<div class='row-meta'>Device ID: ${esc(g.id)}</div>`}):''}</section>`).join(''):uiEmpty('No manual buttons configured')}</div>`;}
function commandSupportsScenarioParams(command){return !!(command&&command.command);}
function questDeviceCommandName(deviceId,commandId){const dev=questDeviceById(deviceId);const cmd=dev&&Array.isArray(dev.commands)?dev.commands.find(c=>(c.id||'')===commandId):null;return cmd&&(cmd.label||cmd.id)||commandId||'command';}
function questDeviceEventName(deviceId,eventId){const dev=questDeviceById(deviceId);const ev=dev&&Array.isArray(dev.events)?dev.events.find(item=>(item.id||'')===eventId):null;return ev&&(ev.label||ev.id)||eventId||'event';}
function questDeviceEventNameByType(deviceId,eventType){const dev=questDeviceById(deviceId);const ev=dev&&Array.isArray(dev.events)?dev.events.find(item=>(item.event||item.id||'')===eventType):null;return ev&&(ev.label||ev.id)||eventType||'event';}
function detailsKeyFor(el){
if(!el||String(el.tagName||'').toLowerCase()!=='details')return '';
if(el.dataset&&el.dataset.detailsKey)return el.dataset.detailsKey;
const summary=(el.querySelector('summary')&&el.querySelector('summary').textContent||'details').trim().toLowerCase();
let scope='';
const scoped=el.closest('[data-scenario-step],[data-quest-command],[data-quest-event],[data-quest-device-editor]');
if(scoped){
['scenarioStep','questCommand','questEvent','questDeviceEditor'].some(k=>{
if(scoped.dataset&&scoped.dataset[k]!==undefined){scope=`${k}:${scoped.dataset[k]||'1'}`;return true;}
return false;
});
}
return `${currentView}:${currentRoomId||''}:${scenarioEditor.room_id||''}:${scenarioEditor.scenario_id||''}:${scope}:${summary}`;
}
function detailsAttrs(key,defaultOpen){
const open=gmOpenDetails[key]!==undefined?gmOpenDetails[key]:!!defaultOpen;
return `data-details-key='${esc(key)}' ${open?'open':''}`;
}
function slugifyId(value,fallback){
const base=String(value||'').toLowerCase().replace(/[^a-z0-9]+/g,'_').replace(/^_+|_+$/g,'');
return base||`${fallback||'item'}_${Date.now().toString(16)}`;
}
function stateClass(v){return v==='fault'||v==='error'||v==='offline'?'state-fault':(v==='degraded'||v==='warning'?'state-degraded':(v==='ok'||v==='online'?'state-ok':'state-unknown'));}
function healthLabel(v){return v||'unknown';}
function fmtClock(ms){const total=Math.max(0,Math.floor((Number(ms)||0)/1000));const h=Math.floor(total/3600);const m=Math.floor((total%3600)/60);const s=total%60;return h>0?`${h}:${String(m).padStart(2,'0')}:${String(s).padStart(2,'0')}`:`${m}:${String(s).padStart(2,'0')}`;}
function roomTimerDisplayMs(room){
const remaining=Math.max(0,Number(room&&room.timer_remaining_ms)||0);
if((room&&room.timer_state)!=='running')return remaining;
const synced=Number(room&&room._timer_synced_at_ms)||performance.now();
return Math.max(0,remaining-(performance.now()-synced));
}
function roomClockAttrs(room){return `data-room-clock='${esc(room&&room.room_id||'')}' data-room-clock-state='${esc(room&&room.timer_state||'idle')}'`;}
function roomClockHtml(room,tag,cls){const name=tag||'span';const classAttr=cls?` class='${esc(cls)}'`:'';return `<${name}${classAttr} ${roomClockAttrs(room)}>${fmtClock(roomTimerDisplayMs(room))}</${name}>`;}
function ago(ms){if(!ms)return 'never';const age=Math.max(0,Math.floor((performance.now()-Number(ms))/1000));return age<60?`${age}s ago`:`${Math.floor(age/60)}m ago`;}
function audioBaseName(path){if(!path)return '';const parts=String(path).split('/').filter(Boolean);return parts.length?parts[parts.length-1]:path;}
function audioDirName(path){if(!path)return '/';const raw=String(path);const idx=raw.lastIndexOf('/');if(idx<0)return '/';return raw.slice(0,idx)||'/';}
function compactText(value,max){const text=String(value||'');const limit=Math.max(8,Number(max)||32);return text.length>limit?`${text.slice(0,limit-1)}...`:text;}
function roomById(id){return (gmState&&Array.isArray(gmState.rooms)?gmState.rooms:[]).find(r=>r.room_id===id)||null;}
function deviceById(id){return (gmState&&Array.isArray(gmState.devices)?gmState.devices:[]).find(d=>d.device_id===id)||null;}
function roomName(id){const r=roomById(id);return r&&(r.title||r.name||r.room_id)||id||'No room';}
function deviceDisplayName(id){const live=deviceById(id);const quest=questDevices().find(d=>(d.id||'')===id);const cfg=configDevices().find(d=>(d.id||d.device_id||'')===id);return live&&(live.display_name||live.device_id)||quest&&(quest.name||quest.id)||cfg&&(cfg.display_name||cfg.name||cfg.id||cfg.device_id)||id||'Device';}
function roomDevices(id){return (gmState&&Array.isArray(gmState.devices)?gmState.devices:[]).filter(d=>d.room_id===id);}
function roomIssues(id){return (gmState&&Array.isArray(gmState.issues)?gmState.issues:[]).filter(i=>!id||!i.room_id||i.room_id===id);}
function roomRelatedIssues(room){
const all=(gmState&&Array.isArray(gmState.issues)?gmState.issues:[]);
if(Array.isArray(room&&room.related_issue_ids)&&room.related_issue_ids.length){
const wanted=new Set(room.related_issue_ids.filter(Boolean));
return all.filter(issue=>wanted.has(String(issue&&issue.issue_id||'')));
}
return roomIssues(room&&room.room_id);
}
function observedRegistration(id){const key=String(id||'');if(!key)return null;const live=(gmState&&Array.isArray(gmState.devices)?gmState.devices:[]).find(d=>d.device_id===key);if(live)return {device_id:live.device_id,name:live.display_name||live.device_id,via:'direct'};const quest=questDevices().find(dev=>(dev.client_id||dev.id||'')===key);if(quest)return {device_id:quest.id,name:quest.name||quest.id,via:'quest_device'};const cfg=configDevices().find(dev=>(Array.isArray(dev.bindings)?dev.bindings:[]).some(b=>(b&&b.client_id||'')===key));if(cfg){const devId=cfg.id||cfg.device_id||'';return {device_id:devId,name:cfg.display_name||cfg.name||devId,via:'binding'};}return null;}
function knownDeviceIds(){const ids=new Set((gmState&&Array.isArray(gmState.devices)?gmState.devices:[]).map(d=>d.device_id));questDevices().forEach(dev=>{if(dev.id)ids.add(dev.id);if(dev.client_id)ids.add(dev.client_id);});configDevices().forEach(dev=>(Array.isArray(dev.bindings)?dev.bindings:[]).forEach(b=>{if(b&&b.client_id)ids.add(b.client_id);}));return ids;}
function observedItems(){return (gmObserved&&Array.isArray(gmObserved.items))?gmObserved.items:[];}
function auditItems(){return (gmAudit&&Array.isArray(gmAudit.items))?gmAudit.items:[];}
function timelineItems(){return (gmTimeline&&Array.isArray(gmTimeline.items))?gmTimeline.items:[];}
function roomScenarios(id){return (gmRoomScenarios&&Array.isArray(gmRoomScenarios[id]))?gmRoomScenarios[id]:[];}
function roomProfiles(id){const data=gmRoomProfiles?gmRoomProfiles[id]:null;return data&&Array.isArray(data.profiles)?data.profiles:[];}
function roomSelectedProfileId(id){return currentRoomProfileId[id]||(gmRoomProfiles[id]&&gmRoomProfiles[id].selected_profile_id)||'';}
function scenarioName(roomId,scenarioId){const s=roomScenarios(roomId).find(x=>x.id===scenarioId);return s&&(s.name||s.id)||scenarioId||'none';}
function scenarioDisplayName(roomId,scenarioId,fallback){const s=scenarioById(roomId,scenarioId);return s&&(s.name||s.id)||fallback||scenarioId||'none';}
function configDevices(){return gmDeviceConfig&&Array.isArray(gmDeviceConfig.devices)?gmDeviceConfig.devices:[];}
function questDevices(){return gmQuestDevices&&Array.isArray(gmQuestDevices.devices)?gmQuestDevices.devices:[];}
function observedByClientId(id){const key=String(id||'');if(!key)return null;return observedItems().find(o=>o.device_id===key)||null;}
function questDeviceById(id){return questDevices().find(d=>(d.id||'')===id)||null;}
function scenarioEditorCatalog(roomId){return gmScenarioEditorCatalogs[roomId]||{quest_devices:[],step_schemas:[]};}
function optionList(items,selected,emptyLabel){let found=false;const opts=[];if(emptyLabel)opts.push(`<option value=''>${esc(emptyLabel)}</option>`);(Array.isArray(items)?items:[]).forEach(item=>{const id=item.id||'';if(id===selected)found=true;opts.push(`<option value='${esc(id)}' ${id===selected?'selected':''}>${esc(item.name||id)}</option>`);});if(selected&&!found)opts.push(`<option value='${esc(selected)}' selected>${esc(selected)} (missing)</option>`);return opts.join('');}
// GM panel source part. Edit this file, then rebuild gm_panel.js.
function renderRoomGameButtons(room,canStart,canStop,canReset){
return uiActions([
uiButton({label:'Start game',action:'room.game',kind:'approve',dataset:{op:'start','room-id':room.room_id},disabled:!canStart}),
uiButton({label:'Stop game',action:'room.game',dataset:{op:'stop','room-id':room.room_id},disabled:!canStop,confirm:'Stop this game session?'}),
uiButton({label:'Reset game',action:'room.game',kind:'danger',dataset:{op:'reset','room-id':room.room_id},disabled:!canReset,confirm:'Reset this game session?'}),
]);
}

function renderRoomProfileControl(room){
const profiles=roomProfiles(room.room_id);
const selectedId=roomSelectedProfileId(room.room_id)||room.selected_profile_id||'';
const selected=profiles.find(p=>p.id===selectedId)||null;
const selectedName=room.selected_profile_name||((selected&&selected.id===selectedId)?selected.name:'');
const selectedScenarioId=room.selected_profile_scenario_id||((selected&&selected.scenario_id)||'');
const canStart=!!selected&&selected.valid!==false;
return `<div class='card'><h2 class='section-title'>Game mode</h2>${profiles.length?`<label class='field-stack'><span>Selected game mode</span><select class='scenario-select' data-room-profile-room='${esc(room.room_id)}'><option value='' ${
selected?'':'selected'}
>Select game mode</option>${
profiles.map(p=>`<option value='${esc(p.id)}' ${selected&&selected.id===p.id?'selected':''} ${p.valid===false?'disabled':''}>${esc(p.name||p.id)} (${fmtClock(p.duration_ms)}${p.valid===false?', invalid':''})</option>`).join('')}
</select></label><div class='kvs' style='margin-top:12px'><div class='kv'><span class='k'>Mode</span><span class='v'>${
esc(selectedName||room.selected_profile_id||'none')}
</span></div><div class='kv'><span class='k'>Scenario</span><span class='v'> ${
esc(scenarioName(room.room_id,selectedScenarioId))}
</span></div><div class='kv'><span class='k'>Duration</span><span class='v'>${esc(selected?fmtClock(selected.duration_ms):'none')}</span></div></div><div style='height:12px'></div>${renderRoomGameButtons(room,canStart,true,true)}`:noProfilesHtml(room.room_id)}</div><div style='height:12px'></div>`;
}

function renderRoomOperatorConsole(room){
const profiles=roomProfiles(room.room_id);
const selectedId=roomSelectedProfileId(room.room_id)||room.selected_profile_id||'';
const selected=profiles.find(p=>p.id===selectedId)||null;
const selectedName=room.selected_profile_name||((selected&&selected.id===selectedId)?selected.name:'');
const scenarioId=room.selected_profile_scenario_id||((selected&&selected.scenario_id)||room.selected_scenario_id||'');
const scenario=roomSelectedScenarioObject(room);
const runtime=room.scenario_runtime_state||'idle';
const waitType=room.scenario_wait_type||'none';
const hasBranchRuntime=Array.isArray(room.scenario_branches)&&room.scenario_branches.length>1;
const runningName=room.running_scenario_name||scenarioDisplayName(room.room_id,room.running_scenario_id||scenarioId,'none');
const currentStepText=roomCurrentScenarioText(room)||'';
const canStart=!!selected&&selected.valid!==false;
const sessionPresent=!!room.session_present||['running','paused','finished'].includes(room.session_state||'');
const canStop=sessionPresent&&room.session_state!=='finished';
const canReset=sessionPresent;
const canPause=room.timer_state==='running';
const canResume=room.timer_state==='paused';
const canAdjust=(Number(room.timer_duration_ms)||0)>0||(Number(room.timer_remaining_ms)||0)>0;
const canApprove=!!(room.selected_scenario_id||room.running_scenario_id)&&runtime==='waiting'&&waitType==='operator';
const canSkipWait=!hasBranchRuntime&&!!(room.selected_scenario_id||room.running_scenario_id)&&runtime==='waiting'&&waitType!=='none'&&!!room.scenario_wait_operator_skip_allowed;
const approveLabel=room.scenario_wait_operator_label||'Continue';
const skipWaitLabel=room.scenario_wait_operator_skip_label||'Skip wait';
const waitPrompt=room.scenario_wait_operator_prompt||scenarioWaitText(room);
const flags=Array.isArray(room.scenario_flags)?room.scenario_flags:[];
const flagsHtml=flags.length?`<details class='scenario-advanced'><summary>Runtime flags</summary><div class='step-list'>${flags.map(flag=>`<div class='step-item'><span>${esc(flag.name||'flag')}</span><span class='badge'>${flag.value?'true':'false'}</span></div>`).join('')}</div></details>`:'';
const assetTotal=Number(room.asset_audio_total)||0;
const assetProblem=(Number(room.asset_audio_missing)||0)+(Number(room.asset_audio_bad)||0)+(Number(room.asset_audio_unsupported)||0)+(Number(room.asset_audio_io_error)||0);
const assetPending=Number(room.asset_audio_unknown)||0;
const assetClass=assetProblem?'bad-text':(assetPending?'warn-text':'');
const assetHtml=assetTotal?`<div class='row-meta ${assetClass}'>Assets: ${esc(room.asset_prepare_state||'unknown')} / ${esc(room.asset_audio_ready||0)} ready of ${esc(assetTotal)}${assetProblem?`, ${esc(assetProblem)} error`:''}${assetPending?`, ${esc(assetPending)} pending`:''}</div>`:'';
const clockState=room.timer_state||room.session_state||'idle';
const startMinutes=Math.max(1,Math.round(((Number(room.timer_duration_ms)||3600000)/60000)));
return `<div class='room-console' data-room-operator-console='1'><div class='card room-primary'><div class='card-head'><div><h2 class='section-title'>Game control</h2>${roomClockHtml(room,'div','room-clock')}<div class='row-meta'>${esc(clockState)} / session ${esc(room.session_state||'idle')}</div></div>${status(roomDerivedHealth(room))}</div>${profiles.length?`<label class='field-stack'><span>Game mode</span><select class='scenario-select' data-room-profile-room='${esc(room.room_id)}'><option value='' ${
selected?'':'selected'}
>Select game mode</option>${
profiles.map(p=>`<option value='${esc(p.id)}' ${selected&&selected.id===p.id?'selected':''} ${p.valid===false?'disabled':''}>${esc(p.name||p.id)} (${fmtClock(p.duration_ms)}${p.valid===false?', invalid':''})</option>`).join('')}
</select></label><div class='kvs' style='margin-top:12px'><div class='kv'><span class='k'>Mode</span><span class='v'>${
esc(selectedName||selectedId||'none')}
</span></div><div class='kv'><span class='k'>Scenario</span><span class='v'> ${
esc(scenarioName(room.room_id,scenarioId))}
</span></div><div class='kv'><span class='k'>Duration</span><span class='v'>${esc(selected?fmtClock(selected.duration_ms):'none')}</span></div></div>${assetHtml}`:noProfilesHtml(room.room_id)}<div style='height:12px'></div>${renderRoomGameButtons(room,canStart,canStop,canReset)}</div><div class='card ${canApprove?'operator-gate':(waitType!=='none'?'room-wait':'')}'><h2 class='section-title'>Runtime</h2><div class='kvs'><div class='kv'><span class='k'>Scenario</span><span class='v'>${esc(runningName)}</span></div><div class='kv'><span class='k'>Runtime</span><span class='v'>${esc(runtime)}</span></div><div class='kv'><span class='k'>Step</span><span class='v'>${esc(scenarioStepLabel(room))}</span></div><div class='kv'><span class='k'>Current</span><span class='v'>${esc(currentStepText||'none')}</span></div><div class='kv'><span class='k'>Waiting</span><span class='v'>${esc(scenarioWaitText(room))}</span></div></div>${canApprove?`<div class='operator-prompt'>${
esc(waitPrompt)}
</div>`:''}${canSkipWait?`<div class='operator-prompt'>Operator override available: ${esc(skipWaitLabel)}</div>`:''}${room.scenario_operator_message?`<div class='operator-prompt'>${
esc(room.scenario_operator_message)}
</div>`:''}${flagsHtml}${room.scenario_last_error?`<div class='row-meta bad-text'>${
esc(room.scenario_last_error)}
</div>`:''}<div style='height:12px'></div>${uiActions([
uiButton({label:approveLabel,kind:'approve',action:'room.scenario.runtime',dataset:{op:'approve','room-id':room.room_id},disabled:!canApprove}),
canSkipWait?uiButton({label:skipWaitLabel,action:'room.scenario.runtime',dataset:{op:'next','room-id':room.room_id},confirm:'Force complete current scenario wait?'}):'',
uiButton({label:'Pause',action:'room.timer',dataset:{op:'pause','room-id':room.room_id},disabled:!canPause}),
uiButton({label:'Resume',action:'room.timer',dataset:{op:'resume','room-id':room.room_id},disabled:!canResume}),
uiButton({label:'+1 min',action:'room.timer',dataset:{op:'plus1','room-id':room.room_id},disabled:!canAdjust}),
uiButton({label:'-1 min',action:'room.timer',dataset:{op:'minus1','room-id':room.room_id},disabled:!canAdjust}),
])}<details class='scenario-advanced'><summary>Manual timer start</summary><div class='timer-start'><input id='gm_timer_minutes' type='number' min='1' step='1' value='${startMinutes}' placeholder='Minutes' aria-label='Duration in minutes'>${uiButton({label:'Start timer',action:'room.timer',dataset:{op:'start','room-id':room.room_id}})}</div></details></div></div><div class='card'><h2 class='section-title'>Scenario progress</h2>${renderScenarioProgress(room,scenario)}</div><div style='height:12px'></div>`;
}

function renderRoomScenarioControl(room){
const scenarios=roomScenarios(room.room_id);
const selectedId=currentRoomScenarioId[room.room_id]||room.selected_scenario_id||'';
const selected=scenarios.find(s=>s.id===selectedId)||null;
const selectedName=room.selected_scenario_name||((selected&&selected.id===room.selected_scenario_id)?selected.name:'');
const runningName=room.running_scenario_name||scenarioDisplayName(room.room_id,room.running_scenario_id,'');
const runtime=room.scenario_runtime_state||'idle';
const waitType=room.scenario_wait_type||'none';
const canRun=!!(room.selected_scenario_id||room.running_scenario_id);
const canStart=canRun&&(!selected||selected.valid!==false);
const canNext=canRun&&(runtime==='running'||runtime==='waiting');
const canApprove=canRun&&runtime==='waiting'&&waitType==='operator';
const approveLabel=room.scenario_wait_operator_label||'Continue';
if(!isAdmin()){
return '';
}
return `<details class='scenario-advanced'><summary>Advanced scenario control</summary>${scenarios.length?`<div class='row'><select class='scenario-select' data-room-scenario-room='${esc(room.room_id)}'><option value='' ${
selected?'':'selected'}
>Select scenario</option>${
scenarios.map(s=>`<option value='${esc(s.id)}' ${selected&&selected.id===s.id?'selected':''}>${esc(s.name||s.id)} (${esc(s.step_count||0)} steps${s.valid===false?', invalid':''})</option>`).join('')}
</select></div><div class='row-meta'>Selected: ${
esc(selectedName||room.selected_scenario_id||'none')}
 / ${
esc(scenarioValidationText(selected))}
</div>${
runningName?`<div class='row-meta'>Running snapshot: ${esc(runningName)} #${esc(room.running_scenario_generation||0)}</div>`:''}
${selected&&selected.valid===false&&Array.isArray(selected.validation_issues)?`<div class='row-meta bad-text'>${esc((selected.validation_issues[0]&&selected.validation_issues[0].message)||'Scenario validation failed')}</div>`:''}
${
room.scenario_last_error?`<div class='row-meta bad-text'>${esc(room.scenario_last_error)}</div>`:''}
<div style='height:12px'></div>${uiActions([
uiButton({label:'Start',action:'room.scenario.runtime',dataset:{op:'start','room-id':room.room_id},disabled:!canStart}),
uiButton({label:'Stop',action:'room.scenario.runtime',dataset:{op:'stop','room-id':room.room_id},disabled:!canRun}),
uiButton({label:approveLabel,kind:'approve',action:'room.scenario.runtime',dataset:{op:'approve','room-id':room.room_id},disabled:!canApprove}),
uiButton({label:'Next',kind:'danger',action:'room.scenario.runtime',dataset:{op:'next','room-id':room.room_id},disabled:!canNext,confirm:'Force complete current scenario wait?'}),
uiButton({label:'Reset',action:'room.scenario.runtime',dataset:{op:'reset','room-id':room.room_id},disabled:!canRun}),
])}`:noScenariosHtml(room.room_id)}</details><div style='height:12px'></div>`;
}

function injectRoomScenarios(){
if(currentView!=='room'||roomTab!=='control')return;
const room=roomById(currentRoomId);
const root=document.getElementById('gm_content');
if(!room||!root)return;
if(root.querySelector('[data-room-operator-console]'))return;
const first=root.querySelector('.card');
if(first)first.insertAdjacentHTML('beforebegin',renderRoomOperatorConsole(room)+(isAdmin()?renderRoomScenarioControl(room):''));
}

function tabs(active,names,scope){
return `<div class='tabs'>${names.map(n=>`<button class='tab-btn ${active===n?'active':''}' data-action='room.tab' data-scope='${scope}' data-tab='${n}'>${
esc(n[0].toUpperCase()+n.slice(1))}
</button>`).join('')}</div>`;
}

function setPage(title,sub){
document.getElementById('page_title').textContent=title;

document.getElementById('page_sub').textContent=sub||'';
const navView=currentView==='room'?'rooms':currentView;

document.querySelectorAll('.nav-btn').forEach(b=>b.classList.toggle('active',b.dataset.view===navView));
}
// GM panel source part. Edit this file, then rebuild gm_panel.js.
function dashboardRoomRow(room){
const scenario=roomSelectedScenarioObject(room);
const profileId=roomSelectedProfileId(room.room_id)||room.selected_profile_id||'';
const profile=roomProfiles(room.room_id).find(p=>p.id===profileId)||null;
const gameName=profile&&(profile.name||profile.id)||room.selected_profile_name||room.profile_name||'none';
const scenarioNameText=scenario&&(scenario.name||scenario.id)||room.running_scenario_name||room.selected_profile_scenario_id||room.selected_scenario_id||'none';
const currentText=roomCurrentScenarioText(room);
const issues=Number(room&&room.issue_count)||0;
const devices=Number(room&&room.scenario_device_count)||Number(room&&room.device_count)||0;
const runtime=room.scenario_runtime_state||room.session_state||'idle';
return `<tr class='clickable-row' data-action='room.open' data-room-id='${esc(room.room_id)}'><td><strong>${esc(room.title||room.name||room.room_id)}</strong><span>${esc(room.room_id||'')}</span></td><td>${status(roomDerivedHealth(room))}</td><td><strong>${esc(gameName)}</strong><span>${esc(scenarioNameText)}</span></td><td>${roomClockHtml(room,'span','')}</td><td>${esc(runtime)}</td><td>${esc(currentText||'none')}</td><td>${esc(scenarioWaitText(room))}</td><td>${esc(devices)}</td><td>${esc(issues)}</td><td class='observed-actions'>${uiButton({label:'Open',kind:'small-btn',action:'room.open',dataset:{'room-id':room.room_id}})}</td></tr>`;
}

function dashboardIssueRow(issue){
const subject=issue.device_id?deviceDisplayName(issue.device_id):(issue.room_id?roomName(issue.room_id):issue.scope||'System');
return `<tr><td>${status(issue.severity||'warning')}</td><td><strong>${esc(subject)}</strong><span>${esc(issue.device_id||issue.room_id||issue.scope||'')}</span></td><td>${esc(issue.title||issue.code||'Issue')}</td><td>${esc(issue.details||'')}</td></tr>`;
}

function renderDashboard(){
const s=gmState||{
summary:{
}
,rooms:[],issues:[]}
;
setPage('Dashboard','What is happening now');
const rooms=Array.isArray(s.rooms)?s.rooms:[];
const baseIssues=Array.isArray(s.issues)?s.issues:[];
const baseIssueIds=new Set(baseIssues.map(issue=>String(issue&&issue.issue_id||'')).filter(Boolean));
const questIssues=rooms.reduce((out,room)=>out.concat(roomRelatedIssues(room).filter(issue=>!baseIssueIds.has(String(issue&&issue.issue_id||''))).map(issue=>Object.assign({room_id:room.room_id},issue))),[]);
const allIssues=baseIssues.concat(questIssues);
const runningRooms=rooms.filter(r=>['running','waiting'].includes(String(r.scenario_runtime_state||r.session_state||''))).length;
const savedQuestDevices=questDevices().filter(d=>d&&!d.system_device);
const offlineDevices=savedQuestDevices.filter(d=>questDeviceHealth(d)==='fault').length;
const roomRows=rooms.length?rooms.map(dashboardRoomRow).join(''):`<tr><td colspan='10' class='observed-empty'>No rooms</td></tr>`;
const issueRows=allIssues.length?allIssues.slice(0,8).map(dashboardIssueRow).join(''):`<tr><td colspan='4' class='observed-empty'>No active issues</td></tr>`;
return `<div class='dashboard-summary observed-summary'><span>Rooms <strong>${esc(s.summary.rooms_total||rooms.length||0)}</strong></span><span>Running <strong>${esc(runningRooms)}</strong></span><span>Devices <strong>${esc(s.summary.devices_total||savedQuestDevices.length||0)}</strong></span><span>Offline <strong>${esc(offlineDevices)}</strong></span><span>Issues <strong>${esc(s.summary.issues_total||allIssues.length||0)}</strong></span></div><div class='dashboard-grid'><section><h2 class='section-title'>Rooms</h2><div class='observed-table-wrap'><table class='observed-table dashboard-room-table'><thead><tr><th>Room</th><th>Status</th><th>Game</th><th>Timer</th><th>Runtime</th><th>Current step</th><th>Waiting</th><th>Devices</th><th>Issues</th><th></th></tr></thead><tbody>${roomRows}</tbody></table></div></section><section><h2 class='section-title'>Needs attention</h2><div class='observed-table-wrap'><table class='observed-table dashboard-issue-table'><thead><tr><th>Severity</th><th>Target</th><th>Problem</th><th>Details</th></tr></thead><tbody>${issueRows}</tbody></table></div></section></div>`;
}

function renderRoomsView(){
setPage('Rooms','Room status and entry points');
const rooms=(gmState&&Array.isArray(gmState.rooms))?gmState.rooms:[];
const create=isAdmin()?`<div class='actions' style='margin-bottom:14px'>${uiButton({label:'Create room',action:'room.create'})}</div>`:'';
return `${create}<div class='grid auto'>${rooms.length?rooms.map(roomCard).join(''):`<div class='card empty'>No rooms</div>`}</div>`;
}

function renderRoomView(){
const room=roomById(currentRoomId)||((gmState&&gmState.rooms&&gmState.rooms[0])?gmState.rooms[0]:null);
if(room)currentRoomId=room.room_id;
setPage(room?`Room: ${room.title||room.room_id}`:'Room','Room control');
if(!room)return `<div class='card empty'>No room selected</div>`;
const roomNameText=room.title||room.name||room.room_id;
const adminActions=isAdmin()?`<div class='actions' style='margin-bottom:14px'>${uiButton({label:'Delete room',kind:'danger',action:'room.delete',dataset:{'room-id':room.room_id},confirm:`Delete room ${roomNameText}? This also removes profiles and scenarios for this room. Quest devices stay untouched.`})}</div>`:'';
const devs=roomDevices(room.room_id);
const questIds=roomScenarioDeviceIds(room);
const questDevs=questIds.map(id=>questDeviceById(id)).filter(Boolean);
const issues=roomRelatedIssues(room);
const canReset=room.session_present;
const canFinish=room.session_present&&room.session_state!=='finished';
const canScenarioNext=(room.selected_scenario_id||room.running_scenario_id)&&(room.scenario_runtime_state==='running'||room.scenario_runtime_state==='waiting');
let body='';
if(roomTab==='overview'){
body=`<div class='grid cols-2'><div class='card'><div class='card-head'><div><div class='card-title'>Room state</div><div class='card-sub'>${esc(room.title||room.name||'Room')}</div></div>${status(roomDerivedHealth(room))}</div><div class='kvs'><div class='kv'><span class='k'>Timer</span>${roomClockHtml(room,'span','v')}</div><div class='kv'><span class='k'>Session</span><span class='v'>${esc(room.session_state||'idle')}</span></div><div class='kv'><span class='k'>Scenario devices</span><span class='v'>${esc(Number(room&&room.scenario_device_count)||0)}</span></div><div class='kv'><span class='k'>Hints</span><span class='v'>${esc(room.hint_sent_count||0)}</span></div></div></div><div class='card'><h2 class='section-title'>Problems</h2><div class='list'>${issues.length?issues.slice(0,4).map(issueRow).join(''):`<div class='empty'>No room issues</div>`}</div></div></div>`;
}
else if(roomTab==='devices'){
const questRows=questDevs.length?questDevs.map(questDeviceMonitorRow).join(''):`<div class='card empty'>No quest devices referenced by selected scenario</div>`;
body=`<section><h2 class='section-title'>Scenario devices</h2><div class='list'>${questRows}</div></section>`;
}
else if(roomTab==='issues'){
body=`<div class='list'>${issues.length?issues.map(issueRow).join(''):`<div class='card empty'>No issues for this room</div>`}</div>`;
}
else{
body=`<div data-room-control-runtime='${esc(room.room_id)}'>${renderRoomOperatorConsole(room)}${isAdmin()?renderRoomScenarioControl(room):''}</div><div class='grid cols-2'><div class='card'><h2 class='section-title'>Hint</h2><div class='hint-row'><input id='gm_hint_input' value='${esc(room.hint_message||'')}' placeholder='Hint for players / operator note'>${uiButton({label:'Send hint',action:'room.hint',dataset:{op:'send','room-id':room.room_id}})}${uiButton({label:'Clear',action:'room.hint',dataset:{op:'clear','room-id':room.room_id},disabled:!room.hint_active})}</div></div><div class='card'><h2 class='section-title'>Device issues</h2><div class='list'>${issues.length?issues.slice(0,5).map(issueRow).join(''):`<div class='empty'>No room issues</div>`}</div></div></div>${uiDetails({summary:'Emergency controls',content:uiActions([
uiButton({label:'Stop game',action:'room.game',dataset:{op:'stop','room-id':room.room_id},disabled:!canFinish,confirm:'Stop this game session?'}),
uiButton({label:'Reset timer',action:'room.timer',dataset:{op:'reset','room-id':room.room_id},disabled:!canReset}),
uiButton({label:'Finish session',kind:'danger',action:'room.timer',dataset:{op:'finish','room-id':room.room_id},disabled:!canFinish}),
uiButton({label:'Force next step',kind:'danger',action:'room.scenario.runtime',dataset:{op:'next','room-id':room.room_id},disabled:!canScenarioNext,confirm:'Force complete current scenario wait?'}),
])})}`;
}
return `${adminActions}${tabs(roomTab,['control','overview','devices','issues'],'room')}<div>${body}</div>`;
}

function renderDevicesView(){
setPage('Devices','Quest devices and physical clients');
const savedQuestDevices=questDevices().filter(d=>d&&!d.system_device);
const observed=observedItems();
const registered=observed.filter(o=>knownDeviceIds().has(o.device_id)).length;
const fault=savedQuestDevices.filter(d=>questDeviceHealth(d)==='fault').length;
const degraded=savedQuestDevices.filter(d=>questDeviceHealth(d)==='degraded').length;
const setupAction=isAdmin()?uiButton({label:'Add device',action:'device.setup.open',dataset:{'device-id':'new'}}):'';
const questRows=savedQuestDevices.length?savedQuestDevices.map(d=>{const observedClient=observedByClientId(d.client_id||d.id);const health=questDeviceHealth(d);const caps=`${(d.commands||[]).length} cmd / ${(d.events||[]).length} evt`;const setup=isAdmin()?uiButton({label:'Setup',kind:'small-btn',action:'device.setup.open',dataset:{'device-id':d.id||'1'}}):'';return `<tr><td><strong>${esc(questDeviceDisplayName(d))}</strong><span>${esc(d.id||'')}</span></td><td>${status(health)}</td><td>${esc(questDeviceStatusText(d))}</td><td>${esc(d.client_id||'none')}</td><td>${esc(caps)}</td><td>${observedClient?`${esc(observedClient.connectivity||'unknown')} / fw ${esc(observedClient.fw_version||'n/a')}`:'not observed'}</td><td>${d.enabled===false?'<span class="badge">disabled</span>':'<span class="badge selected-badge">enabled</span>'}</td><td class='observed-actions'>${setup}</td></tr>`;}).join(''):`<tr><td colspan='8' class='observed-empty'>No saved quest devices${isAdmin()?` ${uiButton({label:'Add device',kind:'small-btn',action:'device.setup.open',dataset:{'device-id':'new'}})}`:''}</td></tr>`;
const observedRows=observed.length?observed.map(o=>{const reg=observedRegistration(o.device_id);return `<tr><td><strong>${esc(observedDisplayName(o))}</strong><span>${esc(o.device_id||'')}</span></td><td>${status(o.connectivity)}</td><td><span class='badge ${reg?'selected-badge':''}'>${reg?'registered':'unregistered'}</span></td><td>${esc(o.fw_version||'n/a')}</td><td>${esc(o.mode||'')}</td><td>${esc(o.state||'')}</td><td>${esc(o.boot_id||'n/a')}</td></tr>`;}).join(''):`<tr><td colspan='7' class='observed-empty'>No physical clients observed</td></tr>`;
return `<div class='observed-toolbar'><div class='observed-summary'><span>Quest devices <strong>${esc(savedQuestDevices.length)}</strong></span><span>Observed <strong>${esc(observed.length)}</strong></span><span>Registered <strong>${esc(registered)}</strong></span><span>Degraded <strong>${esc(degraded)}</strong></span><span>Offline/Fault <strong>${esc(fault)}</strong></span></div><div class='actions'>${setupAction}</div></div><section><h2 class='section-title'>Quest devices</h2><div class='observed-table-wrap'><table class='observed-table device-table'><thead><tr><th>Device</th><th>Health</th><th>Status</th><th>Client</th><th>Caps</th><th>Observed</th><th>Enabled</th><th></th></tr></thead><tbody>${questRows}</tbody></table></div></section><div style='height:12px'></div><section><h2 class='section-title'>Physical clients</h2><div class='observed-table-wrap'><table class='observed-table device-table'><thead><tr><th>Client</th><th>Status</th><th>Link</th><th>FW</th><th>Mode</th><th>State</th><th>Boot</th></tr></thead><tbody>${observedRows}</tbody></table></div></section>`;
}

function renderObservedView(){
setPage('Observed clients','Physical MQTT clients');
const known=knownDeviceIds();
const all=observedItems();
const items=all.filter(o=>observedFilter==='registered'?known.has(o.device_id):(observedFilter==='unregistered'?!known.has(o.device_id):true));
const registered=all.filter(o=>known.has(o.device_id)).length;
const unregistered=all.length-registered;
const rows=items.length?items.map(o=>{
const reg=observedRegistration(o.device_id);
const action=reg&&reg.via==='quest_device'?uiButton({label:'Setup',kind:'small-btn',action:'device.setup.open',dataset:{'device-id':reg.device_id}}):(reg?`<span class='muted'>linked</span>`:uiButton({label:'Add',kind:'small-btn',action:'device.setup.open',dataset:{'device-id':'new'}}));
return `<tr><td><strong>${esc(observedDisplayName(o))}</strong><span>${esc(o.device_id||'')}</span></td><td>${status(o.connectivity)}</td><td><span class='badge ${reg?'selected-badge':''}'>${reg?'registered':'unregistered'}</span></td><td>${esc(o.fw_version||'n/a')}</td><td>${esc(o.mode||'')}</td><td>${esc(o.state||'')}</td><td>${esc(o.boot_id||'n/a')}</td><td class='observed-actions'>${action}</td></tr>`;
}).join(''):`<tr><td colspan='8' class='observed-empty'>No observed clients</td></tr>`;
return `<div class='observed-toolbar'><select class='scenario-select' data-observed-filter><option value='all' ${observedFilter==='all'?'selected':''}>All observed</option><option value='registered' ${observedFilter==='registered'?'selected':''}>Registered</option><option value='unregistered' ${observedFilter==='unregistered'?'selected':''}>Unregistered</option></select><div class='observed-summary'><span>Observed <strong>${esc(all.length)}</strong></span><span>Registered <strong>${esc(registered)}</strong></span><span>Unregistered <strong>${esc(unregistered)}</strong></span></div></div><div class='observed-table-wrap'><table class='observed-table'><thead><tr><th>Client</th><th>Status</th><th>Link</th><th>FW</th><th>Mode</th><th>State</th><th>Boot</th><th></th></tr></thead><tbody>${rows}</tbody></table></div>`;
}

function auditRow(a){
return `<tr><td>${esc(a.timestamp_ms||0)}</td><td><span class='${a.success?'ok-text':'bad-text'}'>${a.success?'OK':'FAIL'}</span></td><td><strong>${esc(deviceDisplayName(a.device_id))}</strong><span>${esc(a.device_id||'')}</span></td><td>${esc(a.action_id||'action')}</td><td>${esc(a.source||'')}</td><td>${esc(a.error_code||'ok')}</td></tr>`;
}

function renderAuditView(){
setPage('Audit','Recent operator actions');
const items=auditItems();
return `<div class='observed-table-wrap'><table class='observed-table audit-table'><thead><tr><th>Time</th><th>Result</th><th>Device</th><th>Action</th><th>Source</th><th>Error</th></tr></thead><tbody>${items.length?items.map(auditRow).join(''):`<tr><td colspan='6' class='observed-empty'>No audit entries</td></tr>`}</tbody></table></div>`;
}

function timelineRow(t){
const target=t.device_id||t.room_id||t.source||'';
const targetName=t.device_id?deviceDisplayName(t.device_id):(t.room_id?roomName(t.room_id):target);
const sev=t.severity||'info';
const cls=sev==='error'?'bad-text':(sev==='warning'?'warn-text':'ok-text');
return `<tr><td>${esc(t.timestamp_ms||0)}</td><td><span class='${cls}'>${esc(sev)}</span></td><td><strong>${esc(t.title||t.type)}</strong>${t.details?`<span>${esc(t.details)}</span>`:''}</td><td>${esc(targetName||'')}</td><td>${esc(t.type||'event')}</td><td>${esc(t.source||'system')}</td></tr>`;
}

function renderTimelineView(){
setPage('Timeline','Recent system events');
const items=timelineItems();
return `<div class='observed-table-wrap'><table class='observed-table timeline-table'><thead><tr><th>Time</th><th>Severity</th><th>Event</th><th>Target</th><th>Type</th><th>Source</th></tr></thead><tbody>${items.length?items.map(timelineRow).join(''):`<tr><td colspan='6' class='observed-empty'>No timeline events</td></tr>`}</tbody></table></div>`;
}

function renderAdminPlaceholder(title,sub){
setPage(title,sub);
return `<div class='card empty'>Admin editor section is reserved for the next implementation step.</div>`;
}
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
// GM panel source part. Edit this file, then rebuild gm_panel.js.
function renderProfilesAdminView(){
setPage('Game Modes','Start presets for room scenarios');
const rooms=(gmState&&Array.isArray(gmState.rooms))?gmState.rooms:[];
if(!rooms.length)return `<div class='card empty'>No rooms available</div>`;
if(!profileEditor.room_id||!rooms.some(r=>r.room_id===profileEditor.room_id)){
profileEditor.room_id=rooms[0].room_id;
}
const roomId=profileEditor.room_id;
const profiles=roomProfiles(roomId);
const scenarios=roomScenarios(roomId);
const editing=profiles.find(p=>p.id===profileEditor.profile_id)||null;
const prefill=(!editing&&profileEditor.prefill&&profileEditor.prefill.room_id===roomId)?profileEditor.prefill:null;
const editorOpen=!!(profileEditor.open||editing||profileEditor.dirty);
const modeName=(editing&&editing.name)||(prefill&&prefill.name)||'';
const modeId=(editing&&editing.id)||(prefill&&prefill.id)||'';
const firstValidScenario=scenarios.find(s=>s.valid!==false)||scenarios[0]||null;
const scenarioValue=(editing&&editing.scenario_id)||(prefill&&prefill.scenario_id)||(firstValidScenario&&firstValidScenario.id)||'';
const scenarioMissing=!!(scenarioValue&&!scenarios.some(s=>s.id===scenarioValue));
const scenarioInvalid=!!(scenarios.find(s=>s.id===scenarioValue&&s.valid===false));
const scenarioOptions=scenarios.length?scenarios.map(s=>`<option value='${esc(s.id)}' ${s.id===scenarioValue?'selected':''} ${s.valid===false?'disabled':''}>${esc(s.name||s.id)}${s.valid===false?' (invalid)':''}</option>`).join('')+(scenarioMissing?`<option value='${esc(scenarioValue)}' selected>${esc(scenarioValue)} (missing)</option>`:''):`<option value=''>No scenarios</option>`;
const scenarioHelp=!scenarios.length?`<div class='empty'>Create a room scenario before saving a game mode.</div><div class='actions'>${uiButton({label:'Create scenario',action:'admin.open',dataset:{view:'scenarios','room-id':roomId}})}</div>`:(scenarioMissing?`<div class='row-meta bad-text'>Selected scenario is missing. Choose another scenario before saving.</div>`:(scenarioInvalid?`<div class='row-meta bad-text'>Selected scenario has validation errors.</div>`:''));
const minutes=Math.max(1,Math.round(((editing&&editing.duration_ms)||(prefill&&prefill.duration_ms)||3600000)/60000));
const hintPack=(editing&&editing.hint_pack_id)||(prefill&&prefill.hint_pack_id)||'';
const audioPack=(editing&&editing.audio_pack_id)||(prefill&&prefill.audio_pack_id)||'';
const enabled=!editing||editing.enabled!==false;
const selectedProfileId=roomSelectedProfileId(roomId);
const selectedProfile=profiles.find(p=>p.id===selectedProfileId)||null;
const profileRows=profiles.length?profiles.map(p=>{
const selected=p.id===selectedProfileId;
const invalid=p.valid===false;
const disabled=p.enabled===false;
return `<div class='row-card profile-row ${selected?'selected-row':''}'><div class='row-main'><div class='row-title'>${esc(p.name||p.id)} ${selected?`<span class='badge selected-badge'>selected</span>`:''} ${disabled?`<span class='badge'>disabled</span>`:''} ${invalid?`<span class='badge scenario-issue-badge error'>invalid</span>`:''}</div><div class='profile-mode-summary'><span>${esc(scenarioName(roomId,p.scenario_id))}</span><span>${esc(fmtClock(p.duration_ms))}</span></div></div>${uiActions([
uiButton({label:'Edit',action:'profile.edit',dataset:{'profile-id':p.id}}),
uiButton({label:'Select',action:'profile.select',dataset:{'profile-id':p.id},disabled:selected||invalid||disabled}),
uiButton({label:'Delete',kind:'danger',action:'profile.delete',dataset:{'profile-id':p.id},confirm:`Delete game mode ${p.id}?`}),
])}</div>`;
}).join(''):`<div class='card empty'>No game modes for this room</div>`;
const saveDisabled=!scenarios.length||scenarioMissing||scenarioInvalid;
const editorHtml=editorOpen?`<div class='card'><div class='card-head'><div><h2 class='section-title'>${editing?'Edit game mode':'New game mode'}${profileEditor.dirty?' *':''}</h2><div class='card-sub'>A game mode selects one scenario and game duration for operators.</div></div><label class='row-meta'><input id='profile_enabled' type='checkbox' ${enabled?'checked':''} style='min-width:auto'> Enabled</label></div><div class='field-grid'><label class='field-stack'><span>Mode name</span><input id='profile_name' placeholder='Garri Potter' value='${esc(modeName)}'></label><label class='field-stack'><span>Duration, min</span><input id='profile_duration' type='number' min='1' step='1' placeholder='60' value='${minutes}'></label></div><div class='form-section'><label class='field-stack'><span>Scenario</span><select id='profile_scenario' class='scenario-select'>${scenarioOptions}</select></label><div class='profile-selected-summary'><div><span>Scenario</span><strong>${esc(scenarioName(roomId,scenarioValue))}</strong></div><div><span>Duration</span><strong>${esc(fmtClock(minutes*60000))}</strong></div></div></div><details class='scenario-advanced'><summary>Advanced</summary><div class='row'><input id='profile_id' placeholder='Mode ID' value='${esc(modeId)}'></div><div class='row'><input id='profile_hint_pack' placeholder='Hint pack ID' value='${esc(hintPack)}'><input id='profile_audio_pack' placeholder='Audio pack ID' value='${esc(audioPack)}'></div></details>${scenarioHelp}<div style='height:12px'></div>${uiActions([
uiButton({label:'Save game mode',action:'profile.save',disabled:saveDisabled}),
editing?uiButton({label:'Select for room',action:'profile.select',dataset:{'profile-id':editing.id},disabled:editing.id===selectedProfileId||saveDisabled||enabled===false}):'',
])}<div id='profile_editor_status' class='row-meta'></div></div>`:`<div class='card empty'><h2 class='section-title'>Game mode editor</h2><div class='row-meta'>Select a game mode or create a new one.</div></div>`;
return `<div class='scenario-room-bar'><div><h2 class='section-title'>Room</h2><select class='scenario-select' data-profile-room-select>${rooms.map(r=>`<option value='${esc(r.room_id)}' ${r.room_id===roomId?'selected':''}>${esc(r.title||r.room_id)}</option>`).join('')}</select></div><div class='row-meta'>Selected: <strong>${esc((selectedProfile&&(selectedProfile.name||selectedProfile.id))||'none')}</strong></div></div><div class='profile-admin-layout'><section><div class='card-head'><h2 class='section-title'>Game modes</h2>${uiActions([uiButton({label:'Add game mode',action:'profile.new'})])}</div><div class='list'>${profileRows}</div></section><section>${editorHtml}</section></div>`;
}
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
;if(step.allow_operator_skip)out.allow_operator_skip=true;if(step.operator_skip_label)out.operator_skip_label=step.operator_skip_label;if(step.device_id)out.device_id=step.device_id;if(step.scenario_id)out.scenario_id=step.scenario_id;if(step.command_id)out.command_id=step.command_id;if(step.event_id)out.event_id=step.event_id;if(step.params)out.params=step.params;if(step.duration_ms!==undefined&&step.duration_ms!==null)out.duration_ms=Number(step.duration_ms)||0;if(step.event_type)out.event_type=step.event_type;if(step.source_id)out.source_id=step.source_id;if(step.operator_prompt)out.prompt=step.operator_prompt;if(step.operator_approve_label)out.approve_label=step.operator_approve_label;if(step.prompt)out.prompt=step.prompt;if(step.approve_label)out.approve_label=step.approve_label;if(Array.isArray(step.commands))out.commands=step.commands.map(cmd=>({device_id:cmd.device_id||'',command_id:cmd.command_id||'',params:cmd.params&&typeof cmd.params==='object'?cmd.params:{}}));if(Array.isArray(step.events))out.events=step.events.map(ev=>({device_id:ev.device_id||'',event_id:ev.event_id||''}));if(Array.isArray(step.flags))out.flags=step.flags.map(flag=>({flag_name:flag.flag_name||flag.name||'',value:flag.value!==false}));if(step.message)out.message=step.message;if(step.operator_message)out.message=step.operator_message;if(step.flag_name)out.flag_name=step.flag_name;if(step.flag_value!==undefined)out.value=!!step.flag_value;if(step.value!==undefined)out.value=!!step.value;if(type==='WAIT_TIME'&&(!Number.isFinite(Number(out.duration_ms))||Number(out.duration_ms)<=0))out.duration_ms=1000;return out;}
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
setTimeout(()=>loadHardwareIoStatus(true),0);
}
const catalog=scenarioEditorCatalog(scenarioEditor.room_id);
const catalogDevices=Array.isArray(catalog.quest_devices)?catalog.quest_devices:[];
const base=catalogDevices.length?catalogDevices:questDevices().map(device=>({
id:device.id||'',name:device.name||device.id||'',room_id:device.room_id||'',commands:Array.isArray(device.commands)?device.commands:[],events:Array.isArray(device.events)?device.events:[]}
)).filter(device=>device.id);
return base.map(scenarioNormalizeHardwareDevice).filter(device=>device.id&&(Array.isArray(device.commands)&&device.commands.length||Array.isArray(device.events)&&device.events.length||device.id!=='system_io'));
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
out.commands=Array.isArray(device.commands)?device.commands.slice():[];
out.events=Array.isArray(device.events)?device.events.slice():[];
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

function renderAudioChannelParam(key,label,value){
const selected=audioChannelValue({channel:value});
return `<div class='row'><select class='scenario-select' data-step-param='${esc(key)}'><option value='effect' ${selected==='effect'?'selected':''}>Effect / one-shot</option><option value='background' ${selected==='background'?'selected':''}>Background / music bed (WAV only)</option></select></div>`;
}

function renderAudioFileParam(key,label,value,channel){
scheduleGMAudioFilesLoad();
const selected=value===undefined?'':String(value||'');
const background=String(channel||'effect')==='background';
const files=gmAudioFileItems().filter(item=>background?audioFileIsWav(item.path):audioFileIsPlayableEffect(item.path));
const refresh=uiButton({label:gmAudioFiles.loading?'Loading files':'Refresh files',action:'audio.files.refresh',disabled:gmAudioFiles.loading});
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
if(type==='SET_FLAG')return renderSetFlagPayload(step);
if(scenarioStepSchema(type))return renderScenarioSchemaPayload(step,type);
if(type==='OPERATOR_APPROVAL')return `<div class='row'><input data-step-field='prompt' placeholder='Operator prompt' value='${esc(step.prompt||step.operator_prompt||'')}'><input data-step-field='approve_label' placeholder='Approve label' value='${esc(step.approve_label||step.operator_approve_label||'Continue')}'></div>`;
return `<div class='row'><input data-step-field='duration_ms' type='number' min='1' step='1' placeholder='Duration sec' value='${esc(durationMsToSeconds(step.duration_ms||1000))}'><span class='row-meta'>sec</span></div>`;
}

// GM panel source part. Edit this file, then rebuild gm_panel.js.
function scenarioIsReactiveV2Branch(branch){
return scenarioBranchTypeValue(branch)==='reactive'&&(Array.isArray(branch&&branch.variants)||!!(branch&&branch.trigger));
}

function reactiveV2ActionTypes(){
return ['DEVICE_COMMAND','WAIT_TIME','SET_FLAG','SHOW_OPERATOR_MESSAGE'];
}

function reactiveV2ActionTypeOptions(type){
const selected=scenarioStepTypeValue({type});
const types=reactiveV2ActionTypes();
const all=types.includes(selected)?types:[selected].concat(types);
return all.map(t=>`<option value='${esc(t)}' ${selected===t?'selected':''}>${esc(scenarioStepTypeLabel(t))}</option>`).join('');
}

function defaultReactiveV2Trigger(){
const device=firstDeviceWithEvent();
const event=firstEventForDevice(device);
return {kind:'device_event',device_id:device&&device.id||'',event_id:event&&event.id||''};
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
branch.variants=branch.variants.map((variant,index)=>({id:variant&&variant.id||`variant_${index+1}`,label:variant&&variant.label||variant&&variant.name||(index===0?'Actions':`Variant ${index+1}`),actions:Array.isArray(variant&&variant.actions)?variant.actions:[]}));
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
let selectedDevice=trigger.device_id||((devices[0]&&devices[0].id)||'');
const device=scenarioDeviceById(selectedDevice)||devices[0]||null;
if(device&&!selectedDevice)selectedDevice=device.id||'';
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
body=`<div class='scenario-v2-inline-fields'><input data-v2-trigger-field='operator_event' placeholder='Operator event' value='${esc(trigger.operator_event||'')}'></div>`;
}else{
body=`<div class='scenario-v2-inline-fields'><input data-v2-trigger-field='runtime_event' placeholder='Runtime event' value='${esc(trigger.runtime_event||'')}'></div>`;
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

function renderReactiveV2Action(action,variantIndex,actionIndex){
const type=scenarioStepTypeValue(action);
const summary=scenarioStepSummaryText(action);
const key=reactiveV2ActionKey(variantIndex,actionIndex);
const expanded=scenarioEditor.expanded_v2_action===key;
const controls=`<button class='icon-btn' data-action='scenario.reactive_v2' data-op='edit_action' data-variant-index='${variantIndex}' data-action-index='${actionIndex}' title='Edit'>${expanded?'&times;':'&#9998;'}</button><button class='icon-btn danger' data-action='scenario.reactive_v2' data-op='delete_action' data-variant-index='${variantIndex}' data-action-index='${actionIndex}'>&times;</button>`;
const body=expanded?`<div class='scenario-step-edit'><div class='scenario-v2-action-fields'><input data-step-field='label' placeholder='Action label' value='${esc(action.label||'')}'><select data-step-field='type'>${reactiveV2ActionTypeOptions(type)}</select></div>${renderScenarioStepPayload(action,type)}</div>`:'';
return `<div class='builder-step scenario-step-row scenario-step-${scenarioStepVisualType(action)} compact-step scenario-v2-action ${expanded?'scenario-step-expanded':''}' data-v2-action='${actionIndex}' data-variant-index='${variantIndex}'><div class='scenario-step-line'><div class='scenario-step-line-main'><span class='scenario-step-number'>${actionIndex+1}.</span><span class='scenario-step-icon'>${scenarioStepIcon(action)}</span><span class='scenario-step-summary'>${esc(summary)}</span><span class='badge scenario-type-badge'>${esc(scenarioStepBadgeLabel(action))}</span></div><div class='actions compact-actions'>${controls}</div></div>${body}</div>`;
}

function reactiveV2ActionKey(variantIndex,actionIndex){
return `${Number(variantIndex)||0}:${Number(actionIndex)||0}`;
}

function renderReactiveV2ActionAddButtons(variantIndex){
return `<div class='scenario-v2-action-add'>${uiButton({label:'Add device command',action:'scenario.reactive_v2',dataset:{op:'add_action','action-type':'DEVICE_COMMAND','variant-index':variantIndex}})}${uiButton({label:'Add wait',action:'scenario.reactive_v2',dataset:{op:'add_action','action-type':'WAIT_TIME','variant-index':variantIndex}})}${uiButton({label:'Add flag',action:'scenario.reactive_v2',dataset:{op:'add_action','action-type':'SET_FLAG','variant-index':variantIndex}})}${uiButton({label:'Add message',action:'scenario.reactive_v2',dataset:{op:'add_action','action-type':'SHOW_OPERATOR_MESSAGE','variant-index':variantIndex}})}</div>`;
}

function renderReactiveV2Variants(branch){
const variants=Array.isArray(branch.variants)?branch.variants:[];
const mode=String(branch.policy&&branch.policy.mode||'single');
const isSingle=mode==='single';
const isEscalate=mode==='escalate';
const title=isEscalate?'Escalation levels':(isSingle?'Actions':'Variants');
const addLabel=isEscalate?'Add level':'Add variant';
const shown=isSingle?(variants.length?[variants[0]]:[{id:'variant_1',label:'Actions',actions:[]}]):variants;
const maxFireValue=Number(branch.policy&&branch.policy.max_fire_count)||Number(branch.max_fire_count)||shown.length||1;
const escalateControls=isEscalate?`<label class='scenario-v2-max-fire'><span>Stop after level</span><input data-v2-policy-field='max_fire_count' type='number' min='0' step='1' value='${esc(maxFireValue)}'></label>`:'';
return `<section class='scenario-v2-rule scenario-v2-then'><div class='scenario-v2-rule-label'>Then</div><div class='scenario-v2-rule-body'><div class='scenario-v2-subtitle-row'><div class='scenario-v2-subtitle'>${esc(title)}</div>${escalateControls}</div><div class='scenario-v2-variant-list'>${shown.map((variant,index)=>{const originalIndex=isSingle?0:index;const label=isEscalate?`Level ${index+1}`:(isSingle?'Actions':`Variant ${index+1}`);const nameValue=isSingle?'Actions':(variant.label||variant.name||label);return `<div class='scenario-v2-variant' data-v2-variant='${originalIndex}'><div class='scenario-v2-variant-head'><label class='field-stack'><span>${esc(label)}</span><input data-v2-variant-field='label' placeholder='${esc(label)} label' value='${esc(nameValue)}' ${isSingle?'readonly':''}></label><div class='actions'>${isSingle?'':uiButton({label:`Delete ${isEscalate?'level':'variant'}`,kind:'danger',action:'scenario.reactive_v2',dataset:{op:'delete_variant','variant-index':originalIndex},disabled:variants.length<=1})}</div></div><details class='scenario-advanced compact-advanced scenario-v2-variant-id'><summary>${esc(isEscalate?'Level id':'Variant id')}</summary><div class='row'><input data-v2-variant-field='id' placeholder='Variant ID' value='${esc(variant.id||'')}'></div></details><div class='scenario-v2-action-list'>${(Array.isArray(variant.actions)?variant.actions:[]).map((action,actionIndex)=>renderReactiveV2Action(action,originalIndex,actionIndex)).join('')||`<div class='empty'>No actions yet. Add one or more actions below.</div>`}</div>${renderReactiveV2ActionAddButtons(originalIndex)}</div>`;}).join('')}</div>${isSingle?'':uiButton({label:addLabel,action:'scenario.reactive_v2',dataset:{op:'add_variant'}})}</div></section>`;
}

function renderReactiveV2Editor(branch){
ensureReactiveV2Branch(branch);
return `<div class='scenario-v2-editor'>${renderReactiveV2Type(branch)}${renderReactiveV2Trigger(branch)}${renderReactiveV2Guards(branch)}${renderReactiveV2Variants(branch)}${renderReactiveV2Policy(branch)}</div>`;
}

function collectReactiveActionFromElement(el,previous,index){
if(!el.querySelector(`[data-step-field='type']`)&&previous&&previous.type)return JSON.parse(JSON.stringify(previous));
const get=name=>{const n=el.querySelector(`[data-step-field='${name}']`);return n?n.value:'';};
const type=get('type')||previous.type||'SET_FLAG';
if(scenarioStepTypeValue(previous)!==scenarioStepTypeValue({type})){
previous=newScenarioStepForType(index,type);
}
let label=get('label')||previous.label||'';
const action={id:previous.id||slugifyId(label||`action_${index+1}`,'action'),label,type};
if(type==='DEVICE_COMMAND'){
action.device_id=get('device_id')||previous.device_id||'';
const device=scenarioDeviceById(action.device_id);
action.command_id=scenarioValidCommandId(device,get('command_id')||previous.command_id||'');
const commandName=scenarioCommandName(action.device_id,action.command_id);
if(!label||label===previous.label||label.indexOf(' - ')>=0){
label=action.device_id==='system_audio'?commandName:`${scenarioRoomNameForDevice(device)}: ${scenarioDeviceName(device)} - ${commandName}`;
action.label=label;
}
const command=scenarioCommandById(action.device_id,action.command_id);
const params=commandSupportsScenarioParams(command)?{...(previous.params&&typeof previous.params==='object'?previous.params:{})}:{};
el.querySelectorAll('[data-step-param]').forEach(input=>{const key=input.dataset.stepParam||'';if(!key)return;const typeAttr=(input.getAttribute('type')||'').toLowerCase();if(input.type==='checkbox')params[key]=input.checked;else if(typeAttr==='number')params[key]=Number(input.value)||0;else params[key]=input.value;});
if(Object.keys(params).length)action.params=params;
}else if(type==='DEVICE_COMMAND_GROUP'){
action.mode=previous.mode||'sequential';
action.commands=[];
el.querySelectorAll('[data-command-group-item]').forEach((item,itemIndex)=>{const deviceField=item.querySelector('[data-group-command-field="device_id"]');const commandField=item.querySelector('[data-group-command-field="command_id"]');const previousItem=Array.isArray(previous.commands)?(previous.commands[itemIndex]||{}):{};const deviceId=(deviceField?deviceField.value:'')||previousItem.device_id||'';const device=scenarioDeviceById(deviceId);const commandId=scenarioValidCommandId(device,(commandField?commandField.value:'')||previousItem.command_id||'');const command=scenarioCommandById(deviceId,commandId);const params=commandSupportsScenarioParams(command)?{...(previousItem.params&&typeof previousItem.params==='object'?previousItem.params:{})}:{};item.querySelectorAll('[data-step-param]').forEach(input=>{const key=input.dataset.stepParam||'';if(!key)return;const typeAttr=(input.getAttribute('type')||'').toLowerCase();if(input.type==='checkbox')params[key]=input.checked;else if(typeAttr==='number')params[key]=Number(input.value)||0;else params[key]=input.value;});const out={device_id:deviceId,command_id:commandId};if(Object.keys(params).length)out.params=params;action.commands.push(out);});
}else if(type==='WAIT_TIME'){
action.duration_ms=get('duration_ms')?durationSecondsToMs(get('duration_ms')):(previous.duration_ms||1000);
}else if(type==='SHOW_OPERATOR_MESSAGE'){
action.message=get('message')||previous.message||'';
}else if(type==='SET_FLAG'){
const valueField=el.querySelector(`[data-step-field='value']`);
action.flag_name=get('flag_name')||previous.flag_name||previous.flag||'';
action.value=valueField?(valueField.type==='checkbox'?valueField.checked:valueField.value!=='false'):(previous.value!==false);
}
return action;
}

function collectReactiveV2BranchFromDom(branch,root){
if(!branch||!root)return branch;
ensureReactiveV2Branch(branch);
const value=(selector,def='')=>{const nodes=Array.from(root.querySelectorAll(selector));if(!nodes.length)return def;const checked=nodes.find(n=>n.type==='radio'&&n.checked);const n=checked||nodes[0];return n?n.value:def;};
const runOnceField=root.querySelector('[data-scenario-branch-field="run_once"]');
branch.priority=Number(value('[data-v2-branch-field="priority"]',branch.priority))||0;
branch.policy=branch.policy&&typeof branch.policy==='object'?branch.policy:{};
branch.policy.mode=value('[data-v2-policy-field="mode"]',branch.policy.mode||'single')||'single';
if(branch.policy.mode==='single'&&runOnceField){
branch.run_once=runOnceField.type==='checkbox'?!!runOnceField.checked:String(runOnceField.value)==='true';
}
branch.policy.cooldown_ms=Number(branch.cooldown_ms)||Number(branch.policy.cooldown_ms)||0;
branch.policy.max_fire_count=Math.max(0,Math.round(Number(value('[data-v2-policy-field="max_fire_count"]',branch.policy.max_fire_count||0))||0));
branch.max_fire_count=branch.policy.max_fire_count;
normalizeReactiveV2RepeatPolicy(branch);
branch.reentry={mode:value('[data-v2-reentry-field="mode"]',branch.reentry&&branch.reentry.mode||'ignore')||'ignore'};
branch.result_policy={
on_done:value('[data-v2-result-field="on_done"]',branch.result_policy&&branch.result_policy.on_done||'continue')||'continue',
on_fail:value('[data-v2-result-field="on_fail"]',branch.result_policy&&branch.result_policy.on_fail||'fail_reaction')||'fail_reaction',
on_timeout:value('[data-v2-result-field="on_timeout"]',branch.result_policy&&branch.result_policy.on_timeout||'fail_reaction')||'fail_reaction'}
;const resultFlag=value('[data-v2-result-field="timeout_flag"]',branch.result_policy&&branch.result_policy.timeout_flag||branch.result_policy&&branch.result_policy.flag||'');if(resultFlag){branch.result_policy.flag=resultFlag;branch.result_policy.timeout_flag=resultFlag;}
const kind=value('[data-v2-trigger-field="kind"]',branch.trigger&&branch.trigger.kind||'device_event')||'device_event';
branch.trigger={kind};
if(kind==='device_event'){branch.trigger.device_id=value('[data-v2-trigger-field="device_id"]',branch.trigger.device_id||'');const device=scenarioDeviceById(branch.trigger.device_id);branch.trigger.event_id=scenarioValidEventId(device,value('[data-v2-trigger-field="event_id"]',branch.trigger.event_id||''));}
else if(kind==='flag_changed')branch.trigger.flag_name=value('[data-v2-trigger-field="flag_name"]',branch.trigger.flag_name||'');
else if(kind==='operator_event')branch.trigger.operator_event=value('[data-v2-trigger-field="operator_event"]',branch.trigger.operator_event||'');
else if(kind==='runtime_event')branch.trigger.runtime_event=value('[data-v2-trigger-field="runtime_event"]',branch.trigger.runtime_event||'');
branch.guard_flags=[];
root.querySelectorAll('[data-v2-guard-item]').forEach(item=>{const name=(item.querySelector('[data-v2-guard-field="flag"]')||{}).value||'';const val=(item.querySelector('[data-v2-guard-field="value"]')||{}).value;if(name)branch.guard_flags.push({flag:name,value:val!=='false'});});
const previousVariants=Array.isArray(branch.variants)?branch.variants.map(variant=>JSON.parse(JSON.stringify(variant))):[];
branch.variants=[];
root.querySelectorAll('[data-v2-variant]').forEach((variantEl,variantIndex)=>{const id=(variantEl.querySelector('[data-v2-variant-field="id"]')||{}).value||`variant_${variantIndex+1}`;const label=(variantEl.querySelector('[data-v2-variant-field="label"]')||{}).value||`Variant ${variantIndex+1}`;const previous=previousVariants[variantIndex]||{};const variant={id,label,actions:[]};variantEl.querySelectorAll('[data-v2-action]').forEach((actionEl,actionIndex)=>{const previousAction=Array.isArray(previous.actions)?(previous.actions[actionIndex]||{}):{};variant.actions.push(collectReactiveActionFromElement(actionEl,previousAction,actionIndex));});branch.variants.push(variant);});
if(!branch.variants.length)branch.variants=defaultReactiveV2BranchFields().variants;
branch.steps=[];
if(!Array.isArray(branch.on_complete)||!branch.on_complete.length)delete branch.on_complete;
return normalizeReactiveV2RepeatPolicy(branch);
}

function applyReactiveV2Action(action,variantIndex,actionIndex,actionType){
const draft=collectScenarioEditor();
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
scenarioEditor.draft=draft;
scenarioEditor.dirty=true;
scenarioEditor.validation_report=null;
skipNextScenarioDomSync();
render();
}
// GM panel source part. Edit this file, then rebuild gm_panel.js.
function renderScenarioBranchTabs(base,activeIndex){
const branches=Array.isArray(base&&base.branches)?base.branches:[];
if(!branches.length)return '';
const flow=branches.map((branch,index)=>({branch,index})).filter(item=>scenarioBranchTypeValue(item.branch)==='normal');
const reactions=branches.map((branch,index)=>({branch,index})).filter(item=>scenarioBranchTypeValue(item.branch)==='reactive');
const tab=item=>`<button class='scenario-branch-tab ${item.index===activeIndex?'active':''}' data-action='scenario.branch' data-op='select' data-branch-index='${item.index}'><span>${esc(item.branch.name||item.branch.id||`Branch ${item.index+1}`)}</span><em>${esc(scenarioIsReactiveV2Branch(item.branch)?(Array.isArray(item.branch.variants)?item.branch.variants.length:0):(Array.isArray(item.branch.steps)?item.branch.steps.length:0))}</em></button>`;
return `<div class='scenario-branch-tabs grouped'><div class='scenario-branch-tab-group'><span class='row-meta'>Scenario flow</span>${flow.map(tab).join('')}${uiButton({label:'+ Branch',kind:'scenario-branch-add',action:'scenario.branch',dataset:{op:'add'}})}</div><div class='scenario-branch-tab-group'><span class='row-meta'>Reactions</span>${reactions.map(tab).join('')}${uiButton({label:'+ Reaction',kind:'scenario-branch-add',action:'scenario.branch',dataset:{op:'add_reactive'}})}</div></div>`;
}

function renderScenarioBranchSettings(branch,index,total){
if(!branch)return '';
const branchIdKey=`scenario:branch:${scenarioEditor.room_id}:${branch.id||index}`;
const type=scenarioBranchTypeValue(branch);
const isV2=scenarioIsReactiveV2Branch(branch);
const typeField=type==='normal'?`<div class='field-stack'><span>Type</span><select data-scenario-branch-field='type'><option value='normal' selected>Scenario flow</option><option value='reactive'>Reaction</option></select></div>`:`<input type='hidden' data-scenario-branch-field='type' value='reactive'>`;
const controls=type==='normal'?`<label class='row-meta branch-toggle'><input data-scenario-branch-field='required_for_completion' type='checkbox' ${branch.required_for_completion!==false?'checked':''}> Required for finish</label>`:(isV2?'':`<label class='row-meta branch-toggle'><input data-scenario-branch-field='run_once' type='checkbox' ${branch.run_once?'checked':''}> Run once</label><div class='field-stack compact-field'><span>Cooldown, sec</span><input data-scenario-branch-field='cooldown_sec' type='number' min='0' step='1' value='${esc(Math.round((Number(branch.cooldown_ms)||0)/1000))}'></div>`);
return `<div class='scenario-branch-settings ${type==='reactive'?'reactive':''}'><div class='field-stack branch-name-field'><span>${type==='reactive'?'Reaction name':'Branch name'}</span><input data-scenario-branch-field='name' placeholder='${type==='reactive'?'Reaction name':'Branch name'}' value='${esc(branch.name||'')}'></div>${typeField}<label class='row-meta branch-toggle'><input data-scenario-branch-field='enabled' type='checkbox' ${branch.enabled!==false?'checked':''}> Enabled</label>${controls}${uiButton({label:'Delete',kind:'danger scenario-branch-delete',action:'scenario.branch',dataset:{op:'delete','branch-index':index},disabled:total<=1})}<details class='scenario-advanced compact-advanced' ${detailsAttrs(branchIdKey,false)}><summary>Branch id</summary><div class='row'><input data-scenario-branch-field='id' placeholder='Branch ID' value='${esc(branch.id||'')}'></div></details></div>`;
}

function applyScenarioBranchAction(action,index){
const wasDirty=!!scenarioEditor.dirty;
const draft=collectScenarioEditor();
draft.branches=Array.isArray(draft.branches)&&draft.branches.length?draft.branches:[defaultScenarioBranch(0,[])];
if(action==='select'){
scenarioEditor.active_branch=Number.isFinite(index)?index:0;
scenarioEditor.expanded_step=-1;
scenarioEditor.expanded_v2_action='';
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
const branchId=uniqueScenarioBranchId(draft.branches,branchType);
const branch=defaultScenarioBranch(nextIndex,[],branchType);

branch.id=branchId;
branch.name=defaultScenarioBranchName(branchId,branchType);
if(branchType==='reactive')ensureReactiveV2Branch(branch);
draft.branches.push(branch);
scenarioEditor.active_branch=draft.branches.length-1;
scenarioEditor.expanded_step=-1;
scenarioEditor.expanded_v2_action='';
}
else if(action==='delete'){
const removeIndex=Number.isFinite(index)?index:scenarioActiveBranchIndex(draft);
if(draft.branches.length<=1)return;
if(!confirm('Delete this scenario branch?'))return;
draft.branches.splice(removeIndex,1);
scenarioEditor.branch_count_shrink_allowed=true;
scenarioEditor.branch_count_shrink_floor=draft.branches.length;
scenarioEditor.active_branch=Math.max(0,Math.min(removeIndex,draft.branches.length-1));
scenarioEditor.expanded_step=-1;
scenarioEditor.expanded_v2_action='';
}
else return;
scenarioEditor.draft=draft;
scenarioEditor.dirty=true;
scenarioEditor.validation_report=null;
skipNextScenarioDomSync();
render();
}
// GM panel source part. Edit this file, then rebuild gm_panel.js.
function scenarioStepPresetButtons(branch){
if(scenarioIsReactiveV2Branch(branch))return reactiveV2PresetButtons(branch);
const allowed=scenarioAllowedStepTypesForBranch(branch);
const allowedSet=allowed?new Set(allowed):null;
const schemas=scenarioStepSchemas().filter(schema=>!allowedSet||allowedSet.has(schema.type||''));
const title=allowedSet?(Array.isArray(branch&&branch.steps)&&branch.steps.length?'Add action':'Add trigger'):'Add step';
const hasSteps=Array.isArray(branch&&branch.steps)&&branch.steps.length>0;
const hint=allowedSet&&!hasSteps?`<div class='row-meta scenario-reaction-hint'>Add one trigger first. Actions become available after the trigger.</div>`:'';
return `<h2 class='section-title'>${esc(title)}</h2>${hint}<div class='scenario-step-presets'>${schemas.map(schema=>`<div class='scenario-step-preset-row'><button data-action='scenario.step' data-op='add_schema' data-step-type='${esc(schema.type||'WAIT_TIME')}'>${esc(schema.label||schema.type)}</button><button class='icon-btn' title='Show example' aria-label='Show step example' data-action='scenario.step.help' data-step-type='${esc(schema.type||'WAIT_TIME')}'>?</button></div>`).join('')}</div>`;
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
const controls=`<div class='actions compact-actions'><button class='icon-btn' title='${expanded?'Close':'Edit'}' aria-label='${expanded?'Close':'Edit'}' data-action='scenario.step' data-op='edit' data-step-index='${index}'>${expanded?'&#10005;':'&#9998;'}</button><button class='icon-btn' title='Move up' aria-label='Move up' data-action='scenario.step' data-op='up' data-step-index='${index}' ${index<=0?'disabled':''}>&uarr;</button><button class='icon-btn' title='Move down' aria-label='Move down' data-action='scenario.step' data-op='down' data-step-index='${index}' ${index>=total-1?'disabled':''}>&darr;</button><button class='icon-btn danger' title='Delete' aria-label='Delete' data-action='scenario.step' data-op='delete' data-step-index='${index}'>&times;</button></div>`;
if(!expanded){
return `<div class='builder-step scenario-step-row scenario-step-${visual} ${validationClass} compact-step' data-scenario-step='${index}'><div class='scenario-step-line'><div class='scenario-step-line-main'><span class='scenario-step-number'>${index+1}.</span><span class='scenario-step-icon'>${scenarioStepIcon(step)}</span><span class='scenario-step-summary'>${esc(summary)}</span><span class='badge scenario-type-badge' title='${esc(fullType)}'>${esc(badge)}</span>${issueBadge}${step.enabled===false?`<span class='badge'>disabled</span>`:''}</div>${controls}</div>${renderScenarioInlineIssues(stepIssues)}</div>`;
}
return `<div class='builder-step scenario-step-row scenario-step-${visual} scenario-step-expanded ${validationClass} compact-step' data-scenario-step='${index}'><div class='scenario-step-line'><div class='scenario-step-line-main'><span class='scenario-step-number'>${index+1}.</span><span class='scenario-step-icon'>${scenarioStepIcon(step)}</span><span class='scenario-step-summary'>${esc(summary)}</span><span class='badge scenario-type-badge' title='${esc(fullType)}'>${esc(badge)}</span>${issueBadge}</div>${controls}</div>${renderScenarioInlineIssues(stepIssues)}<div class='scenario-step-edit'><div class='row compact-row'><input data-step-field='label' placeholder='Step label' value='${esc(step.label||'')}'><select data-step-field='type'>${scenarioTypeOptions(type)}</select><label class='row-meta enabled-inline'><input data-step-field='enabled' type='checkbox' ${step.enabled!==false?'checked':''} style='min-width:auto'> Enabled</label></div>${renderScenarioStepPayload(step,type)}</div></div>`;
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
if(scenarioEditor.draft&&
   scenarioEditor.draft.room_id===roomId&&
   String(scenarioEditor.draft.id||'')===String(scenarioEditor.scenario_id||''))source=JSON.parse(JSON.stringify(scenarioEditor.draft));
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
// GM panel source part. Edit this file, then rebuild gm_panel.js.
function renderStorageAdminView(){
setPage('Storage','Import, export, save and load');
const storageButtons=prefix=>uiActions([
uiButton({label:'Save to SD',action:'storage.run',dataset:{op:`${prefix}_save`}}),
uiButton({label:'Load from SD',action:'storage.run',dataset:{op:`${prefix}_load`}}),
uiButton({label:'Export JSON',action:'storage.run',dataset:{op:`${prefix}_export`}}),
]);
return `<div class='grid cols-2'><div class='card'><h2 class='section-title'>Quest devices</h2>${storageButtons('device')}<div style='height:12px'></div><div class='row'><input id='storage_devices_file' type='file' accept='.json,application/json'>${uiButton({label:'Import JSON',action:'storage.run',dataset:{op:'device_import'}})}</div><div class='row-meta'>Path: /sdcard/quest/quest_devices.json</div></div><div class='card'><h2 class='section-title'>Room scenarios</h2>${storageButtons('scenario')}<div style='height:12px'></div><div class='row'><input id='storage_scenarios_file' type='file' accept='.json,application/json'>${uiButton({label:'Import JSON',action:'storage.run',dataset:{op:'scenario_import'}})}</div><div class='row-meta'>Path: /sdcard/quest/room_scenarios.json</div></div><div class='card'><h2 class='section-title'>Game modes</h2>${storageButtons('profile')}<div style='height:12px'></div><div class='row'><input id='storage_profiles_file' type='file' accept='.json,application/json'>${uiButton({label:'Import JSON',action:'storage.run',dataset:{op:'profile_import'}})}</div><div class='row-meta'>Path: /sdcard/quest/game_profiles.json</div></div></div>`;
}

function questEditableDevices(){
return questDevices().filter(d=>d&&!d.system_device);
}

function newQuestDeviceDraft(){
return {id:'',client_id:'',name:'',enabled:true,commands:[],events:[]};
}

function currentQuestDeviceDraft(){
if(questDeviceEditor.draft)return JSON.parse(JSON.stringify(questDeviceEditor.draft));
const existing=questEditableDevices().find(d=>d.id===questDeviceEditor.device_id);
return existing?JSON.parse(JSON.stringify(existing)):newQuestDeviceDraft();
}

function physicalClientOptions(selected){
const seen=new Set();
const items=[];
observedItems().forEach(item=>{
const id=item&&item.device_id||'';
if(!id||seen.has(id))return;
seen.add(id);
const health=item.connectivity==='offline'?'offline':(item.health||item.state||'seen');
items.push({id,name:`${id} - ${health}${item.fw_version?` / fw ${item.fw_version}`:''}`});
});
if(selected&&!seen.has(selected))items.unshift({id:selected,name:`${selected} (saved)`});
return optionList(items,selected,'Select physical client');
}

function renderQuestDiscoveryPreview(){
const d=questDeviceEditor.discovery;
if(!d||!d.device)return '';
const dev=d.device;
const commands=Array.isArray(dev.commands)?dev.commands:[];
const events=Array.isArray(dev.events)?dev.events:[];
return `<div class='builder-step'><div class='card-head'><div><h2 class='section-title'>Discovered config</h2><div class='row-meta'>${esc(d.client_id||'')} / ${commands.length} commands / ${events.length} events</div></div><div class='actions'>${uiButton({label:'Import',action:'quest.discovery.apply'})}${uiButton({label:'Discard',action:'quest.discovery.discard'})}</div></div><div class='kvs'><div class='kv'><span class='k'>Commands</span><span class='v'>${esc(commands.map(c=>c.label||c.id).join(', ')||'none')}</span></div><div class='kv'><span class='k'>Events</span><span class='v'>${esc(events.map(e=>e.label||e.id).join(', ')||'none')}</span></div></div></div>`;
}

function renderQuestCommandRow(cmd,index){
const c=cmd||{};
const p=c.policy&&typeof c.policy==='object'?c.policy:{};
const params=Array.isArray(c.args_schema)?c.args_schema:[];
const paramsNote=params.length?`<div class='row-meta'>Args: ${esc(params.map(p=>p.label||p.key).join(', '))}</div>`:'';
const defaultArgs=c.default_args&&typeof c.default_args==='object'?JSON.stringify(c.default_args):'';
return `<div class='builder-step' data-quest-command='${index}'><div class='builder-step-head'><div class='builder-step-title'>Command ${index+1}${params.length?` <span class='badge'>${params.length} args</span>`:''}</div><div class='actions'>${uiButton({label:'Delete',action:'quest.command.delete',kind:'danger',dataset:{index}})}</div></div><div class='row'><input data-quest-command-field='label' placeholder='Button label' value='${esc(c.label||'')}'><input data-quest-command-field='command' placeholder='Command, e.g. relay.pulse' value='${esc(c.command||'')}'></div><div class='row'><input data-quest-command-field='capability' placeholder='Capability, e.g. relay' value='${esc(c.capability||'')}'><input data-quest-command-field='default_args' placeholder='Default args JSON' value='${esc(defaultArgs)}'></div><div class='row'><input data-quest-command-field='timeout_ms' placeholder='Timeout ms' value='${esc(p.timeout_ms||3000)}'><input data-quest-command-field='danger_level' placeholder='Danger level' value='${esc(p.danger_level||'normal')}'></div><details class='scenario-advanced'><summary>Advanced</summary><div class='row'><input data-quest-command-field='id' placeholder='Command ID' value='${esc(c.id||'')}'></div>${paramsNote}</details><label class='row-meta'><input data-quest-command-field='manual_allowed' type='checkbox' ${p.manual_allowed!==false?'checked':''} style='min-width:auto'> Allow manual control</label><label class='row-meta'><input data-quest-command-field='scenario_allowed' type='checkbox' ${p.scenario_allowed!==false?'checked':''} style='min-width:auto'> Allow scenario control</label><label class='row-meta'><input data-quest-command-field='requires_confirmation' type='checkbox' ${p.requires_confirmation?'checked':''} style='min-width:auto'> Require confirmation</label><label class='row-meta'><input data-quest-command-field='result_required' type='checkbox' ${p.result_required!==false?'checked':''} style='min-width:auto'> Wait for result</label></div>`;
}

function renderQuestEventRow(ev,index){
const e=ev||{};
const match=e.match&&typeof e.match==='object'?JSON.stringify(e.match):'';
return `<div class='builder-step' data-quest-event='${index}'><div class='builder-step-head'><div class='builder-step-title'>Event ${index+1}</div><div class='actions'>${uiButton({label:'Delete',action:'quest.event.delete',kind:'danger',dataset:{index}})}</div></div><div class='row'><input data-quest-event-field='label' placeholder='Event label' value='${esc(e.label||'')}'><input data-quest-event-field='event' placeholder='Event, e.g. input.pressed' value='${esc(e.event||'')}'></div><div class='row'><input data-quest-event-field='capability' placeholder='Capability, e.g. input' value='${esc(e.capability||'')}'><input data-quest-event-field='match' placeholder='Match JSON' value='${esc(match)}'></div><details class='scenario-advanced'><summary>Advanced</summary><div class='row'><input data-quest-event-field='id' placeholder='Event ID' value='${esc(e.id||'')}'></div></details></div>`;
}

function renderQuestDeviceListRow(d){
const health=questDeviceHealth(d);
return `<div class='row-card'><div class='row-main'><div class='row-title'>${esc(d.name||d.id)} ${d.enabled===false?`<span class='badge'>disabled</span>`:''}</div><div class='row-meta'>${esc((d.commands||[]).length)} commands / ${esc((d.events||[]).length)} events</div><div class='row-meta'>${esc(questDeviceStatusText(d))}</div><details class='scenario-advanced'><summary>Debug ids</summary><div class='row-meta'>Device ID: ${esc(d.id||'')}</div><div class='row-meta'>Client: ${esc(d.client_id||'')}</div></details></div><div>${status(health)}</div><div class='actions'>${uiButton({label:'Edit',action:'quest.device.edit',dataset:{'device-id':d.id||''}})}${uiButton({label:'Delete',action:'quest.device.delete',kind:'danger',dataset:{'device-id':d.id||''},confirm:`Delete device ${d.id||''}?`})}</div></div>`;
}

function renderQuestDeviceEditor(draft){
if(!draft){
return `<div class='card empty-state'><h2 class='section-title'>Device editor</h2><div class='empty-title'>Select a quest device or create a new one</div><div class='row-meta'>Quest devices are physical client capabilities: commands, events and manual buttons. They are used later in room scenarios.</div><div class='actions'>${uiButton({label:'Add device',action:'quest.device.new'})}</div></div>`;
}
const clientControl=observedItems().length?`<select class='scenario-select' data-quest-device-field='client_id'>${physicalClientOptions(draft&&draft.client_id||'')}</select>`:`<input data-quest-device-field='client_id' placeholder='Physical client ID' value='${esc(draft&&draft.client_id||'')}'>`;
const commandRows=(draft.commands||[]).length?draft.commands.map(renderQuestCommandRow).join(''):`<div class='empty'>No commands. Import config from the client or add a command manually.</div>`;
const eventRows=(draft.events||[]).length?draft.events.map(renderQuestEventRow).join(''):`<div class='empty'>No events. Import config from the client or add an event manually.</div>`;
return `<div class='card' data-quest-device-editor='1'><div class='card-head'><div><h2 class='section-title'>${questDeviceEditor.device_id?'Edit quest device':'New quest device'}${questDeviceEditor.dirty?' *':''}</h2><div class='card-sub'>Define what this physical client can do and report.</div></div><label class='row-meta'><input data-quest-device-field='enabled' type='checkbox' ${draft.enabled!==false?'checked':''} style='min-width:auto'> Enabled</label></div><div class='form-section'><h2 class='section-title'>Basics</h2><div class='field-grid'><label class='field-stack'><span>Device name</span><input data-quest-device-field='name' placeholder='Altar controller' value='${esc(draft.name||'')}'></label><label class='field-stack'><span>Physical client</span>${clientControl}</label></div><details class='scenario-advanced'><summary>Advanced</summary><div class='row'><input data-quest-device-field='id' placeholder='Device ID' value='${esc(draft.id||'')}'></div></details></div><div class='form-section import-panel'><div><h2 class='section-title'>Import capabilities</h2><div class='row-meta'>Ask the selected physical client for its supported commands and events.</div></div><div class='actions'>${uiButton({label:'Get config',action:'quest.device.discover',kind:'approve'})}</div></div>${renderQuestDiscoveryPreview()}<div class='form-section'><div class='card-head'><div><h2 class='section-title'>Commands</h2><div class='row-meta'>Commands can become scenario actions and manual buttons.</div></div><div class='actions'>${uiButton({label:'Add command',action:'quest.command.add'})}</div></div><div>${commandRows}</div></div><div class='form-section'><div class='card-head'><div><h2 class='section-title'>Events</h2><div class='row-meta'>Events are available as scenario waits.</div></div><div class='actions'>${uiButton({label:'Add event',action:'quest.event.add'})}</div></div><div>${eventRows}</div></div><div class='actions sticky-actions'>${uiButton({label:'Save device',action:'quest.device.save'})}${questDeviceEditor.device_id?uiButton({label:'Delete',action:'quest.device.delete',kind:'danger',dataset:{'device-id':questDeviceEditor.device_id},confirm:`Delete device ${questDeviceEditor.device_id}?`}):''}</div></div>`;
}

function renderDeviceSetupAdminView(){
setPage('Quest Devices','Device capabilities and manual controls');
const devices=questEditableDevices();
const draft=questDeviceEditor.open?currentQuestDeviceDraft():null;
const rows=devices.length?devices.map(renderQuestDeviceListRow).join(''):`<div class='card empty-state'><div class='empty-title'>No quest devices yet</div><div class='row-meta'>Add a device, select its physical client and import capabilities.</div><div class='actions'>${uiButton({label:'Add device',action:'quest.device.new'})}</div></div>`;
return `<div class='device-setup-layout'><section><div class='card-head'><div><h2 class='section-title'>Quest devices</h2><div class='card-sub'>Saved device capability sets</div></div><div class='actions'>${uiButton({label:'Add device',action:'quest.device.new'})}</div></div><div class='list'>${rows}</div></section><section>${renderQuestDeviceEditor(draft)}</section></div>`;
}

function initDeviceSetupWizard(){
return;
}

function syncGMSummaryStatus(){
const summary=gmState&&gmState.summary?gmState.summary:{};
setStatus(summary.has_fault?'fault':(summary.has_degraded?'degraded':'ok'),summary.has_fault?'state-fault':(summary.has_degraded?'state-degraded':'state-ok'));
}

function renderMainContent(){
const root=document.getElementById('gm_content');
if(!root)return;
if(gmSkipScenarioDomSync)gmSkipScenarioDomSync=false;
applyGMRoleLayout();
syncGMSummaryStatus();
let html='';
if(currentView==='dashboard')html=renderDashboard();
else if(currentView==='rooms')html=renderRoomsView();
else if(currentView==='room')html=renderRoomView();
else if(currentView==='devices')html=renderDevicesView();
else if(currentView==='observed')html=renderObservedView();
else if(currentView==='timeline')html=renderTimelineView();
else if(currentView==='audit')html=renderAuditView();
else if(currentView==='profiles')html=renderProfilesAdminView();
else if(currentView==='scenarios')html=renderScenariosAdminView();
else if(currentView==='device_setup')html=renderDeviceSetupAdminView();
else if(currentView==='hardware_io')html=renderHardwareIoView();
else if(currentView==='storage')html=renderStorageAdminView();
root.innerHTML=html;
injectRoomScenarios();
const navView=currentView==='room'?'rooms':currentView;

document.querySelectorAll('.nav-btn').forEach(b=>b.classList.toggle('active',b.dataset.view===navView));
}

function render(){
renderMainContent();
renderRightSidebar(false);
}
// GM panel source part. Edit this file, then rebuild gm_panel.js.
const GM_STATIC_TTL_MS=30000;
const gmLoadTimes={observed:0,audit:0,timeline:0,questDevices:0,roomScenarios:0,roomProfiles:0,scenarioCatalogs:0};
const gmRuntimeRenderKeys={};
const gmRuntimeRequestSeq={};
const gmLocalRuntimeRefreshUntil={};
let gmLastVersions=null;
let gmSnapshotRequestSeq=0;

function gmStaticFresh(key,ttl){
return !!gmLoadTimes[key]&&performance.now()-gmLoadTimes[key]<(ttl||GM_STATIC_TTL_MS);
}

function gmMarkStaticLoaded(key){
gmLoadTimes[key]=performance.now();
}

function gmVersionsKey(versions){
if(!versions)return '';
return [
versions.rooms||0,
versions.devices||0,
versions.scenarios||0,
versions.profiles||0,
versions.ingest||0,
versions.session||0,
versions.static||0,
versions.runtime||0,
].join(':');
}

function gmVersionChanged(prev,next,fields){
if(!prev||!next)return true;
return fields.some(field=>(prev[field]||0)!==(next[field]||0));
}

function gmCurrentViewUsesQuestDeviceStatic(){
return ['dashboard','room','devices','observed','device_setup','scenarios','hardware_io'].includes(currentView);
}

function gmCurrentViewUsesScenarioStatic(){
return ['room','scenarios','profiles'].includes(currentView);
}

function gmCurrentViewUsesProfileStatic(){
return ['room','profiles'].includes(currentView);
}

async function loadGMVersions(){
return await api.gm.versionsJson();
}

async function loadObserved(force){
if(!force&&gmStaticFresh('observed'))return;
try{
const res=await api.orchestrator.controlDevices();
gmObserved=await gmJsonOrNull(res);
gmMarkStaticLoaded('observed');
}
catch(err){
gmObserved=null;
}
}

async function loadAudit(force){
if(!force&&gmStaticFresh('audit'))return;
try{
const res=await api.orchestrator.auditRecent();
gmAudit=await gmJsonOrNull(res);
gmMarkStaticLoaded('audit');
}
catch(err){
gmAudit=null;
}
}

async function loadTimeline(force){
if(!force&&gmStaticFresh('timeline'))return;
try{
const res=await api.orchestrator.timelineRecent();
gmTimeline=await gmJsonOrNull(res);
gmMarkStaticLoaded('timeline');
}
catch(err){
gmTimeline=null;
}
}

async function loadQuestDevices(force){
if(!force&&gmStaticFresh('questDevices'))return;
try{
const res=await api.device.list(true);
gmQuestDevices=await gmJsonOrNull(res);
gmMarkStaticLoaded('questDevices');
}
catch(err){
gmQuestDevices=null;
}
}

function gmAudioFileItems(){
return gmAudioFiles&&Array.isArray(gmAudioFiles.items)?gmAudioFiles.items:[];
}

async function gmFetchAudioDir(path,depth,seen){
if(depth<0||seen.has(path))return [];
seen.add(path);
const res=await api.files.list(path);
if(!res.ok)throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
const list=await res.json();
const files=[];
const dirs=[];
(Array.isArray(list)?list:[]).forEach(item=>{
if(!item||!item.path)return;
if(item.dir)dirs.push(item.path);
else files.push({
path:item.path,size:item.size||0,dur:item.dur||0}
);
}
);
if(depth>0){
for(const dir of dirs.slice(0,24)){
files.push(...await gmFetchAudioDir(dir,depth-1,seen));
}
}
return files;
}

async function loadGMAudioFiles(force){
if(!isAdmin())return;
if(gmAudioFiles.loading)return;
if(gmAudioFiles.loaded&&!force)return;
gmAudioFiles.loading=true;
gmAudioFiles.error='';
if(force)render();
try{
const items=await gmFetchAudioDir('/sdcard',3,new Set());
const dedup=new Map();
items.forEach(item=>{if(item&&item.path)dedup.set(item.path,item);});
gmAudioFiles.items=Array.from(dedup.values()).sort((a,b)=>String(a.path).localeCompare(String(b.path)));
gmAudioFiles.loaded=true;
}
catch(err){
gmAudioFiles.error=err.message||'Audio file scan failed';
gmAudioFiles.loaded=false;
}
finally{
gmAudioFiles.loading=false;
if(currentView==='scenarios')render();
}
}

function scheduleGMAudioFilesLoad(){
if(!isAdmin()||gmAudioFiles.loaded||gmAudioFiles.loading||gmAudioFiles.scheduled)return;
gmAudioFiles.scheduled=true;
setTimeout(()=>{
gmAudioFiles.scheduled=false;
loadGMAudioFiles(false);
}
,0);
}

window.__gmRefreshManualSidebar=async function(){
renderRightSidebar(true);
};

async function loadRoomScenarios(force){
if(!force&&gmStaticFresh('roomScenarios'))return;
gmRoomScenarios={
}
;
const rooms=(gmState&&Array.isArray(gmState.rooms))?gmState.rooms:[];
await Promise.all(rooms.map(async r=>loadRoomScenariosForRoom(r.room_id,true)));
gmMarkStaticLoaded('roomScenarios');
}

function normalizeRoomScenarioSelection(roomId){
if(!roomId)return;
const scenarios=roomScenarios(roomId);
const override=currentRoomScenarioId[roomId]||'';
if(override&&scenarios.some(scenario=>(scenario&&scenario.id||'')===override))return;
delete currentRoomScenarioId[roomId];
}

async function loadRoomScenariosForRoom(roomId,force){
if(!roomId)return;
if(!force&&gmStaticFresh('roomScenarios')&&gmRoomScenarios[roomId])return;
try{
const res=await api.room.scenarios(roomId);
const data=res.ok?await res.json():null;
gmRoomScenarios[roomId]=(data&&Array.isArray(data.scenarios))?data.scenarios:[];
}
catch(err){
gmRoomScenarios[roomId]=[];
}
normalizeRoomScenarioSelection(roomId);
}

function roomIdForScenario(scenarioId){
const target=String(scenarioId||'');
if(!target)return '';
const roomIds=Object.keys(gmRoomScenarios||{});
for(const roomId of roomIds){
if(roomScenarios(roomId).some(scenario=>(scenario&&scenario.id||'')===target)){
return roomId;
}
}
return '';
}

async function refreshRoomScenariosAfterMutation(roomId){
if(!roomId){
await loadRoomScenarios(true);
if(isAdmin())await loadScenarioEditorCatalogs(true);
render();
return;
}
await loadRoomScenariosForRoom(roomId,true);
if(roomById(roomId)){
await loadGMRuntimeOnly(roomId,false);
return;
}
render();
}

async function loadRoomProfiles(force){
if(!force&&gmStaticFresh('roomProfiles'))return;
gmRoomProfiles={
}
;
const rooms=(gmState&&Array.isArray(gmState.rooms))?gmState.rooms:[];
await Promise.all(rooms.map(async r=>loadRoomProfilesForRoom(r.room_id,true)));
gmMarkStaticLoaded('roomProfiles');
}

function normalizeRoomProfilesSelection(roomId){
if(!roomId)return;
const data=gmRoomProfiles&&gmRoomProfiles[roomId]?gmRoomProfiles[roomId]:null;
const profiles=data&&Array.isArray(data.profiles)?data.profiles:[];
const selectedId=data&&data.selected_profile_id?data.selected_profile_id:'';
if(selectedId){
currentRoomProfileId[roomId]=selectedId;
return;
}
const override=currentRoomProfileId[roomId]||'';
if(override&&profiles.some(profile=>(profile&&profile.id||'')===override))return;
delete currentRoomProfileId[roomId];
}

async function loadRoomProfilesForRoom(roomId,force){
if(!roomId)return;
if(!force&&gmStaticFresh('roomProfiles')&&gmRoomProfiles[roomId])return;
try{
const res=await api.room.profiles(roomId);
const data=res.ok?await res.json():null;
gmRoomProfiles[roomId]=data&&Array.isArray(data.profiles)?data:{
profiles:[],selected_profile_id:''}
;
}
catch(err){
gmRoomProfiles[roomId]={
profiles:[],selected_profile_id:''}
;
}
normalizeRoomProfilesSelection(roomId);
}

function roomIdForProfile(profileId){
const target=String(profileId||'');
if(!target)return '';
const roomIds=Object.keys(gmRoomProfiles||{});
for(const roomId of roomIds){
if(roomProfiles(roomId).some(profile=>(profile&&profile.id||'')===target)){
return roomId;
}
}
return '';
}

async function refreshRoomProfilesAfterMutation(roomId){
if(!roomId){
await loadRoomProfiles(true);
render();
return;
}
await loadRoomProfilesForRoom(roomId,true);
if(roomById(roomId)){
await loadGMRuntimeOnly(roomId,false);
return;
}
render();
}

async function loadScenarioEditorCatalogs(force){
if(!force&&gmStaticFresh('scenarioCatalogs'))return;
gmScenarioEditorCatalogs={
}
;
if(!isAdmin()){
gmMarkStaticLoaded('scenarioCatalogs');
return;
}
const rooms=(gmState&&Array.isArray(gmState.rooms))?gmState.rooms:[];
await Promise.all(rooms.map(async r=>{
try{
const res=await api.room.scenarioEditorCatalog(r.room_id);const data=res.ok?await res.json():null;gmScenarioEditorCatalogs[r.room_id]=data&&Array.isArray(data.quest_devices)?data:{
quest_devices:[],step_schemas:[]}
;}
catch(err){
gmScenarioEditorCatalogs[r.room_id]={
quest_devices:[],step_schemas:[]}
;}
}
));
gmMarkStaticLoaded('scenarioCatalogs');
}

async function refreshQuestDevicesAfterMutation(){
if(!gmState){
await loadGMFullSnapshot(true,true);
return;
}
if(isAdmin()){
await Promise.all([loadQuestDevices(true),loadScenarioEditorCatalogs(true)]);
}
else{
await loadQuestDevices(true);
}
render();
}

async function loadGMLightStaticData(force){
await Promise.all([loadObserved(force),loadQuestDevices(force)]);
}

async function loadGMViewData(force){
if(currentView==='audit')await loadAudit(force);
else if(currentView==='timeline')await loadTimeline(force);
else if(currentView==='scenarios')await Promise.all([loadRoomScenarios(force),loadQuestDevices(force),loadScenarioEditorCatalogs(force)]);
else if(currentView==='profiles')await Promise.all([loadRoomProfiles(force),loadRoomScenarios(force)]);
else if(currentView==='room')await Promise.all([loadRoomProfiles(force),loadRoomScenarios(force),loadQuestDevices(force)]);
else if(currentView==='device_setup'||currentView==='devices'||currentView==='observed')await Promise.all([loadObserved(force),loadQuestDevices(force)]);
else if(currentView==='dashboard')await Promise.all([loadObserved(force),loadQuestDevices(force)]);
}

async function loadGMStaticData(force){
await loadGMLightStaticData(force);
await loadGMViewData(force);
}

function mergeGMSystemSummary(data){
if(!data||!data.summary)return false;
if(!gmState||typeof gmState!=='object'){
gmState={ok:true,rooms:[],devices:[],issues:[]};
}
gmState.ok=data.ok!==false;
if(Object.prototype.hasOwnProperty.call(data,'generation'))gmState.generation=data.generation;
gmState.summary=data.summary;
return true;
}

async function loadGMSystemSummaryOnly(forceRender){
if(!gmState){
await loadGMFullSnapshot(true,true);
return;
}
const data=await api.gm.systemSummaryJson();
if(!mergeGMSystemSummary(data)){
syncGMSummaryStatus();
if(forceRender)render();
else renderRightSidebar(true);
return;
}
syncGMSummaryStatus();
if(forceRender){
render();
return;
}
if(shouldDeferAutoRender()){
gmAutoRenderDeferred=true;
renderRightSidebar(true);
return;
}
renderRightSidebar(true);
}

async function loadGMFullSnapshot(silent,forceRender,opts){
opts=opts||{};
const requestSeq=++gmSnapshotRequestSeq;
if(!silent){
setStatus('loading','state-unknown');
}
try{
const data=await api.gm.stateJson();
if(requestSeq!==gmSnapshotRequestSeq)return;
gmState=data;
syncRoomTimerBaselines();
loadGMVersions().then(v=>{gmLastVersions=v;}).catch(()=>{});
applyInitialOperatorRoute();
const shouldRenderBeforeStatic=currentView==='dashboard'||currentView==='rooms';
const staticLoadPromise=loadGMStaticData(!silent||!!forceRender||!!opts.forceStatic);
if(shouldRenderBeforeStatic){
if(silent&&!forceRender&&shouldDeferAutoRender()){
gmAutoRenderDeferred=true;
renderRightSidebar(false);
}
else{
gmAutoRenderDeferred=false;
render();
}
}
await staticLoadPromise;
if(requestSeq!==gmSnapshotRequestSeq)return;
if(silent&&!forceRender&&shouldDeferAutoRender()){
gmAutoRenderDeferred=true;
renderRightSidebar(false);
return;
}
gmAutoRenderDeferred=false;
render();
}
catch(err){
setStatus('load failed','state-fault');

document.getElementById('gm_content').innerHTML='<div class="card empty">Failed to load GM state</div>';
renderRightSidebar(false);
}
}

function syncRoomTimerBaselines(){
const now=performance.now();
(gmState&&Array.isArray(gmState.rooms)?gmState.rooms:[]).forEach(room=>{
room._timer_synced_at_ms=now;
});
}

const ROOM_RUNTIME_FIELDS=[
'runtime_schema_version',
'session_present','session_state','timer_state','timer_duration_ms','timer_remaining_ms',
'hint_active','hint_sent_count','hint_message',
'selected_profile_id','selected_profile_name','selected_profile_scenario_id',
'selected_scenario_id','selected_scenario_name',
'running_scenario_id','running_scenario_name','running_scenario_generation',
'scenario_runtime_state','scenario_total_steps','scenario_done_steps','scenario_current_step_text',
'scenario_wait_type','scenario_wait_until_ms','scenario_wait_started_at_ms',
'scenario_wait_summary',
'scenario_wait_events',
'scenario_wait_flags',
'scenario_wait_operator_prompt','scenario_wait_operator_label',
'scenario_wait_operator_skip_allowed','scenario_wait_operator_skip_label',
'scenario_operator_message',
'scenario_device_ids','scenario_device_count',
'scenario_flags',
'scenario_branches',
'scenario_last_error',
'asset_prepare_state','asset_audio_total','asset_audio_ready',
'asset_audio_missing','asset_audio_bad','asset_audio_unsupported',
'asset_audio_io_error','asset_audio_unknown'
];

const ROOM_RUNTIME_CLOCK_FIELDS=new Set([
'timer_remaining_ms',
'scenario_wait_until_ms',
'scenario_wait_started_at_ms'
]);

function mergeRoomRuntimeState(roomId,data){
if(!gmState||!Array.isArray(gmState.rooms)||!roomId||!data)return false;
const room=gmState.rooms.find(r=>(r.room_id||'')===roomId);
if(!room)return false;
ROOM_RUNTIME_FIELDS.forEach(key=>{
if(Object.prototype.hasOwnProperty.call(data,key))room[key]=data[key];
});
room._timer_synced_at_ms=performance.now();
return true;
}

async function loadGMRoomsRuntimeOnly(roomIds,forceRender){
const rooms=gmState&&Array.isArray(gmState.rooms)?gmState.rooms:[];
if(!gmState||!rooms.length){
await loadGMFullSnapshot(true,true);
return;
}
const ids=Array.from(new Set((Array.isArray(roomIds)&&roomIds.length?roomIds:rooms.map(room=>room&&room.room_id)).filter(roomId=>roomId&&roomById(roomId))));
if(!ids.length){
await loadGMSystemSummaryOnly(forceRender);
return;
}
const results=await Promise.all(ids.map(async roomId=>{
const requestSeq=(gmRuntimeRequestSeq[roomId]||0)+1;
gmRuntimeRequestSeq[roomId]=requestSeq;
const data=await api.room.runtimeJson(roomId,'summary');
return {roomId,requestSeq,data};
}));
let merged=false;
results.forEach(result=>{
if(gmRuntimeRequestSeq[result.roomId]!==result.requestSeq)return;
if(mergeRoomRuntimeState(result.roomId,result.data))merged=true;
});
if(!merged){
await loadGMSystemSummaryOnly(forceRender);
return;
}
syncGMSummaryStatus();
if(forceRender){
render();
return;
}
if(shouldDeferAutoRender()){
gmAutoRenderDeferred=true;
renderRightSidebar(true);
return;
}
render();
}

function updateVisibleRoomClocks(){
const rooms=gmState&&Array.isArray(gmState.rooms)?gmState.rooms:[];
if(!rooms.length)return;
document.querySelectorAll('[data-room-clock]').forEach(el=>{
const roomId=el.dataset.roomClock||'';
const room=rooms.find(item=>(item.room_id||'')===roomId);
if(!room)return;
const text=fmtClock(roomTimerDisplayMs(room));
if(el.textContent!==text)el.textContent=text;
});
}

function roomRuntimeRenderKey(room){
const key={};
ROOM_RUNTIME_FIELDS.forEach(field=>{
if(ROOM_RUNTIME_CLOCK_FIELDS.has(field))return;
key[field]=room[field]===undefined?null:room[field];
});
return JSON.stringify(key);
}

function renderRoomRuntimePanel(roomId){
if(currentView!=='room'||currentRoomId!==roomId||roomTab!=='control')return false;
const room=roomById(roomId);
if(!room)return false;
const panels=Array.from(document.querySelectorAll('[data-room-control-runtime]'));
const panel=panels.find(el=>(el.dataset.roomControlRuntime||'')===roomId);
if(!panel)return false;
const key=roomRuntimeRenderKey(room);
if(gmRuntimeRenderKeys[roomId]===key)return true;
gmRuntimeRenderKeys[roomId]=key;
panel.innerHTML=`${renderRoomOperatorConsole(room)}${isAdmin()?renderRoomScenarioControl(room):''}`;
return true;
}

async function loadGMRuntimeOnly(roomId,forceFullRender){
if(!roomId){
await loadGMSystemSummaryOnly(false);
return;
}
if(!gmState){
await loadGMFullSnapshot(true,true);
return;
}
if(!Array.isArray(gmState.rooms)){
await loadGMFullSnapshot(true,true);
return;
}
const requestSeq=(gmRuntimeRequestSeq[roomId]||0)+1;
gmRuntimeRequestSeq[roomId]=requestSeq;
const data=await api.room.runtimeJson(roomId);
if(gmRuntimeRequestSeq[roomId]!==requestSeq)return;
if(!mergeRoomRuntimeState(roomId,data)){
await loadGMSystemSummaryOnly(false);
return;
}
if(!forceFullRender&&renderRoomRuntimePanel(roomId))return;
render();
}

let gmRuntimePollBusy=false;
let gmStatePollBusy=false;

async function pollActiveRoomRuntime(){
if(gmRuntimePollBusy)return;
if(currentView!=='room'||roomTab!=='control'||!currentRoomId||!gmState)return;
gmRuntimePollBusy=true;
try{
await loadGMRuntimeOnly(currentRoomId,false);
}
catch(err){
setGMStatus('Runtime refresh failed','gm-bad');
}
finally{
gmRuntimePollBusy=false;
}
}

async function pollGMStateSnapshot(){
if(gmStatePollBusy)return;
gmStatePollBusy=true;
try{
const versions=await loadGMVersions();
await refreshGMByVersions(versions);
}
finally{
gmStatePollBusy=false;
}
}

async function refreshGMByVersions(versions){
if(!versions)return;
const prev=gmLastVersions;
if(!prev){
gmLastVersions=versions;
return;
}
if(gmVersionsKey(versions)===gmVersionsKey(prev))return;
gmLastVersions=versions;
if(gmVersionChanged(prev,versions,['rooms'])){
await loadGMFullSnapshot(true,true,{forceStatic:true});
return;
}
let shouldRender=false;
let shouldPatchSidebar=false;
if(gmVersionChanged(prev,versions,['devices','ingest'])){
const deviceRefreshes=[loadObserved(true),loadQuestDevices(true)];
if(currentView==='scenarios'&&isAdmin())deviceRefreshes.push(loadScenarioEditorCatalogs(true));
await Promise.all(deviceRefreshes);
shouldPatchSidebar=true;
shouldRender=shouldRender||gmCurrentViewUsesQuestDeviceStatic();
if(currentView==='scenarios'&&isAdmin())shouldRender=true;
}
if(gmVersionChanged(prev,versions,['scenarios'])){
await loadRoomScenarios(true);
shouldRender=shouldRender||gmCurrentViewUsesScenarioStatic();
}
if(gmVersionChanged(prev,versions,['profiles'])){
await loadRoomProfiles(true);
shouldRender=shouldRender||gmCurrentViewUsesProfileStatic();
}
if(gmVersionChanged(prev,versions,['session','runtime'])){
if(currentView==='room'&&roomTab==='control'&&currentRoomId){
await loadGMRuntimeOnly(currentRoomId,false);
}
else if(currentView==='dashboard'||currentView==='rooms'){
await loadGMRoomsRuntimeOnly([],false);
return;
}
else if(!shouldRender){
await loadGMSystemSummaryOnly(false);
return;
}
}
if(shouldRender){
if(shouldDeferAutoRender()){
gmAutoRenderDeferred=true;
renderRightSidebar(shouldPatchSidebar);
}
else{
render();
}
}
else if(shouldPatchSidebar){
renderRightSidebar(true);
}
}

async function refreshGMByInvalidationSlices(items){
const values=Array.isArray(items)?items.filter(Boolean):[];
const slices=values.map(item=>typeof item==='string'?item:String(item.slice||'')).filter(Boolean);
const roomScenarioTargets=Array.from(new Set(values.map(item=>item&&item.slice==='room.scenarios'?(item.target_id||''):'').filter(Boolean)));
const roomProfileTargets=Array.from(new Set(values.map(item=>item&&item.slice==='room.profiles'?(item.target_id||''):'').filter(Boolean)));
const roomRuntimeTargets=Array.from(new Set(values.map(item=>item&&item.slice==='room.runtime'?(item.target_id||''):'').filter(Boolean)));
if(!slices.length)return;
if(slices.includes('full.snapshot')||slices.includes('room.catalog')){
await loadGMFullSnapshot(true,true,{forceStatic:true});
return;
}
const needsDeviceCatalog=slices.includes('devices.catalog');
const needsDeviceRuntime=slices.includes('devices.runtime');
const needsScenarioCatalog=slices.includes('room.scenarios');
const needsProfileCatalog=slices.includes('room.profiles');
const needsRoomRuntime=slices.includes('room.runtime');
const needsSystemSummary=slices.includes('system.summary');
let shouldRender=false;
let shouldPatchSidebar=false;
const localRuntimeRefreshUntil=(currentRoomId&&gmLocalRuntimeRefreshUntil[currentRoomId])||0;
const localRuntimeRefreshActive=
currentView==='room'&&roomTab==='control'&&currentRoomId&&Date.now()<localRuntimeRefreshUntil;

if(needsDeviceCatalog){
if(isAdmin()){
await Promise.all([loadQuestDevices(true),loadScenarioEditorCatalogs(true)]);
}
else{
await loadQuestDevices(true);
}
shouldPatchSidebar=true;
shouldRender=shouldRender||gmCurrentViewUsesQuestDeviceStatic();
}
if(needsDeviceRuntime){
await loadObserved(true);
shouldPatchSidebar=true;
shouldRender=shouldRender||gmCurrentViewUsesQuestDeviceStatic();
}
if(needsScenarioCatalog){
if(roomScenarioTargets.length){
await Promise.all(roomScenarioTargets.map(roomId=>loadRoomScenariosForRoom(roomId,true)));
}
else{
await loadRoomScenarios(true);
}
shouldRender=shouldRender||gmCurrentViewUsesScenarioStatic();
}
if(needsProfileCatalog){
if(roomProfileTargets.length){
await Promise.all(roomProfileTargets.map(roomId=>loadRoomProfilesForRoom(roomId,true)));
}
else{
await loadRoomProfiles(true);
}
shouldRender=shouldRender||gmCurrentViewUsesProfileStatic();
}
if(needsRoomRuntime||needsSystemSummary){
const localRuntimeTargetMatches=!roomRuntimeTargets.length||
(roomRuntimeTargets.length===1&&roomRuntimeTargets[0]===currentRoomId);
if(localRuntimeRefreshActive&&localRuntimeTargetMatches){
return;
}
if(roomRuntimeTargets.length===1&&roomRuntimeTargets[0]&&roomById(roomRuntimeTargets[0])){
await loadGMRuntimeOnly(roomRuntimeTargets[0],false);
}
else if(currentView==='room'&&roomTab==='control'&&currentRoomId){
await loadGMRuntimeOnly(currentRoomId,false);
}
else if(needsRoomRuntime&&(currentView==='dashboard'||currentView==='rooms')){
await loadGMRoomsRuntimeOnly(roomRuntimeTargets,false);
}
else{
await loadGMSystemSummaryOnly(false);
}
return;
}
if(shouldRender){
if(shouldDeferAutoRender()){
gmAutoRenderDeferred=true;
renderRightSidebar(shouldPatchSidebar);
}
else{
render();
}
}
else if(shouldPatchSidebar){
renderRightSidebar(true);
}
}

async function refreshAfterRuntimeAction(roomId,forceFullRender){
clearTransientFieldDirty();
await loadGMRuntimeOnly(roomId,forceFullRender);
if(roomId)gmLocalRuntimeRefreshUntil[roomId]=Date.now()+400;
}

async function runManualDeviceCommand(deviceId,commandId){
if(!deviceId||!commandId)throw new Error('Manual button is incomplete');
setGMStatus('Triggering button...');
const command=scenarioCommandById(deviceId,commandId);
const body={device_id:deviceId,command_id:commandId};
if(command&&command.default_args&&typeof command.default_args==='object'){
body.params=command.default_args;
}
const res=await api.device.runCommand(body.device_id,body.command_id,body.params);
await gmExpectOk(res);
setGMStatus('Button sent','gm-ok');
}

async function createRoomFromPrompt(){
if(!isAdmin())return;
const name=(prompt('Room name')||'').trim();
if(!name)return;
const roomId=slugifyId(name,'room');
setGMStatus('Saving room...');
const res=await api.room.save({room_id:roomId,name});
await gmExpectOk(res);
clearTransientFieldDirty();
await loadGMFullSnapshot(true,true);
if(typeof window.__gmRefreshManualSidebar==='function'){
await window.__gmRefreshManualSidebar();
}
setGMStatus('Room saved','gm-ok');
}

async function deleteRoom(roomId,confirmHandled){
if(!isAdmin())return;
if(!roomId)throw new Error('Room is not selected');
const room=roomById(roomId);
const name=room&&(room.title||room.name)||roomId;
if(!confirmHandled&&!confirm(`Delete room ${name}? This also removes profiles and scenarios for this room. Quest devices stay untouched.`))return;
setGMStatus('Deleting room...');
const res=await api.room.delete({room_id:roomId,delete_content:true});
await gmExpectOk(res);
currentRoomId='';
delete currentRoomProfileId[roomId];
delete currentRoomScenarioId[roomId];
roomTab='control';
clearTransientFieldDirty();
await loadGMFullSnapshot(true,true);
currentView='rooms';
render();
setGMStatus('Room deleted','gm-ok');
}

async function runRoomTimer(action,roomId){
let res=null;
setGMStatus('Updating timer...');
if(action==='start'){
const input=document.getElementById('gm_timer_minutes');
const minutes=Number(input&&input.value);
if(!Number.isFinite(minutes)||minutes<=0)throw new Error('Duration must be greater than 0');
const durationMs=Math.round(minutes*60000);
res=await api.room.timerStart(roomId,durationMs);
}
else if(action==='pause'){
res=await api.room.timer(roomId,'pause');
}
else if(action==='resume'){
res=await api.room.timer(roomId,'resume');
}
else if(action==='reset'){
res=await api.room.timer(roomId,'reset');
}
else if(action==='finish'){
res=await api.room.sessionFinish(roomId);
}
else if(action==='plus1'){
res=await api.room.timerAdd(roomId,60000);
}
else if(action==='minus1'){
res=await api.room.timerAdd(roomId,-60000);
}
else{
throw new Error('Unsupported timer action');
}
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
clearTransientFieldDirty();
await refreshAfterRuntimeAction(roomId,false);
setGMStatus('Timer updated','gm-ok');
}

async function runRoomHint(action,roomId){
if(action==='send'){
const input=document.getElementById('gm_hint_input');
const message=(input&&input.value||'').trim();
if(!message)throw new Error('Hint message is empty');
setGMStatus('Sending hint...');
const res=await api.room.hintSend({room_id:roomId,message});
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
}
else if(action==='clear'){
setGMStatus('Clearing hint...');
const res=await api.room.hintClear(roomId);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
}
else{
throw new Error('Unsupported hint action');
}
clearTransientFieldDirty();
await refreshAfterRuntimeAction(roomId,true);
setGMStatus('Hint updated','gm-ok');
}

async function selectRoomProfile(roomId,profileId){
if(!roomId||!profileId)throw new Error('Game mode selection is incomplete');
setGMStatus('Selecting game mode...');
const res=await api.room.profileSelect({room_id:roomId,profile_id:profileId});
await gmExpectOk(res);
currentRoomProfileId[roomId]=profileId;
clearTransientFieldDirty();
await refreshAfterRuntimeAction(roomId,false);
setGMStatus('Game mode selected','gm-ok');
}

async function runRoomGame(action,roomId,confirmHandled){
if(!roomId||!action)throw new Error('Game command is incomplete');
if(!confirmHandled&&action==='stop'&&!confirm('Stop this game session?'))return;
if(!confirmHandled&&action==='reset'&&!confirm('Reset this game session?'))return;
setGMStatus('Updating game...');
const res=await api.room.game(roomId,action);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
clearTransientFieldDirty();
await refreshAfterRuntimeAction(roomId,false);
setGMStatus('Game updated','gm-ok');
}
// GM panel source part. Edit this file, then rebuild gm_panel.js.
function collectQuestDeviceEditor(strict){
const root=document.querySelector('[data-quest-device-editor]');
const base=questDeviceEditor.draft?JSON.parse(JSON.stringify(questDeviceEditor.draft)):currentQuestDeviceDraft();
if(!root)return base;
const field=name=>root.querySelector(`[data-quest-device-field="${name}"]`);
const name=(field('name')&&field('name').value||'').trim();
const rawId=(field('id')&&field('id').value||'').trim();
const id=rawId||(questDeviceEditor.device_id?base.id:'')||slugifyId(name,'device');
const clientId=(field('client_id')&&field('client_id').value||'').trim();
const enabled=!!(field('enabled')&&field('enabled').checked);
const commands=[];
const baseCommands=Array.isArray(base.commands)?base.commands:[];
root.querySelectorAll('[data-quest-command]').forEach(row=>{
const get=k=>row.querySelector(`[data-quest-command-field="${k}"]`);
const label=(get('label')&&get('label').value||'').trim();
const rawCmdId=(get('id')&&get('id').value||'').trim();
const cmdId=rawCmdId||slugifyId(label,'command');
const command=(get('command')&&get('command').value||'').trim();
const capability=(get('capability')&&get('capability').value||'').trim()||(command.split('.')[0]||'');
const defaultArgsText=(get('default_args')&&get('default_args').value||'').trim();
const defaultArgs=defaultArgsText?JSON.parse(defaultArgsText):undefined;
const timeoutMs=Math.max(1,Number(get('timeout_ms')&&get('timeout_ms').value||3000)||3000);
const manualAllowed=!!(get('manual_allowed')&&get('manual_allowed').checked);
const scenarioAllowed=!!(get('scenario_allowed')&&get('scenario_allowed').checked);
const requiresConfirmation=!!(get('requires_confirmation')&&get('requires_confirmation').checked);
const resultRequired=!!(get('result_required')&&get('result_required').checked);
const dangerLevel=(get('danger_level')&&get('danger_level').value||'normal').trim()||'normal';
const existing=baseCommands.find(c=>(c.id||'')===(rawCmdId||cmdId))||baseCommands[Number(row.dataset.questCommand)]||{};
const argsSchema=Array.isArray(existing.args_schema)?existing.args_schema:[];
if(label||rawCmdId||command){
const policy={manual_allowed:manualAllowed,scenario_allowed:scenarioAllowed,requires_confirmation:requiresConfirmation,result_required:resultRequired,timeout_ms:timeoutMs,danger_level:dangerLevel};
const cmd={id:cmdId,label:label||cmdId,capability,command,policy,args_schema:argsSchema};
if(defaultArgs)cmd.default_args=defaultArgs;
commands.push(cmd);
}
});
const events=[];
root.querySelectorAll('[data-quest-event]').forEach(row=>{
const get=k=>row.querySelector(`[data-quest-event-field="${k}"]`);
const label=(get('label')&&get('label').value||'').trim();
const rawEventId=(get('id')&&get('id').value||'').trim();
const eventId=rawEventId||slugifyId(label,'event');
const eventName=(get('event')&&get('event').value||'').trim();
const capability=(get('capability')&&get('capability').value||'').trim()||(eventName.split('.')[0]||'');
const matchText=(get('match')&&get('match').value||'').trim();
const match=matchText?JSON.parse(matchText):undefined;
if(label||rawEventId||eventName){
const event={id:eventId,label:label||eventId,capability,event:eventName};
if(match)event.match=match;
events.push(event);
}
});
if(strict&&(!name||!clientId))throw new Error('Fill device name and physical client ID');
return {id,name,client_id:clientId,enabled,commands,events};
}

function normalizeQuestParamSchema(items){
return (Array.isArray(items)?items:[]).map(item=>{
const key=String(item&&item.key||'').trim();
const label=String(item&&item.label||key).trim();
const type=String(item&&item.type||'text').trim()||'text';
if(!key)return null;
return {key,label,type,optional:!!(item&&item.optional)};
}).filter(Boolean);
}

function normalizeDiscoveredCommand(item,index){
const label=String(item&&item.label||item&&item.name||item&&item.id||`Command ${index+1}`).trim();
const id=String(item&&item.id||slugifyId(label,'command')).trim();
const command=String(item&&item.command||'').trim();
const policy=item&&item.policy&&typeof item.policy==='object'?item.policy:{};
return {
id,
label:label||id,
capability:String(item&&item.capability||command.split('.')[0]||'').trim(),
command,
default_args:item&&item.default_args&&typeof item.default_args==='object'?item.default_args:undefined,
policy:{
manual_allowed:policy.manual_allowed===false?false:true,
scenario_allowed:policy.scenario_allowed===false?false:true,
requires_confirmation:!!policy.requires_confirmation,
result_required:policy.result_required===false?false:true,
timeout_ms:Number(policy.timeout_ms)||3000,
danger_level:String(policy.danger_level||'normal')
},
args_schema:normalizeQuestParamSchema(item&&item.args_schema)
};
}

function normalizeDiscoveredEvent(item,index){
const label=String(item&&item.label||item&&item.name||item&&item.id||`Event ${index+1}`).trim();
const id=String(item&&item.id||slugifyId(label,'event')).trim();
const eventName=String(item&&item.event||'').trim()||id;
return {
id,
label:label||id,
capability:String(item&&item.capability||eventName.split('.')[0]||'').trim(),
event:eventName,
match:item&&item.match&&typeof item.match==='object'?item.match:undefined
};
}

function questDeviceFromDiscoveredInterface(clientId,iface){
const base=collectQuestDeviceEditor(false);
const name=(base.name||iface&&iface.name||iface&&iface.label||clientId||'Quest device').trim();
const id=(base.id||questDeviceEditor.device_id||slugifyId(name,'device')).trim();
const commands=(Array.isArray(iface&&iface.commands)?iface.commands:[]).map(normalizeDiscoveredCommand).filter(c=>c.id&&c.command);
const events=(Array.isArray(iface&&iface.events)?iface.events:[]).map(normalizeDiscoveredEvent).filter(ev=>ev.id&&ev.event);
return {
id,
client_id:clientId,
name,
enabled:base.enabled!==false,
commands,
events
};
}

async function discoverQuestDeviceInterface(){
if(!isAdmin())throw new Error('Admin role required');
const current=collectQuestDeviceEditor(false);
const clientId=(current.client_id||'').trim();
if(!clientId)throw new Error('Select physical client');
setGMStatus('Requesting device config...');
const res=await api.device.describeInterface(clientId);
let body=null;
try{body=await res.json();}catch(err){}
if(!res.ok){
const msg=body&&(body.message||body.error||body.code);
throw new Error(msg||('HTTP '+res.status));
}
const iface=body&&body.device_description;
if(!iface||typeof iface!=='object')throw new Error('Device returned no device_description');
const device=questDeviceFromDiscoveredInterface(clientId,iface);
questDeviceEditor.draft=current;
questDeviceEditor.discovery={client_id:clientId,device_description:iface,device};
setGMStatus('Config received','gm-ok');
render();
}

function applyQuestDeviceDiscovery(){
const discovery=questDeviceEditor.discovery;
if(!discovery||!discovery.device)return;
if(!confirm('Import discovered commands and events into this device?'))return;
questDeviceEditor.draft=JSON.parse(JSON.stringify(discovery.device));
questDeviceEditor.discovery=null;
questDeviceEditor.dirty=true;
clearTransientFieldDirty();
render();
}

function discardQuestDeviceDiscovery(){
questDeviceEditor.discovery=null;
render();
}

function setQuestDeviceDraftFromEditor(){
questDeviceEditor.draft=collectQuestDeviceEditor(false);
}

function addQuestDeviceCommand(){
setQuestDeviceDraftFromEditor();
questDeviceEditor.draft.commands=Array.isArray(questDeviceEditor.draft.commands)?questDeviceEditor.draft.commands:[];
questDeviceEditor.draft.commands.push({id:'',label:'',capability:'',command:'',policy:{manual_allowed:true,scenario_allowed:true,requires_confirmation:false,result_required:true,timeout_ms:3000,danger_level:'normal'},args_schema:[]});
markQuestDeviceDirty();
render();
}

function addQuestDeviceEvent(){
setQuestDeviceDraftFromEditor();
questDeviceEditor.draft.events=Array.isArray(questDeviceEditor.draft.events)?questDeviceEditor.draft.events:[];
questDeviceEditor.draft.events.push({id:'',label:'',capability:'',event:''});
markQuestDeviceDirty();
render();
}

function deleteQuestDeviceCommand(index){
setQuestDeviceDraftFromEditor();
questDeviceEditor.draft.commands.splice(index,1);
markQuestDeviceDirty();
render();
}

function deleteQuestDeviceEvent(index){
setQuestDeviceDraftFromEditor();
questDeviceEditor.draft.events.splice(index,1);
markQuestDeviceDirty();
render();
}

async function saveQuestDeviceEditor(){
if(!isAdmin())throw new Error('Admin role required');
const device=collectQuestDeviceEditor(true);
if(!device.commands.length&&!device.events.length)throw new Error('Add at least one command or event');
setGMStatus('Saving device...');
const res=await api.device.save(device);
await gmExpectOk(res);
questDeviceEditor.device_id=device.id;
questDeviceEditor.open=true;
clearQuestDeviceDirty();
await refreshQuestDevicesAfterMutation();
setGMStatus('Device saved','gm-ok');
}

async function deleteQuestDeviceEditor(deviceId,confirmHandled){
if(!isAdmin())throw new Error('Admin role required');
if(!deviceId)return;
if(!confirmHandled&&!confirm(`Delete device ${deviceId}?`))return;
setGMStatus('Deleting device...');
const res=await api.device.delete(deviceId);
await gmExpectOk(res);
if(questDeviceEditor.device_id===deviceId){
questDeviceEditor.device_id='';
questDeviceEditor.open=false;
}
clearQuestDeviceDirty();
await refreshQuestDevicesAfterMutation();
setGMStatus('Device deleted','gm-ok');
}

async function saveProfileEditor(){
if(!isAdmin())throw new Error('Admin role required');
const name=(document.getElementById('profile_name')&&document.getElementById('profile_name').value||'').trim();
const rawId=(document.getElementById('profile_id')&&document.getElementById('profile_id').value||'').trim();
const id=rawId||slugifyId(name,'mode');
const scenarioId=(document.getElementById('profile_scenario')&&document.getElementById('profile_scenario').value||'').trim();
const minutes=Number(document.getElementById('profile_duration')&&document.getElementById('profile_duration').value);
const hintPack=(document.getElementById('profile_hint_pack')&&document.getElementById('profile_hint_pack').value||'').trim();
const audioPack=(document.getElementById('profile_audio_pack')&&document.getElementById('profile_audio_pack').value||'').trim();
const enabled=!!(document.getElementById('profile_enabled')&&document.getElementById('profile_enabled').checked);
if(!name||!scenarioId||!Number.isFinite(minutes)||minutes<=0)throw new Error('Fill mode name, scenario and duration');
const profile={
id,name,room_id:profileEditor.room_id,scenario_id:scenarioId,duration_ms:Math.round(minutes*60000),hint_pack_id:hintPack,audio_pack_id:audioPack,enabled}
;
const roomId=profile.room_id;
setGMStatus('Saving game mode...');
const res=await api.room.profileSave({profile});
await gmExpectOk(res);
profileEditor.profile_id=id;
profileEditor.open=true;
clearProfileDirty();
await refreshRoomProfilesAfterMutation(roomId);
setGMStatus('Game mode saved','gm-ok');
}

async function deleteProfileEditor(profileId,confirmHandled){
if(!isAdmin())throw new Error('Admin role required');
if(!profileId)return;
if(!confirmHandled&&!confirm(`Delete game mode ${profileId}?`))return;
const roomId=profileEditor.room_id||roomIdForProfile(profileId);
setGMStatus('Deleting game mode...');
const res=await api.room.profileDelete(profileId);
await gmExpectOk(res);
if(profileEditor.profile_id===profileId){
profileEditor.profile_id='';
profileEditor.open=false;
}
clearProfileDirty();
await refreshRoomProfilesAfterMutation(roomId);
setGMStatus('Game mode deleted','gm-ok');
}

function collectScenarioForSave(){
let scenario=null;
if(document.getElementById('scenario_id')){
scenario=collectScenarioEditor();
}
else{
const box=document.getElementById('scenario_json');
try{
scenario=JSON.parse(box&&box.value||'');
}
catch(err){
throw new Error('Scenario JSON is invalid');
}
}
if(scenario&&!scenario.id&&scenario.name)scenario.id=slugifyId(scenario.name,'scenario');
if(!scenario||!scenario.name)throw new Error('Scenario name is required');
scenario.room_id=scenarioEditor.room_id;
const existing=roomScenarios(scenario.room_id).find(item=>(item.id||'')===(scenario.id||''))||null;
if(existing&&Array.isArray(existing.branches)&&Array.isArray(scenario.branches)&&
existing.branches.length>scenario.branches.length&&
(!scenarioEditor.branch_count_shrink_allowed||(Number(scenarioEditor.branch_count_shrink_floor)||0)>scenario.branches.length)){
throw new Error(`Refusing to save incomplete scenario: editor has ${scenario.branches.length} branches, saved scenario has ${existing.branches.length}. Refresh and try again; use Delete on a branch when you really want to remove it.`);
}
if(!Array.isArray(scenario.branches)||!scenario.branches.length){
if(!Array.isArray(scenario.steps))scenario.steps=[];
}
else{
delete scenario.steps;
}
return scenario;
}

async function validateScenarioDraft(scenario,showStatus){
if(!isAdmin())throw new Error('Admin role required');
const draft=scenario||collectScenarioForSave();
const localReport=scenarioClientValidationReport(draft);
if(!localReport.valid){
scenarioEditor.validation_report=localReport;
scenarioEditor.draft=draft;
if(showStatus){
setGMStatus('Scenario has editor validation errors','state-fault');
render();
}
return localReport;
}
setGMStatus('Validating scenario...');
const res=await api.room.scenarioValidate(draft);
await gmExpectOk(res);
const report=await res.json();
scenarioEditor.validation_report=report;
scenarioEditor.draft=draft;
if(showStatus){
const errors=Number(report.error_count)||0;
const warnings=Number(report.warning_count)||0;
setGMStatus(errors?'Scenario validation failed':(warnings?'Scenario has warnings':'Scenario valid'),errors?'state-fault':'state-ok');
render();
}
return report;
}

async function validateScenarioEditor(){
const scenario=collectScenarioForSave();
await validateScenarioDraft(scenario,true);
}

async function saveScenarioEditor(){
if(!isAdmin())throw new Error('Admin role required');
const scenario=collectScenarioForSave();
const report=await validateScenarioDraft(scenario,false);
const errors=Number(report.error_count)||0;
const warnings=Number(report.warning_count)||0;
if(errors>0){
setGMStatus('Scenario has validation errors','state-fault');
render();
return;
}
if(warnings>0&&!confirm(`Save scenario with ${warnings} validation warning${warnings===1?'':'s'}?`)){
render();
return;
}
setGMStatus('Saving scenario...');
const res=await api.room.scenarioSave(scenario);
await gmExpectOk(res);
scenarioEditor.scenario_id=scenario.id;
scenarioEditor.open=true;
clearScenarioDirty();
await refreshRoomScenariosAfterMutation(scenario.room_id);
setGMStatus('Scenario saved','gm-ok');
}

async function deleteScenarioEditor(scenarioId,confirmHandled){
if(!isAdmin())throw new Error('Admin role required');
if(!scenarioId)return;
if(!confirmHandled&&!confirm(`Delete scenario ${scenarioId}?`))return;
const roomId=scenarioEditor.room_id||roomIdForScenario(scenarioId);
setGMStatus('Deleting scenario...');
const res=await api.room.scenarioDelete(scenarioId);
await gmExpectOk(res);
if(scenarioEditor.scenario_id===scenarioId){
scenarioEditor.scenario_id='';
scenarioEditor.open=false;
}
clearScenarioDirty();
await refreshRoomScenariosAfterMutation(roomId);
setGMStatus('Scenario deleted','gm-ok');
}

async function importStorageJson(inputId,url,label){
const input=document.getElementById(inputId);
if(!input||!input.files||!input.files[0])throw new Error('Select JSON file');
const text=await input.files[0].text();
JSON.parse(text);
setGMStatus(`Importing ${label}...`);
const res=await api.storage.importJson(url,text);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
clearTransientFieldDirty();
if(url==='/api/gm/devices/import'){
await refreshQuestDevicesAfterMutation();
}
else if(url==='/api/gm/room/scenarios/import'){
await loadRoomScenarios(true);
if(isAdmin())await loadScenarioEditorCatalogs(true);
render();
}
else if(url==='/api/gm/profiles/import'){
await loadRoomProfiles(true);
render();
}
else{
await loadGMFullSnapshot(true,true);
}
setGMStatus(`${label} imported`,'gm-ok');
}

async function postStorageCommand(url,label){
setGMStatus(`${label}...`);
const res=await api.storage.post(url);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
clearTransientFieldDirty();
if(url===api.storage.commandUrl('device','save')||
   url===api.storage.commandUrl('scenario','save')||
   url===api.storage.commandUrl('profile','save')){
setGMStatus(`${label} done`,'gm-ok');
return;
}
if(url===api.storage.commandUrl('device','load')){
await refreshQuestDevicesAfterMutation();
}
else if(url===api.storage.commandUrl('scenario','load')){
await loadRoomScenarios(true);
if(isAdmin())await loadScenarioEditorCatalogs(true);
render();
}
else if(url===api.storage.commandUrl('profile','load')){
await loadRoomProfiles(true);
render();
}
else{
await loadGMFullSnapshot(true,true);
}
setGMStatus(`${label} done`,'gm-ok');
}

async function runStorageAction(action){
if(!isAdmin())throw new Error('Admin role required');
if(action==='scenario_export'){
window.location=api.storage.exportUrl('scenario');
return;
}
if(action==='device_export'){
window.location=api.storage.exportUrl('device');
return;
}
if(action==='profile_export'){
window.location=api.storage.exportUrl('profile');
return;
}
if(action==='device_import')return importStorageJson('storage_devices_file','/api/gm/devices/import','Devices');
if(action==='scenario_import')return importStorageJson('storage_scenarios_file','/api/gm/room/scenarios/import','Scenarios');
if(action==='profile_import')return importStorageJson('storage_profiles_file','/api/gm/profiles/import','Game modes');
if(action==='device_save')return postStorageCommand(api.storage.commandUrl('device','save'),'Save devices');
if(action==='device_load')return postStorageCommand(api.storage.commandUrl('device','load'),'Load devices');
if(action==='scenario_save')return postStorageCommand(api.storage.commandUrl('scenario','save'),'Save scenarios');
if(action==='scenario_load')return postStorageCommand(api.storage.commandUrl('scenario','load'),'Load scenarios');
if(action==='profile_save')return postStorageCommand(api.storage.commandUrl('profile','save'),'Save game modes');
if(action==='profile_load')return postStorageCommand(api.storage.commandUrl('profile','load'),'Load game modes');
throw new Error('Unsupported storage action');
}

async function selectRoomScenario(roomId,scenarioId){
if(!roomId||!scenarioId)throw new Error('Scenario selection is incomplete');
setGMStatus('Selecting scenario...');
const res=await api.room.scenarioSelect({room_id:roomId,scenario_id:scenarioId});
await gmExpectOk(res);
currentRoomScenarioId[roomId]=scenarioId;
clearTransientFieldDirty();
await refreshAfterRuntimeAction(roomId,false);
setGMStatus('Scenario selected','gm-ok');
}

async function runRoomScenarioRuntime(action,roomId,branchId,confirmHandled){
if(!roomId||!action)throw new Error('Scenario command is incomplete');
if(!confirmHandled&&action==='next'&&!confirm(branchId?'Force complete this branch wait?':'Force complete current scenario wait?'))return;
setGMStatus('Updating scenario...');
const res=await api.room.scenarioRuntime(roomId,action,branchId);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
clearTransientFieldDirty();
await refreshAfterRuntimeAction(roomId,false);
setGMStatus('Scenario updated','gm-ok');
}
// GM panel source part. Edit this file, then rebuild gm_panel.js.
function gmScenarioChangeCommitDraft(draft,index){
scenarioEditor.draft=draft;
if(Number.isFinite(index))scenarioEditor.expanded_step=index;
scenarioEditor.dirty=true;
scenarioEditor.validation_report=null;
skipNextScenarioDomSync();
render();
}

function gmHandleScenarioBranchTypeChange(branchType){
const draft=collectScenarioEditor();
const branch=scenarioActiveBranch(draft);
if(branch){
branch.type=scenarioBranchTypeValue({type:branchType.value});
branch.required_for_completion=branch.type==='normal'&&branch.required_for_completion!==false;
if(branch.type==='reactive'){
branch.required_for_completion=false;
ensureReactiveV2Branch(branch);
}
}
gmScenarioChangeCommitDraft(draft);
return true;
}

function gmHandleReactiveV2Change(){
gmScenarioChangeCommitDraft(collectScenarioEditor());
return true;
}

function gmHandleScenarioStepDeviceChange(stepDevice){
const stepEl=stepDevice.closest('[data-scenario-step]');
const index=Number(stepEl&&stepEl.dataset.scenarioStep);
const draft=collectScenarioEditor();
const steps=scenarioActiveSteps(draft);
if(Number.isFinite(index)&&steps[index]){
const step=steps[index];
const type=scenarioStepTypeValue(step);
const device=scenarioDeviceById(stepDevice.value||'');
step.device_id=stepDevice.value||'';
if(type==='DEVICE_COMMAND'){
step.command_id=scenarioValidCommandId(device,'');
step.params=defaultParamsForCommand(device,scenarioCommandById(step.device_id,step.command_id));
}
else if(type==='WAIT_DEVICE_EVENT'){
step.event_id=scenarioValidEventId(device,'');
}
}
gmScenarioChangeCommitDraft(draft,index);
return true;
}

function gmHandleScenarioStepCommandOrEventChange(field){
refreshScenarioStepLabel(field.closest('[data-scenario-step]'));
markScenarioDirty();
render();
return true;
}

function gmHandleScenarioStepAudioChannelChange(stepParamChannel){
const stepEl=stepParamChannel.closest('[data-scenario-step]');
const index=Number(stepEl&&stepEl.dataset.scenarioStep);
const draft=collectScenarioEditor();
const steps=scenarioActiveSteps(draft);
if(Number.isFinite(index)&&steps[index]){
const params=steps[index].params&&typeof steps[index].params==='object'?steps[index].params:{};
params.channel=stepParamChannel.value||'effect';
if(params.channel==='background'&&params.file&&!/\.wav$/i.test(String(params.file))){
delete params.file;
}
if(params.channel!=='background'){
params.repeat=false;
}
steps[index].params=params;
}
gmScenarioChangeCommitDraft(draft,index);
return true;
}

function gmHandleScenarioCommandGroupChange(groupDevice,groupCommand){
const control=groupDevice||groupCommand;
const stepEl=control.closest('[data-scenario-step]');
const itemEl=control.closest('[data-command-group-item]');
const index=Number(stepEl&&stepEl.dataset.scenarioStep);
const itemIndex=Number(itemEl&&itemEl.dataset.commandGroupItem);
const draft=collectScenarioEditor();
const steps=scenarioActiveSteps(draft);
if(Number.isFinite(index)&&Number.isFinite(itemIndex)&&steps[index]){
const step=steps[index];
step.commands=Array.isArray(step.commands)?step.commands:[];
const item=step.commands[itemIndex]||defaultScenarioCommandItem();
if(groupDevice){
const device=scenarioDeviceById(groupDevice.value||'');
item.device_id=groupDevice.value||'';
item.command_id=scenarioValidCommandId(device,'');
item.params=defaultParamsForCommand(device,scenarioCommandById(item.device_id,item.command_id));
}
else{
item.command_id=groupCommand.value||'';
item.params=defaultParamsForCommand(scenarioDeviceById(item.device_id),scenarioCommandById(item.device_id,item.command_id));
}
step.commands[itemIndex]=item;
}
gmScenarioChangeCommitDraft(draft,index);
return true;
}

function gmHandleScenarioEventGroupChange(eventGroupDevice,eventGroupEvent){
const control=eventGroupDevice||eventGroupEvent;
const stepEl=control.closest('[data-scenario-step]');
const itemEl=control.closest('[data-event-group-item]');
const index=Number(stepEl&&stepEl.dataset.scenarioStep);
const itemIndex=Number(itemEl&&itemEl.dataset.eventGroupItem);
const draft=collectScenarioEditor();
const steps=scenarioActiveSteps(draft);
if(Number.isFinite(index)&&Number.isFinite(itemIndex)&&steps[index]){
const step=steps[index];
step.events=Array.isArray(step.events)?step.events:[];
const item=step.events[itemIndex]||defaultScenarioEventItem();
if(eventGroupDevice){
const device=scenarioDeviceById(eventGroupDevice.value||'');
item.device_id=eventGroupDevice.value||'';
item.event_id=scenarioValidEventId(device,'');
}
else{
item.event_id=eventGroupEvent.value||'';
}
step.events[itemIndex]=item;
}
gmScenarioChangeCommitDraft(draft,index);
return true;
}

function gmHandleScenarioStepTypeChange(stepType){
const stepEl=stepType.closest('[data-scenario-step]');
const index=Number(stepEl&&stepEl.dataset.scenarioStep);
const draft=collectScenarioEditor();
const steps=scenarioActiveSteps(draft);
if(Number.isFinite(index)&&steps[index]){
const previous=steps[index];
const replacement=newScenarioStepForType(index,stepType.value||'WAIT_TIME');
replacement.id=previous.id||replacement.id;
replacement.enabled=previous.enabled!==false;
steps[index]=replacement;
}
gmScenarioChangeCommitDraft(draft,index);
return true;
}

function gmHandleScenarioEditorChange(e){
const stepType=e.target.closest('select[data-step-field="type"]');
const stepDevice=e.target.closest('select[data-step-field="device_id"]');
const stepCommand=e.target.closest('select[data-step-field="command_id"]');
const stepDeviceEvent=e.target.closest('select[data-step-field="event_id"]');
const stepParamChannel=e.target.closest('select[data-step-param="channel"]');
const groupDevice=e.target.closest('select[data-group-command-field="device_id"]');
const groupCommand=e.target.closest('select[data-group-command-field="command_id"]');
const eventGroupDevice=e.target.closest('select[data-event-group-field="device_id"]');
const eventGroupEvent=e.target.closest('select[data-event-group-field="event_id"]');
const branchType=e.target.closest('select[data-scenario-branch-field="type"]');
const reactiveV2Field=e.target.closest('[data-v2-branch-field],[data-v2-trigger-field],[data-v2-policy-field],[data-v2-reentry-field],[data-v2-result-field],[data-v2-guard-field],[data-v2-variant-field]');
const reactiveV2ActionField=e.target.closest('[data-v2-action] [data-step-field],[data-v2-action] [data-step-param],[data-v2-action] [data-group-command-field]');
if(branchType)return gmHandleScenarioBranchTypeChange(branchType);
if(reactiveV2Field||reactiveV2ActionField)return gmHandleReactiveV2Change();
if(stepDevice)return gmHandleScenarioStepDeviceChange(stepDevice);
if(stepCommand||stepDeviceEvent)return gmHandleScenarioStepCommandOrEventChange(stepCommand||stepDeviceEvent);
if(stepParamChannel)return gmHandleScenarioStepAudioChannelChange(stepParamChannel);
if(groupDevice||groupCommand)return gmHandleScenarioCommandGroupChange(groupDevice,groupCommand);
if(eventGroupDevice||eventGroupEvent)return gmHandleScenarioEventGroupChange(eventGroupDevice,eventGroupEvent);
if(stepType)return gmHandleScenarioStepTypeChange(stepType);
return false;
}
// GM panel source part. Edit this file, then rebuild gm_panel.js.
function gmMarkEditorDirtyFromEvent(e){
markControlDirty(e.target);
const profileField=e.target.closest('#profile_id,#profile_name,#profile_duration,#profile_hint_pack,#profile_audio_pack,#profile_scenario,#profile_enabled');
const scenarioField=e.target.closest('#scenario_id,#scenario_name,[data-scenario-branch-field],[data-step-field],[data-step-param],[data-group-command-field],[data-event-group-field],[data-flag-list-field],[data-v2-branch-field],[data-v2-trigger-field],[data-v2-policy-field],[data-v2-reentry-field],[data-v2-result-field],[data-v2-guard-field],[data-v2-variant-field]');
const questDeviceField=e.target.closest('[data-quest-device-field],[data-quest-command-field],[data-quest-event-field]');
if(profileField)markProfileDirty();
if(scenarioField)markScenarioDirty();
if(questDeviceField)markQuestDeviceDirty();
}

function initGMEditorFocusAndDetails(content){
content.addEventListener('focusin',e=>{
markControlEditing(e.target);
});
content.addEventListener('focusout',e=>{
unmarkControlEditing(e.target);
});
document.addEventListener('toggle',e=>{
const detail=e.target;
if(!detail||String(detail.tagName||'').toLowerCase()!=='details')return;
const key=detailsKeyFor(detail);
if(key)gmOpenDetails[key]=detail.open;
}
,true);
}

async function gmHandleProfileRoomChange(editorRoom){
if(!confirmDiscardProfile()){
render();
return;
}
profileEditor.room_id=editorRoom.value||'';
profileEditor.profile_id='';
profileEditor.open=false;
clearProfileDirty();
render();
}

async function gmHandleScenarioRoomChange(scenarioRoom){
if(!confirmDiscardScenario()){
render();
return;
}
scenarioEditor.room_id=scenarioRoom.value||'';
scenarioEditor.scenario_id='';
scenarioEditor.open=false;
scenarioEditor.expanded_step=-1;
scenarioEditor.expanded_v2_action='';
scenarioEditor.active_branch=0;
clearScenarioDirty();
skipNextScenarioDomSync();
render();
}

function gmHandleDeviceFilterChange(deviceRoom){
deviceFilterRoom=deviceRoom.value||'';
clearTransientFieldDirty();
render();
}

function gmHandleObservedFilterChange(observed){
observedFilter=observed.value||'all';
clearTransientFieldDirty();
render();
}

async function gmHandleEditorChange(e){
const editorRoom=e.target.closest('select[data-profile-room-select]');
const scenarioRoom=e.target.closest('select[data-scenario-room-select]');
const deviceRoom=e.target.closest('select[data-device-room-filter]');
const observed=e.target.closest('select[data-observed-filter]');
const profile=e.target.closest('select[data-room-profile-room]');
const scenario=e.target.closest('select[data-room-scenario-room]');
try{
gmMarkEditorDirtyFromEvent(e);
if(editorRoom)return gmHandleProfileRoomChange(editorRoom);
if(scenarioRoom)return gmHandleScenarioRoomChange(scenarioRoom);
if(deviceRoom)return gmHandleDeviceFilterChange(deviceRoom);
if(observed)return gmHandleObservedFilterChange(observed);
if(gmHandleScenarioEditorChange(e))return;
if(profile&&profile.value){
await selectRoomProfile(profile.dataset.roomProfileRoom||'',profile.value||'');
return;
}
if(scenario&&scenario.value){
await selectRoomScenario(scenario.dataset.roomScenarioRoom||'',scenario.value||'');
return;
}
}
catch(err){
setStatus(err.message||'selection failed','state-fault');
}
}

function initGMEditorEventHandlers(){
const content=document.getElementById('gm_content');
if(!content)return;
initGMEditorFocusAndDetails(content);
content.oninput=gmMarkEditorDirtyFromEvent;
content.onchange=gmHandleEditorChange;
}
// GM panel source part. Edit this file, then rebuild gm_panel.js.
const GM_WS_RECONNECT_MS=3000;
let gmWsSocket=null;
let gmWsReconnectTimer=0;
let gmWsFlushTimer=0;
let gmWsVersionsIgnoreUntilMs=0;
const gmWsPendingSlices=new Map();

function gmWsUrl(){
const proto=window.location.protocol==='https:'?'wss:':'ws:';
return `${proto}//${window.location.host}/api/ws`;
}

function gmWsScheduleReconnect(){
if(gmWsReconnectTimer)return;
gmWsReconnectTimer=window.setTimeout(()=>{
gmWsReconnectTimer=0;
gmInitWebSocket();
}
,GM_WS_RECONNECT_MS);
}

function gmWsQueueInvalidation(slice){
if(!slice||typeof slice!=='object')return;
const key=`${slice.slice||''}:${slice.target_id||''}`;
if(!key||key===':')return;
gmWsPendingSlices.set(key,{slice:slice.slice||'',target_id:slice.target_id||'',scope:slice.scope||'',generation:Number(slice.generation)||0,reason:slice.reason||''});
if(gmWsFlushTimer)return;
gmWsFlushTimer=window.setTimeout(async()=>{
const slices=Array.from(gmWsPendingSlices.values());
gmWsPendingSlices.clear();
gmWsFlushTimer=0;
try{
await refreshGMByInvalidationSlices(slices);
}
catch(err){
setGMStatus('WS refresh failed','gm-bad');
}
}
,50);
}

async function gmWsHandleVersionsChanged(payload){
if(!payload||Date.now()<gmWsVersionsIgnoreUntilMs)return;
await refreshGMByVersions({
rooms:Number(payload.rooms)||0,
devices:Number(payload.devices)||0,
scenarios:Number(payload.scenarios)||0,
profiles:Number(payload.profiles)||0,
ingest:Number(payload.ingest)||0,
session:Number(payload.session)||0,
static_generation:Number(payload.static)||0,
runtime_generation:Number(payload.runtime)||0
});
}

function gmWsHandleEnvelope(message){
if(!message||typeof message!=='object')return;
const type=String(message.type||'');
const payload=message.payload&&typeof message.payload==='object'?message.payload:null;
if(type==='gm.invalidate'&&payload){
gmWsVersionsIgnoreUntilMs=Date.now()+500;
gmWsQueueInvalidation({
slice:String(payload.slice||''),
target_id:String(payload.target_id||''),
scope:String(payload.scope||''),
generation:Number(payload.generation)||0,
reason:String(payload.reason||'')
});
return;
}
if(type==='gm.resync.required'&&payload){
gmWsVersionsIgnoreUntilMs=Date.now()+500;
refreshGMByInvalidationSlices([{slice:'full.snapshot',target_id:String(payload.target_id||''),scope:'recovery',generation:Number(payload.generation)||0,reason:String(payload.reason||'resync_required')}]).catch(()=>{
setGMStatus('WS resync failed','gm-bad');
});
return;
}
if(type==='gm.versions.changed'&&payload){
gmWsHandleVersionsChanged(payload).catch(()=>{});
}
}

function gmInitWebSocket(){
if(gmWsSocket&&(
gmWsSocket.readyState===WebSocket.OPEN||
gmWsSocket.readyState===WebSocket.CONNECTING
))return;
if(typeof WebSocket!=='function')return;
try{
const socket=new WebSocket(gmWsUrl());
gmWsSocket=socket;
socket.onopen=()=>{
socket.send(JSON.stringify({type:'subscribe'}));
};
socket.onmessage=event=>{
let message=null;
try{
message=JSON.parse(event&&event.data||'');
}
catch(err){
return;
}
gmWsHandleEnvelope(message);
};
socket.onclose=()=>{
if(gmWsSocket===socket)gmWsSocket=null;
gmWsScheduleReconnect();
};
socket.onerror=()=>{
try{socket.close();}catch(err){}
};
}
catch(err){
gmWsScheduleReconnect();
}
}

function setGMStatus(text,cls){
setStatus(text,cls==='gm-bad'?'state-fault':(cls==='gm-ok'?'state-ok':'state-unknown'));
}

document.getElementById('gm_nav').onclick=async e=>{
const btn=e.target.closest('.nav-btn');
if(!btn)return;
const view=btn.dataset.view||'dashboard';
if(!canOpenView(view))return;
if(view!==currentView&&!confirmDiscardEditorChanges())return;
currentView=view;
try{
await loadGMViewData(false);
}
catch(err){
setGMStatus('View data refresh failed','gm-bad');
}
render();
}
;

document.getElementById('gm_content').onclick=async e=>{
await gmHandleActionClick(e);
}
;

const gmRightSidebar=document.getElementById('gm_right_sidebar');
if(gmRightSidebar){
gmRightSidebar.onclick=async e=>{
if(await gmHandleActionClick(e))return;
}
;
}

initGMEditorEventHandlers();

document.getElementById('gm_refresh').onclick=()=>{
if(!confirmDiscardEditorChanges())return;
clearProfileDirty();
clearScenarioDirty();
clearQuestDeviceDirty();
clearTransientFieldDirty();
loadGMFullSnapshot();
}
;

document.getElementById('gm_logout').onclick=()=>{
if(!confirmDiscardEditorChanges())return;
clearProfileDirty();
clearScenarioDirty();
clearQuestDeviceDirty();
clearTransientFieldDirty();
fetch('/api/auth/logout',{
method:'POST'}
).finally(()=>window.location='/login');
}
;

const gmAdminHome=document.getElementById('gm_admin_home');
if(gmAdminHome){
gmAdminHome.onclick=()=>{
clearProfileDirty();
clearScenarioDirty();
clearQuestDeviceDirty();
clearTransientFieldDirty();
}
;
}

window.addEventListener('beforeunload',e=>{
if(gmWsReconnectTimer){
clearTimeout(gmWsReconnectTimer);
gmWsReconnectTimer=0;
}
if(gmWsFlushTimer){
clearTimeout(gmWsFlushTimer);
gmWsFlushTimer=0;
}
if(!hasUnsavedEditorChanges())return;e.preventDefault();e.returnValue='';}
);

window.__sessionRolePromise=loadGMSession();

window.__sessionRolePromise.then(async()=>{
try{
await loadGMFullSnapshot();
}
finally{
gmInitWebSocket();
}
});

function gmPollActiveRoomRuntimeVisible(){
if(document.hidden)return;
pollActiveRoomRuntime();
}

function gmPollStateSnapshotVisible(){
if(document.hidden)return;
pollGMStateSnapshot();
}

function gmUpdateVisibleRoomClocksVisible(){
if(document.hidden)return;
updateVisibleRoomClocks();
}

document.addEventListener('visibilitychange',()=>{
if(document.hidden)return;
updateVisibleRoomClocks();
pollActiveRoomRuntime();
pollGMStateSnapshot();
});

setInterval(gmPollActiveRoomRuntimeVisible,1000);
setInterval(gmPollStateSnapshotVisible,10000);
setInterval(gmUpdateVisibleRoomClocksVisible,250);
