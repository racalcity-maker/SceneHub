// GM panel source part. Edit this file, then rebuild gm_panel.js.
let gmState=null;

let gmObserved=null;

let gmAudit=null;

let gmTimeline=null;

let gmRoomScenarios={
}
;

let gmRoomProfiles={
}
;

let gmScenarioEditorCatalogs={
}
;

let gmDeviceConfig=null;

let gmQuestDevices=null;

let gmAudioFiles={
loaded:false,loading:false,scheduled:false,error:'',items:[]}
;

let gmSession={
role:'user',username:''}
;

let currentRoomScenarioId={
}
;

let currentRoomProfileId={
}
;

let profileEditor={
room_id:'',profile_id:'',dirty:false,open:false,prefill:null}
;

let scenarioEditor={
room_id:'',scenario_id:'',dirty:false,open:false,draft:null,validation_report:null,expanded_step:-1,active_branch:0}
;

let questDeviceEditor={
device_id:'',dirty:false,open:false,draft:null,discovery:null}
;

let deviceFilterRoom='';

let observedFilter='all';

let currentView='dashboard';

let currentRoomId='';

let roomTab='overview';

let gmInputDirty=false;

let gmAutoRenderDeferred=false;

let gmInitialRouteApplied=false;

let gmSkipScenarioDomSync=false;

let gmOpenDetails={
}
;

