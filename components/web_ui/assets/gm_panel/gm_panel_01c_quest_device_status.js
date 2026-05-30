// GM panel source part. Edit this file, then rebuild gm_panel.js.
function questDeviceDisplayName(dev){return dev&&(dev.name||dev.display_name||dev.id)||'Device';}
function observedDisplayName(item){if(!item)return 'Device';const reg=observedRegistration(item.device_id);return reg&&(reg.name||reg.device_id)||item.name||item.display_name||item.device_id||'Device';}
function questDeviceHealth(dev){return dev&&(dev.health||'unknown')||'unknown';}
function questDeviceStatusText(dev){return dev&&(dev.status_text||dev.state_text||'unknown')||'missing device';}
function questDeviceCompactSummary(dev){return dev&&dev.manifest_summary&&dev.manifest_summary.compact?dev.manifest_summary:null;}
function questDeviceCommandList(dev){return typeof compactCommandsForDevice==='function'?compactCommandsForDevice(dev):Array.isArray(dev&&dev.commands)?dev.commands:[];}
function questDeviceEventList(dev){return typeof compactEventsForDevice==='function'?compactEventsForDevice(dev):Array.isArray(dev&&dev.events)?dev.events:[];}
function questDeviceCapabilityMeta(dev){
const summary=questDeviceCompactSummary(dev);
if(summary)return `${Number(summary.resource_count)||0} resources / ${Number(summary.command_template_count)||0} command templates / ${Number(summary.event_template_count)||0} event templates`;
return `${(dev.commands||[]).length} commands / ${(dev.events||[]).length} events`;
}
function questDeviceMonitorRow(dev){const observed=observedByClientId(dev.client_id||dev.id);const health=questDeviceHealth(dev);const meta=[questDeviceCapabilityMeta(dev),dev.enabled===false?'disabled':'enabled'].join(' / ');const setup=isAdmin()?uiButton({label:'Device Setup',action:'device.setup.open',dataset:{'device-id':dev.id||'1'}}):'';return `<div class='row-card'><div class='row-main'><div class='row-title'>${esc(questDeviceDisplayName(dev))} ${dev.enabled===false?`<span class='badge'>disabled</span>`:''}</div><div class='row-meta'>${esc(meta)}</div><div class='row-meta'>${observed?`${esc(observed.connectivity||'unknown')} / fw ${esc(observed.fw_version||'n/a')}`:'not observed'}</div></div><div>${status(health)}<div class='row-meta'>${esc(questDeviceStatusText(dev))}</div></div><div class='actions'>${setup}</div></div>`;}
function commandPolicy(cmd){return cmd&&cmd.policy&&typeof cmd.policy==='object'?cmd.policy:{};}
function commandRequiresConfirmation(cmd){const p=commandPolicy(cmd);return !!p.requires_confirmation||(p.danger_level&&p.danger_level!=='normal');}
function sidebarActionKey(action){return String(action&&action.action_key||action&&action.command_id||'');}
function sidebarIsDoorLikeResource(label){return /(door|gate|lock|portal|doorway|двер|ворот|замок)/i.test(String(label||''));}
function sidebarActionNeedsConfirmation(action){
if(action&&typeof action.requires_confirmation==='boolean')return action.requires_confirmation;
return commandRequiresConfirmation(action&&action.command||null);
}
function sidebarActionVariantMatches(action,commandId,params){
if(!action||String(action.command_id||'')!==String(commandId||''))return false;
const match=action.variant_match&&typeof action.variant_match==='object'?action.variant_match:null;
if(!match)return true;
const actual=params&&typeof params==='object'?params:{};
return Object.keys(match).every(key=>String(actual[key])===String(match[key]));
}
function sidebarFindActionByPreset(actions,commandId,params){
const list=Array.isArray(actions)?actions:[];
return list.find(action=>sidebarActionVariantMatches(action,commandId,params))
||list.find(action=>String(action&&action.command_id||'')===String(commandId||''))
||null;
}
function sidebarExpandRelayActions(baseAction,resourceLabel){
const commandName=String(baseAction&&baseAction.command&&baseAction.command.command||'');
if(commandName==='relay.pulse'){
const params={...(baseAction.base_params||{})};
if(params.duration_ms===undefined||params.duration_ms===null||params.duration_ms==='')params.duration_ms=500;
return [{...baseAction,action_key:`${baseAction.command_id}:pulse`,label:baseAction.label||baseAction.command_id,params}];
}
if(commandName!=='relay.set')return [{...baseAction,action_key:baseAction.command_id,params:{...(baseAction.base_params||{})}}];
const params={...(baseAction.base_params||{})};
const doorLike=sidebarIsDoorLikeResource(resourceLabel);
return [
{...baseAction,action_key:`${baseAction.command_id}:on`,label:doorLike?`Open ${resourceLabel}`:`Turn on ${resourceLabel}`,params:{...params,on:true},variant_match:{on:true},requires_confirmation:doorLike},
{...baseAction,action_key:`${baseAction.command_id}:off`,label:doorLike?`Close ${resourceLabel}`:`Turn off ${resourceLabel}`,params:{...params,on:false},variant_match:{on:false},requires_confirmation:false}
];
}

function sidebarPresetWizardDefault(){
return {open:false,editing_id:'',device_id:'',resource_key:'',action_key:'',command_id:'',label:'',params:{}};
}

