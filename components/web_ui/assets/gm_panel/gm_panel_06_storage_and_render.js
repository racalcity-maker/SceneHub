// GM panel source part. Edit this file, then rebuild gm_panel.js.
function renderStorageAdminView(){
setPage('Storage','Import, export, save and load');
const storageButtons=prefix=>uiActions([
uiButton({label:'Save to SD',action:'storage.run',dataset:{op:`${prefix}_save`}}),
uiButton({label:'Load from SD',action:'storage.run',dataset:{op:`${prefix}_load`}}),
uiButton({label:'Export JSON',action:'storage.run',dataset:{op:`${prefix}_export`}}),
]);
const presetButtons=uiActions([
uiButton({label:'Load from SD',action:'storage.run',dataset:{op:'preset_load'}}),
uiButton({label:'Export JSON',action:'storage.run',dataset:{op:'preset_export'}}),
]);
return `<div class='grid cols-2'><div class='card'><h2 class='section-title'>Quest devices</h2>${storageButtons('device')}<div style='height:12px'></div><div class='row'><input id='storage_devices_file' type='file' accept='.json,application/json'>${uiButton({label:'Import JSON',action:'storage.run',dataset:{op:'device_import'}})}</div><div class='row-meta'>Path: /sdcard/quest/quest_devices.json</div></div><div class='card'><h2 class='section-title'>Room scenarios</h2>${storageButtons('scenario')}<div style='height:12px'></div><div class='row'><input id='storage_scenarios_file' type='file' accept='.json,application/json'>${uiButton({label:'Import JSON',action:'storage.run',dataset:{op:'scenario_import'}})}</div><div class='row-meta'>Path: /sdcard/quest/room_scenarios.json</div></div><div class='card'><h2 class='section-title'>Game modes</h2>${storageButtons('profile')}<div style='height:12px'></div><div class='row'><input id='storage_profiles_file' type='file' accept='.json,application/json'>${uiButton({label:'Import JSON',action:'storage.run',dataset:{op:'profile_import'}})}</div><div class='row-meta'>Path: /sdcard/quest/game_profiles.json</div></div><div class='card'><h2 class='section-title'>GM quick actions</h2>${presetButtons}<div style='height:12px'></div><div class='row'><input id='storage_presets_file' type='file' accept='.json,application/json'>${uiButton({label:'Import JSON',action:'storage.run',dataset:{op:'preset_import'}})}</div><div class='row-meta'>Path: /sdcard/quest/gm_sidebar_presets.json</div><div class='row-meta'>Device Controls edits persist immediately to the controller store.</div></div></div>`;
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

function renderCompactQuestDeviceSummary(device,title){
const manifest=compactManifest(device);
if(!manifest)return '';
const resources=manifest.resources&&typeof manifest.resources==='object'?manifest.resources:{};
const count=key=>Array.isArray(resources[key])?resources[key].length:0;
const templates=Array.isArray(manifest.command_templates)?manifest.command_templates:[];
const eventTemplates=Array.isArray(manifest.event_templates)?manifest.event_templates:[];
return `<div class='builder-step'><div class='card-head'><div><h2 class='section-title'>${esc(title||'Compact node interface')}</h2><div class='row-meta'>manifest v${esc(manifest.manifest_version)} / ${esc(manifest.node_kind||'node')} / ${esc(manifest.capability_contract||'')}</div></div></div><div class='kvs'><div class='kv'><span class='k'>Resources</span><span class='v'>Relays ${count('relays')}, MOSFETs ${count('mosfets')}, inputs ${count('inputs')}, outputs ${count('outputs')}, LED strips ${count('led_strips')}</span></div><div class='kv'><span class='k'>Templates</span><span class='v'>${templates.length} commands / ${eventTemplates.length} events</span></div></div><details class='scenario-advanced'><summary>Manifest JSON</summary><pre class='code-block'>${esc(JSON.stringify(manifest,null,2))}</pre></details></div>`;
}

function renderQuestDiscoveryPreview(){
const d=questDeviceEditor.discovery;
if(!d||!d.device)return '';
const dev=d.device;
const manifest=compactManifest(dev);
if(manifest){
return renderCompactQuestDeviceSummary(dev,'Discovered compact node config').replace('</div><div class=\'kvs\'>',`</div><div class='actions'>${uiButton({label:'Import',action:'quest.discovery.apply'})}${uiButton({label:'Discard',action:'quest.discovery.discard'})}</div></div><div class='kvs'>`);
}
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
const manifest=compactManifest(d);
const meta=manifest?`${(manifest.command_templates||[]).length} templates / compact v${manifest.manifest_version}`:`${esc((d.commands||[]).length)} commands / ${esc((d.events||[]).length)} events`;
return `<div class='row-card admin-item-card'><div class='admin-item-main'><div class='admin-item-title-row'><div class='row-title'>${esc(d.name||d.id)}</div>${d.enabled===false?`<span class='badge'>disabled</span>`:''}</div><div class='admin-item-meta'><span>${meta}</span><span>${esc(questDeviceStatusText(d))}</span></div></div><div class='admin-item-side'><div>${status(health)}</div><div class='actions'>${uiButton({label:'Edit',action:'quest.device.edit',dataset:{'device-id':d.id||''}})}${uiButton({label:'Delete',action:'quest.device.delete',kind:'danger',dataset:{'device-id':d.id||''},confirm:`Delete device ${d.id||''}?`})}</div></div></div>`;
}

function renderQuestDeviceEditor(draft){
if(!draft){
return `<div class='card empty-state'><h2 class='section-title'>Device editor</h2><div class='empty-title'>Select a quest device or create a new one</div><div class='row-meta'>Quest devices are physical client capabilities: commands, events and manual buttons. They are used later in room scenarios.</div><div class='actions'>${uiButton({label:'Add device',action:'quest.device.new'})}</div></div>`;
}
const clientControl=observedItems().length?`<select class='scenario-select' data-quest-device-field='client_id'>${physicalClientOptions(draft&&draft.client_id||'')}</select>`:`<input data-quest-device-field='client_id' placeholder='Physical client ID' value='${esc(draft&&draft.client_id||'')}'>`;
const compact=compactManifest(draft);
const commandRows=!compact&&((draft.commands||[]).length?draft.commands.map(renderQuestCommandRow).join(''):`<div class='empty'>No commands. Import config from the client or add a command manually.</div>`);
const eventRows=!compact&&((draft.events||[]).length?draft.events.map(renderQuestEventRow).join(''):`<div class='empty'>No events. Import config from the client or add an event manually.</div>`);
const compactSummary=compact?(renderQuestDiscoveryPreview()||renderCompactQuestDeviceSummary(draft,'Compact node interface')):'';
const flatEditors=compact?'':`<div class='form-section'><div class='card-head'><div><h2 class='section-title'>Commands</h2><div class='row-meta'>Commands can become scenario actions and manual buttons.</div></div><div class='actions'>${uiButton({label:'Add command',action:'quest.command.add'})}</div></div><div>${commandRows}</div></div><div class='form-section'><div class='card-head'><div><h2 class='section-title'>Events</h2><div class='row-meta'>Events are available as scenario waits.</div></div><div class='actions'>${uiButton({label:'Add event',action:'quest.event.add'})}</div></div><div>${eventRows}</div></div>`;
return uiOverlayCard({
title:`${questDeviceEditor.device_id?'Edit quest device':'New quest device'}${questDeviceEditor.dirty?' *':''}`,
subtitle:'Define what this physical client can do and report.',
closeAction:'quest.device.cancel',
className:'card editor-modal-card',
dataset:{'quest-device-editor-modal':'1'},
content:`<div data-quest-device-editor='1'><label class='row-meta'><input data-quest-device-field='enabled' type='checkbox' ${draft.enabled!==false?'checked':''} style='min-width:auto'> Enabled</label><div class='form-section'><h2 class='section-title'>Basics</h2><div class='field-grid'><label class='field-stack'><span>Device name</span><input data-quest-device-field='name' placeholder='Altar controller' value='${esc(draft.name||'')}'></label><label class='field-stack'><span>Physical client</span>${clientControl}</label></div><details class='scenario-advanced'><summary>Advanced</summary><div class='row'><input data-quest-device-field='id' placeholder='Device ID' value='${esc(draft.id||'')}'></div></details></div><div class='form-section import-panel'><div><h2 class='section-title'>Import capabilities</h2><div class='row-meta'>Ask the selected physical client for its supported commands and events.</div></div><div class='actions'>${uiButton({label:'Get config',action:'quest.device.discover',kind:'approve'})}</div></div>${compactSummary||renderQuestDiscoveryPreview()}${flatEditors}<div class='actions sticky-actions'>${uiButton({label:'Save device',action:'quest.device.save'})}${questDeviceEditor.device_id?uiButton({label:'Delete',action:'quest.device.delete',kind:'danger',dataset:{'device-id':questDeviceEditor.device_id},confirm:`Delete device ${questDeviceEditor.device_id}?`}):''}${uiButton({label:'Cancel',action:'quest.device.cancel'})}</div></div>`
});
}

function renderDeviceSetupAdminView(){
setPage('Quest Devices','Device capabilities and manual controls');
const devices=questEditableDevices();
const draft=questDeviceEditor.open?currentQuestDeviceDraft():null;
const rows=devices.length?devices.map(renderQuestDeviceListRow).join(''):`<div class='card empty-state'><div class='empty-title'>No quest devices yet</div><div class='row-meta'>Add a device, select its physical client and import capabilities.</div><div class='actions'>${uiButton({label:'Add device',action:'quest.device.new'})}</div></div>`;
return `<section class='card'><div class='card-head'><div><h2 class='section-title'>Quest devices</h2><div class='card-sub'>Saved device capability sets</div></div><div class='actions'>${uiButton({label:'Add device',action:'quest.device.new'})}</div></div><div class='admin-entity-grid'>${rows}</div></section>${draft?renderQuestDeviceEditor(draft):''}`;
}

function initDeviceSetupWizard(){
return;
}

function syncGMSummaryStatus(){
const summary=gmState&&gmState.summary?gmState.summary:{};
setStatus(summary.has_fault?'fault':(summary.has_degraded?'degraded':'ok'),summary.has_fault?'state-fault':(summary.has_degraded?'state-degraded':'state-ok'));
}

function captureScenarioModalRenderState(){
const card=document.querySelector('[data-scenario-editor-modal] .scenario-modal-card');
if(!card)return null;
return {scrollTop:card.scrollTop||0};
}

function restoreScenarioModalRenderState(state){
if(!state)return;
const card=document.querySelector('[data-scenario-editor-modal] .scenario-modal-card');
if(card)card.scrollTop=state.scrollTop||0;
}

function renderMainContent(){
const root=document.getElementById('gm_content');
if(!root)return 'none';
const scenarioModalState=captureScenarioModalRenderState();
if(gmSkipScenarioDomSync)gmSkipScenarioDomSync=false;
applyGMRoleLayout();
syncGMSummaryStatus();
if(currentView==='room'&&roomTab==='control'){
const room=roomById(currentRoomId)||((gmState&&gmState.rooms&&gmState.rooms[0])?gmState.rooms[0]:null);
if(room&&patchRoomControlView(root,room)){
gmStatInc('render.room_control_patch');
return 'room_control_patch';
}
}
let html='';
if(currentView==='rooms')html=renderRoomsView();
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
else html=renderRoomsView();
root.innerHTML=html;
injectRoomScenarios();
restoreScenarioModalRenderState(scenarioModalState);
const navView=currentView==='room'?'rooms':currentView;

document.querySelectorAll('.nav-btn').forEach(b=>b.classList.toggle('active',b.dataset.view===navView));
return 'full';
}

function render(){
const mode=renderMainContent();
if(mode==='full')gmStatInc('render.full');
}
