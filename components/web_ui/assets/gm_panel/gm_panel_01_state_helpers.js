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
