// GM panel source part. Edit this file, then rebuild gm_panel.js.
const GM={
data:{
state:null,
observed:null,
audit:null,
timeline:null,
roomScenarios:{},
roomScenarioDetails:{},
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
currentView:'rooms',
currentRoomId:'',
roomTab:'control',
inputDirty:false,
autoRenderDeferred:false,
initialRouteApplied:false,
skipScenarioDomSync:false,
openDetails:{},
flagDatalistSeq:0,
quickPresets:null,
quickPresetWizard:{}
},
editors:{
profile:{room_id:'',profile_id:'',dirty:false,open:false,prefill:null},
 scenario:{room_id:'',scenario_id:'',dirty:false,open:false,draft:null,original_scenario:null,validation_report:null,draft_revision:0,validation_revision:0,expanded_step:-1,expanded_v2_action:'',active_branch:0,branch_count_shrink_allowed:false,branch_count_shrink_floor:0},
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
gmRoomScenarioDetails:{get(){return GM.data.roomScenarioDetails;},set(v){GM.data.roomScenarioDetails=v||{};}},
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
currentView:{get(){return GM.ui.currentView;},set(v){GM.ui.currentView=v||'rooms';}},
currentRoomId:{get(){return GM.ui.currentRoomId;},set(v){GM.ui.currentRoomId=v||'';}},
roomTab:{get(){return GM.ui.roomTab;},set(v){GM.ui.roomTab=v||'control';}},
gmInputDirty:{get(){return GM.ui.inputDirty;},set(v){GM.ui.inputDirty=!!v;}},
gmAutoRenderDeferred:{get(){return GM.ui.autoRenderDeferred;},set(v){GM.ui.autoRenderDeferred=!!v;}},
gmInitialRouteApplied:{get(){return GM.ui.initialRouteApplied;},set(v){GM.ui.initialRouteApplied=!!v;}},
gmSkipScenarioDomSync:{get(){return GM.ui.skipScenarioDomSync;},set(v){GM.ui.skipScenarioDomSync=!!v;}},
gmOpenDetails:{get(){return GM.ui.openDetails;},set(v){GM.ui.openDetails=v||{};}},
gmFlagDatalistSeq:{get(){return GM.ui.flagDatalistSeq;},set(v){GM.ui.flagDatalistSeq=Number(v)||0;}},
gmQuickPresets:{get(){return GM.ui.quickPresets;},set(v){GM.ui.quickPresets=Array.isArray(v)?v:v===null?null:[];}},
gmQuickPresetWizard:{get(){return GM.ui.quickPresetWizard;},set(v){GM.ui.quickPresetWizard=v&&typeof v==='object'?v:{};}}
});

function setStatus(text,cls){const el=document.getElementById('system_status');if(!el)return;el.textContent=text||'';el.className='status '+(cls||'state-unknown');}
async function gmFetch(url,options){const res=await fetch(url,options);if(res.status===401){window.location='/login';throw new Error('Unauthorized');}return res;}
function isAdmin(){return gmSession&&gmSession.role==='admin';}
function canOpenView(view){return !['devices','profiles','scenarios','device_setup','hardware_io','observed','storage'].includes(view)||isAdmin();}
function ensureAllowedView(){if(!canOpenView(currentView)||currentView==='dashboard'){currentView='rooms';}}
function applyGMRoleLayout(){const admin=isAdmin();
document.body.classList.toggle('role-admin',admin);const badge=document.getElementById('gm_role_badge');if(badge)badge.textContent=admin?'admin':'operator';
document.querySelectorAll('[data-view]').forEach(el=>{if(['devices','profiles','scenarios','device_setup','observed','storage'].includes(el.dataset.view||'')){el.style.display=admin?'':'none';}});ensureAllowedView();}
async function loadGMSession(){try{const res=await api.session.info();if(res.ok){gmSession=await res.json();}}catch(err){gmSession={role:'user',username:''};}window.__WEB_SESSION=gmSession;applyGMRoleLayout();return gmSession;}
function metric(label,value){return `<div class='card metric'><div class='label'>${esc(label)}</div><div class='value'>${esc(value)}</div></div>`;}
function status(v){return `<span class='status ${stateClass(v)}'>${esc(healthLabel(v))}</span>`;}
function roomCard(r){const derived=roomDerivedHealth(r);const issueCount=Number(r&&r.issue_count)||0;const deviceCount=Number(r&&r.scenario_device_count)||Number(r&&r.device_count)||0;return `<article class='card clickable' data-action='room.open' data-room-id='${esc(r.room_id)}'><div class='card-head'><div><div class='card-title'>${esc(r.title||r.name||r.room_id)}</div><div class='card-sub'>Room</div></div>${status(derived)}</div><div class='kvs'><div class='kv'><span class='k'>Devices</span><span class='v'>${esc(deviceCount)}</span></div><div class='kv'><span class='k'>Issues</span><span class='v'>${esc(issueCount)}</span></div><div class='kv'><span class='k'>Timer</span>${roomClockHtml(r,'span','v')}</div></div></article>`;}
function issueRow(i){const subject=i.device_id?deviceDisplayName(i.device_id):(i.room_id?roomName(i.room_id):i.scope);return `<div class='row-card'><div class='row-main'><div class='row-title'>${esc(subject)} - ${esc(i.title||i.code)}</div><div class='row-meta'>${esc(i.details||'')}</div></div>${status(i.severity)}</div>`;}
function noProfilesHtml(roomId){return isAdmin()?`<div class='empty'>No game modes for this room</div><div class='actions'>${uiButton({label:'Create game mode',action:'admin.open',dataset:{view:'profiles','room-id':roomId||''}})}</div>`:`<div class='empty'>No game modes available. Ask admin.</div>`;}
function noScenariosHtml(roomId){return isAdmin()?`<div class='empty'>No room scenarios</div><div class='actions'>${uiButton({label:'Create scenario',action:'admin.open',dataset:{view:'scenarios','room-id':roomId||''}})}</div>`:`<div class='empty'>No room scenarios</div>`;}
function applyInitialOperatorRoute(){if(gmInitialRouteApplied)return;gmInitialRouteApplied=true;if(currentView==='dashboard'||!currentView)currentView='rooms';if(!canOpenView(currentView))currentView='rooms';}
