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
return `${create}<div class='grid auto' data-rooms-grid='1'>${rooms.length?rooms.map(roomCard).join(''):`<div class='card empty'>No rooms</div>`}</div>`;
}

function renderRoomView(){
const room=roomById(currentRoomId)||((gmState&&gmState.rooms&&gmState.rooms[0])?gmState.rooms[0]:null);
if(room)currentRoomId=room.room_id;
if(room)setPage(`Room: ${room.title||room.room_id}`,'Room control',{titleHtml:`<div class='page-room-titlebar'><span class='page-room-title-text'>${esc(`Room: ${room.title||room.room_id}`)}</span><div class='page-room-tabs'>${tabs(roomTab,['control','overview','devices','issues'],'room')}</div></div>`});
else setPage('Room','Room control');
if(!room)return `<div class='card empty'>No room selected</div>`;
if(roomTab==='control')return renderRoomControlView(room);
const roomNameText=room.title||room.name||room.room_id;
const deleteRoomAction=isAdmin()?`<div class='actions' style='margin-top:14px;justify-content:flex-end'>${uiButton({label:'Delete room',kind:'danger',action:'room.delete',dataset:{'room-id':room.room_id},confirm:`Delete room ${roomNameText}? This also removes profiles and scenarios for this room. Quest devices stay untouched.`})}</div>`:'';
const devs=roomDevices(room.room_id);
const questIds=roomScenarioDeviceIds(room);
const questDevs=questIds.map(id=>questDeviceById(id)).filter(Boolean);
const issues=roomRelatedIssues(room);
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
 return `<div>${body}</div>${roomTab!=='control'?deleteRoomAction:''}`;
}

function renderDevicesView(){
setPage('Device Controls','Admin quick-access presets and saved devices');
if(!isAdmin())return `<div class='card empty'>Device controls are available to admin only.</div>`;
sidebarPresets();
if(!gmHardwareIo.loaded&&!gmHardwareIo.loading&&typeof loadHardwareIoStatus==='function'){
setTimeout(()=>loadHardwareIoStatus(true),0);
}
const savedQuestDevices=questDevices().filter(d=>d&&!d.system_device);
const observed=observedItems();
const registered=observed.filter(o=>knownDeviceIds().has(o.device_id)).length;
const fault=savedQuestDevices.filter(d=>questDeviceHealth(d)==='fault').length;
const degraded=savedQuestDevices.filter(d=>questDeviceHealth(d)==='degraded').length;
const presets=sidebarPresets();
const presetGroups=sidebarPresetAdminGroups();
const legacyMigration=sidebarPresetMigrationPending()?`<div class='card'><div class='card-head'><div><h2 class='section-title'>Import legacy browser presets</h2><div class='card-sub'>Found ${esc(legacySidebarPresetCount())} quick action${legacySidebarPresetCount()===1?'':'s'} saved in this browser from the old localStorage model.</div></div><div class='actions'>${uiButton({label:'Import to controller',action:'sidebar.preset.import_legacy'})}</div></div><div class='row-meta'>Import them once into /sdcard/quest/gm_sidebar_presets.json so every browser sees the same operator sidebar.</div></div><div style='height:12px'></div>`:'';
const presetRows=presetGroups.length?presetGroups.map(group=>renderSidebarPresetGroupCard(group,presets.length)).join(''):`<div class='manual-empty'>No quick actions yet. Add the first quick action.</div>`;
return `<div class='observed-toolbar'><div class='observed-summary'><span>Quick actions <strong>${esc(presets.length)}</strong></span><span>Quest devices <strong>${esc(savedQuestDevices.length)}</strong></span><span>Observed <strong>${esc(observed.length)}</strong></span><span>Registered <strong>${esc(registered)}</strong></span><span>Degraded <strong>${esc(degraded)}</strong></span><span>Offline/Fault <strong>${esc(fault)}</strong></span></div></div>${legacyMigration}<section class='card'><div class='card-head'><div><h2 class='section-title'>Sidebar quick actions</h2><div class='card-sub'>Only these presets are shown to the operator in the right sidebar.</div></div><div class='actions'>${uiButton({label:'New quick action',action:'sidebar.preset.new'})}</div></div><div class='admin-entity-grid'>${presetRows}</div></section>${renderSidebarPresetWizard()}`;
}

function renderObservedView(){
setPage('Observed clients','Physical MQTT clients');
const known=knownDeviceIds();
const all=observedItems();
const items=all.filter(o=>observedFilter==='registered'?known.has(o.device_id):(observedFilter==='unregistered'?!known.has(o.device_id):true));
const registered=all.filter(o=>known.has(o.device_id)).length;
const unregistered=all.length-registered;
const offline=all.filter(o=>String(o&&o.connectivity||'')==='offline').length;
const rows=items.length?items.map(o=>{
const reg=observedRegistration(o.device_id);
const action=reg&&reg.via==='quest_device'?uiButton({label:'Setup',kind:'small-btn',action:'device.setup.open',dataset:{'device-id':reg.device_id}}):(reg?`<span class='muted'>linked</span>`:uiButton({label:'Add',kind:'small-btn',action:'device.setup.open',dataset:{'device-id':'new'}}));
const clientMeta=[o.device_id||'',o.fw_version?`fw ${o.fw_version}`:'',o.mode||''].filter(Boolean).join(' / ');
const stateMeta=[o.state||'idle',o.boot_id?`boot ${o.boot_id}`:''].filter(Boolean).join(' / ');
return `<tr><td><strong>${esc(observedDisplayName(o))}</strong><span>${esc(clientMeta||o.device_id||'')}</span></td><td>${status(o.connectivity)}</td><td><span class='badge ${reg?'selected-badge':''}'>${reg?'registered':'unregistered'}</span></td><td><strong>${esc(o.state||'idle')}</strong><span>${esc(stateMeta||'no state')}</span></td><td class='observed-actions'>${action}</td></tr>`;
}).join(''):`<tr><td colspan='5' class='observed-empty'>No observed clients</td></tr>`;
return `<div class='ops-summary-strip observed-summary'><span>Observed <strong>${esc(all.length)}</strong></span><span>Registered <strong>${esc(registered)}</strong></span><span>Unregistered <strong>${esc(unregistered)}</strong></span><span>Offline <strong>${esc(offline)}</strong></span></div><div class='observed-toolbar ops-toolbar'><select class='scenario-select' data-observed-filter><option value='all' ${observedFilter==='all'?'selected':''}>All observed</option><option value='registered' ${observedFilter==='registered'?'selected':''}>Registered</option><option value='unregistered' ${observedFilter==='unregistered'?'selected':''}>Unregistered</option></select><div class='row-meta'>Physical MQTT clients and their registration state.</div></div><div class='observed-table-wrap ops-table-wrap'><table class='observed-table ops-table'><thead><tr><th>Client</th><th>Status</th><th>Link</th><th>State</th><th></th></tr></thead><tbody>${rows}</tbody></table></div>`;
}

