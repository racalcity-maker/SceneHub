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
list:(includeSystem=true,includeManifestJson=false)=>gmGet(`/api/gm/devices?include_system=${includeSystem?'1':'0'}&include_manifest_json=${includeManifestJson?'1':'0'}`),
save:device=>gmPostJson('/api/gm/device/save',{device}),
delete:deviceId=>gmPostJson('/api/gm/device/delete',{device_id:deviceId}),
describeInterface:clientId=>gmPostJson('/api/gm/device/describe-interface',{client_id:clientId}),
runCommand:(deviceId,commandId,params)=>gmPostJson('/api/gm/device/command/run',{
device_id:deviceId,
command_id:commandId,
...(params&&typeof params==='object'?{params}:{}),
}),
},
sidebarPresets:{
list:()=>gmGet('/api/gm/sidebar-presets'),
listJson:()=>gmGetJson('/api/gm/sidebar-presets'),
save:presets=>gmPostJsonResult('/api/gm/sidebar-presets/save',{presets}),
load:()=>gmPostJsonResult('/api/gm/sidebar-presets/load',{}),
exportUrl:'/api/gm/sidebar-presets/export',
importJson:text=>gmFetch('/api/gm/sidebar-presets/import',{method:'POST',headers:{'Content-Type':'application/json'},body:text}),
},
  room:{
  runtime:(roomId,detail='detail',options)=>{
  const opts=options&&typeof options==='object'?options:{};
  let url=`/api/gm/room/runtime?room_id=${enc(roomId)}${detail&&detail!=='detail'?`&detail=${enc(detail)}`:''}`;
  if(Object.prototype.hasOwnProperty.call(opts,'include_assets'))url+=`&include_assets=${opts.include_assets?'1':'0'}`;
  return gmGet(url);
},
  runtimeJson:(roomId,detail='detail',options)=>{
  const opts=options&&typeof options==='object'?options:{};
  let url=`/api/gm/room/runtime?room_id=${enc(roomId)}${detail&&detail!=='detail'?`&detail=${enc(detail)}`:''}`;
  if(Object.prototype.hasOwnProperty.call(opts,'include_assets'))url+=`&include_assets=${opts.include_assets?'1':'0'}`;
  return gmGetJson(url);
},
  scenarios:(roomId,options)=>{
  if(!roomId)throw new Error('roomId required');
  const opts=options&&typeof options==='object'?options:{};
  let url=`/api/gm/room/scenarios?room_id=${enc(roomId)}`;
  if(opts.detail&&opts.detail!=='detail')url+=`&detail=${enc(opts.detail)}`;
  if(opts.scenario_id)url+=`&scenario_id=${enc(opts.scenario_id)}`;
  return gmGet(url);
},
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
rooms:{
runtimeSummaryJson:()=>gmGetJson('/api/gm/rooms/runtime'),
},
storage:{
post:url=>gmPost(url),
importJson:(url,text)=>gmFetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:text}),
exportUrl:kind=>kind==='device'?'/api/gm/devices/export':(kind==='scenario'?'/api/gm/room/scenarios/export':(kind==='preset'?'/api/gm/sidebar-presets/export':'/api/gm/profiles/export')),
commandUrl:(kind,op)=>{
if(kind==='device')return `/api/gm/devices/${op}`;
if(kind==='scenario')return `/api/gm/room/scenarios/${op}`;
if(kind==='preset')return `/api/gm/sidebar-presets/${op}`;
return `/api/gm/profiles/${op}`;
},
run:(kind,op)=>gmPost(kind==='device'?`/api/gm/devices/${op}`:(kind==='scenario'?`/api/gm/room/scenarios/${op}`:(kind==='preset'?`/api/gm/sidebar-presets/${op}`:`/api/gm/profiles/${op}`))),
},
};
