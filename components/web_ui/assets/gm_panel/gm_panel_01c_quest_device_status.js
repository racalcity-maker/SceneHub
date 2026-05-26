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
function questDeviceMonitorRow(dev){const observed=observedByClientId(dev.client_id||dev.id);const health=questDeviceHealth(dev);const meta=[questDeviceCapabilityMeta(dev),dev.enabled===false?'disabled':'enabled'].join(' / ');const setup=isAdmin()?uiButton({label:'Device Setup',action:'device.setup.open',dataset:{'device-id':dev.id||'1'}}):'';const debug=isAdmin()?`<details class='scenario-advanced'><summary>Debug ids</summary><div class='row-meta'>Device ID: ${esc(dev.id||'')}</div><div class='row-meta'>Client: ${esc(dev.client_id||'none')}</div></details>`:'';return `<div class='row-card'><div class='row-main'><div class='row-title'>${esc(questDeviceDisplayName(dev))} ${dev.enabled===false?`<span class='badge'>disabled</span>`:''}</div><div class='row-meta'>${esc(meta)}</div><div class='row-meta'>${observed?`${esc(observed.connectivity||'unknown')} / fw ${esc(observed.fw_version||'n/a')}`:'not observed'}</div>${debug}</div><div>${status(health)}<div class='row-meta'>${esc(questDeviceStatusText(dev))}</div></div><div class='actions'>${setup}</div></div>`;}
function commandPolicy(cmd){return cmd&&cmd.policy&&typeof cmd.policy==='object'?cmd.policy:{};}
function commandRequiresConfirmation(cmd){const p=commandPolicy(cmd);return !!p.requires_confirmation||(p.danger_level&&p.danger_level!=='normal');}

function sidebarPresetWizardDefault(){
return {editing_id:'',device_id:'',resource_key:'',command_id:'',label:'',params:{}};
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
gmMarkStaticLoaded('sidebarPresets');
if(!gmQuickPresetWizard||!Object.keys(gmQuickPresetWizard).length)gmQuickPresetWizard=sidebarPresetWizardDefault();
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

function resetSidebarPresetWizard(preset){
if(!preset){
gmQuickPresetWizard=sidebarPresetWizardDefault();
return gmQuickPresetWizard;
}
gmQuickPresetWizard={
editing_id:String(preset.id||''),
device_id:String(preset.device_id||''),
resource_key:String(preset.resource_key||'device'),
command_id:String(preset.command_id||''),
label:String(preset.label||''),
params:preset.params&&typeof preset.params==='object'?JSON.parse(JSON.stringify(preset.params)):{},
};
return gmQuickPresetWizard;
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
const params={...baseAction.base_params,[selector.param_key]:sidebarPresetCoerceValue(option.id)};
const existing=groups.get(groupKey)||{key:groupKey,label:option.name||option.id,actions:[]};
existing.actions.push({...baseAction,resource_label:option.name||option.id,resource_value:option.id,params});
groups.set(groupKey,existing);
});
return;
}
const existing=groups.get('device')||{key:'device',label:'Device actions',actions:[]};
existing.actions.push({...baseAction,resource_label:'Device actions',resource_value:'',params:{...baseAction.base_params}});
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
const command=action&&action.command||null;
const danger=commandRequiresConfirmation(command);
return {
id:String(action&&action.command_id||''),
name:`${String(action&&action.label||action&&action.command_id||'Action')}${danger?' [confirm]':''}`
};
});
}

function sidebarWizardAction(){
const wizard=sidebarPresetWizard();
return sidebarWizardActions().find(action=>action.command_id===wizard.command_id)||null;
}

