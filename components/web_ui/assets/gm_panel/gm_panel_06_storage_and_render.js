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