function legacySidebarPresetStorageKey(){
const host=(typeof window!=='undefined'&&window.location&&window.location.host)||'embedded';
return `scenehub:gm:sidebar-presets:${host}`;
}

function sidebarPresetCoerceValue(value){
const text=String(value===undefined||value===null?'':value);
if(text!==''&&/^-?\d+$/.test(text))return Number(text);
return text;
}

function normalizeSidebarPresetItem(item,index){
item=item&&typeof item==='object'?item:{};
const label=String(item.label||'').trim();
const deviceId=String(item.device_id||'').trim();
const commandId=String(item.command_id||'').trim();
const resourceKey=String(item.resource_key||'device').trim()||'device';
return {
id:String(item.id||`preset_${index+1}`),
label:label||`Preset ${index+1}`,
device_id:deviceId,
resource_key:resourceKey,
resource_label:String(item.resource_label||'').trim(),
command_id:commandId,
command_label:String(item.command_label||'').trim(),
params:item.params&&typeof item.params==='object'?JSON.parse(JSON.stringify(item.params)):{},
};
}

function applySidebarPresetPayload(data){
const items=Array.isArray(data)?data:(Array.isArray(data&&data.gm_sidebar_presets)?data.gm_sidebar_presets:(Array.isArray(data&&data.presets)?data.presets:[]));
gmQuickPresets=items.map(normalizeSidebarPresetItem).filter(item=>item.device_id&&item.command_id);
pruneSidebarResolvedPresetCache(gmQuickPresets);
gmMarkStaticLoaded('sidebarPresets');
if(!gmQuickPresetWizard||!Object.keys(gmQuickPresetWizard).length)gmQuickPresetWizard=sidebarPresetWizardDefault();
if(typeof renderRightSidebar==='function'&&gmRightSidebarStructureKey){
if(shouldDeferAutoRender())gmQueueDeferredRender('sidebar','',true);
else renderRightSidebar(false);
}
return gmQuickPresets;
}

function sidebarPresets(){
if(gmQuickPresets===null)gmQuickPresets=[];
if(!gmQuickPresetWizard||!Object.keys(gmQuickPresetWizard).length)gmQuickPresetWizard=sidebarPresetWizardDefault();
return Array.isArray(gmQuickPresets)?gmQuickPresets:[];
}

function legacySidebarPresets(){
let items=[];
try{
const raw=typeof localStorage!=='undefined'?localStorage.getItem(legacySidebarPresetStorageKey()):'';
const parsed=raw?JSON.parse(raw):[];
items=(Array.isArray(parsed)?parsed:[]).map(normalizeSidebarPresetItem).filter(item=>item.device_id&&item.command_id);
}
catch(err){
items=[];
}
return items;
}

function legacySidebarPresetCount(){
return legacySidebarPresets().length;
}

function sidebarPresetMigrationPending(){
return !sidebarPresets().length&&legacySidebarPresetCount()>0;
}

async function persistSidebarPresets(items){
const normalized=(Array.isArray(items)?items:[]).map(normalizeSidebarPresetItem);
const data=await api.sidebarPresets.save(normalized);
applySidebarPresetPayload(data);
return sidebarPresets();
}

function sidebarPresetWizard(){
sidebarPresets();
if(!gmQuickPresetWizard||typeof gmQuickPresetWizard!=='object'||!Object.keys(gmQuickPresetWizard).length)gmQuickPresetWizard=sidebarPresetWizardDefault();
return gmQuickPresetWizard;
}

function resetSidebarPresetWizard(preset,open){
const visible=open===undefined?false:!!open;
if(!preset){
gmQuickPresetWizard={...sidebarPresetWizardDefault(),open:visible};
return gmQuickPresetWizard;
}
gmQuickPresetWizard={
open:visible,
editing_id:String(preset.id||''),
device_id:String(preset.device_id||''),
resource_key:String(preset.resource_key||'device'),
action_key:'',
command_id:String(preset.command_id||''),
label:String(preset.label||''),
params:preset.params&&typeof preset.params==='object'?JSON.parse(JSON.stringify(preset.params)):{},
};
return gmQuickPresetWizard;
}

function openSidebarPresetWizard(preset){
return resetSidebarPresetWizard(preset,true);
}

function sidebarManualCommandsForDevice(device){
return questDeviceCommandList(device).filter(cmd=>cmd&&cmd.id&&commandPolicy(cmd).manual_allowed!==false);
}

function sidebarCommandResourceSelector(command){
if(command&&Array.isArray(command.channel_options)){
return {param_key:'channel',target:String(command.target||'channel'),options:command.channel_options.map(option=>({id:String(option&&option.id||''),name:String(option&&option.name||option&&option.id||'').trim()||String(option&&option.id||'')})).filter(option=>option.id)};
}
const schema=Array.isArray(command&&command.args_schema)?command.args_schema:[];
const resourceParam=schema.find(param=>param&&param.type==='resource_channel'&&Array.isArray(param.resource_options));
if(!resourceParam)return null;
return {param_key:String(resourceParam.key||'channel'),target:String(resourceParam.resource_target||command&&command.target||resourceParam.key||'resource'),options:resourceParam.resource_options.map(option=>({id:String(option&&option.id||''),name:String(option&&option.name||option&&option.id||'').trim()||String(option&&option.id||'')})).filter(option=>option.id)};
}