function sidebarWizardApplyDefaults(){
const wizard=sidebarPresetWizard();
const resources=sidebarWizardResources();
if(!wizard.resource_key&&resources.length)wizard.resource_key=String(resources[0].key||'');
const actions=sidebarWizardActions();
if(!wizard.command_id&&actions.length){
wizard.command_id=String(actions[0].command_id||'');
const action=sidebarWizardAction();
wizard.params=action&&action.params&&typeof action.params==='object'?JSON.parse(JSON.stringify(action.params)):{};
}
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
return `sidebar-preset:${wizard.editing_id||wizard.command_id||action&&action.command_id||'new'}`;
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

function resolveSidebarPreset(preset){
if(!preset||!preset.device_id||!preset.command_id)return null;
const device=sidebarDeviceById(preset.device_id)||questDeviceById(preset.device_id);
const liveDevice=questDeviceById(preset.device_id)||device;
if(!device)return null;
const resources=sidebarResourceGroupsForDevice(device);
const resource=resources.find(item=>item.key===preset.resource_key)||resources.find(item=>item.actions.some(action=>action.command_id===preset.command_id))||null;
const action=resource&&resource.actions.find(item=>item.command_id===preset.command_id)||sidebarManualCommandsForDevice(device).find(cmd=>cmd.id===preset.command_id)&&{command_id:preset.command_id,label:(sidebarCommandById(preset.device_id,preset.command_id)&&sidebarCommandById(preset.device_id,preset.command_id).label)||preset.command_id,command:sidebarCommandById(preset.device_id,preset.command_id),params:preset.params&&typeof preset.params==='object'?preset.params:{},resource_label:preset.resource_label||'Device actions'};
const command=action&&(action.command||sidebarCommandById(preset.device_id,preset.command_id))||sidebarCommandById(preset.device_id,preset.command_id);
if(!command)return null;
const params={...(action&&action.params&&typeof action.params==='object'?action.params:{}),...(preset.params&&typeof preset.params==='object'?preset.params:{})};
const deviceName=questDeviceDisplayName(liveDevice||device);
return {
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
command_label:command.label||preset.command_label||preset.command_id,
params,
requires_confirmation:commandRequiresConfirmation(command)
};
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

let gmRightSidebarRenderKey='';
function rightSidebarRenderKey(groups){
return JSON.stringify({
admin:isAdmin(),
groups:groups.map(group=>({id:group.id,name:group.name,health:group.health,items:group.items.map(item=>({id:item.id,label:item.label,command_id:item.command_id,resource:item.resource_label,danger:item.requires_confirmation,params:item.params}))}))
});
}

function renderRightSidebar(force){
const root=document.getElementById('gm_right_sidebar');
if(!root)return;
const groups=sidebarPresetGroups();
const key=rightSidebarRenderKey(groups);
if(!force&&gmRightSidebarRenderKey===key)return;
gmRightSidebarRenderKey=key;
root.innerHTML=`<div class='right-brand'><h2>Quick access</h2><p>Operator device actions</p></div><div class='manual-groups'>${groups.length?groups.map(group=>`<section class='manual-group'><div class='manual-group-head'><div><div class='manual-title'>${esc(group.name)}</div><div class='manual-meta'>${esc(group.items.length)} quick action${group.items.length===1?'':'s'}</div></div>${status(group.health)}</div><div class='manual-buttons'>${group.items.map(item=>uiButton({label:item.label,action:'manual.device.command',kind:item.requires_confirmation?'danger':'',dataset:{'device-id':item.device_id,'command-id':item.command_id,params:JSON.stringify(item.params||{})},confirm:item.requires_confirmation?`Run "${item.label}"?`:''})).join('')}</div>${isAdmin()?uiDetails({summary:'Preset details',content:group.items.map(item=>`<div class='row-meta'><strong>${esc(item.label)}</strong> - ${esc(item.resource_label)} / ${esc(item.command_label)}</div>`).join('')}):''}</section>`).join(''):uiEmpty('No quick actions configured. Admin can add them in Device Controls.')}</div>`;
}

function renderSidebarPresetRow(preset,index,total){
const summary=sidebarPresetActionSummary(preset);
return `<div class='row-card preset-row ${sidebarPresetWizard().editing_id===preset.id?'selected-row':''}'><div class='row-main'><div class='row-title'>${esc(preset.label||`Preset ${index+1}`)}</div><div class='row-meta'>${esc(summary.device_name)} / ${esc(summary.resource_label)} / ${esc(summary.command_label)}</div></div><div class='actions'>${uiButton({label:'Edit',kind:'small-btn',action:'sidebar.preset.edit',dataset:{'preset-id':preset.id}})}${uiButton({label:'Test',kind:'small-btn',action:'sidebar.preset.run',dataset:{'preset-id':preset.id}})}${uiIconButton({label:'Up',title:'Move up',action:'sidebar.preset.move',dataset:{'preset-id':preset.id,direction:'up'},disabled:index<=0})}${uiIconButton({label:'Down',title:'Move down',action:'sidebar.preset.move',dataset:{'preset-id':preset.id,direction:'down'},disabled:index>=total-1})}${uiButton({label:'Delete',kind:'danger small-btn',action:'sidebar.preset.delete',dataset:{'preset-id':preset.id},confirm:`Delete quick action "${preset.label}"?`})}</div></div>`;
}

function renderSidebarPresetWizard(){
const wizard=sidebarWizardApplyDefaults();
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
return `<section class='card' data-sidebar-preset-editor='1'><div class='card-head'><div><h2 class='section-title'>${wizard.editing_id?'Edit quick action':'New quick action'}</h2><div class='card-sub'>Build one operator-facing sidebar button from a saved device command.</div></div><div class='actions'>${uiButton({label:'New preset',action:'sidebar.preset.new'})}</div></div><div class='preset-wizard-grid'><div class='preset-step-card'><div class='preset-step-head'><span class='preset-step-index'>1</span><div><h2 class='section-title'>Device</h2><div class='card-sub'>Pick the saved quest device.</div></div></div>${devices.length?`<select class='scenario-select preset-select' data-sidebar-preset-field='device_id'>${optionList(devices,wizard.device_id,'Select saved device')}</select><div class='row-meta'>${device?esc(sidebarWizardDeviceMeta(device)):'Only saved devices with manual actions appear here.'}</div>`:`<div class='manual-empty'>No saved devices with manual actions available.</div>`}</div><div class='preset-step-card'><div class='preset-step-head'><span class='preset-step-index'>2</span><div><h2 class='section-title'>Resource</h2><div class='card-sub'>Choose the relay, MOSFET, output or other target.</div></div></div>${device?`<select class='scenario-select preset-select' data-sidebar-preset-field='resource_key' ${resourceOptions.length?'':'disabled'}>${optionList(resourceOptions,wizard.resource_key,'Select resource')}</select><div class='row-meta'>${resource?esc(`${resource.actions.length} action${resource.actions.length===1?'':'s'} available`):(resourceOptions.length?'Pick the exact channel/resource for the operator button.':'This device has no selectable resources for manual actions.')}</div>`:`<div class='manual-empty'>Choose a device first.</div>`}</div><div class='preset-step-card'><div class='preset-step-head'><span class='preset-step-index'>3</span><div><h2 class='section-title'>Action</h2><div class='card-sub'>Pick what the operator button will do.</div></div></div>${resource?`<select class='scenario-select preset-select' data-sidebar-preset-field='command_id' ${actionOptions.length?'':'disabled'}>${optionList(actionOptions,wizard.command_id,'Select action')}</select><div class='row-meta'>${action?esc(action.command&&action.command.command||action.command_id):(actionOptions.length?'Choose the action template for this resource.':'No manual actions available for this resource.')}</div>`:`<div class='manual-empty'>Choose a resource first.</div>`}</div>${paramsHtml}<div class='preset-step-card preset-step-wide'><div class='preset-step-head'><span class='preset-step-index'>5</span><div><h2 class='section-title'>Operator label</h2><div class='card-sub'>Use room language, not technical ids.</div></div></div><label class='field-stack'><span>Sidebar name</span><input data-sidebar-preset-field='label' placeholder='Open secret door' value='${esc(labelValue)}'></label><div class='row-meta'>Examples: Open secret door, Blink red beacon, Pulse lock.</div></div><div class='preset-step-card preset-step-wide'><div class='preset-step-head'><span class='preset-step-index'>6</span><div><h2 class='section-title'>Preview</h2><div class='card-sub'>This is how the quick action will read for the operator.</div></div></div>${preview}</div></div><div class='actions sticky-actions'>${uiButton({label:wizard.editing_id?'Save changes':'Add to sidebar',action:'sidebar.preset.save',disabled:!action})}${wizard.editing_id?uiButton({label:'Cancel edit',action:'sidebar.preset.cancel'}):''}</div><div class='row-meta'>Presets are stored on the controller: /sdcard/quest/gm_sidebar_presets.json</div></section>`;
}

function syncSidebarPresetWizardFromDom(){
const root=document.querySelector('[data-sidebar-preset-editor]');
const wizard=sidebarPresetWizard();
if(!root)return wizard;
const field=name=>root.querySelector(`[data-sidebar-preset-field="${name}"]`);
wizard.device_id=(field('device_id')&&field('device_id').value||wizard.device_id||'').trim();
wizard.resource_key=(field('resource_key')&&field('resource_key').value||wizard.resource_key||'').trim();
wizard.command_id=(field('command_id')&&field('command_id').value||wizard.command_id||'').trim();
wizard.label=(field('label')&&field('label').value||wizard.label||'').trim();
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
resetSidebarPresetWizard(preset);
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
wizard.command_id='';
wizard.label='';
wizard.params={};
sidebarWizardApplyDefaults();
render();
return true;
}
if((field.dataset.sidebarPresetField||'')==='resource_key'){
wizard.resource_key=field.value||'';
wizard.command_id='';
wizard.label='';
wizard.params={};
sidebarWizardApplyDefaults();
render();
return true;
}
if((field.dataset.sidebarPresetField||'')==='command_id'){
wizard.command_id=field.value||'';
const device=sidebarWizardDevice();
const resource=sidebarWizardResource();
const action=sidebarWizardAction();
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
resetSidebarPresetWizard();
clearTransientFieldDirty();
render();
setGMStatus('Quick action saved','gm-ok');
}

function cancelSidebarPresetWizard(){
resetSidebarPresetWizard();
clearTransientFieldDirty();
render();
}

async function runSidebarPreset(presetId){
const preset=sidebarPresetById(presetId);
const resolved=resolveSidebarPreset(preset);
if(!resolved)throw new Error('Preset is incomplete');
await runManualDeviceCommand(resolved.device_id,resolved.command_id,resolved.params);
}

async function importLegacySidebarPresets(){
const legacy=legacySidebarPresets();
if(!legacy.length)throw new Error('No browser presets available for import');
const data=await api.sidebarPresets.save(legacy);
applySidebarPresetPayload(data);
resetSidebarPresetWizard();
clearTransientFieldDirty();
render();
setGMStatus('Legacy browser presets imported','gm-ok');
}
