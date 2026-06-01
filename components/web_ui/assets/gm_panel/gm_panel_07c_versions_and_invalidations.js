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
gmStatTag('render.full.request','versions.rooms');
await loadGMFullSnapshot(true,false,{forceStatic:true,reason:'versions.rooms'});
return;
}
let shouldRender=false;
let shouldPatchSidebar=false;
	const devicesChanged=gmVersionChanged(prev,versions,['devices']);
	const ingestChanged=gmVersionChanged(prev,versions,['ingest']);
	if(devicesChanged){
		gmStatTag('versions.change','devices');
		if(currentView==='room'||currentView==='rooms'){
			gmStatTag('render.full.request','versions.devices_room_snapshot');
			await loadGMFullSnapshot(true,false,{forceStatic:true,reason:'versions.devices_room_snapshot'});
			return;
		}
		const deviceRefreshes=[loadObserved(true),loadQuestDevices(true)];
		if(currentView==='scenarios'&&isAdmin())deviceRefreshes.push(loadScenarioEditorCatalogs(true));
		await Promise.all(deviceRefreshes);
shouldPatchSidebar=true;
shouldRender=shouldRender||gmCurrentViewNeedsQuestDeviceFullRender();
if(currentView==='scenarios'&&isAdmin())shouldRender=true;
}
	if(ingestChanged){
		gmStatTag('versions.change','ingest');
		await loadObserved(true);
		shouldPatchSidebar=true;
		if(currentView==='room'&&roomTab==='control'&&currentRoomId){
			await loadGMRuntimeOnly(currentRoomId,false);
			if(shouldDeferAutoRender())gmQueueDeferredRender('sidebar','',true);
			else renderRightSidebar(false);
			return;
		}
		if(currentView==='rooms'){
			await loadGMRoomsRuntimeOnly([],false);
			return;
		}
		shouldRender=shouldRender||gmCurrentViewNeedsQuestDeviceFullRender();
	}
if(gmVersionChanged(prev,versions,['scenarios'])){
gmStatTag('versions.change','scenarios');
await loadRoomScenarios(true);
shouldRender=shouldRender||gmCurrentViewUsesScenarioStatic();
}
if(gmVersionChanged(prev,versions,['profiles'])){
gmStatTag('versions.change','profiles');
await loadRoomProfiles(true);
shouldRender=shouldRender||gmCurrentViewUsesProfileStatic();
}
if(gmVersionChanged(prev,versions,['session','runtime'])){
gmStatTag('versions.change','session_or_runtime');
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
gmStatTag('render.full.request','versions.static_refresh');
if(shouldDeferAutoRender()){
gmQueueDeferredRender('full','',shouldPatchSidebar);
}
else{
render();
}
}
else if(shouldPatchSidebar){
if(shouldDeferAutoRender())gmQueueDeferredRender('sidebar','',true);
else renderRightSidebar(false);
}
}

async function refreshGMByInvalidationSlices(items){
const values=Array.isArray(items)?items.filter(Boolean):[];
const slices=values.map(item=>typeof item==='string'?item:String(item.slice||'')).filter(Boolean);
Array.from(new Set(slices)).forEach(slice=>gmStatTag('invalidate.slice',slice));
const roomScenarioTargets=Array.from(new Set(values.map(item=>item&&item.slice==='room.scenarios'?(item.target_id||''):'').filter(Boolean)));
const roomProfileTargets=Array.from(new Set(values.map(item=>item&&item.slice==='room.profiles'?(item.target_id||''):'').filter(Boolean)));
const roomRuntimeTargets=Array.from(new Set(values.map(item=>item&&item.slice==='room.runtime'?(item.target_id||''):'').filter(Boolean)));
if(!slices.length)return;
if(slices.includes('full.snapshot')||slices.includes('room.catalog')){
gmStatTag('render.full.request',slices.includes('full.snapshot')?'invalidate.full_snapshot':'invalidate.room_catalog');
await loadGMFullSnapshot(true,false,{forceStatic:true,reason:slices.includes('full.snapshot')?'invalidate.full_snapshot':'invalidate.room_catalog'});
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
		if(currentView==='room'||currentView==='rooms'){
			gmStatTag('render.full.request','invalidate.devices_catalog_room_snapshot');
			await loadGMFullSnapshot(true,false,{forceStatic:true,reason:'invalidate.devices_catalog_room_snapshot'});
			return;
		}
		if(isAdmin()){
			await Promise.all([loadQuestDevices(true),loadScenarioEditorCatalogs(true)]);
		}
else{
await loadQuestDevices(true);
}
shouldPatchSidebar=true;
shouldRender=shouldRender||gmCurrentViewNeedsQuestDeviceFullRender();
}
	if(needsDeviceRuntime){
		await loadObserved(true);
		if(currentView==='room'&&roomTab==='control'&&currentRoomId){
			await loadGMRuntimeOnly(currentRoomId,false);
			if(shouldDeferAutoRender())gmQueueDeferredRender('sidebar','',true);
			else renderRightSidebar(false);
			return;
		}
		if(currentView==='rooms'){
			await loadGMRoomsRuntimeOnly([],false);
			return;
		}
		shouldPatchSidebar=true;
		shouldRender=shouldRender||gmCurrentViewNeedsQuestDeviceFullRender();
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
let summaryRefreshed=false;
if(needsSystemSummary){
await loadGMSystemSummaryOnly(false);
summaryRefreshed=true;
}
if(needsRoomRuntime){
if(localRuntimeRefreshActive&&localRuntimeTargetMatches)return;
if(runtimeRefreshRecent&&localRuntimeTargetMatches)return;
if(roomRuntimeTargets.length===1&&roomRuntimeTargets[0]&&roomById(roomRuntimeTargets[0])){
await loadGMRuntimeOnly(roomRuntimeTargets[0],false);
}
else if(currentView==='room'&&roomTab==='control'&&currentRoomId){
await loadGMRuntimeOnly(currentRoomId,false);
}
else if(currentView==='rooms'){
await loadGMRoomsRuntimeOnly(roomRuntimeTargets,false);
}
else if(!summaryRefreshed){
await loadGMSystemSummaryOnly(false);
}
}
else if(!summaryRefreshed){
await loadGMSystemSummaryOnly(false);
}
return;
}
if(shouldRender){
gmStatTag('render.full.request','invalidate.static_refresh');
if(shouldDeferAutoRender()){
gmQueueDeferredRender('full','',shouldPatchSidebar);
}
else{
render();
}
}
else if(shouldPatchSidebar){
if(shouldDeferAutoRender())gmQueueDeferredRender('sidebar','',true);
else renderRightSidebar(false);
}
}