function sidebarResourceGroupsForDevice(device){
const groups=new Map();
sidebarManualCommandsForDevice(device).forEach(command=>{
const defaults=defaultParamsForCommand(device,command);
const selector=sidebarCommandResourceSelector(command);
const schema=Array.isArray(command.args_schema)?command.args_schema:[];
const baseAction={command_id:command.id,label:command.label||command.id,command,base_params:defaults&&typeof defaults==='object'?JSON.parse(JSON.stringify(defaults)):{},schema,resource_param_key:selector&&selector.param_key||'',resource_target:selector&&selector.target||''};
if(selector){
selector.options.forEach(option=>{
const groupKey=`${selector.target||selector.param_key}:${option.id}`;
const resourceLabel=option.name||option.id;
const params={...baseAction.base_params,[selector.param_key]:sidebarPresetCoerceValue(option.id)};
const existing=groups.get(groupKey)||{key:groupKey,label:option.name||option.id,actions:[]};
sidebarExpandRelayActions({...baseAction,resource_label:resourceLabel,resource_value:option.id,base_params:params},resourceLabel).forEach(action=>existing.actions.push(action));
groups.set(groupKey,existing);
});
return;
}
const existing=groups.get('device')||{key:'device',label:'Device actions',actions:[]};
sidebarExpandRelayActions({...baseAction,resource_label:'Device actions',resource_value:'',base_params:{...baseAction.base_params}},'Device actions').forEach(action=>existing.actions.push(action));
groups.set('device',existing);
});
return Array.from(groups.values()).sort((a,b)=>String(a.label||'').localeCompare(String(b.label||'')));
}

function sidebarSourceDevices(){
return questDevices().map(device=>scenarioNormalizeHardwareDevice({
id:device.id||'',
name:device.name||device.id||'',
room_id:device.room_id||'',
device_description:device.device_description,
commands:Array.isArray(device.commands)?device.commands:[],
events:Array.isArray(device.events)?device.events:[]
})).filter(device=>device&&device.id);
}

function sidebarDeviceById(deviceId){
return sidebarSourceDevices().find(device=>device.id===deviceId)||null;
}

function sidebarCommandById(deviceId,commandId){
const device=sidebarDeviceById(deviceId);
return device&&Array.isArray(device.commands)?device.commands.find(cmd=>cmd.id===commandId)||null:null;
}

function sidebarManualDevices(){
return sidebarSourceDevices().filter(device=>device&&device.id&&sidebarResourceGroupsForDevice(device).length);
}

function sidebarWizardDevice(){
const wizard=sidebarPresetWizard();
return sidebarManualDevices().find(device=>device.id===wizard.device_id)||null;
}

function sidebarWizardResources(){
const device=sidebarWizardDevice();
return device?sidebarResourceGroupsForDevice(device):[];
}

function sidebarWizardResource(){
const wizard=sidebarPresetWizard();
return sidebarWizardResources().find(resource=>resource.key===wizard.resource_key)||null;
}

function sidebarWizardActions(){
const resource=sidebarWizardResource();
return resource&&Array.isArray(resource.actions)?resource.actions:[];
}

function sidebarWizardDeviceMeta(device){
if(!device)return '';
const resources=sidebarResourceGroupsForDevice(device);
const actionCount=resources.reduce((sum,resource)=>sum+(Array.isArray(resource.actions)?resource.actions.length:0),0);
return `${resources.length} resource${resources.length===1?'':'s'} / ${actionCount} action${actionCount===1?'':'s'}`;
}

function sidebarWizardResourceOptions(resources){
return (Array.isArray(resources)?resources:[]).map(resource=>{
const actions=Array.isArray(resource&&resource.actions)?resource.actions:[];
const count=actions.length;
return {
id:String(resource&&resource.key||''),
name:`${String(resource&&resource.label||'Resource')} (${count})`
};
});
}

function sidebarWizardActionOptions(actions){
return (Array.isArray(actions)?actions:[]).map(action=>{
const danger=sidebarActionNeedsConfirmation(action);
return {
id:sidebarActionKey(action),
name:`${String(action&&action.label||action&&action.command_id||'Action')}${danger?' [confirm]':''}`
};
});
}

function sidebarWizardAction(){
const wizard=sidebarPresetWizard();
return sidebarWizardActions().find(action=>sidebarActionKey(action)===String(wizard.action_key||''))
||sidebarFindActionByPreset(sidebarWizardActions(),wizard.command_id,wizard.params)
||null;
}

function sidebarWizardApplyDefaults(){
const wizard=sidebarPresetWizard();
const resources=sidebarWizardResources();
if(!wizard.resource_key&&resources.length)wizard.resource_key=String(resources[0].key||'');
const actions=sidebarWizardActions();
let action=sidebarWizardAction();
if(!action&&actions.length){
action=actions[0];
wizard.action_key=sidebarActionKey(action);
wizard.command_id=String(action.command_id||'');
wizard.params=action&&action.params&&typeof action.params==='object'?JSON.parse(JSON.stringify(action.params)):{};
}
if(action&&!wizard.action_key)wizard.action_key=sidebarActionKey(action);
return wizard;
}

function sidebarWizardLabel(device,resource,action){
if(!device||!action)return '';
if(resource&&resource.key!=='device')return `${resource.label} - ${action.label}`;
return `${questDeviceDisplayName(device)} - ${action.label}`;
}