function auditRow(a){
const resultClass=a.success?'ok-text':'bad-text';
const resultText=a.success?'OK':'FAIL';
const actionText=a.action_id||'action';
const sourceText=[a.source||'system',a.error_code&&a.error_code!=='ok'?`error ${a.error_code}`:'ok'].filter(Boolean).join(' / ');
return `<tr><td><strong>${esc(fmtLogTimestamp(a.timestamp_ms||0))}</strong><span>${esc(fmtLogTimestampMeta(a.timestamp_ms||0))}</span></td><td><span class='${resultClass}'>${resultText}</span></td><td><strong>${esc(actionText)}</strong><span>${esc(sourceText)}</span></td><td><strong>${esc(deviceDisplayName(a.device_id))}</strong><span>${esc(a.device_id||'')}</span></td></tr>`;
}

function renderAuditView(){
setPage('Audit','Recent operator actions');
const items=auditItems();
const okCount=items.filter(item=>!!item&&item.success).length;
const failCount=Math.max(0,items.length-okCount);
const newestTs=items.reduce((max,item)=>Math.max(max,Number(item&&item.timestamp_ms||0)),0);
const recentThreshold=Math.max(0,newestTs-300000);
const recentCount=items.filter(item=>Number(item&&item.timestamp_ms||0)>=recentThreshold).length;
return `<div class='ops-summary-strip observed-summary'><span>Total <strong>${esc(items.length)}</strong></span><span>OK <strong>${esc(okCount)}</strong></span><span>Fail <strong>${esc(failCount)}</strong></span><span>Recent window <strong>${esc(recentCount)}</strong></span></div><div class='observed-toolbar ops-toolbar'><div class='row-meta'>Recent operator actions with result and target.</div></div><div class='observed-table-wrap ops-table-wrap'><table class='observed-table audit-table ops-table'><thead><tr><th>When</th><th>Result</th><th>Action</th><th>Target</th></tr></thead><tbody>${items.length?items.map(auditRow).join(''):`<tr><td colspan='4' class='observed-empty'>No audit entries</td></tr>`}</tbody></table></div>`;
}

function timelineRow(t){
const target=t.device_id||t.room_id||t.source||'';
const targetName=t.device_id?deviceDisplayName(t.device_id):(t.room_id?roomName(t.room_id):target);
const sev=t.severity||'info';
return `<article class='ops-feed-item timeline-feed-item ${esc(`severity-${sev}`)}'><div class='ops-feed-time'><strong>${esc(fmtLogTimestamp(t.timestamp_ms||0))}</strong><span>${esc(fmtLogTimestampMeta(t.timestamp_ms||0))}</span></div><div class='ops-feed-body'><div class='ops-feed-head timeline-feed-head'><span class='badge ${sev==='error'?'bad-badge':(sev==='warning'?'warn-badge':'selected-badge')}'>${esc(sev)}</span><strong>${esc(t.title||t.type||'event')}</strong>${t.details?`<span class='timeline-feed-inline'>${esc(t.details)}</span>`:''}</div><div class='ops-feed-meta timeline-feed-meta'><span>${esc(targetName||'system')}</span><span>${esc(t.type||'event')}</span><span>${esc(t.source||'system')}</span></div></div></article>`;
}

function renderTimelineView(){
setPage('Timeline','Recent system events');
const items=timelineItems();
const errorCount=items.filter(item=>String(item&&item.severity||'')==='error').length;
const warningCount=items.filter(item=>String(item&&item.severity||'')==='warning').length;
const infoCount=Math.max(0,items.length-errorCount-warningCount);
return `<div class='ops-summary-strip observed-summary'><span>Total <strong>${esc(items.length)}</strong></span><span>Errors <strong>${esc(errorCount)}</strong></span><span>Warnings <strong>${esc(warningCount)}</strong></span><span>Info <strong>${esc(infoCount)}</strong></span></div><div class='observed-toolbar ops-toolbar'><div class='row-meta'>Recent system events, waits and device activity.</div></div><div class='ops-feed'>${items.length?items.map(timelineRow).join(''):`<div class='manual-empty'>No timeline events</div>`}</div>`;
}

function renderAdminPlaceholder(title,sub){
setPage(title,sub);
return `<div class='card empty'>Admin editor section is reserved for the next implementation step.</div>`;
}
