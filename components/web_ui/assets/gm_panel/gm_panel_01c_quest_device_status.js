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