function sidebarPresetParamSchema(action){
const schema=Array.isArray(action&&action.schema)?action.schema:[];
const skipKey=String(action&&action.resource_param_key||'');
return schema.filter(field=>field&&field.key&&field.key!==skipKey);
}

function sidebarPresetParamScope(){
const wizard=sidebarPresetWizard();
const action=sidebarWizardAction();
return `sidebar-preset:${wizard.editing_id||wizard.action_key||sidebarActionKey(action)||wizard.command_id||'new'}`;
}

function sidebarPresetParamModel(){
const action=sidebarWizardAction();
const wizard=sidebarPresetWizard();
const params=wizard.params&&typeof wizard.params==='object'?wizard.params:{};
return {...(action&&action.params&&typeof action.params==='object'?action.params:{}),...params};
}

function sidebarSyncWizardParamsFromDom(){
const action=sidebarWizardAction();
if(!action)return {};
const schema=sidebarPresetParamSchema(action);
const scope=sidebarPresetParamScope();
const root=document.querySelector('[data-sidebar-preset-editor]');
const params={...(action.params&&typeof action.params==='object'?action.params:{}),...(collectFormFields(root,schema,scope)||{})};
sidebarPresetWizard().params=params;
return params;
}

function syncSidebarPresetPreviewFromState(){
const root=document.querySelector('[data-sidebar-preset-editor]');
if(!root)return;
const device=sidebarWizardDevice();
const resource=sidebarWizardResource();
const action=sidebarWizardAction();
const preview=root.querySelector('[data-sidebar-preview]');
const empty=root.querySelector('[data-sidebar-preview-empty]');
if(!device||!action){
if(preview)preview.style.display='none';
if(empty)empty.style.display='';
return;
}
const label=sidebarPresetWizard().label||sidebarWizardLabel(device,resource,action)||'Quick action';
const labelEl=root.querySelector('[data-sidebar-preview-label]');
const pathEl=root.querySelector('[data-sidebar-preview-path]');
const deviceEl=root.querySelector('[data-sidebar-preview-device]');
if(labelEl)labelEl.textContent=label;
if(deviceEl)deviceEl.textContent=questDeviceDisplayName(device);
if(pathEl)pathEl.textContent=`${resource&&resource.label||'Device actions'} -> ${action.label||action.command_id}`;
if(preview)preview.style.display='';
if(empty)empty.style.display='none';
}

function sidebarPresetActionSummary(preset){
const resolved=resolveSidebarPreset(preset);
if(!resolved)return {device_name:preset.device_id||'Missing device',resource_label:preset.resource_label||'Missing resource',command_label:preset.command_label||preset.command_id||'Missing command'};
return {device_name:resolved.device_name,resource_label:resolved.resource_label,command_label:resolved.command_label};
}

const gmSidebarResolvedPresetCache=new Map();

function sidebarPresetResolveSignature(preset){
return JSON.stringify({
device_id:String(preset&&preset.device_id||''),
resource_key:String(preset&&preset.resource_key||''),
command_id:String(preset&&preset.command_id||''),
params:preset&&preset.params&&typeof preset.params==='object'?preset.params:{}
});
}

function pruneSidebarResolvedPresetCache(items){
const allowed=new Set((Array.isArray(items)?items:[]).map(item=>String(item&&item.id||'')).filter(Boolean));
Array.from(gmSidebarResolvedPresetCache.keys()).forEach(key=>{
if(!allowed.has(key))gmSidebarResolvedPresetCache.delete(key);
});
}

function sidebarResolvedPresetFromCache(preset){
const presetId=String(preset&&preset.id||'');
if(!presetId)return null;
const cached=gmSidebarResolvedPresetCache.get(presetId);
if(!cached||cached.sig!==sidebarPresetResolveSignature(preset))return null;
return {
...cached.value,
id:presetId,
label:String(preset&&preset.label||'').trim()||cached.value.label||cached.value.command_label||cached.value.command_id||'Quick action',
params:preset&&preset.params&&typeof preset.params==='object'?preset.params:(cached.value.params||{})
};
}

function cacheResolvedSidebarPreset(preset,resolved){
const presetId=String(preset&&preset.id||'');
if(!presetId||!resolved)return resolved;
gmSidebarResolvedPresetCache.set(presetId,{
sig:sidebarPresetResolveSignature(preset),
value:{
...resolved,
params:resolved.params&&typeof resolved.params==='object'?JSON.parse(JSON.stringify(resolved.params)):{}
}
});
return resolved;
}

function resolveSidebarPresetFallback(preset){
return sidebarResolvedPresetFromCache(preset)||{
id:preset.id,
label:preset.label||preset.command_label||preset.command_id||'Quick action',
device:null,
device_id:preset.device_id,
device_name:preset.device_id||'Device',
device_health:'unknown',
resource_key:preset.resource_key||'device',
resource_label:preset.resource_label||'Device actions',
command:null,
command_id:preset.command_id,
command_label:preset.command_label||preset.command_id,
params:preset.params&&typeof preset.params==='object'?preset.params:{},
requires_confirmation:false
};
}

