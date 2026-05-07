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
return `<tr class='clickable-row' data-action='room.open' data-room-id='${esc(room.room_id)}'><td><strong>${esc(room.title||room.name||room.room_id)}</strong><span>${esc(room.room_id||'')}</span></td><td>${status(roomDerivedHealth(room))}</td><td><strong>${esc(gameName)}</strong><span>${esc(scenarioNameText)}</span></td><td>${roomClockHtml(room,'span','')}</td><td>${esc(runtime)}</td><td>${esc(current?scenarioStepText(current):'none')}</td><td>${esc(scenarioWaitText(room))}</td><td>${esc(devices)}</td><td>${esc(issues)}</td><td class='observed-actions'>${uiButton({label:'Open',kind:'small-btn',action:'room.open',dataset:{'room-id':room.room_id}})}</td></tr>`;
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
const issues=roomIssues(room.room_id).concat(roomQuestDeviceIssues(room));
const canReset=room.session_present;
const canFinish=room.session_present&&room.session_state!=='finished';
const canScenarioNext=(room.selected_scenario_id||room.running_scenario_id)&&(room.scenario_runtime_state==='running'||room.scenario_runtime_state==='waiting');
let body='';
if(roomTab==='overview'){
body=`<div class='grid cols-2'><div class='card'><div class='card-head'><div><div class='card-title'>Room state</div><div class='card-sub'>${esc(room.title||room.name||'Room')}</div></div>${status(roomDerivedHealth(room))}</div><div class='kvs'><div class='kv'><span class='k'>Timer</span>${roomClockHtml(room,'span','v')}</div><div class='kv'><span class='k'>Session</span><span class='v'>${esc(room.session_state||'idle')}</span></div><div class='kv'><span class='k'>Scenario devices</span><span class='v'>${esc(questDevs.length)}</span></div><div class='kv'><span class='k'>Hints</span><span class='v'>${esc(room.hint_sent_count||0)}</span></div></div></div><div class='card'><h2 class='section-title'>Problems</h2><div class='list'>${issues.length?issues.slice(0,4).map(issueRow).join(''):`<div class='empty'>No room issues</div>`}</div></div></div>`;
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
