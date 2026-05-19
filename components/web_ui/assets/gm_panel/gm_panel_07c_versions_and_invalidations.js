// GM panel source part. Edit this file, then rebuild gm_panel.js.
async function refreshGMByVersions(versions){
if(!versions)return;
const prev=gmLastVersions;
if(!prev){
gmLastVersions=versions;
return;
}
if(gmVersionsKey(versions)===gmVersionsKey(prev))return;
gmLastVersions=versions;
if(gmVersionChanged(prev,versions,['rooms'])){
await loadGMFullSnapshot(true,true,{forceStatic:true});
return;
}
let shouldRender=false;
let shouldPatchSidebar=false;
if(gmVersionChanged(prev,versions,['devices','ingest'])){
const deviceRefreshes=[loadObserved(true),loadQuestDevices(true)];
if(currentView==='scenarios'&&isAdmin())deviceRefreshes.push(loadScenarioEditorCatalogs(true));
await Promise.all(deviceRefreshes);
shouldPatchSidebar=true;
shouldRender=shouldRender||gmCurrentViewUsesQuestDeviceStatic();
if(currentView==='scenarios'&&isAdmin())shouldRender=true;
}
if(gmVersionChanged(prev,versions,['scenarios'])){
await loadRoomScenarios(true);
shouldRender=shouldRender||gmCurrentViewUsesScenarioStatic();
}
if(gmVersionChanged(prev,versions,['profiles'])){
await loadRoomProfiles(true);
shouldRender=shouldRender||gmCurrentViewUsesProfileStatic();
}
if(gmVersionChanged(prev,versions,['session','runtime'])){
if(currentView==='room'&&roomTab==='control'&&currentRoomId){
await loadGMRuntimeOnly(currentRoomId,false);
}
else if(currentView==='rooms'){
await loadGMRoomsRuntimeOnly([],false);
return;
}
else if(!shouldRender){
await loadGMSystemSummaryOnly(false);
return;
}
}
if(shouldRender){
if(shouldDeferAutoRender()){
gmAutoRenderDeferred=true;
renderRightSidebar(shouldPatchSidebar);
}
else{
render();
}
}
else if(shouldPatchSidebar){
renderRightSidebar(true);
}
}

async function refreshGMByInvalidationSlices(items){
const values=Array.isArray(items)?items.filter(Boolean):[];
const slices=values.map(item=>typeof item==='string'?item:String(item.slice||'')).filter(Boolean);
const roomScenarioTargets=Array.from(new Set(values.map(item=>item&&item.slice==='room.scenarios'?(item.target_id||''):'').filter(Boolean)));
const roomProfileTargets=Array.from(new Set(values.map(item=>item&&item.slice==='room.profiles'?(item.target_id||''):'').filter(Boolean)));
const roomRuntimeTargets=Array.from(new Set(values.map(item=>item&&item.slice==='room.runtime'?(item.target_id||''):'').filter(Boolean)));
if(!slices.length)return;
if(slices.includes('full.snapshot')||slices.includes('room.catalog')){
await loadGMFullSnapshot(true,true,{forceStatic:true});
return;
}
const needsDeviceCatalog=slices.includes('devices.catalog');
const needsDeviceRuntime=slices.includes('devices.runtime');
const needsSidebarPresets=slices.includes('gm.sidebar_presets');
const needsScenarioCatalog=slices.includes('room.scenarios');
const needsProfileCatalog=slices.includes('room.profiles');
const needsRoomRuntime=slices.includes('room.runtime');
const needsSystemSummary=slices.includes('system.summary');
let shouldRender=false;
let shouldPatchSidebar=false;
const localRuntimeRefreshUntil=(currentRoomId&&gmLocalRuntimeRefreshUntil[currentRoomId])||0;
const localRuntimeRefreshActive=currentView==='room'&&roomTab==='control'&&currentRoomId&&Date.now()<localRuntimeRefreshUntil;
const runtimeRefreshRecent=currentView==='room'&&roomTab==='control'&&currentRoomId&&Date.now()-((currentRoomId&&gmRuntimeLastRefreshAt[currentRoomId])||0)<900;

if(needsDeviceCatalog){
if(isAdmin()){
await Promise.all([loadQuestDevices(true),loadScenarioEditorCatalogs(true)]);
}
else{
await loadQuestDevices(true);
}
shouldPatchSidebar=true;
shouldRender=shouldRender||gmCurrentViewUsesQuestDeviceStatic();
}
if(needsDeviceRuntime){
await loadObserved(true);
shouldPatchSidebar=true;
shouldRender=shouldRender||gmCurrentViewUsesQuestDeviceStatic();
}
if(needsSidebarPresets){
await loadSidebarPresets(true);
shouldPatchSidebar=true;
shouldRender=shouldRender||currentView==='devices'||currentView==='storage';
}
if(needsScenarioCatalog){
if(roomScenarioTargets.length){
await Promise.all(roomScenarioTargets.map(roomId=>loadRoomScenariosForRoom(roomId,true)));
}
else{
await loadRoomScenarios(true);
}
shouldRender=shouldRender||gmCurrentViewUsesScenarioStatic();
}
if(needsProfileCatalog){
if(roomProfileTargets.length){
await Promise.all(roomProfileTargets.map(roomId=>loadRoomProfilesForRoom(roomId,true)));
}
else{
await loadRoomProfiles(true);
}
shouldRender=shouldRender||gmCurrentViewUsesProfileStatic();
}
if(needsRoomRuntime||needsSystemSummary){
const localRuntimeTargetMatches=!roomRuntimeTargets.length||(roomRuntimeTargets.length===1&&roomRuntimeTargets[0]===currentRoomId);
if(localRuntimeRefreshActive&&localRuntimeTargetMatches)return;
if(runtimeRefreshRecent&&localRuntimeTargetMatches)return;
if(roomRuntimeTargets.length===1&&roomRuntimeTargets[0]&&roomById(roomRuntimeTargets[0])){
await loadGMRuntimeOnly(roomRuntimeTargets[0],false);
}
else if(currentView==='room'&&roomTab==='control'&&currentRoomId){
await loadGMRuntimeOnly(currentRoomId,false);
}
else if(needsRoomRuntime&&currentView==='rooms'){
await loadGMRoomsRuntimeOnly(roomRuntimeTargets,false);
}
else{
await loadGMSystemSummaryOnly(false);
}
return;
}
if(shouldRender){
if(shouldDeferAutoRender()){
gmAutoRenderDeferred=true;
renderRightSidebar(shouldPatchSidebar);
}
else{
render();
}
}
else if(shouldPatchSidebar){
renderRightSidebar(true);
}
}