function resolveSidebarPreset(preset){
if(!preset||!preset.device_id||!preset.command_id)return null;
const device=sidebarDeviceById(preset.device_id)||questDeviceById(preset.device_id);
const liveDevice=questDeviceById(preset.device_id)||device;
if(!device)return resolveSidebarPresetFallback(preset);
const resources=sidebarResourceGroupsForDevice(device);
const resource=resources.find(item=>item.key===preset.resource_key)||resources.find(item=>item.actions.some(action=>action.command_id===preset.command_id))||null;
const action=sidebarFindActionByPreset(resource&&resource.actions,preset.command_id,preset.params)
||sidebarManualCommandsForDevice(device).find(cmd=>cmd.id===preset.command_id)&&{command_id:preset.command_id,label:(sidebarCommandById(preset.device_id,preset.command_id)&&sidebarCommandById(preset.device_id,preset.command_id).label)||preset.command_id,command:sidebarCommandById(preset.device_id,preset.command_id),params:preset.params&&typeof preset.params==='object'?preset.params:{},resource_label:preset.resource_label||'Device actions'};
const command=action&&(action.command||sidebarCommandById(preset.device_id,preset.command_id))||sidebarCommandById(preset.device_id,preset.command_id);
if(!command)return resolveSidebarPresetFallback(preset);
const params={...(action&&action.params&&typeof action.params==='object'?action.params:{}),...(preset.params&&typeof preset.params==='object'?preset.params:{})};
const deviceName=questDeviceDisplayName(liveDevice||device);
return cacheResolvedSidebarPreset(preset,{
id:preset.id,
label:preset.label||sidebarWizardLabel(device,resource,action||{label:(command&&command.label)||preset.command_id}),
device:liveDevice||device,
device_id:device.id||preset.device_id,
device_name:deviceName,
device_health:questDeviceHealth(liveDevice||device),
resource_key:resource&&resource.key||preset.resource_key||'device',
resource_label:resource&&resource.label||preset.resource_label||'Device actions',
command,
command_id:command.id||preset.command_id,
command_label:action&&action.label||command.label||preset.command_label||preset.command_id,
params,
requires_confirmation:sidebarActionNeedsConfirmation(action||{command})
});
}

function sidebarPresetGroups(){
const groups=new Map();
sidebarPresets().forEach(preset=>{
const resolved=resolveSidebarPreset(preset);
if(!resolved)return;
const groupKey=resolved.device_id;
const existing=groups.get(groupKey)||{id:groupKey,name:resolved.device_name,health:resolved.device_health,items:[]};
existing.health=resolved.device_health;
existing.items.push(resolved);
groups.set(groupKey,existing);
});
return Array.from(groups.values());
}

function sidebarPresetAdminGroups(){
const groups=new Map();
sidebarPresets().forEach((preset,index)=>{
const resolved=resolveSidebarPreset(preset);
const groupKey=String((resolved&&resolved.device_id)||preset.device_id||`missing_${index}`);
const summary=resolved
?{device_name:resolved.device_name,resource_label:resolved.resource_label,command_label:resolved.command_label}
:sidebarPresetActionSummary(preset);
const existing=groups.get(groupKey)||{
id:groupKey,
name:(resolved&&resolved.device_name)||summary.device_name,
health:(resolved&&resolved.device_health)||'unknown',
items:[]
};
if(resolved){
existing.name=resolved.device_name;
existing.health=resolved.device_health;
}
existing.items.push({preset,summary,index});
groups.set(groupKey,existing);
});
return Array.from(groups.values());
}

let gmRightSidebarStructureKey='';
let gmRightSidebarRuntimeKey='';
function rightSidebarStructureKey(groups){
return JSON.stringify({
admin:isAdmin(),
groups:groups.map(group=>({id:group.id,items:group.items.map(item=>({id:item.id,label:item.label,command_id:item.command_id,resource:item.resource_label,danger:item.requires_confirmation,params:item.params}))}))
});
}

function rightSidebarRuntimeKey(groups){
return JSON.stringify({
admin:isAdmin(),
groups:groups.map(group=>({id:group.id,name:group.name,health:group.health,count:group.items.length}))
});
}

function renderRightSidebar(force){
const root=document.getElementById('gm_right_sidebar');
if(!root)return;
const groups=sidebarPresetGroups();
const structureKey=rightSidebarStructureKey(groups);
const runtimeKey=rightSidebarRuntimeKey(groups);
if(!force&&gmRightSidebarStructureKey===structureKey&&gmRightSidebarRuntimeKey===runtimeKey){
gmStatInc('render.sidebar_skip');
return;
}
gmStatInc(force?'render.sidebar_forced':'render.sidebar');
gmRightSidebarStructureKey=structureKey;
gmRightSidebarRuntimeKey=runtimeKey;
root.innerHTML=`<div class='right-brand'><h2>Quick access</h2><p>Operator device actions</p></div><div class='manual-groups'>${groups.length?groups.map(group=>`<section class='manual-group'><div class='manual-group-head'><div><div class='manual-title'>${esc(group.name)}</div><div class='manual-meta'>${esc(group.items.length)} quick action${group.items.length===1?'':'s'}</div></div>${status(group.health)}</div><div class='manual-buttons'>${group.items.map(item=>uiButton({label:item.label,action:'manual.device.command',kind:item.requires_confirmation?'danger':'',dataset:{'device-id':item.device_id,'command-id':item.command_id,params:JSON.stringify(item.params||{})},confirm:item.requires_confirmation?`Run "${item.label}"?`:''})).join('')}</div></section>`).join(''):uiEmpty('No quick actions configured. Admin can add them in Device Controls.')}</div>`;
}