function esc(v){
const value=(v===undefined||v===null)?'':v;

return String(value).replace(/[&<>"']/g,m=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[m]));}
function detailsKeyFor(el){
if(!el||String(el.tagName||'').toLowerCase()!=='details')return '';
if(el.dataset&&el.dataset.detailsKey)return el.dataset.detailsKey;
const summary=(el.querySelector('summary')&&el.querySelector('summary').textContent||'details').trim().toLowerCase();
let scope='';
const scoped=el.closest('[data-scenario-step],[data-quest-command],[data-quest-event],[data-quest-device-editor],[data-manual-device],[data-open-room],[data-open-device-setup]');
if(scoped){
['scenarioStep','questCommand','questEvent','questDeviceEditor','manualDevice','openRoom','openDeviceSetup'].some(k=>{
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
function ago(ms){if(!ms)return 'never';const age=Math.max(0,Math.floor((performance.now()-Number(ms))/1000));return age<60?`${age}s ago`:`${Math.floor(age/60)}m ago`;}
function audioBaseName(path){if(!path)return '';const parts=String(path).split('/').filter(Boolean);return parts.length?parts[parts.length-1]:path;}
function audioDirName(path){if(!path)return '/';const raw=String(path);const idx=raw.lastIndexOf('/');if(idx<0)return '/';return raw.slice(0,idx)||'/';}
function roomById(id){return (gmState&&Array.isArray(gmState.rooms)?gmState.rooms:[]).find(r=>r.room_id===id)||null;}
function deviceById(id){return (gmState&&Array.isArray(gmState.devices)?gmState.devices:[]).find(d=>d.device_id===id)||null;}
function roomName(id){const r=roomById(id);return r&&(r.title||r.name||r.room_id)||id||'No room';}
function deviceDisplayName(id){const live=deviceById(id);const quest=questDevices().find(d=>(d.id||'')===id);const cfg=configDevices().find(d=>(d.id||d.device_id||'')===id);return live&&(live.display_name||live.device_id)||quest&&(quest.name||quest.id)||cfg&&(cfg.display_name||cfg.name||cfg.id||cfg.device_id)||id||'Device';}
function roomDevices(id){return (gmState&&Array.isArray(gmState.devices)?gmState.devices:[]).filter(d=>d.room_id===id);}
function roomIssues(id){return (gmState&&Array.isArray(gmState.issues)?gmState.issues:[]).filter(i=>!id||!i.room_id||i.room_id===id);}
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
function questDeviceDisplayName(dev){return dev&&(dev.name||dev.display_name||dev.id)||'Device';}
function observedDisplayName(item){if(!item)return 'Device';const reg=observedRegistration(item.device_id);return reg&&(reg.name||reg.device_id)||item.name||item.display_name||item.device_id||'Device';}
function questDeviceHealth(dev){if(!dev)return 'offline';if(dev.system_device)return 'ok';if(dev.enabled===false)return 'degraded';const observed=observedByClientId(dev.client_id||dev.id);if(!observed)return 'fault';if(observed.connectivity==='offline'||observed.connectivity==='unknown')return 'fault';if(observed.health==='fault'||observed.state==='fault')return 'fault';if(observed.health==='degraded'||observed.state==='degraded')return 'degraded';return 'ok';}
function questDeviceStatusText(dev){if(!dev)return 'missing device';if(dev.system_device)return 'system device';if(dev.enabled===false)return 'disabled';const observed=observedByClientId(dev.client_id||dev.id);if(!observed)return 'not observed';if(observed.connectivity==='offline'||observed.connectivity==='unknown')return 'offline';return observed.health||observed.state||'online';}
function scenarioById(roomId,scenarioId){return roomScenarios(roomId).find(s=>s.id===scenarioId)||null;}
function roomSelectedScenarioObject(room){if(!room)return null;const profiles=roomProfiles(room.room_id);const profileId=roomSelectedProfileId(room.room_id)||room.selected_profile_id||'';const profile=profiles.find(p=>p.id===profileId)||null;const preferred=room.running_scenario_id||room.selected_profile_scenario_id||(profile&&profile.scenario_id)||room.selected_scenario_id||'';return scenarioById(room.room_id,preferred)||scenarioById(room.room_id,room.selected_scenario_id)||null;}
function scenarioFlattenedSteps(scenario){if(!scenario)return [];if(Array.isArray(scenario.branches)&&scenario.branches.length){return scenario.branches.reduce((out,branch)=>out.concat(Array.isArray(branch.steps)?branch.steps:[]),[]);}return Array.isArray(scenario.steps)?scenario.steps:[];}
function roomScenarioSteps(room){const scenario=roomSelectedScenarioObject(room);return scenarioFlattenedSteps(scenario);}
function roomCurrentScenarioStep(room){const steps=roomScenarioSteps(room);const index=Math.max(0,Number(room&&room.scenario_current_step_index)||0);return steps[index]||null;}
function roomScenarioDeviceIds(room){const ids=new Set();roomScenarioSteps(room).forEach(step=>{const type=String(step&&step.type||'').toLowerCase();if((type==='device_command'||type==='wait_device_event')&&step.device_id)ids.add(step.device_id);});return Array.from(ids);}
function roomQuestDeviceIssues(room){return roomScenarioDeviceIds(room).map(id=>questDeviceById(id)).filter(Boolean).map(dev=>{const health=questDeviceHealth(dev);if(health==='ok')return null;return {device_id:dev.id,title:health==='fault'?'Device offline':'Device degraded',details:`${dev.name||dev.id}: ${questDeviceStatusText(dev)}`,severity:health};}).filter(Boolean);}
function roomDerivedHealth(room){const issues=roomQuestDeviceIssues(room);if(issues.some(i=>i.severity==='fault'))return 'fault';if(issues.some(i=>i.severity==='degraded'))return 'degraded';return room&&room.health||'unknown';}
function scenarioEditorCatalog(roomId){return gmScenarioEditorCatalogs[roomId]||{quest_devices:[],step_schemas:[]};}
function optionList(items,selected,emptyLabel){let found=false;const opts=[];if(emptyLabel)opts.push(`<option value=''>${esc(emptyLabel)}</option>`);(Array.isArray(items)?items:[]).forEach(item=>{const id=item.id||'';if(id===selected)found=true;opts.push(`<option value='${esc(id)}' ${id===selected?'selected':''}>${esc(item.name||id)}</option>`);});if(selected&&!found)opts.push(`<option value='${esc(selected)}' selected>${esc(selected)} (missing)</option>`);return opts.join('');}
function setStatus(text,cls){const el=document.getElementById('system_status');if(!el)return;el.textContent=text||'';el.className='status '+(cls||'state-unknown');}
async function gmFetch(url,options){const res=await fetch(url,options);if(res.status===401){window.location='/login';throw new Error('Unauthorized');}return res;}
function isAdmin(){return gmSession&&gmSession.role==='admin';}
function canOpenView(view){return !['profiles','scenarios','device_setup','observed','storage'].includes(view)||isAdmin();}
function ensureAllowedView(){if(!canOpenView(currentView)){currentView='dashboard';}}
function applyGMRoleLayout(){const admin=isAdmin();
document.body.classList.toggle('role-admin',admin);const badge=document.getElementById('gm_role_badge');if(badge)badge.textContent=admin?'admin':'operator';
document.querySelectorAll('[data-view]').forEach(el=>{if(['profiles','scenarios','device_setup','observed','storage'].includes(el.dataset.view||'')){el.style.display=admin?'':'none';}});ensureAllowedView();}
async function loadGMSession(){try{const res=await gmFetch('/api/session/info');if(res.ok){gmSession=await res.json();}}catch(err){gmSession={role:'user',username:''};}window.__WEB_SESSION=gmSession;applyGMRoleLayout();return gmSession;}
function metric(label,value){return `<div class='card metric'><div class='label'>${esc(label)}</div><div class='value'>${esc(value)}</div></div>`;}
function status(v){return `<span class='status ${stateClass(v)}'>${esc(healthLabel(v))}</span>`;}
function roomCard(r){const derived=roomDerivedHealth(r);const scenarioIssues=roomQuestDeviceIssues(r).length;const issueCount=(Number(r.issue_count)||0)+scenarioIssues;return `<article class='card clickable' data-open-room='${esc(r.room_id)}'><div class='card-head'><div><div class='card-title'>${esc(r.title||r.name||r.room_id)}</div><div class='card-sub'>Room</div></div>${status(derived)}</div><div class='kvs'><div class='kv'><span class='k'>Devices</span><span class='v'>${esc(roomScenarioDeviceIds(r).length||r.device_count||0)}</span></div><div class='kv'><span class='k'>Issues</span><span class='v'>${esc(issueCount)}</span></div><div class='kv'><span class='k'>Timer</span><span class='v'>${fmtClock(r.timer_remaining_ms)}</span></div></div></article>`;}
function questDeviceMonitorRow(dev){const observed=observedByClientId(dev.client_id||dev.id);const health=questDeviceHealth(dev);const meta=[`${(dev.commands||[]).length} commands`,`${(dev.events||[]).length} events`,dev.enabled===false?'disabled':'enabled'].join(' / ');const setup=isAdmin()?`<button data-open-device-setup='${esc(dev.id||'1')}'>Device Setup</button>`:'';const debug=isAdmin()?`<details class='scenario-advanced'><summary>Debug ids</summary><div class='row-meta'>Device ID: ${esc(dev.id||'')}</div><div class='row-meta'>Client: ${esc(dev.client_id||'none')}</div></details>`:'';return `<div class='row-card'><div class='row-main'><div class='row-title'>${esc(questDeviceDisplayName(dev))} ${dev.enabled===false?`<span class='badge'>disabled</span>`:''}</div><div class='row-meta'>${esc(meta)}</div><div class='row-meta'>${observed?`${esc(observed.connectivity||'unknown')} / fw ${esc(observed.fw_version||'n/a')}`:'not observed'}</div>${debug}</div><div>${status(health)}<div class='row-meta'>${esc(questDeviceStatusText(dev))}</div></div><div class='actions'>${setup}</div></div>`;}
function issueRow(i){const subject=i.device_id?deviceDisplayName(i.device_id):(i.room_id?roomName(i.room_id):i.scope);return `<div class='row-card'><div class='row-main'><div class='row-title'>${esc(subject)} - ${esc(i.title||i.code)}</div><div class='row-meta'>${esc(i.details||'')}</div></div>${status(i.severity)}</div>`;}
function manualButtonGroups(){return questDevices().map(dev=>{const id=dev.id||'';const commands=(Array.isArray(dev.commands)?dev.commands:[]).filter(cmd=>cmd&&cmd.button_enabled&&cmd.id);if(!id||!commands.length)return null;return {id,name:dev.name||id,room_id:'',health:questDeviceHealth(dev),commands};}).filter(Boolean);}
function renderRightSidebar(){const root=document.getElementById('gm_right_sidebar');if(!root)return;const groups=manualButtonGroups();root.innerHTML=`<div class='right-brand'><h2>Manual buttons</h2><p>Single-device controls</p></div><div class='manual-groups'>${groups.length?groups.map(g=>`<section class='manual-group'><div class='manual-group-head'><div><div class='manual-title'>${esc(g.name)}</div><div class='manual-meta'>${g.room_id?esc(roomName(g.room_id)):'Quest device'}</div></div>${status(g.health)}</div><div class='manual-buttons'>${g.commands.map(cmd=>`<button class='${cmd.dangerous?'danger':''}' data-manual-device='${esc(g.id)}' data-manual-command='${esc(cmd.id)}' data-dangerous='${cmd.dangerous?'1':'0'}'>${esc(cmd.label||cmd.id)}</button>`).join('')}</div>${isAdmin()?`<details class='scenario-advanced'><summary>Debug ids</summary><div class='row-meta'>Device ID: ${esc(g.id)}</div></details>`:''}</section>`).join(''):`<div class='manual-empty'>No manual buttons configured</div>`}</div>`;}
function commandSupportsScenarioParams(command){const kind=String(command&&command.kind||'mqtt_publish');return kind.indexOf('internal_')===0;}
function questDeviceCommandName(deviceId,commandId){const dev=questDeviceById(deviceId);const cmd=dev&&Array.isArray(dev.commands)?dev.commands.find(c=>(c.id||'')===commandId):null;return cmd&&(cmd.label||cmd.id)||commandId||'command';}
function questDeviceEventName(deviceId,eventId){const dev=questDeviceById(deviceId);const ev=dev&&Array.isArray(dev.events)?dev.events.find(item=>(item.id||'')===eventId):null;return ev&&(ev.label||ev.id)||eventId||'event';}
function questDeviceEventNameByType(deviceId,eventType){const dev=questDeviceById(deviceId);const ev=dev&&Array.isArray(dev.events)?dev.events.find(item=>(item.event_type||item.id||'')===eventType):null;return ev&&(ev.label||ev.id)||eventType||'event';}
function scenarioStepText(s){if(!s)return '';const type=String(s.type||'').toLowerCase();if(type==='device_command')return `${deviceDisplayName(s.device_id)} -> ${questDeviceCommandName(s.device_id,s.command_id)}`;if(type==='wait_device_event')return `Wait ${deviceDisplayName(s.device_id)}: ${questDeviceEventName(s.device_id,s.event_id)}`;if(type==='wait_time')return `Wait ${Math.max(1,Math.round((Number(s.duration_ms)||1000)/1000))} sec`;if(type==='operator_approval')return `Operator approval: ${s.operator_prompt||s.prompt||s.label||'Confirm'}`;return s.label||s.id||s.type||'Step';}
function scenarioStepLabel(room,total){const state=room.scenario_runtime_state||'idle';const idx=Number(room.scenario_current_step_index)||0;if(!total)return '0 / 0';if(state==='idle'||state==='stopped')return `0 / ${total}`;if(state==='done')return `${total} / ${total}`;return `${Math.min(total,idx+1)} / ${total}`;}
function scenarioWaitText(room){const t=room.scenario_wait_type||'none';if(t==='time')return `time until ${room.scenario_wait_until_ms||0}`;if(t==='event'||t==='any_events'||t==='all_events'){const events=Array.isArray(room.scenario_wait_events)?room.scenario_wait_events:[];if(events.length>1)return `${t==='all_events'?'all of':'any of'} ${events.length} events`;const source=room.scenario_wait_source_id||'';const eventType=room.scenario_wait_event_type||'any';return `${source?deviceDisplayName(source):'Any device'}: ${source?questDeviceEventNameByType(source,eventType):eventType}`;}if(t==='flags'){const flags=Array.isArray(room.scenario_wait_flags)?room.scenario_wait_flags:[];return flags.length?`flags: ${flags.map(flag=>`${flag.name||'flag'}=${flag.value?'true':'false'}`).join(', ')}`:'flags';}if(t==='operator')return `operator: ${room.scenario_wait_operator_prompt||'approval'}`;return 'none';}
function scenarioProgressStepState(room,index){const runtime=room&&room.scenario_runtime_state||'idle';const current=Math.max(0,Number(room&&room.scenario_current_step_index)||0);if(runtime==='done')return 'done';if(runtime==='error')return index<current?'done':(index===current?'error':'pending');if(runtime==='running'||runtime==='waiting')return index<current?'done':(index===current?'current':'pending');return 'pending';}
function scenarioProgressBranchCurrentIndex(branchRuntime){
if(!branchRuntime)return null;
const raw=Math.max(0,Number(branchRuntime.current_step_index)||0);
const start=Math.max(0,Number(branchRuntime.step_start_index)||0);
const count=Math.max(0,Number(branchRuntime.step_count)||0);
if(count>0&&raw>=start&&raw<=start+count&&raw>count)return raw-start;
return raw;
}
function scenarioProgressBranchState(room,branchRuntime,localIndex,globalIndex){const runtime=(branchRuntime&&branchRuntime.state)||room&&room.scenario_runtime_state||'idle';const branchCurrent=scenarioProgressBranchCurrentIndex(branchRuntime);const current=branchCurrent!==null?branchCurrent:Math.max(0,Number(room&&room.scenario_current_step_index)||0);const index=branchRuntime?localIndex:globalIndex;if(runtime==='done')return 'done';if(runtime==='error')return index<current?'done':(index===current?'error':'pending');if(runtime==='running'||runtime==='waiting')return index<current?'done':(index===current?'current':'pending');return 'pending';}
function scenarioProgressIcon(state){if(state==='done')return '&#10003;';if(state==='current')return '&rarr;';if(state==='error')return '!';return '';}
function scenarioProgressBranches(scenarioOrSteps){if(scenarioOrSteps&&Array.isArray(scenarioOrSteps.branches)&&scenarioOrSteps.branches.length)return scenarioOrSteps.branches.map((branch,index)=>{const type=String(branch.type||'normal').toLowerCase()==='reactive'?'reactive':'normal';return {id:branch.id||`branch_${index+1}`,name:branch.name||`Branch ${index+1}`,type,enabled:branch.enabled!==false,required_for_completion:type==='normal'&&branch.required_for_completion!==false,steps:Array.isArray(branch.steps)?branch.steps:[]};});const steps=Array.isArray(scenarioOrSteps)?scenarioOrSteps:(scenarioOrSteps&&Array.isArray(scenarioOrSteps.steps)?scenarioOrSteps.steps:[]);return steps.length?[{id:'main',name:'Main',type:'normal',enabled:true,required_for_completion:true,steps}]:[];}
function scenarioProgressBranchRuntime(room,branch,index){const runtimes=Array.isArray(room&&room.scenario_branches)?room.scenario_branches:[];const branchId=branch&&branch.id||'';return (branchId&&runtimes.find(item=>(item.id||'')===branchId))||runtimes.find(item=>Number(item.index)===index)||null;}
function renderScenarioProgressStep(room,step,index,globalIndex,branchRuntime){const disabled=step&&step.enabled===false;const state=disabled?'disabled':scenarioProgressBranchState(room,branchRuntime,index,globalIndex);return `<div class='scenario-progress-step ${state}'><span class='scenario-progress-icon'>${scenarioProgressIcon(state)}</span><span class='scenario-progress-index'>${esc(index+1)}.</span><span class='scenario-progress-text'>${esc(scenarioStepText(step))}</span>${disabled?`<span class='badge'>disabled</span>`:''}</div>`;}
function scenarioBranchDoneCount(room,branch,branchRuntime,globalStart){
const steps=Array.isArray(branch&&branch.steps)?branch.steps:[];
const total=steps.length;
if(!total)return 0;
const runtime=(branchRuntime&&branchRuntime.state)||room&&room.scenario_runtime_state||'idle';
if(runtime==='done')return total;
if(runtime==='idle'||runtime==='stopped'||runtime==='disabled')return 0;
if(runtime==='running'||runtime==='waiting'||runtime==='error'){
const current=scenarioProgressBranchCurrentIndex(branchRuntime);
if(current!==null)return Math.max(0,Math.min(total,current));
return Math.max(0,Math.min(total,Math.max(0,Number(room&&room.scenario_current_step_index)||0)-globalStart));
}
return 0;
}
function scenarioBranchCurrentStep(branch,branchRuntime){
const steps=Array.isArray(branch&&branch.steps)?branch.steps:[];
if(!steps.length)return 'No steps';
const runtime=branchRuntime&&branchRuntime.state||'idle';
if(runtime==='done')return 'Complete';
const current=scenarioProgressBranchCurrentIndex(branchRuntime);
if(current!==null&&steps[current])return scenarioStepText(steps[current]);
return scenarioStepText(steps[0]);
}
function scenarioProgressBar(done,total){const pct=total?Math.max(0,Math.min(100,Math.round(done*100/total))):0;return `<div class='scenario-progress-bar' title='${esc(done)} / ${esc(total)}'><span style='width:${pct}%'></span></div>`;}
function scenarioProgressTypeLabel(branch){return branch.type==='reactive'?'reaction':(branch.required_for_completion?'required':'optional');}
function scenarioBranchWaitText(branchRuntime){
const t=branchRuntime&&branchRuntime.wait_type||'none';
if(t==='time')return `time until ${branchRuntime.wait_until_ms||0}`;
if(t==='event'||t==='any_events'||t==='all_events'){const events=Array.isArray(branchRuntime.wait_events)?branchRuntime.wait_events:[];if(events.length>1)return `${t==='all_events'?'all of':'any of'} ${events.length} events`;return branchRuntime.wait_event_type||branchRuntime.wait_event_id||'device event';}
if(t==='flags'){const flags=Array.isArray(branchRuntime.wait_flags)?branchRuntime.wait_flags:[];return flags.length?flags.map(flag=>`${flag.name||flag.flag_name||'flag'}=${flag.value?'true':'false'}`).join(', '):'flags';}
if(t==='operator')return branchRuntime.wait_operator_prompt||'operator approval';
return 'none';
}
function renderScenarioBranchSkipButton(room,branch,branchRuntime){
if(!room||!branchRuntime||branchRuntime.state!=='waiting'||!branchRuntime.wait_operator_skip_allowed)return '';
const label=branchRuntime.wait_operator_skip_label||'Skip wait';
const branchId=branchRuntime.id||branch.id||'';
return `<button data-room-scenario-runtime='next' data-room-id='${esc(room.room_id||'')}' data-room-scenario-branch='${esc(branchId)}'>${esc(label)}</button>`;
}
function renderScenarioActiveWaits(room,items){
const waits=items.filter(item=>item.runtime&&item.runtime.state==='waiting'&&item.runtime.wait_operator_skip_allowed);
if(!waits.length)return '';
return `<div class='scenario-active-waits'>${waits.map(item=>{
const skip=renderScenarioBranchSkipButton(room,item.branch,item.runtime);
return `<div class='scenario-active-wait'><div><span class='badge'>operator skip</span> <strong>${esc(item.branch.name||item.branch.id)}</strong><div class='row-meta'>${esc(scenarioBranchWaitText(item.runtime))}</div></div>${skip?`<div class='branch-runtime-actions'>${skip}</div>`:''}</div>`;
}).join('')}</div>`;
}
function renderScenarioProgressBranch(room,item){
const branch=item.branch;
const steps=Array.isArray(branch.steps)?branch.steps:[];
const branchRuntime=item.runtime;
const state=(branchRuntime&&branchRuntime.state)||(!branch.enabled?'disabled':'idle');
const waitType=(branchRuntime&&branchRuntime.wait_type)||'none';
const done=scenarioBranchDoneCount(room,branch,branchRuntime,item.start);
const current=scenarioBranchCurrentStep(branch,branchRuntime);
const detailsKey=`room-progress-steps:${room&&room.room_id||'room'}:${branch.id||item.index}`;
const skip=renderScenarioBranchSkipButton(room,branch,branchRuntime);
return `<section class='scenario-progress-branch ${!branch.enabled?'disabled':''} ${branch.type==='reactive'?'reactive':''} ${state}'><div class='scenario-progress-branch-head'><div class='scenario-progress-branch-main'><div class='scenario-progress-title-row'><div class='scenario-progress-branch-title'>${esc(branch.name||branch.id||`Branch ${item.index+1}`)}</div><span class='badge'>${esc(state)}</span></div><div class='row-meta'>${esc(done)} / ${esc(steps.length)} steps / ${esc(scenarioProgressTypeLabel(branch))}${waitType&&waitType!=='none'?` / waiting ${esc(waitType)}`:''}</div><div class='scenario-progress-current'>${esc(current)}</div>${scenarioProgressBar(done,steps.length)}</div>${skip?`<div class='branch-runtime-actions'>${skip}</div>`:''}</div><details class='scenario-progress-step-details' ${detailsAttrs(detailsKey,false)}><summary>Show steps</summary><div class='scenario-progress'>${steps.length?steps.map((step,stepIndex)=>renderScenarioProgressStep(room,step,stepIndex,item.start+stepIndex,branchRuntime)).join(''):`<div class='empty'>No steps</div>`}</div></details></section>`;
}
function renderScenarioProgressSection(title,items,mode){
if(!items.length)return '';
return `<div class='scenario-progress-section'><div class='scenario-progress-section-title'>${esc(title)}</div><div class='scenario-progress-branches ${esc(mode||'flow')}'>${items.map(item=>renderScenarioProgressBranch(item.room,item)).join('')}</div></div>`;
}
function renderScenarioProgress(room,scenarioOrSteps){
const branches=scenarioProgressBranches(scenarioOrSteps);
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
function noProfilesHtml(roomId){return isAdmin()?`<div class='empty'>No game modes for this room</div><div class='actions'><button data-open-admin-view='profiles' data-open-admin-room='${esc(roomId||'')}'>Create game mode</button></div>`:`<div class='empty'>No game modes available. Ask admin.</div>`;}
function noScenariosHtml(roomId){return isAdmin()?`<div class='empty'>No room scenarios</div><div class='actions'><button data-open-admin-view='scenarios' data-open-admin-room='${esc(roomId||'')}'>Create scenario</button></div>`:`<div class='empty'>No room scenarios</div>`;}
function applyInitialOperatorRoute(){if(gmInitialRouteApplied)return;gmInitialRouteApplied=true;if(isAdmin())return;const rooms=(gmState&&Array.isArray(gmState.rooms))?gmState.rooms:[];if(!rooms.length)return;if(currentView==='dashboard'||currentView==='rooms'){currentView='room';currentRoomId=currentRoomId||rooms[0].room_id;roomTab='control';}}
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
function clearScenarioDirty(){scenarioEditor.dirty=false;scenarioEditor.draft=null;scenarioEditor.validation_report=null;clearTransientFieldDirty();}
function clearQuestDeviceDirty(){questDeviceEditor.dirty=false;questDeviceEditor.draft=null;questDeviceEditor.discovery=null;clearTransientFieldDirty();}
function markProfileDirty(){profileEditor.dirty=true;}
function markScenarioDirty(){scenarioEditor.dirty=true;scenarioEditor.validation_report=null;if(document.getElementById('scenario_id'))scenarioEditor.draft=collectScenarioEditor();}
function syncScenarioDraftFromDom(){if(!scenarioEditor.dirty||currentView!=='scenarios'||!document.getElementById('scenario_id'))return;try{scenarioEditor.draft=collectScenarioEditor();}catch(err){}}
function skipNextScenarioDomSync(){gmSkipScenarioDomSync=true;}
function markQuestDeviceDirty(){questDeviceEditor.dirty=true;if(document.querySelector('[data-quest-device-editor]'))questDeviceEditor.draft=collectQuestDeviceEditor(false);}
// GM panel source part. Edit this file, then rebuild gm_panel.js.
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
</span></div><div class='kv'><span class='k'>Duration</span><span class='v'>${esc(selected?fmtClock(selected.duration_ms):'none')}</span></div></div><div style='height:12px'></div><div class='actions'><button class='approve' data-room-game='start' data-room-id='${esc(room.room_id)}' ${
canStart?'':'disabled'}
>Start game</button><button data-room-game='stop' data-room-id='${esc(room.room_id)}'>Stop game</button><button class='danger' data-room-game='reset' data-room-id='${esc(room.room_id)}'>Reset game</button></div>`:noProfilesHtml(room.room_id)}</div><div style='height:12px'></div>`;
}

function renderRoomOperatorConsole(room){
const profiles=roomProfiles(room.room_id);
const selectedId=roomSelectedProfileId(room.room_id)||room.selected_profile_id||'';
const selected=profiles.find(p=>p.id===selectedId)||null;
const selectedName=room.selected_profile_name||((selected&&selected.id===selectedId)?selected.name:'');
const scenarioId=room.selected_profile_scenario_id||((selected&&selected.scenario_id)||room.selected_scenario_id||'');
const scenario=roomSelectedScenarioObject(room);
const steps=roomScenarioSteps(room);
const runtime=room.scenario_runtime_state||'idle';
const waitType=room.scenario_wait_type||'none';
const hasBranchRuntime=Array.isArray(room.scenario_branches)&&room.scenario_branches.length>1;
const runningName=room.running_scenario_name||scenarioDisplayName(room.room_id,room.running_scenario_id||scenarioId,'none');
const currentStep=roomCurrentScenarioStep(room);
const currentStepText=currentStep?scenarioStepText(currentStep):scenarioStepLabel(room,steps.length);
const canStart=!!selected&&selected.valid!==false;
const canStop=room.session_present&&room.session_state!=='finished';
const canReset=room.session_present;
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
const clockState=room.timer_state||room.session_state||'idle';
const startMinutes=Math.max(1,Math.round(((Number(room.timer_duration_ms)||3600000)/60000)));
return `<div class='room-console' data-room-operator-console='1'><div class='card room-primary'><div class='card-head'><div><h2 class='section-title'>Game control</h2><div class='room-clock'>${fmtClock(room.timer_remaining_ms)}</div><div class='row-meta'>${esc(clockState)} / session ${esc(room.session_state||'idle')}</div></div>${status(roomDerivedHealth(room))}</div>${profiles.length?`<label class='field-stack'><span>Game mode</span><select class='scenario-select' data-room-profile-room='${esc(room.room_id)}'><option value='' ${
selected?'':'selected'}
>Select game mode</option>${
profiles.map(p=>`<option value='${esc(p.id)}' ${selected&&selected.id===p.id?'selected':''} ${p.valid===false?'disabled':''}>${esc(p.name||p.id)} (${fmtClock(p.duration_ms)}${p.valid===false?', invalid':''})</option>`).join('')}
</select></label><div class='kvs' style='margin-top:12px'><div class='kv'><span class='k'>Mode</span><span class='v'>${
esc(selectedName||selectedId||'none')}
</span></div><div class='kv'><span class='k'>Scenario</span><span class='v'> ${
esc(scenarioName(room.room_id,scenarioId))}
</span></div><div class='kv'><span class='k'>Duration</span><span class='v'>${esc(selected?fmtClock(selected.duration_ms):'none')}</span></div></div>`:noProfilesHtml(room.room_id)}<div style='height:12px'></div><div class='actions'><button class='approve' data-room-game='start' data-room-id='${esc(room.room_id)}' ${canStart?'':'disabled'}>Start game</button><button data-room-game='stop' data-room-id='${esc(room.room_id)}' ${canStop?'':'disabled'}>Stop game</button><button class='danger' data-room-game='reset' data-room-id='${esc(room.room_id)}' ${canReset?'':'disabled'}>Reset game</button></div></div><div class='card ${canApprove?'operator-gate':(waitType!=='none'?'room-wait':'')}'><h2 class='section-title'>Runtime</h2><div class='kvs'><div class='kv'><span class='k'>Scenario</span><span class='v'>${esc(runningName)}</span></div><div class='kv'><span class='k'>Runtime</span><span class='v'>${esc(runtime)}</span></div><div class='kv'><span class='k'>Step</span><span class='v'>${esc(scenarioStepLabel(room,steps.length))}</span></div><div class='kv'><span class='k'>Current</span><span class='v'>${esc(currentStepText)}</span></div><div class='kv'><span class='k'>Waiting</span><span class='v'>${esc(scenarioWaitText(room))}</span></div></div>${canApprove?`<div class='operator-prompt'>${
esc(waitPrompt)}
</div>`:''}${canSkipWait?`<div class='operator-prompt'>Operator override available: ${esc(skipWaitLabel)}</div>`:''}${room.scenario_operator_message?`<div class='operator-prompt'>${
esc(room.scenario_operator_message)}
</div>`:''}${flagsHtml}${room.scenario_last_error?`<div class='row-meta bad-text'>${
esc(room.scenario_last_error)}
</div>`:''}<div style='height:12px'></div><div class='actions'><button class='approve' data-room-scenario-runtime='approve' data-room-id='${esc(room.room_id)}' ${canApprove?'':'disabled'}>${esc(approveLabel)}</button>${canSkipWait?`<button data-room-scenario-runtime='next' data-room-id='${esc(room.room_id)}'>${esc(skipWaitLabel)}</button>`:''}<button data-room-timer='pause' data-room-id='${esc(room.room_id)}' ${canPause?'':'disabled'}>Pause</button><button data-room-timer='resume' data-room-id='${esc(room.room_id)}' ${canResume?'':'disabled'}>Resume</button><button data-room-timer='plus1' data-room-id='${esc(room.room_id)}' ${canAdjust?'':'disabled'}>+1 min</button><button data-room-timer='minus1' data-room-id='${esc(room.room_id)}' ${canAdjust?'':'disabled'}>-1 min</button></div><details class='scenario-advanced'><summary>Manual timer start</summary><div class='timer-start'><input id='gm_timer_minutes' type='number' min='1' step='1' value='${startMinutes}' placeholder='Minutes' aria-label='Duration in minutes'><button data-room-timer='start' data-room-id='${esc(room.room_id)}'>Start timer</button></div></details></div></div><div class='card'><h2 class='section-title'>Scenario progress</h2>${renderScenarioProgress(room,scenario||steps)}</div><div style='height:12px'></div>`;
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
<div style='height:12px'></div><div class='actions'><button data-room-scenario-runtime='start' data-room-id='${esc(room.room_id)}' ${
canStart?'':'disabled'}
>Start</button><button data-room-scenario-runtime='stop' data-room-id='${esc(room.room_id)}' ${
canRun?'':'disabled'}
>Stop</button><button class='approve' data-room-scenario-runtime='approve' data-room-id='${esc(room.room_id)}' ${
canApprove?'':'disabled'}
>${
esc(approveLabel)}
</button><button class='danger' data-room-scenario-runtime='next' data-room-id='${esc(room.room_id)}' ${
canNext?'':'disabled'}
>Next</button><button data-room-scenario-runtime='reset' data-room-id='${esc(room.room_id)}' ${
canRun?'':'disabled'}
>Reset</button></div>`:noScenariosHtml(room.room_id)}</details><div style='height:12px'></div>`;
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
return `<div class='tabs'>${names.map(n=>`<button class='tab-btn ${active===n?'active':''}' data-tab-scope='${scope}' data-tab='${n}'>${
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
const current=roomCurrentScenarioStep(room);
const issues=roomQuestDeviceIssues(room).length+(Number(room.issue_count)||0);
const devices=roomScenarioDeviceIds(room).length||room.device_count||0;
const runtime=room.scenario_runtime_state||room.session_state||'idle';
return `<tr class='clickable-row' data-open-room='${esc(room.room_id)}'><td><strong>${esc(room.title||room.name||room.room_id)}</strong><span>${esc(room.room_id||'')}</span></td><td>${status(roomDerivedHealth(room))}</td><td><strong>${esc(gameName)}</strong><span>${esc(scenarioNameText)}</span></td><td>${esc(fmtClock(room.timer_remaining_ms))}</td><td>${esc(runtime)}</td><td>${esc(current?scenarioStepText(current):'none')}</td><td>${esc(scenarioWaitText(room))}</td><td>${esc(devices)}</td><td>${esc(issues)}</td><td class='observed-actions'><button class='small-btn' data-open-room='${esc(room.room_id)}'>Open</button></td></tr>`;
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
const questIssues=rooms.reduce((out,room)=>out.concat(roomQuestDeviceIssues(room).map(issue=>Object.assign({room_id:room.room_id},issue))),[]);
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
const create=isAdmin()?`<div class='actions' style='margin-bottom:14px'><button data-room-new='1'>Create room</button></div>`:'';
return `${create}<div class='grid auto'>${rooms.length?rooms.map(roomCard).join(''):`<div class='card empty'>No rooms</div>`}</div>`;
}

function renderRoomView(){
const room=roomById(currentRoomId)||((gmState&&gmState.rooms&&gmState.rooms[0])?gmState.rooms[0]:null);
if(room)currentRoomId=room.room_id;
setPage(room?`Room: ${room.title||room.room_id}`:'Room','Room control');
if(!room)return `<div class='card empty'>No room selected</div>`;
const adminActions=isAdmin()?`<div class='actions' style='margin-bottom:14px'><button class='danger' data-room-delete='${esc(room.room_id)}'>Delete room</button></div>`:'';
const devs=roomDevices(room.room_id);
const questIds=roomScenarioDeviceIds(room);
const questDevs=questIds.map(id=>questDeviceById(id)).filter(Boolean);
const issues=roomIssues(room.room_id).concat(roomQuestDeviceIssues(room));
const canReset=room.session_present;
const canFinish=room.session_present&&room.session_state!=='finished';
const canScenarioNext=(room.selected_scenario_id||room.running_scenario_id)&&(room.scenario_runtime_state==='running'||room.scenario_runtime_state==='waiting');
let body='';
if(roomTab==='overview'){
body=`<div class='grid cols-2'><div class='card'><div class='card-head'><div><div class='card-title'>Room state</div><div class='card-sub'>${esc(room.title||room.name||'Room')}</div></div>${status(roomDerivedHealth(room))}</div><div class='kvs'><div class='kv'><span class='k'>Timer</span><span class='v'>${fmtClock(room.timer_remaining_ms)}</span></div><div class='kv'><span class='k'>Session</span><span class='v'>${esc(room.session_state||'idle')}</span></div><div class='kv'><span class='k'>Scenario devices</span><span class='v'>${esc(questDevs.length)}</span></div><div class='kv'><span class='k'>Hints</span><span class='v'>${esc(room.hint_sent_count||0)}</span></div></div></div><div class='card'><h2 class='section-title'>Problems</h2><div class='list'>${issues.length?issues.slice(0,4).map(issueRow).join(''):`<div class='empty'>No room issues</div>`}</div></div></div>`;
}
else if(roomTab==='devices'){
const questRows=questDevs.length?questDevs.map(questDeviceMonitorRow).join(''):`<div class='card empty'>No quest devices referenced by selected scenario</div>`;
body=`<section><h2 class='section-title'>Scenario devices</h2><div class='list'>${questRows}</div></section>`;
}
else if(roomTab==='issues'){
body=`<div class='list'>${issues.length?issues.map(issueRow).join(''):`<div class='card empty'>No issues for this room</div>`}</div>`;
}
else{
body=`${renderRoomOperatorConsole(room)}${isAdmin()?renderRoomScenarioControl(room):''}<div class='grid cols-2'><div class='card'><h2 class='section-title'>Hint</h2><div class='hint-row'><input id='gm_hint_input' value='${esc(room.hint_message||'')}' placeholder='Hint for players / operator note'><button data-room-hint='send' data-room-id='${esc(room.room_id)}'>Send hint</button><button data-room-hint='clear' data-room-id='${esc(room.room_id)}' ${room.hint_active?'':'disabled'}>Clear</button></div></div><div class='card'><h2 class='section-title'>Device issues</h2><div class='list'>${issues.length?issues.slice(0,5).map(issueRow).join(''):`<div class='empty'>No room issues</div>`}</div></div></div><details class='scenario-advanced'><summary>Emergency controls</summary><div class='actions'><button data-room-game='stop' data-room-id='${esc(room.room_id)}' ${canFinish?'':'disabled'}>Stop game</button><button data-room-timer='reset' data-room-id='${esc(room.room_id)}' ${canReset?'':'disabled'}>Reset timer</button><button class='danger' data-room-timer='finish' data-room-id='${esc(room.room_id)}' ${canFinish?'':'disabled'}>Finish session</button><button class='danger' data-room-scenario-runtime='next' data-room-id='${esc(room.room_id)}' ${canScenarioNext?'':'disabled'}>Force next step</button></div></details>`;
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
const setupAction=isAdmin()?`<button data-open-device-setup='new'>Add device</button>`:'';
const questRows=savedQuestDevices.length?savedQuestDevices.map(d=>{const observedClient=observedByClientId(d.client_id||d.id);const health=questDeviceHealth(d);const caps=`${(d.commands||[]).length} cmd / ${(d.events||[]).length} evt`;const setup=isAdmin()?`<button class='small-btn' data-open-device-setup='${esc(d.id||'1')}'>Setup</button>`:'';return `<tr><td><strong>${esc(questDeviceDisplayName(d))}</strong><span>${esc(d.id||'')}</span></td><td>${status(health)}</td><td>${esc(questDeviceStatusText(d))}</td><td>${esc(d.client_id||'none')}</td><td>${esc(caps)}</td><td>${observedClient?`${esc(observedClient.connectivity||'unknown')} / fw ${esc(observedClient.fw_version||'n/a')}`:'not observed'}</td><td>${d.enabled===false?'<span class="badge">disabled</span>':'<span class="badge selected-badge">enabled</span>'}</td><td class='observed-actions'>${setup}</td></tr>`;}).join(''):`<tr><td colspan='8' class='observed-empty'>No saved quest devices${isAdmin()?` <button class='small-btn' data-open-device-setup='new'>Add device</button>`:''}</td></tr>`;
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
const action=reg&&reg.via==='quest_device'?`<button class='small-btn' data-open-device-setup='${esc(reg.device_id)}'>Setup</button>`:(reg?`<span class='muted'>linked</span>`:`<button class='small-btn' data-open-device-setup='new'>Add</button>`);
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
const scenarioHelp=!scenarios.length?`<div class='empty'>Create a room scenario before saving a game mode.</div><div class='actions'><button data-open-admin-view='scenarios' data-open-admin-room='${esc(roomId)}'>Create scenario</button></div>`:(scenarioMissing?`<div class='row-meta bad-text'>Selected scenario is missing. Choose another scenario before saving.</div>`:(scenarioInvalid?`<div class='row-meta bad-text'>Selected scenario has validation errors.</div>`:''));
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
return `<div class='row-card profile-row ${selected?'selected-row':''}'><div class='row-main'><div class='row-title'>${esc(p.name||p.id)} ${selected?`<span class='badge selected-badge'>selected</span>`:''} ${disabled?`<span class='badge'>disabled</span>`:''} ${invalid?`<span class='badge scenario-issue-badge error'>invalid</span>`:''}</div><div class='profile-mode-summary'><span>${esc(scenarioName(roomId,p.scenario_id))}</span><span>${esc(fmtClock(p.duration_ms))}</span></div></div><div class='actions'><button data-profile-edit='${esc(p.id)}'>Edit</button><button data-profile-select='${esc(p.id)}' ${selected||invalid||disabled?'disabled':''}>Select</button><button class='danger' data-profile-delete='${esc(p.id)}'>Delete</button></div></div>`;
}).join(''):`<div class='card empty'>No game modes for this room</div>`;
const saveDisabled=!scenarios.length||scenarioMissing||scenarioInvalid;
const editorHtml=editorOpen?`<div class='card'><div class='card-head'><div><h2 class='section-title'>${editing?'Edit game mode':'New game mode'}${profileEditor.dirty?' *':''}</h2><div class='card-sub'>A game mode selects one scenario and game duration for operators.</div></div><label class='row-meta'><input id='profile_enabled' type='checkbox' ${enabled?'checked':''} style='min-width:auto'> Enabled</label></div><div class='field-grid'><label class='field-stack'><span>Mode name</span><input id='profile_name' placeholder='Garri Potter' value='${esc(modeName)}'></label><label class='field-stack'><span>Duration, min</span><input id='profile_duration' type='number' min='1' step='1' placeholder='60' value='${minutes}'></label></div><div class='form-section'><label class='field-stack'><span>Scenario</span><select id='profile_scenario' class='scenario-select'>${scenarioOptions}</select></label><div class='profile-selected-summary'><div><span>Scenario</span><strong>${esc(scenarioName(roomId,scenarioValue))}</strong></div><div><span>Duration</span><strong>${esc(fmtClock(minutes*60000))}</strong></div></div></div><details class='scenario-advanced'><summary>Advanced</summary><div class='row'><input id='profile_id' placeholder='Mode ID' value='${esc(modeId)}'></div><div class='row'><input id='profile_hint_pack' placeholder='Hint pack ID' value='${esc(hintPack)}'><input id='profile_audio_pack' placeholder='Audio pack ID' value='${esc(audioPack)}'></div></details>${scenarioHelp}<div style='height:12px'></div><div class='actions'><button data-profile-save='1' ${saveDisabled?'disabled':''}>Save game mode</button>${editing?`<button data-profile-select='${esc(editing.id)}' ${editing.id===selectedProfileId||saveDisabled||enabled===false?'disabled':''}>Select for room</button>`:''}</div><div id='profile_editor_status' class='row-meta'></div></div>`:`<div class='card empty'><h2 class='section-title'>Game mode editor</h2><div class='row-meta'>Select a game mode or create a new one.</div></div>`;
return `<div class='scenario-room-bar'><div><h2 class='section-title'>Room</h2><select class='scenario-select' data-profile-room-select>${rooms.map(r=>`<option value='${esc(r.room_id)}' ${r.room_id===roomId?'selected':''}>${esc(r.title||r.room_id)}</option>`).join('')}</select></div><div class='row-meta'>Selected: <strong>${esc((selectedProfile&&(selectedProfile.name||selectedProfile.id))||'none')}</strong></div></div><div class='profile-admin-layout'><section><div class='card-head'><h2 class='section-title'>Game modes</h2><div class='actions'><button data-profile-new='1'>Add game mode</button></div></div><div class='list'>${profileRows}</div></section><section>${editorHtml}</section></div>`;
}
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
// GM panel source part. Edit this file, then rebuild gm_panel.js.
function renderStorageAdminView(){
setPage('Storage','Import, export, save and load');
return `<div class='grid cols-2'><div class='card'><h2 class='section-title'>Quest devices</h2><div class='actions'><button data-storage-action='device_save'>Save to SD</button><button data-storage-action='device_load'>Load from SD</button><button data-storage-action='device_export'>Export JSON</button></div><div style='height:12px'></div><div class='row'><input id='storage_devices_file' type='file' accept='.json,application/json'><button data-storage-action='device_import'>Import JSON</button></div><div class='row-meta'>Path: /sdcard/quest/quest_devices.json</div></div><div class='card'><h2 class='section-title'>Room scenarios</h2><div class='actions'><button data-storage-action='scenario_save'>Save to SD</button><button data-storage-action='scenario_load'>Load from SD</button><button data-storage-action='scenario_export'>Export JSON</button></div><div style='height:12px'></div><div class='row'><input id='storage_scenarios_file' type='file' accept='.json,application/json'><button data-storage-action='scenario_import'>Import JSON</button></div><div class='row-meta'>Path: /sdcard/quest/room_scenarios.json</div></div><div class='card'><h2 class='section-title'>Game modes</h2><div class='actions'><button data-storage-action='profile_save'>Save to SD</button><button data-storage-action='profile_load'>Load from SD</button><button data-storage-action='profile_export'>Export JSON</button></div><div style='height:12px'></div><div class='row'><input id='storage_profiles_file' type='file' accept='.json,application/json'><button data-storage-action='profile_import'>Import JSON</button></div><div class='row-meta'>Path: /sdcard/quest/game_profiles.json</div></div></div>`;
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
return `<div class='builder-step'><div class='card-head'><div><h2 class='section-title'>Discovered config</h2><div class='row-meta'>${esc(d.client_id||'')} / ${commands.length} commands / ${events.length} events</div></div><div class='actions'><button data-quest-discovery-apply='1'>Import</button><button data-quest-discovery-discard='1'>Discard</button></div></div><div class='kvs'><div class='kv'><span class='k'>Commands</span><span class='v'>${esc(commands.map(c=>c.label||c.id).join(', ')||'none')}</span></div><div class='kv'><span class='k'>Events</span><span class='v'>${esc(events.map(e=>e.label||e.id).join(', ')||'none')}</span></div></div></div>`;
}

function renderQuestCommandRow(cmd,index){
const c=cmd||{};
const params=Array.isArray(c.params_schema)?c.params_schema:[];
const paramsNote=params.length?(commandSupportsScenarioParams(c)?`<div class='row-meta'>Params: ${esc(params.map(p=>p.label||p.key).join(', '))}</div>`:`<div class='row-meta warn-text'>Params are advertised by the client, but MQTT commands currently publish the static payload below.</div>`):'';
return `<div class='builder-step' data-quest-command='${index}'><div class='builder-step-head'><div class='builder-step-title'>Command ${index+1}${params.length?` <span class='badge'>${commandSupportsScenarioParams(c)?`${params.length} params`:'static payload'}</span>`:''}</div><div class='actions'><button class='danger' data-quest-command-delete='${index}'>Delete</button></div></div><div class='row'><input data-quest-command-field='label' placeholder='Button label' value='${esc(c.label||'')}'><input data-quest-command-field='topic' placeholder='MQTT topic' value='${esc(c.topic||'')}'></div><div class='row'><input data-quest-command-field='payload' placeholder='Payload' value='${esc(c.payload||'')}'></div><details class='scenario-advanced'><summary>Advanced</summary><div class='row'><input data-quest-command-field='id' placeholder='Command ID' value='${esc(c.id||'')}'><input data-quest-command-field='kind' placeholder='Kind' value='${esc(c.kind||'mqtt_publish')}'></div>${paramsNote}</details><label class='row-meta'><input data-quest-command-field='button_enabled' type='checkbox' ${c.button_enabled!==false?'checked':''} style='min-width:auto'> Show as manual button</label><label class='row-meta'><input data-quest-command-field='dangerous' type='checkbox' ${c.dangerous?'checked':''} style='min-width:auto'> Require confirmation</label></div>`;
}

function renderQuestEventRow(ev,index){
const e=ev||{};
return `<div class='builder-step' data-quest-event='${index}'><div class='builder-step-head'><div class='builder-step-title'>Event ${index+1}</div><div class='actions'><button class='danger' data-quest-event-delete='${index}'>Delete</button></div></div><div class='row'><input data-quest-event-field='label' placeholder='Event label' value='${esc(e.label||'')}'><input data-quest-event-field='topic' placeholder='MQTT topic' value='${esc(e.topic||'')}'></div><div class='row'><input data-quest-event-field='payload' placeholder='Payload filter' value='${esc(e.payload||'')}'><input data-quest-event-field='event_type' placeholder='Event type' value='${esc(e.event_type||'')}'></div><details class='scenario-advanced'><summary>Advanced</summary><div class='row'><input data-quest-event-field='id' placeholder='Event ID' value='${esc(e.id||'')}'></div></details></div>`;
}

function renderQuestDeviceListRow(d){
const health=questDeviceHealth(d);
return `<div class='row-card'><div class='row-main'><div class='row-title'>${esc(d.name||d.id)} ${d.enabled===false?`<span class='badge'>disabled</span>`:''}</div><div class='row-meta'>${esc((d.commands||[]).length)} commands / ${esc((d.events||[]).length)} events</div><div class='row-meta'>${esc(questDeviceStatusText(d))}</div><details class='scenario-advanced'><summary>Debug ids</summary><div class='row-meta'>Device ID: ${esc(d.id||'')}</div><div class='row-meta'>Client: ${esc(d.client_id||'')}</div></details></div><div>${status(health)}</div><div class='actions'><button data-quest-device-edit='${esc(d.id)}'>Edit</button><button class='danger' data-quest-device-delete='${esc(d.id)}'>Delete</button></div></div>`;
}

function renderQuestDeviceEditor(draft){
if(!draft){
return `<div class='card empty-state'><h2 class='section-title'>Device editor</h2><div class='empty-title'>Select a quest device or create a new one</div><div class='row-meta'>Quest devices are physical client capabilities: commands, events and manual buttons. They are used later in room scenarios.</div><div class='actions'><button data-quest-device-new='1'>Add device</button></div></div>`;
}
const clientControl=observedItems().length?`<select class='scenario-select' data-quest-device-field='client_id'>${physicalClientOptions(draft&&draft.client_id||'')}</select>`:`<input data-quest-device-field='client_id' placeholder='Physical client ID' value='${esc(draft&&draft.client_id||'')}'>`;
const commandRows=(draft.commands||[]).length?draft.commands.map(renderQuestCommandRow).join(''):`<div class='empty'>No commands. Import config from the client or add a command manually.</div>`;
const eventRows=(draft.events||[]).length?draft.events.map(renderQuestEventRow).join(''):`<div class='empty'>No events. Import config from the client or add an event manually.</div>`;
return `<div class='card' data-quest-device-editor='1'><div class='card-head'><div><h2 class='section-title'>${questDeviceEditor.device_id?'Edit quest device':'New quest device'}${questDeviceEditor.dirty?' *':''}</h2><div class='card-sub'>Define what this physical client can do and report.</div></div><label class='row-meta'><input data-quest-device-field='enabled' type='checkbox' ${draft.enabled!==false?'checked':''} style='min-width:auto'> Enabled</label></div><div class='form-section'><h2 class='section-title'>Basics</h2><div class='field-grid'><label class='field-stack'><span>Device name</span><input data-quest-device-field='name' placeholder='Altar controller' value='${esc(draft.name||'')}'></label><label class='field-stack'><span>Physical client</span>${clientControl}</label></div><details class='scenario-advanced'><summary>Advanced</summary><div class='row'><input data-quest-device-field='id' placeholder='Device ID' value='${esc(draft.id||'')}'></div></details></div><div class='form-section import-panel'><div><h2 class='section-title'>Import capabilities</h2><div class='row-meta'>Ask the selected physical client for its supported commands and events.</div></div><div class='actions'><button class='approve' data-quest-device-discover='1'>Get config</button></div></div>${renderQuestDiscoveryPreview()}<div class='form-section'><div class='card-head'><div><h2 class='section-title'>Commands</h2><div class='row-meta'>Commands can become scenario actions and manual buttons.</div></div><div class='actions'><button data-quest-command-add='1'>Add command</button></div></div><div>${commandRows}</div></div><div class='form-section'><div class='card-head'><div><h2 class='section-title'>Events</h2><div class='row-meta'>Events are available as scenario waits.</div></div><div class='actions'><button data-quest-event-add='1'>Add event</button></div></div><div>${eventRows}</div></div><div class='actions sticky-actions'><button data-quest-device-save='1'>Save device</button>${questDeviceEditor.device_id?`<button class='danger' data-quest-device-delete='${esc(questDeviceEditor.device_id)}'>Delete</button>`:''}</div></div>`;
}

function renderDeviceSetupAdminView(){
setPage('Quest Devices','Device capabilities and manual controls');
const devices=questEditableDevices();
const draft=questDeviceEditor.open?currentQuestDeviceDraft():null;
const rows=devices.length?devices.map(renderQuestDeviceListRow).join(''):`<div class='card empty-state'><div class='empty-title'>No quest devices yet</div><div class='row-meta'>Add a device, select its physical client and import capabilities.</div><div class='actions'><button data-quest-device-new='1'>Add device</button></div></div>`;
return `<div class='device-setup-layout'><section><div class='card-head'><div><h2 class='section-title'>Quest devices</h2><div class='card-sub'>Saved device capability sets</div></div><div class='actions'><button data-quest-device-new='1'>Add device</button></div></div><div class='list'>${rows}</div></section><section>${renderQuestDeviceEditor(draft)}</section></div>`;
}

function initDeviceSetupWizard(){
return;
}

function render(){
const root=document.getElementById('gm_content');
if(!root)return;
if(gmSkipScenarioDomSync)gmSkipScenarioDomSync=false;
else syncScenarioDraftFromDom();
applyGMRoleLayout();
const summary=gmState&&gmState.summary?gmState.summary:{
}
;
setStatus(summary.has_fault?'fault':(summary.has_degraded?'degraded':'ok'),summary.has_fault?'state-fault':(summary.has_degraded?'state-degraded':'state-ok'));
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
else if(currentView==='storage')html=renderStorageAdminView();
root.innerHTML=html;
injectRoomScenarios();
const navView=currentView==='room'?'rooms':currentView;

document.querySelectorAll('.nav-btn').forEach(b=>b.classList.toggle('active',b.dataset.view===navView));
renderRightSidebar();
}
// GM panel source part. Edit this file, then rebuild gm_panel.js.
async function loadObserved(){
try{
const res=await gmFetch('/api/orchestrator/control/devices');
gmObserved=res.ok?await res.json():null;
}
catch(err){
gmObserved=null;
}
}

async function loadAudit(){
try{
const res=await gmFetch('/api/orchestrator/audit/recent');
gmAudit=res.ok?await res.json():null;
}
catch(err){
gmAudit=null;
}
}

async function loadTimeline(){
try{
const res=await gmFetch('/api/orchestrator/timeline/recent');
gmTimeline=res.ok?await res.json():null;
}
catch(err){
gmTimeline=null;
}
}

async function loadQuestDevices(){
try{
const res=await gmFetch('/api/gm/devices?include_system=1');
gmQuestDevices=res.ok?await res.json():null;
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
const res=await gmFetch(`/api/files?path=${encodeURIComponent(path)}`);
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
await loadQuestDevices();
renderRightSidebar();
};

async function loadRoomScenarios(){
gmRoomScenarios={
}
;
const rooms=(gmState&&Array.isArray(gmState.rooms))?gmState.rooms:[];
await Promise.all(rooms.map(async r=>{
try{
const res=await gmFetch(`/api/gm/room/scenarios?room_id=${encodeURIComponent(r.room_id)}`);const data=res.ok?await res.json():null;gmRoomScenarios[r.room_id]=(data&&Array.isArray(data.scenarios))?data.scenarios:[];}
catch(err){
gmRoomScenarios[r.room_id]=[];}
}
));
}

async function loadRoomProfiles(){
gmRoomProfiles={
}
;
const rooms=(gmState&&Array.isArray(gmState.rooms))?gmState.rooms:[];
await Promise.all(rooms.map(async r=>{
try{
const res=await gmFetch(`/api/gm/room/profiles?room_id=${encodeURIComponent(r.room_id)}`);const data=res.ok?await res.json():null;gmRoomProfiles[r.room_id]=data&&Array.isArray(data.profiles)?data:{
profiles:[],selected_profile_id:''}
;}
catch(err){
gmRoomProfiles[r.room_id]={
profiles:[],selected_profile_id:''}
;}
}
));
}

async function loadScenarioEditorCatalogs(){
gmScenarioEditorCatalogs={
}
;
if(!isAdmin())return;
const rooms=(gmState&&Array.isArray(gmState.rooms))?gmState.rooms:[];
await Promise.all(rooms.map(async r=>{
try{
const res=await gmFetch(`/api/gm/room/scenario-editor/catalog?room_id=${encodeURIComponent(r.room_id)}`);const data=res.ok?await res.json():null;gmScenarioEditorCatalogs[r.room_id]=data&&Array.isArray(data.quest_devices)?data:{
quest_devices:[],step_schemas:[]}
;}
catch(err){
gmScenarioEditorCatalogs[r.room_id]={
quest_devices:[],step_schemas:[]}
;}
}
));
}

async function loadGM(silent,forceRender){
if(!silent){
setStatus('loading','state-unknown');
}
try{
const res=await gmFetch('/api/gm/state');
if(!res.ok)throw new Error('HTTP '+res.status);
gmState=await res.json();
await Promise.all([loadObserved(),loadAudit(),loadTimeline(),loadQuestDevices(),loadRoomScenarios(),loadRoomProfiles(),loadScenarioEditorCatalogs()]);
applyInitialOperatorRoute();
if(silent&&!forceRender&&shouldDeferAutoRender()){
gmAutoRenderDeferred=true;
renderRightSidebar();
return;
}
gmAutoRenderDeferred=false;
render();
}
catch(err){
setStatus('load failed','state-fault');

document.getElementById('gm_content').innerHTML='<div class="card empty">Failed to load GM state</div>';
renderRightSidebar();
}
}

async function runManualDeviceCommand(deviceId,commandId){
if(!deviceId||!commandId)throw new Error('Manual button is incomplete');
setGMStatus('Triggering button...');
const res=await gmFetch('/api/gm/device/command/run',{
method:'POST',headers:{
'Content-Type':'application/json'}
,body:JSON.stringify({
device_id:deviceId,command_id:commandId}
)}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
clearTransientFieldDirty();
await loadGM(true,true);
setGMStatus('Button sent','gm-ok');
}

async function createRoomFromPrompt(){
if(!isAdmin())return;
const name=(prompt('Room name')||'').trim();
if(!name)return;
const roomId=slugifyId(name,'room');
setGMStatus('Saving room...');
const res=await gmFetch('/api/gm/room/save',{
method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({room_id:roomId,name})}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
clearTransientFieldDirty();
await loadGM(true,true);
if(typeof window.__gmRefreshManualSidebar==='function'){
await window.__gmRefreshManualSidebar();
}
setGMStatus('Room saved','gm-ok');
}

async function deleteRoom(roomId){
if(!isAdmin())return;
if(!roomId)throw new Error('Room is not selected');
const room=roomById(roomId);
const name=room&&(room.title||room.name)||roomId;
if(!confirm(`Delete room ${name}? This also removes profiles and scenarios for this room. Quest devices stay untouched.`))return;
setGMStatus('Deleting room...');
const res=await gmFetch('/api/gm/room/delete',{
method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({room_id:roomId,delete_content:true})}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
currentRoomId='';
delete currentRoomProfileId[roomId];
delete currentRoomScenarioId[roomId];
roomTab='control';
clearTransientFieldDirty();
await loadGM(true,true);
currentView='rooms';
render();
setGMStatus('Room deleted','gm-ok');
}

async function runRoomTimer(action,roomId){
let url='';
if(action==='start'){
const input=document.getElementById('gm_timer_minutes');
const minutes=Number(input&&input.value);
if(!Number.isFinite(minutes)||minutes<=0)throw new Error('Duration must be greater than 0');
const durationMs=Math.round(minutes*60000);
url=`/api/gm/room/timer/start?room_id=${encodeURIComponent(roomId)}&duration_ms=${durationMs}`;
}
else if(action==='pause'){
url=`/api/gm/room/timer/pause?room_id=${encodeURIComponent(roomId)}`;
}
else if(action==='resume'){
url=`/api/gm/room/timer/resume?room_id=${encodeURIComponent(roomId)}`;
}
else if(action==='reset'){
url=`/api/gm/room/timer/reset?room_id=${encodeURIComponent(roomId)}`;
}
else if(action==='finish'){
url=`/api/gm/room/session/finish?room_id=${encodeURIComponent(roomId)}`;
}
else if(action==='plus1'){
url=`/api/gm/room/timer/add?room_id=${encodeURIComponent(roomId)}&delta_ms=60000`;
}
else if(action==='minus1'){
url=`/api/gm/room/timer/add?room_id=${encodeURIComponent(roomId)}&delta_ms=-60000`;
}
else{
throw new Error('Unsupported timer action');
}
setGMStatus('Updating timer...');
const res=await gmFetch(url,{
method:'POST'}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
clearTransientFieldDirty();
await loadGM(true,true);
setGMStatus('Timer updated','gm-ok');
}

async function runRoomHint(action,roomId){
if(action==='send'){
const input=document.getElementById('gm_hint_input');
const message=(input&&input.value||'').trim();
if(!message)throw new Error('Hint message is empty');
setGMStatus('Sending hint...');
const res=await gmFetch('/api/gm/room/hint/send',{
method:'POST',headers:{
'Content-Type':'application/json'}
,body:JSON.stringify({
room_id:roomId,message}
)}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
}
else if(action==='clear'){
setGMStatus('Clearing hint...');
const res=await gmFetch(`/api/gm/room/hint/clear?room_id=${encodeURIComponent(roomId)}`,{
method:'POST'}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
}
else{
throw new Error('Unsupported hint action');
}
clearTransientFieldDirty();
await loadGM(true,true);
setGMStatus('Hint updated','gm-ok');
}

async function selectRoomProfile(roomId,profileId){
if(!roomId||!profileId)throw new Error('Game mode selection is incomplete');
setGMStatus('Selecting game mode...');
const res=await gmFetch('/api/gm/room/profile/select',{
method:'POST',headers:{
'Content-Type':'application/json'}
,body:JSON.stringify({
room_id:roomId,profile_id:profileId}
)}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
currentRoomProfileId[roomId]=profileId;
clearTransientFieldDirty();
await loadGM(true,true);
setGMStatus('Game mode selected','gm-ok');
}

async function runRoomGame(action,roomId){
if(!roomId||!action)throw new Error('Game command is incomplete');
if(action==='stop'&&!confirm('Stop this game session?'))return;
if(action==='reset'&&!confirm('Reset this game session?'))return;
setGMStatus('Updating game...');
const res=await gmFetch(`/api/gm/room/game/${encodeURIComponent(action)}?room_id=${encodeURIComponent(roomId)}`,{
method:'POST'}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
clearTransientFieldDirty();
await loadGM(true,true);
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
const topic=(get('topic')&&get('topic').value||'').trim();
const payload=(get('payload')&&get('payload').value||'');
const kind=(get('kind')&&get('kind').value||'mqtt_publish').trim()||'mqtt_publish';
const buttonEnabled=!!(get('button_enabled')&&get('button_enabled').checked);
const dangerous=!!(get('dangerous')&&get('dangerous').checked);
const existing=baseCommands.find(c=>(c.id||'')===(rawCmdId||cmdId))||baseCommands[Number(row.dataset.questCommand)]||{};
const paramsSchema=Array.isArray(existing.params_schema)?existing.params_schema:[];
if(label||rawCmdId||topic||payload){
commands.push({id:cmdId,label:label||cmdId,kind,topic,payload,button_enabled:buttonEnabled,dangerous,params_schema:paramsSchema});
}
});
const events=[];
root.querySelectorAll('[data-quest-event]').forEach(row=>{
const get=k=>row.querySelector(`[data-quest-event-field="${k}"]`);
const label=(get('label')&&get('label').value||'').trim();
const rawEventId=(get('id')&&get('id').value||'').trim();
const eventId=rawEventId||slugifyId(label,'event');
const topic=(get('topic')&&get('topic').value||'').trim();
const payload=(get('payload')&&get('payload').value||'');
const eventType=(get('event_type')&&get('event_type').value||eventId).trim()||eventId;
if(label||rawEventId||topic||payload){
events.push({id:eventId,label:label||eventId,topic,payload,event_type:eventType});
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
return {
id,
label:label||id,
kind:String(item&&item.kind||'mqtt_publish').trim()||'mqtt_publish',
topic:String(item&&item.topic||'').trim(),
payload:String(item&&item.payload||''),
button_enabled:item&&item.button_enabled===false?false:true,
dangerous:!!(item&&item.dangerous),
params_schema:normalizeQuestParamSchema(item&&item.params_schema)
};
}

function normalizeDiscoveredEvent(item,index){
const label=String(item&&item.label||item&&item.name||item&&item.id||`Event ${index+1}`).trim();
const id=String(item&&item.id||slugifyId(label,'event')).trim();
return {
id,
label:label||id,
topic:String(item&&item.topic||'').trim(),
payload:String(item&&item.payload||''),
event_type:String(item&&item.event_type||id).trim()||id
};
}

function questDeviceFromDiscoveredInterface(clientId,iface){
const base=collectQuestDeviceEditor(false);
const name=(base.name||iface&&iface.name||iface&&iface.label||clientId||'Quest device').trim();
const id=(base.id||questDeviceEditor.device_id||slugifyId(name,'device')).trim();
const commands=(Array.isArray(iface&&iface.commands)?iface.commands:[]).map(normalizeDiscoveredCommand).filter(c=>c.id);
const events=(Array.isArray(iface&&iface.events)?iface.events:[]).map(normalizeDiscoveredEvent).filter(ev=>ev.id);
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
const res=await gmFetch('/api/gm/device/describe-interface',{
method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({client_id:clientId})}
);
let body=null;
try{body=await res.json();}catch(err){}
if(!res.ok){
const msg=body&&(body.message||body.error||body.code);
throw new Error(msg||('HTTP '+res.status));
}
const iface=body&&body.quest_interface;
if(!iface||typeof iface!=='object')throw new Error('Device returned no quest_interface');
const device=questDeviceFromDiscoveredInterface(clientId,iface);
questDeviceEditor.draft=current;
questDeviceEditor.discovery={client_id:clientId,quest_interface:iface,device};
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
questDeviceEditor.draft.commands.push({id:'',label:'',kind:'mqtt_publish',topic:'',payload:'',button_enabled:true,dangerous:false,params_schema:[]});
markQuestDeviceDirty();
render();
}

function addQuestDeviceEvent(){
setQuestDeviceDraftFromEditor();
questDeviceEditor.draft.events=Array.isArray(questDeviceEditor.draft.events)?questDeviceEditor.draft.events:[];
questDeviceEditor.draft.events.push({id:'',label:'',topic:'',payload:'',event_type:''});
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
const res=await gmFetch('/api/gm/device/save',{
method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({device})}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
questDeviceEditor.device_id=device.id;
questDeviceEditor.open=true;
clearQuestDeviceDirty();
await loadGM(true,true);
if(typeof window.__gmRefreshManualSidebar==='function')await window.__gmRefreshManualSidebar();
setGMStatus('Device saved','gm-ok');
}

async function deleteQuestDeviceEditor(deviceId){
if(!isAdmin())throw new Error('Admin role required');
if(!deviceId)return;
if(!confirm(`Delete device ${deviceId}?`))return;
setGMStatus('Deleting device...');
const res=await gmFetch('/api/gm/device/delete',{
method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({device_id:deviceId})}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
if(questDeviceEditor.device_id===deviceId){
questDeviceEditor.device_id='';
questDeviceEditor.open=false;
}
clearQuestDeviceDirty();
await loadGM(true,true);
if(typeof window.__gmRefreshManualSidebar==='function')await window.__gmRefreshManualSidebar();
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
setGMStatus('Saving game mode...');
const res=await gmFetch('/api/gm/room/profile/save',{
method:'POST',headers:{
'Content-Type':'application/json'}
,body:JSON.stringify({
profile}
)}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
profileEditor.profile_id=id;
profileEditor.open=true;
clearProfileDirty();
await loadGM(true,true);
setGMStatus('Game mode saved','gm-ok');
}

async function deleteProfileEditor(profileId){
if(!isAdmin())throw new Error('Admin role required');
if(!profileId)return;
if(!confirm(`Delete game mode ${profileId}?`))return;
setGMStatus('Deleting game mode...');
const res=await gmFetch('/api/gm/room/profile/delete',{
method:'POST',headers:{
'Content-Type':'application/json'}
,body:JSON.stringify({
profile_id:profileId}
)}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
if(profileEditor.profile_id===profileId){
profileEditor.profile_id='';
profileEditor.open=false;
}
clearProfileDirty();
await loadGM(true,true);
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
const res=await gmFetch('/api/gm/room/scenario/validate',{
method:'POST',headers:{
'Content-Type':'application/json'}
,body:JSON.stringify({
scenario:draft}
)}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
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
const res=await gmFetch('/api/gm/room/scenario/save',{
method:'POST',headers:{
'Content-Type':'application/json'}
,body:JSON.stringify({
scenario}
)}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
scenarioEditor.scenario_id=scenario.id;
scenarioEditor.open=true;
clearScenarioDirty();
await loadGM(true,true);
setGMStatus('Scenario saved','gm-ok');
}

async function deleteScenarioEditor(scenarioId){
if(!isAdmin())throw new Error('Admin role required');
if(!scenarioId)return;
if(!confirm(`Delete scenario ${scenarioId}?`))return;
setGMStatus('Deleting scenario...');
const res=await gmFetch('/api/gm/room/scenario/delete',{
method:'POST',headers:{
'Content-Type':'application/json'}
,body:JSON.stringify({
scenario_id:scenarioId}
)}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
if(scenarioEditor.scenario_id===scenarioId){
scenarioEditor.scenario_id='';
scenarioEditor.open=false;
}
clearScenarioDirty();
await loadGM(true,true);
setGMStatus('Scenario deleted','gm-ok');
}

async function importStorageJson(inputId,url,label){
const input=document.getElementById(inputId);
if(!input||!input.files||!input.files[0])throw new Error('Select JSON file');
const text=await input.files[0].text();
JSON.parse(text);
setGMStatus(`Importing ${label}...`);
const res=await gmFetch(url,{
method:'POST',headers:{
'Content-Type':'application/json'}
,body:text}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
clearTransientFieldDirty();
await loadGM(true,true);
setGMStatus(`${label} imported`,'gm-ok');
}

async function postStorageCommand(url,label){
setGMStatus(`${label}...`);
const res=await gmFetch(url,{
method:'POST'}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
clearTransientFieldDirty();
await loadGM(true,true);
setGMStatus(`${label} done`,'gm-ok');
}

async function runStorageAction(action){
if(!isAdmin())throw new Error('Admin role required');
if(action==='scenario_export'){
window.location='/api/gm/room/scenarios/export';
return;
}
if(action==='device_export'){
window.location='/api/gm/devices/export';
return;
}
if(action==='profile_export'){
window.location='/api/gm/profiles/export';
return;
}
if(action==='device_import')return importStorageJson('storage_devices_file','/api/gm/devices/import','Devices');
if(action==='scenario_import')return importStorageJson('storage_scenarios_file','/api/gm/room/scenarios/import','Scenarios');
if(action==='profile_import')return importStorageJson('storage_profiles_file','/api/gm/profiles/import','Game modes');
if(action==='device_save')return postStorageCommand('/api/gm/devices/save','Save devices');
if(action==='device_load')return postStorageCommand('/api/gm/devices/load','Load devices');
if(action==='scenario_save')return postStorageCommand('/api/gm/room/scenarios/save','Save scenarios');
if(action==='scenario_load')return postStorageCommand('/api/gm/room/scenarios/load','Load scenarios');
if(action==='profile_save')return postStorageCommand('/api/gm/profiles/save','Save game modes');
if(action==='profile_load')return postStorageCommand('/api/gm/profiles/load','Load game modes');
throw new Error('Unsupported storage action');
}

async function selectRoomScenario(roomId,scenarioId){
if(!roomId||!scenarioId)throw new Error('Scenario selection is incomplete');
setGMStatus('Selecting scenario...');
const res=await gmFetch('/api/gm/room/scenario/select',{
method:'POST',headers:{
'Content-Type':'application/json'}
,body:JSON.stringify({
room_id:roomId,scenario_id:scenarioId}
)}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
currentRoomScenarioId[roomId]=scenarioId;
clearTransientFieldDirty();
await loadGM(true,true);
setGMStatus('Scenario selected','gm-ok');
}

async function runRoomScenarioRuntime(action,roomId,branchId){
if(!roomId||!action)throw new Error('Scenario command is incomplete');
if(action==='next'&&!confirm(branchId?'Force complete this branch wait?':'Force complete current scenario wait?'))return;
setGMStatus('Updating scenario...');
let url=`/api/gm/room/scenario/${encodeURIComponent(action)}?room_id=${encodeURIComponent(roomId)}`;
if(branchId)url+=`&branch_id=${encodeURIComponent(branchId)}`;
const res=await gmFetch(url,{
method:'POST'}
);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
clearTransientFieldDirty();
await loadGM(true,true);
setGMStatus('Scenario updated','gm-ok');
}
// GM panel source part. Edit this file, then rebuild gm_panel.js.
function setGMStatus(text,cls){
setStatus(text,cls==='gm-bad'?'state-fault':(cls==='gm-ok'?'state-ok':'state-unknown'));
}

document.getElementById('gm_nav').onclick=e=>{
const btn=e.target.closest('.nav-btn');
if(!btn)return;
const view=btn.dataset.view||'dashboard';
if(!canOpenView(view))return;
if(view!==currentView&&!confirmDiscardEditorChanges())return;
currentView=view;
render();
}
;

document.getElementById('gm_content').onclick=async e=>{
const room=e.target.closest('[data-open-room]');
const roomNew=e.target.closest('button[data-room-new]');
const roomDelete=e.target.closest('button[data-room-delete]');
const setup=e.target.closest('[data-open-device-setup]');
const adminView=e.target.closest('button[data-open-admin-view]');
const tab=e.target.closest('[data-tab-scope]');
const timer=e.target.closest('button[data-room-timer]');
const hint=e.target.closest('button[data-room-hint]');
const game=e.target.closest('button[data-room-game]');
const profileEdit=e.target.closest('button[data-profile-edit]');
const profileDelete=e.target.closest('button[data-profile-delete]');
const profileNew=e.target.closest('button[data-profile-new]');
const profileSave=e.target.closest('button[data-profile-save]');
const profileSelect=e.target.closest('button[data-profile-select]');
const scenarioEdit=e.target.closest('button[data-scenario-edit]');
const scenarioDelete=e.target.closest('button[data-scenario-delete]');
const scenarioMode=e.target.closest('button[data-scenario-mode]');
const scenarioNew=e.target.closest('button[data-scenario-new]');
const scenarioSave=e.target.closest('button[data-scenario-save]');
const scenarioValidate=e.target.closest('button[data-scenario-validate]');
const scenarioBranchAction=e.target.closest('button[data-scenario-branch-action]');
const scenarioStepAction=e.target.closest('button[data-scenario-step-action]');
const scenarioStepHelp=e.target.closest('button[data-scenario-step-help]');
const audioFilesRefresh=e.target.closest('button[data-audio-files-refresh]');
const storageAction=e.target.closest('button[data-storage-action]');
const scenarioRuntime=e.target.closest('button[data-room-scenario-runtime]');
const questDeviceEdit=e.target.closest('button[data-quest-device-edit]');
const questDeviceNew=e.target.closest('button[data-quest-device-new]');
const questDeviceSave=e.target.closest('button[data-quest-device-save]');
const questDeviceDelete=e.target.closest('button[data-quest-device-delete]');
const questDeviceDiscover=e.target.closest('button[data-quest-device-discover]');
const questDiscoveryApply=e.target.closest('button[data-quest-discovery-apply]');
const questDiscoveryDiscard=e.target.closest('button[data-quest-discovery-discard]');
const questCommandAdd=e.target.closest('button[data-quest-command-add]');
const questCommandDelete=e.target.closest('button[data-quest-command-delete]');
const questEventAdd=e.target.closest('button[data-quest-event-add]');
const questEventDelete=e.target.closest('button[data-quest-event-delete]');
try{
if(roomNew&&isAdmin()){
await createRoomFromPrompt();
return;
}
if(roomDelete&&isAdmin()){
if(!confirmDiscardEditorChanges())return;
await deleteRoom(roomDelete.dataset.roomDelete||'');
return;
}
if(room){
const nextRoomId=room.dataset.openRoom||'';
if((currentView!=='room'||currentRoomId!==nextRoomId)&&!confirmDiscardEditorChanges())return;
currentRoomId=nextRoomId;
currentView='room';
roomTab='control';
render();
return;
}
if(setup&&isAdmin()){
if(!confirmDiscardEditorChanges())return;
currentView='device_setup';
const setupTarget=setup.dataset.openDeviceSetup||'';
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
render();
return;
}
if(adminView&&isAdmin()){
if(!confirmDiscardEditorChanges())return;
const roomId=adminView.dataset.openAdminRoom||'';
if(roomId){
profileEditor.room_id=roomId;
scenarioEditor.room_id=roomId;
}
currentView=adminView.dataset.openAdminView||'profiles';
if(currentView==='profiles')profileEditor.open=true;
if(currentView==='scenarios')scenarioEditor.open=true;
render();
return;
}
if(tab){
if(!confirmDiscardEditorChanges())return;
if(tab.dataset.tabScope==='room')roomTab=tab.dataset.tab||'overview';
render();
return;
}
if(profileEdit){
if(!confirmDiscardProfile())return;
profileEditor.profile_id=profileEdit.dataset.profileEdit||'';
profileEditor.open=true;
clearProfileDirty();
render();
return;
}
if(profileNew){
if(!confirmDiscardProfile())return;
profileEditor.profile_id='';
profileEditor.open=true;
profileEditor.prefill=null;
clearProfileDirty();
render();
return;
}
if(profileDelete&&!profileDelete.disabled){
if(!confirmDiscardProfile())return;
await deleteProfileEditor(profileDelete.dataset.profileDelete||'');
return;
}
if(profileSave&&!profileSave.disabled){
await saveProfileEditor();
return;
}
if(profileSelect&&!profileSelect.disabled){
if(!confirmDiscardProfile())return;
await selectRoomProfile(profileEditor.room_id,profileSelect.dataset.profileSelect||'');
return;
}
if(scenarioEdit){
if(!confirmDiscardScenario())return;
scenarioEditor.scenario_id=scenarioEdit.dataset.scenarioEdit||'';
scenarioEditor.open=true;
scenarioEditor.expanded_step=-1;
scenarioEditor.active_branch=0;
clearScenarioDirty();
render();
return;
}
if(scenarioMode){
if(!confirmDiscardEditorChanges())return;
const scenarioId=scenarioMode.dataset.scenarioMode||'';
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
audio_pack_id:''}
;
currentView='profiles';
render();
return;
}
if(scenarioNew){
if(!confirmDiscardScenario())return;
scenarioEditor.scenario_id='';
scenarioEditor.open=true;
scenarioEditor.expanded_step=-1;
scenarioEditor.active_branch=0;
clearScenarioDirty();
scenarioEditor.draft={id:'',name:'',room_id:scenarioEditor.room_id,branches:[defaultScenarioBranch(0,[])]};
skipNextScenarioDomSync();
render();
return;
}
if(scenarioDelete&&!scenarioDelete.disabled){
if(!confirmDiscardScenario())return;
await deleteScenarioEditor(scenarioDelete.dataset.scenarioDelete||'');
return;
}
if(scenarioValidate&&!scenarioValidate.disabled){
await validateScenarioEditor();
return;
}
if(scenarioSave&&!scenarioSave.disabled){
await saveScenarioEditor();
return;
}
if(scenarioStepHelp){
alert(scenarioStepHelpText(scenarioStepHelp.dataset.scenarioStepHelp||''));
return;
}
if(scenarioBranchAction&&!scenarioBranchAction.disabled){
const index=Number(scenarioBranchAction.dataset.branchIndex);
applyScenarioBranchAction(scenarioBranchAction.dataset.scenarioBranchAction||'',Number.isFinite(index)?index:0);
return;
}
if(scenarioStepAction&&!scenarioStepAction.disabled){
const stepEl=scenarioStepAction.closest('[data-scenario-step]');
const fallbackIndex=Number(stepEl&&stepEl.dataset.scenarioStep);
const index=Number.isFinite(Number(scenarioStepAction.dataset.stepIndex))?Number(scenarioStepAction.dataset.stepIndex):fallbackIndex;
applyScenarioStepAction(scenarioStepAction.dataset.scenarioStepAction||'',index,scenarioStepAction.dataset.scenarioStepType||scenarioStepAction.dataset.commandIndex||scenarioStepAction.dataset.eventIndex||scenarioStepAction.dataset.flagIndex||'');
return;
}
if(audioFilesRefresh&&!audioFilesRefresh.disabled){
await loadGMAudioFiles(true);
return;
}
if(storageAction&&!storageAction.disabled){
if(!confirmDiscardEditorChanges())return;
await runStorageAction(storageAction.dataset.storageAction||'');
return;
}
if(timer&&!timer.disabled){
await runRoomTimer(timer.dataset.roomTimer||'',timer.dataset.roomId||'');
return;
}
if(hint&&!hint.disabled){
await runRoomHint(hint.dataset.roomHint||'',hint.dataset.roomId||'');
return;
}
if(game&&!game.disabled){
await runRoomGame(game.dataset.roomGame||'',game.dataset.roomId||'');
return;
}
if(scenarioRuntime&&!scenarioRuntime.disabled){
await runRoomScenarioRuntime(scenarioRuntime.dataset.roomScenarioRuntime||'',scenarioRuntime.dataset.roomId||'',scenarioRuntime.dataset.roomScenarioBranch||'');
return;
}
if(questDeviceEdit){
if(!confirmDiscardQuestDevice())return;
questDeviceEditor.device_id=questDeviceEdit.dataset.questDeviceEdit||'';
questDeviceEditor.open=true;
questDeviceEditor.draft=null;
clearQuestDeviceDirty();
render();
return;
}
if(questDeviceNew){
if(!confirmDiscardQuestDevice())return;
questDeviceEditor.device_id='';
questDeviceEditor.open=true;
questDeviceEditor.draft=newQuestDeviceDraft();
clearQuestDeviceDirty();
render();
return;
}
if(questDeviceDiscover&&!questDeviceDiscover.disabled){
await discoverQuestDeviceInterface();
return;
}
if(questDiscoveryApply&&!questDiscoveryApply.disabled){
applyQuestDeviceDiscovery();
return;
}
if(questDiscoveryDiscard&&!questDiscoveryDiscard.disabled){
discardQuestDeviceDiscovery();
return;
}
if(questDeviceSave&&!questDeviceSave.disabled){
await saveQuestDeviceEditor();
return;
}
if(questDeviceDelete&&!questDeviceDelete.disabled){
if(!confirmDiscardQuestDevice())return;
await deleteQuestDeviceEditor(questDeviceDelete.dataset.questDeviceDelete||'');
return;
}
if(questCommandAdd&&!questCommandAdd.disabled){
addQuestDeviceCommand();
return;
}
if(questCommandDelete&&!questCommandDelete.disabled){
deleteQuestDeviceCommand(Number(questCommandDelete.dataset.questCommandDelete));
return;
}
if(questEventAdd&&!questEventAdd.disabled){
addQuestDeviceEvent();
return;
}
if(questEventDelete&&!questEventDelete.disabled){
deleteQuestDeviceEvent(Number(questEventDelete.dataset.questEventDelete));
return;
}
}
catch(err){
setStatus(err.message||'command failed','state-fault');
}
}
;

const gmRightSidebar=document.getElementById('gm_right_sidebar');
if(gmRightSidebar){
gmRightSidebar.onclick=async e=>{
const btn=e.target.closest('button[data-manual-device][data-manual-command]');
if(!btn||btn.disabled)return;
try{
if(btn.dataset.dangerous==='1'&&!confirm('Run this manual command?'))return;
await runManualDeviceCommand(btn.dataset.manualDevice||'',btn.dataset.manualCommand||'');
}
catch(err){
setStatus(err.message||'button failed','state-fault');
}
}
;
}

document.getElementById('gm_content').addEventListener('focusin',e=>{
markControlEditing(e.target);
});

document.getElementById('gm_content').addEventListener('focusout',e=>{
unmarkControlEditing(e.target);
});

document.addEventListener('toggle',e=>{
const detail=e.target;
if(!detail||String(detail.tagName||'').toLowerCase()!=='details')return;
const key=detailsKeyFor(detail);
if(key)gmOpenDetails[key]=detail.open;
}
,true);

document.getElementById('gm_content').oninput=e=>{
markControlDirty(e.target);
const profileField=e.target.closest('#profile_id,#profile_name,#profile_duration,#profile_hint_pack,#profile_audio_pack,#profile_scenario,#profile_enabled');
const scenarioField=e.target.closest('#scenario_id,#scenario_name,[data-scenario-branch-field],[data-step-field],[data-step-param],[data-group-command-field],[data-event-group-field],[data-flag-list-field]');
const questDeviceField=e.target.closest('[data-quest-device-field],[data-quest-command-field],[data-quest-event-field]');
if(profileField)markProfileDirty();
if(scenarioField)markScenarioDirty();
if(questDeviceField)markQuestDeviceDirty();
}
;

document.getElementById('gm_content').onchange=async e=>{
const editorRoom=e.target.closest('select[data-profile-room-select]');
const scenarioRoom=e.target.closest('select[data-scenario-room-select]');
const deviceRoom=e.target.closest('select[data-device-room-filter]');
const observed=e.target.closest('select[data-observed-filter]');
const stepType=e.target.closest('select[data-step-field="type"]');
const stepDevice=e.target.closest('select[data-step-field="device_id"]');
const stepCommand=e.target.closest('select[data-step-field="command_id"]');
const stepDeviceEvent=e.target.closest('select[data-step-field="event_id"]');
const stepParamChannel=e.target.closest('select[data-step-param="channel"]');
const groupDevice=e.target.closest('select[data-group-command-field="device_id"]');
const groupCommand=e.target.closest('select[data-group-command-field="command_id"]');
const eventGroupDevice=e.target.closest('select[data-event-group-field="device_id"]');
const eventGroupEvent=e.target.closest('select[data-event-group-field="event_id"]');
const flagSuggest=e.target.closest('select[data-scenario-flag-suggest]');
const branchType=e.target.closest('select[data-scenario-branch-field="type"]');
const profile=e.target.closest('select[data-room-profile-room]');
const scenario=e.target.closest('select[data-room-scenario-room]');
const profileField=e.target.closest('#profile_id,#profile_name,#profile_duration,#profile_hint_pack,#profile_audio_pack,#profile_scenario,#profile_enabled');
const scenarioField=e.target.closest('#scenario_id,#scenario_name,[data-scenario-branch-field],[data-step-field],[data-step-param],[data-group-command-field],[data-event-group-field],[data-flag-list-field]');
const questDeviceField=e.target.closest('[data-quest-device-field],[data-quest-command-field],[data-quest-event-field]');
try{
markControlDirty(e.target);
if(profileField)markProfileDirty();
if(scenarioField)markScenarioDirty();
if(questDeviceField)markQuestDeviceDirty();
if(editorRoom){
if(!confirmDiscardProfile()){
render();
return;
}
profileEditor.room_id=editorRoom.value||'';
profileEditor.profile_id='';
profileEditor.open=false;
clearProfileDirty();
render();
return;
}
if(scenarioRoom){
if(!confirmDiscardScenario()){
render();
return;
}
scenarioEditor.room_id=scenarioRoom.value||'';
scenarioEditor.scenario_id='';
scenarioEditor.open=false;
scenarioEditor.expanded_step=-1;
scenarioEditor.active_branch=0;
clearScenarioDirty();
skipNextScenarioDomSync();
render();
return;
}
if(deviceRoom){
deviceFilterRoom=deviceRoom.value||'';
clearTransientFieldDirty();
render();
return;
}
if(observed){
observedFilter=observed.value||'all';
clearTransientFieldDirty();
render();
return;
}
if(flagSuggest){
const wrapper=flagSuggest.closest('.flag-picker');
const input=wrapper&&wrapper.querySelector('[data-step-field="flag_name"],[data-flag-list-field="flag_name"]');
if(input&&flagSuggest.value){
input.value=flagSuggest.value;
markControlDirty(input);
refreshScenarioStepLabel(input.closest('[data-scenario-step]'));
}
markScenarioDirty();
return;
}
if(branchType){
const draft=collectScenarioEditor();
const branch=scenarioActiveBranch(draft);
if(branch){
branch.type=scenarioBranchTypeValue({type:branchType.value});
branch.required_for_completion=branch.type==='normal'&&branch.required_for_completion!==false;
if(branch.type==='reactive')branch.required_for_completion=false;
}
scenarioEditor.draft=draft;
scenarioEditor.dirty=true;
scenarioEditor.validation_report=null;
skipNextScenarioDomSync();
render();
return;
}
if(stepDevice){
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
scenarioEditor.draft=draft;
scenarioEditor.expanded_step=index;
skipNextScenarioDomSync();
}
scenarioEditor.dirty=true;
scenarioEditor.validation_report=null;
render();
return;
}
if(stepCommand||stepDeviceEvent){
refreshScenarioStepLabel((stepCommand||stepDeviceEvent).closest('[data-scenario-step]'));
markScenarioDirty();
render();
return;
}
if(stepParamChannel){
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
scenarioEditor.draft=draft;
scenarioEditor.expanded_step=index;
skipNextScenarioDomSync();
}
markScenarioDirty();
render();
return;
}
if(groupDevice||groupCommand){
const stepEl=(groupDevice||groupCommand).closest('[data-scenario-step]');
const itemEl=(groupDevice||groupCommand).closest('[data-command-group-item]');
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
}else{
item.command_id=groupCommand.value||'';
item.params=defaultParamsForCommand(scenarioDeviceById(item.device_id),scenarioCommandById(item.device_id,item.command_id));
}
step.commands[itemIndex]=item;
scenarioEditor.draft=draft;
scenarioEditor.expanded_step=index;
skipNextScenarioDomSync();
}
markScenarioDirty();
render();
return;
}
if(eventGroupDevice||eventGroupEvent){
const stepEl=(eventGroupDevice||eventGroupEvent).closest('[data-scenario-step]');
const itemEl=(eventGroupDevice||eventGroupEvent).closest('[data-event-group-item]');
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
}else{
item.event_id=eventGroupEvent.value||'';
}
step.events[itemIndex]=item;
scenarioEditor.draft=draft;
scenarioEditor.expanded_step=index;
skipNextScenarioDomSync();
}
markScenarioDirty();
render();
return;
}
if(stepType){
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
scenarioEditor.draft=draft;
scenarioEditor.expanded_step=index;
}
markScenarioDirty();
skipNextScenarioDomSync();
render();
return;
}
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
;

document.getElementById('gm_refresh').onclick=()=>{
if(!confirmDiscardEditorChanges())return;
clearProfileDirty();
clearScenarioDirty();
clearQuestDeviceDirty();
clearTransientFieldDirty();
loadGM();
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
if(!hasUnsavedEditorChanges())return;e.preventDefault();e.returnValue='';}
);

window.__sessionRolePromise=loadGMSession();

window.__sessionRolePromise.then(()=>loadGM());

setInterval(()=>loadGM(true),3000);
