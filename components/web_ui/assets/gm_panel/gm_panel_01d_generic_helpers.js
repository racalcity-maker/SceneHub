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
function scenarioSummariesByRoom(roomId){return roomScenarios(roomId);}
function roomScenarioDetailKey(roomId,scenarioId){return `${String(roomId||'')}::${String(scenarioId||'')}`;}
function roomScenarioDetailById(roomId,scenarioId){const key=roomScenarioDetailKey(roomId,scenarioId);return key&&(gmRoomScenarioDetails&&gmRoomScenarioDetails[key])||null;}
function roomScenarioSummaryById(roomId,scenarioId){return scenarioSummariesByRoom(roomId).find(x=>x.id===scenarioId)||null;}
function roomScenarioRuntimeProjectionById(roomId,scenarioId){return roomScenarioDetailById(roomId,scenarioId)||roomScenarioSummaryById(roomId,scenarioId)||null;}
function scenarioEditorSessionKey(roomId,scenarioId){
return `${String(roomId||scenarioEditor.room_id||'')}::${String(scenarioId||scenarioEditor.scenario_id||'new')}`;
}
function scenarioValidationReportCurrent(){
const report=scenarioEditor.validation_report;
if(!report)return null;
if(Number(scenarioEditor.validation_revision||0)!==Number(scenarioEditor.draft_revision||0))return null;
if(String(report._session_key||'')!==scenarioEditorSessionKey())return null;
return report;
}
function scenarioClientValidationReportCurrent(){
const draft=scenarioEditor.draft;
if(!draft)return null;
if(String(draft.room_id||'')!==String(scenarioEditor.room_id||''))return null;
if(String(draft.id||'')!==String(scenarioEditor.scenario_id||''))return null;
const report=scenarioClientValidationReport(draft);
report._session_key=scenarioEditorSessionKey(draft.room_id,draft.id);
report._source='client';
return report;
}
function scenarioDisplayValidationReport(savedIssues){
return scenarioValidationReportCurrent()||scenarioClientValidationReportCurrent()||(Array.isArray(savedIssues)?{issues:savedIssues,error_count:0,warning_count:0,_source:'saved'}:null);
}
function roomProfiles(id){const data=gmRoomProfiles?gmRoomProfiles[id]:null;return data&&Array.isArray(data.profiles)?data.profiles:[];}
function roomSelectedProfileId(id){return currentRoomProfileId[id]||(gmRoomProfiles[id]&&gmRoomProfiles[id].selected_profile_id)||'';}
function scenarioName(roomId,scenarioId){const s=roomScenarioRuntimeProjectionById(roomId,scenarioId);return s&&(s.name||s.id)||scenarioId||'none';}
function scenarioDisplayName(roomId,scenarioId,fallback){const s=scenarioById(roomId,scenarioId);return s&&(s.name||s.id)||fallback||scenarioId||'none';}
function roomActiveScenarioId(roomId){
const room=roomById(roomId);
if(!room)return '';
const profiles=roomProfiles(roomId);
const profileId=roomSelectedProfileId(roomId)||room.selected_profile_id||'';
const profile=profiles.find(p=>p.id===profileId)||null;
return room.running_scenario_id||room.selected_profile_scenario_id||(profile&&profile.scenario_id)||room.selected_scenario_id||'';
}
function configDevices(){return gmDeviceConfig&&Array.isArray(gmDeviceConfig.devices)?gmDeviceConfig.devices:[];}
function questDevices(){return gmQuestDevices&&Array.isArray(gmQuestDevices.devices)?gmQuestDevices.devices:[];}
function observedByClientId(id){const key=String(id||'');if(!key)return null;return observedItems().find(o=>o.device_id===key)||null;}
function questDeviceById(id){return questDevices().find(d=>(d.id||'')===id)||null;}
function scenarioEditorCatalog(roomId){return gmScenarioEditorCatalogs[roomId]||{quest_devices:[],step_schemas:[]};}
function optionList(items,selected,emptyLabel){let found=false;const opts=[];if(emptyLabel)opts.push(`<option value=''>${esc(emptyLabel)}</option>`);(Array.isArray(items)?items:[]).forEach(item=>{const id=item.id||'';if(id===selected)found=true;opts.push(`<option value='${esc(id)}' ${id===selected?'selected':''}>${esc(item.name||id)}</option>`);});if(selected&&!found)opts.push(`<option value='${esc(selected)}' selected>${esc(selected)} (missing)</option>`);return opts.join('');}
function commandSupportsScenarioParams(command){
const schema=Array.isArray(command&&command.args_schema)?command.args_schema:[];
return !!(schema.length||(command&&command.default_args&&typeof command.default_args==='object'));
}
function questDeviceCommandName(deviceId,commandId){return typeof scenarioCommandName==='function'?scenarioCommandName(deviceId,commandId):(commandId||'command');}
function questDeviceEventName(deviceId,eventId){return typeof scenarioDeviceEventName==='function'?scenarioDeviceEventName(deviceId,eventId):(eventId||'event');}