function renderSidebarPresetRow(preset,index,total){
const summary=sidebarPresetActionSummary(preset);
const wizard=sidebarPresetWizard();
return `<div class='row-card preset-row admin-item-card ${wizard.open&&wizard.editing_id===preset.id?'selected-row':''}'><div class='admin-item-main'><div class='admin-item-title-row'><div class='row-title'>${esc(preset.label||`Preset ${index+1}`)}</div></div><div class='admin-item-meta'><span>${esc(summary.device_name)}</span><span>${esc(summary.resource_label)}</span><span>${esc(summary.command_label)}</span></div></div><div class='admin-item-side'><div class='actions'>${uiButton({label:'Edit',kind:'small-btn',action:'sidebar.preset.edit',dataset:{'preset-id':preset.id}})}${uiButton({label:'Test',kind:'small-btn',action:'sidebar.preset.run',dataset:{'preset-id':preset.id}})}${uiIconButton({label:'Up',title:'Move up',action:'sidebar.preset.move',dataset:{'preset-id':preset.id,direction:'up'},disabled:index<=0})}${uiIconButton({label:'Down',title:'Move down',action:'sidebar.preset.move',dataset:{'preset-id':preset.id,direction:'down'},disabled:index>=total-1})}${uiButton({label:'Delete',kind:'danger small-btn',action:'sidebar.preset.delete',dataset:{'preset-id':preset.id},confirm:`Delete quick action "${preset.label}"?`})}</div></div></div>`;
}

function renderSidebarPresetGroupItem(entry,total){
const preset=entry&&entry.preset?entry.preset:{};
const summary=entry&&entry.summary?entry.summary:sidebarPresetActionSummary(preset);
const index=Number(entry&&entry.index)||0;
const wizard=sidebarPresetWizard();
return `<div class='preset-device-item ${wizard.open&&wizard.editing_id===preset.id?'selected-row':''}'><div class='admin-item-main'><div class='admin-item-title-row'><div class='row-title'>${esc(preset.label||`Preset ${index+1}`)}</div></div><div class='admin-item-meta'><span>${esc(summary.resource_label)}</span><span>${esc(summary.command_label)}</span></div></div><div class='admin-item-side'><div class='actions'>${uiButton({label:'Edit',kind:'small-btn',action:'sidebar.preset.edit',dataset:{'preset-id':preset.id}})}${uiButton({label:'Test',kind:'small-btn',action:'sidebar.preset.run',dataset:{'preset-id':preset.id}})}${uiIconButton({label:'Up',title:'Move up',action:'sidebar.preset.move',dataset:{'preset-id':preset.id,direction:'up'},disabled:index<=0})}${uiIconButton({label:'Down',title:'Move down',action:'sidebar.preset.move',dataset:{'preset-id':preset.id,direction:'down'},disabled:index>=total-1})}${uiButton({label:'Delete',kind:'danger small-btn',action:'sidebar.preset.delete',dataset:{'preset-id':preset.id},confirm:`Delete quick action "${preset.label}"?`})}</div></div></div>`;
}

function renderSidebarPresetGroupCard(group,total){
return `<section class='manual-group admin-item-card preset-device-group'><div class='manual-group-head'><div><div class='manual-title'>${esc(group.name)}</div><div class='manual-meta'>${esc(group.items.length)} quick action${group.items.length===1?'':'s'}</div></div>${status(group.health)}</div><div class='preset-device-list'>${group.items.map(entry=>renderSidebarPresetGroupItem(entry,total)).join('')}</div></section>`;
}

function renderSidebarPresetWizard(){
const wizard=sidebarWizardApplyDefaults();
if(!wizard.open)return '';
const devices=sidebarManualDevices();
const device=sidebarWizardDevice();
const resources=sidebarWizardResources();
const resource=sidebarWizardResource();
const actions=sidebarWizardActions();
const action=sidebarWizardAction();
const resourceOptions=sidebarWizardResourceOptions(resources);
const actionOptions=sidebarWizardActionOptions(actions);
const labelValue=wizard.label||sidebarWizardLabel(device,resource,action)||'';
const paramsSchema=sidebarPresetParamSchema(action);
const paramsScope=sidebarPresetParamScope();
const paramsHtml=action&&paramsSchema.length?`<div class='preset-step-card preset-step-wide'><div class='preset-step-head'><span class='preset-step-index'>4</span><div><h2 class='section-title'>Fixed parameters</h2><div class='card-sub'>Lock channel, effect or mode into this operator button.</div></div></div><div class='field-grid'>${renderFormFields(paramsSchema,sidebarPresetParamModel(),paramsScope)}</div></div>`:(action?`<div class='preset-step-card preset-step-wide'><div class='preset-step-head'><span class='preset-step-index'>4</span><div><h2 class='section-title'>Fixed parameters</h2><div class='card-sub'>This action does not need extra fixed values.</div></div></div></div>`:`<div class='preset-step-card preset-step-wide muted-step'><div class='preset-step-head'><span class='preset-step-index'>4</span><div><h2 class='section-title'>Fixed parameters</h2><div class='card-sub'>Choose an action to configure extra values.</div></div></div></div>`);
const preview=device&&action?`<div class='preset-preview' data-sidebar-preview='1'><div class='preset-preview-label' data-sidebar-preview-label='1'>${esc(labelValue||'Quick action')}</div><div class='row-meta' data-sidebar-preview-device='1'>${esc(questDeviceDisplayName(device))}</div><div class='preset-preview-path' data-sidebar-preview-path='1'>${esc(resource&&resource.label||'Device actions')} -> ${esc(action.label||action.command_id)}</div></div><div class='manual-empty' data-sidebar-preview-empty='1' style='display:none'>Choose device, resource and action to preview the sidebar item.</div>`:`<div class='preset-preview' data-sidebar-preview='1' style='display:none'><div class='preset-preview-label' data-sidebar-preview-label='1'></div><div class='row-meta' data-sidebar-preview-device='1'></div><div class='preset-preview-path' data-sidebar-preview-path='1'></div></div><div class='manual-empty' data-sidebar-preview-empty='1'>Choose device, resource and action to preview the sidebar item.</div>`;
return uiOverlayCard({
title:wizard.editing_id?'Edit quick action':'New quick action',
subtitle:'Build one operator-facing sidebar button from a saved device command.',
closeAction:'sidebar.preset.cancel',
className:'card preset-modal-card',
dataset:{'sidebar-preset-modal':'1'},
content:`<section data-sidebar-preset-editor='1'><div class='preset-wizard-grid'><div class='preset-step-card'><div class='preset-step-head'><span class='preset-step-index'>1</span><div><h2 class='section-title'>Device</h2><div class='card-sub'>Pick the saved quest device.</div></div></div>${devices.length?`<select class='scenario-select preset-select' data-sidebar-preset-field='device_id'>${optionList(devices,wizard.device_id,'Select saved device')}</select><div class='row-meta'>${device?esc(sidebarWizardDeviceMeta(device)):'Only saved devices with manual actions appear here.'}</div>`:`<div class='manual-empty'>No saved devices with manual actions available.</div>`}</div><div class='preset-step-card'><div class='preset-step-head'><span class='preset-step-index'>2</span><div><h2 class='section-title'>Resource</h2><div class='card-sub'>Choose the relay, MOSFET, output or other target.</div></div></div>${device?`<select class='scenario-select preset-select' data-sidebar-preset-field='resource_key' ${resourceOptions.length?'':'disabled'}>${optionList(resourceOptions,wizard.resource_key,'Select resource')}</select><div class='row-meta'>${resource?esc(`${resource.actions.length} action${resource.actions.length===1?'':'s'} available`):(resourceOptions.length?'Pick the exact channel/resource for the operator button.':'This device has no selectable resources for manual actions.')}</div>`:`<div class='manual-empty'>Choose a device first.</div>`}</div><div class='preset-step-card'><div class='preset-step-head'><span class='preset-step-index'>3</span><div><h2 class='section-title'>Action</h2><div class='card-sub'>Pick what the operator button will do.</div></div></div>${resource?`<select class='scenario-select preset-select' data-sidebar-preset-field='action_key' ${actionOptions.length?'':'disabled'}>${optionList(actionOptions,wizard.action_key,'Select action')}</select><div class='row-meta'>${action?esc(action.command&&action.command.command||action.command_id):(actionOptions.length?'Choose the action template for this resource.':'No manual actions available for this resource.')}</div>`:`<div class='manual-empty'>Choose a resource first.</div>`}</div>${paramsHtml}<div class='preset-step-card preset-step-wide'><div class='preset-step-head'><span class='preset-step-index'>5</span><div><h2 class='section-title'>Operator label</h2><div class='card-sub'>Use room language, not technical ids.</div></div></div><label class='field-stack'><span>Sidebar name</span><input data-sidebar-preset-field='label' placeholder='Open secret door' value='${esc(labelValue)}'></label><div class='row-meta'>Examples: Open secret door, Blink red beacon, Pulse lock.</div></div><div class='preset-step-card preset-step-wide'><div class='preset-step-head'><span class='preset-step-index'>6</span><div><h2 class='section-title'>Preview</h2><div class='card-sub'>This is how the quick action will read for the operator.</div></div></div>${preview}</div></div><div class='actions sticky-actions'>${uiButton({label:wizard.editing_id?'Save changes':'Add to sidebar',action:'sidebar.preset.save',disabled:!action})}${uiButton({label:'Cancel',action:'sidebar.preset.cancel'})}</div><div class='row-meta'>Presets are stored on the controller: /sdcard/quest/gm_sidebar_presets.json</div></section>`
});
}

function syncSidebarPresetWizardFromDom(){
const root=document.querySelector('[data-sidebar-preset-editor]');
const wizard=sidebarPresetWizard();
if(!root)return wizard;
const field=name=>root.querySelector(`[data-sidebar-preset-field="${name}"]`);
wizard.device_id=(field('device_id')&&field('device_id').value||wizard.device_id||'').trim();
wizard.resource_key=(field('resource_key')&&field('resource_key').value||wizard.resource_key||'').trim();
wizard.action_key=(field('action_key')&&field('action_key').value||wizard.action_key||'').trim();
wizard.label=(field('label')&&field('label').value||wizard.label||'').trim();
const action=sidebarWizardAction();
wizard.command_id=action&&action.command_id||wizard.command_id||'';
sidebarSyncWizardParamsFromDom();
return wizard;
}

function sidebarPresetFromWizard(){
const wizard=syncSidebarPresetWizardFromDom();
const device=sidebarWizardDevice();
const resource=sidebarWizardResource();
const action=sidebarWizardAction();
if(!device||!action)throw new Error('Choose device, resource and action');
const label=(wizard.label||sidebarWizardLabel(device,resource,action)||'').trim();
if(!label)throw new Error('Enter operator label');
return normalizeSidebarPresetItem({
id:wizard.editing_id||`preset_${Date.now().toString(16)}`,
label,
device_id:device.id||wizard.device_id,
resource_key:resource&&resource.key||wizard.resource_key||'device',
resource_label:resource&&resource.label||'Device actions',
command_id:action.command_id,
command_label:action.label||action.command_id,
params:wizard.params&&typeof wizard.params==='object'?wizard.params:{},
});
}

function sidebarPresetById(presetId){
return sidebarPresets().find(item=>item.id===presetId)||null;
}

function editSidebarPreset(presetId){
const preset=sidebarPresetById(presetId);
if(!preset)throw new Error('Preset not found');
openSidebarPresetWizard(preset);
clearTransientFieldDirty();
render();
}

async function deleteSidebarPreset(presetId){
const items=sidebarPresets().filter(item=>item.id!==presetId);
await persistSidebarPresets(items);
if(sidebarPresetWizard().editing_id===presetId)resetSidebarPresetWizard();
clearTransientFieldDirty();
render();
}

async function moveSidebarPreset(presetId,direction){
const items=sidebarPresets().slice();
const index=items.findIndex(item=>item.id===presetId);
if(index<0)return;
const target=direction==='up'?index-1:index+1;
if(target<0||target>=items.length)return;
const swap=items[index];
items[index]=items[target];
items[target]=swap;
await persistSidebarPresets(items);
render();
}

function handleSidebarPresetFieldChange(field){
const wizard=sidebarPresetWizard();
if(!field)return false;
if((field.dataset.sidebarPresetField||'')==='device_id'){
wizard.device_id=field.value||'';
wizard.resource_key='';
wizard.action_key='';
wizard.command_id='';
wizard.label='';
wizard.params={};
sidebarWizardApplyDefaults();
render();
return true;
}
if((field.dataset.sidebarPresetField||'')==='resource_key'){
wizard.resource_key=field.value||'';
wizard.action_key='';
wizard.command_id='';
wizard.label='';
wizard.params={};
sidebarWizardApplyDefaults();
render();
return true;
}
if((field.dataset.sidebarPresetField||'')==='action_key'){
wizard.action_key=field.value||'';
const device=sidebarWizardDevice();
const resource=sidebarWizardResource();
const action=sidebarWizardAction();
wizard.command_id=action&&action.command_id||'';
wizard.params=action&&action.params&&typeof action.params==='object'?JSON.parse(JSON.stringify(action.params)):{};
if(!wizard.label)wizard.label=sidebarWizardLabel(device,resource,action);
render();
return true;
}
if((field.dataset.sidebarPresetField||'')==='label'){
wizard.label=field.value||'';
syncSidebarPresetPreviewFromState();
return true;
}
if((field.dataset.scope||'')===sidebarPresetParamScope()){
sidebarSyncWizardParamsFromDom();
return true;
}
return false;
}

function handleSidebarPresetFieldInput(field){
if(!field)return false;
if((field.dataset.sidebarPresetField||'')==='label'){
sidebarPresetWizard().label=field.value||'';
syncSidebarPresetPreviewFromState();
return true;
}
if((field.dataset.scope||'')===sidebarPresetParamScope()){
sidebarSyncWizardParamsFromDom();
return true;
}
return false;
}

function sidebarPresetWizardValidationErrors(action,params){
const schema=sidebarPresetParamSchema(action).map(field=>({...field,required:field.required||!field.optional}));
return validateFormFields(params||{},schema);
}

async function saveSidebarPresetWizard(){
const next=sidebarPresetFromWizard();
const action=sidebarWizardAction();
const errors=sidebarPresetWizardValidationErrors(action,next.params);
if(errors.length)throw new Error(errors[0]);
const items=sidebarPresets().slice();
const index=items.findIndex(item=>item.id===next.id);
if(index>=0)items[index]=next;
else items.push(next);
await persistSidebarPresets(items);
resetSidebarPresetWizard(null,false);
clearTransientFieldDirty();
render();
setGMStatus('Quick action saved','gm-ok');
}

function cancelSidebarPresetWizard(){
resetSidebarPresetWizard(null,false);
clearTransientFieldDirty();
render();
}

async function runSidebarPreset(presetId){
const preset=sidebarPresetById(presetId);
const resolved=resolveSidebarPreset(preset);
if(!resolved)throw new Error('Preset is incomplete');
const confirmed=!resolved.requires_confirmation||confirm(`Run "${resolved.label}"?`);
if(!confirmed)return;
await runManualDeviceCommand(resolved.device_id,resolved.command_id,resolved.params,resolved.requires_confirmation);
}

async function importLegacySidebarPresets(){
const legacy=legacySidebarPresets();
if(!legacy.length)throw new Error('No browser presets available for import');
const data=await api.sidebarPresets.save(legacy);
applySidebarPresetPayload(data);
resetSidebarPresetWizard(null,false);
clearTransientFieldDirty();
render();
setGMStatus('Legacy browser presets imported','gm-ok');
}
