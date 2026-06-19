// GM panel source part. Edit this file, then rebuild gm_panel.js.
async function refreshAfterRuntimeAction(roomId,forceFullRender){
clearTransientFieldDirty();
await loadGMRuntimeOnly(roomId,forceFullRender);
if(roomId)gmLocalRuntimeRefreshUntil[roomId]=Date.now()+400;
}

async function runManualDeviceCommand(deviceId,commandId,paramsOverride,confirmed){
if(!deviceId||!commandId)throw new Error('Manual button is incomplete');
setGMStatus('Triggering button...');
const command=scenarioCommandById(deviceId,commandId);
const body={device_id:deviceId,command_id:commandId};
if(paramsOverride&&typeof paramsOverride==='object'){
body.params=paramsOverride;
}
else if(command&&command.default_args&&typeof command.default_args==='object'){
body.params=command.default_args;
}
else if(command&&typeof defaultParamsForCommand==='function'){
const defaults=defaultParamsForCommand(scenarioDeviceById(deviceId)||questDeviceById(deviceId),command);
if(defaults&&typeof defaults==='object'&&Object.keys(defaults).length)body.params=defaults;
}
const res=await api.device.runCommand(body.device_id,body.command_id,body.params,!!confirmed);
await gmExpectOk(res);
setGMStatus('Button sent','gm-ok');
}

async function runDeviceAdminCommand(deviceId,commandId,paramsOverride,confirmed,statusText){
if(!deviceId||!commandId)throw new Error('Admin action is incomplete');
setGMStatus(statusText||'Running admin action...');
const res=await api.device.runAdminCommand(deviceId,commandId,paramsOverride,!!confirmed);
return await gmReadJson(res);
}

async function createRoomFromPrompt(){
if(!isAdmin())return;
const name=(prompt('Room name')||'').trim();
if(!name)return;
const roomId=slugifyId(name,'room');
setGMStatus('Saving room...');
const res=await api.room.save({room_id:roomId,name});
await gmExpectOk(res);
clearTransientFieldDirty();
await loadGMFullSnapshot(true,true);
if(typeof window.__gmRefreshManualSidebar==='function'){
await window.__gmRefreshManualSidebar();
}
setGMStatus('Room saved','gm-ok');
}

async function deleteRoom(roomId,confirmHandled){
if(!isAdmin())return;
if(!roomId)throw new Error('Room is not selected');
const room=roomById(roomId);
const name=room&&(room.title||room.name)||roomId;
if(!confirmHandled&&!confirm(`Delete room ${name}? This also removes profiles and scenarios for this room. Quest devices stay untouched.`))return;
setGMStatus('Deleting room...');
const res=await api.room.delete({room_id:roomId,delete_content:true});
await gmExpectOk(res);
currentRoomId='';
delete currentRoomProfileId[roomId];
delete currentRoomScenarioId[roomId];
roomTab='control';
clearTransientFieldDirty();
await loadGMFullSnapshot(true,true);
currentView='rooms';
render();
setGMStatus('Room deleted','gm-ok');
}

async function runRoomTimer(action,roomId){
let res=null;
setGMStatus('Updating timer...');
if(action==='start'){
const input=document.getElementById('gm_timer_minutes');
const minutes=Number(input&&input.value);
if(!Number.isFinite(minutes)||minutes<=0)throw new Error('Duration must be greater than 0');
const durationMs=Math.round(minutes*60000);
res=await api.room.timerStart(roomId,durationMs);
}
else if(action==='pause'){
res=await api.room.timer(roomId,'pause');
}
else if(action==='resume'){
res=await api.room.timer(roomId,'resume');
}
else if(action==='reset'){
res=await api.room.timer(roomId,'reset');
}
else if(action==='finish'){
res=await api.room.sessionFinish(roomId);
}
else if(action==='plus1'){
res=await api.room.timerAdd(roomId,60000);
}
else if(action==='minus1'){
res=await api.room.timerAdd(roomId,-60000);
}
else{
throw new Error('Unsupported timer action');
}
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
if(action==='start'&&roomId)delete gmRoomTimerMinutesDraft[roomId];
clearTransientFieldDirty();
await refreshAfterRuntimeAction(roomId,false);
setGMStatus('Timer updated','gm-ok');
}

async function runRoomHint(action,roomId){
if(action==='send'){
const input=document.getElementById('gm_hint_input');
const message=(input&&input.value||'').trim();
if(!message)throw new Error('Hint message is empty');
setGMStatus('Sending hint...');
const res=await api.room.hintSend({room_id:roomId,message});
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
}
else if(action==='clear'){
setGMStatus('Clearing hint...');
const res=await api.room.hintClear(roomId);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
}
else{
throw new Error('Unsupported hint action');
}
clearTransientFieldDirty();
await refreshAfterRuntimeAction(roomId,true);
setGMStatus('Hint updated','gm-ok');
}

async function selectRoomProfile(roomId,profileId){
if(!roomId||!profileId)throw new Error('Game mode selection is incomplete');
setGMStatus('Selecting game mode...');
const res=await api.room.profileSelect({room_id:roomId,profile_id:profileId});
await gmExpectOk(res);
currentRoomProfileId[roomId]=profileId;
clearTransientFieldDirty();
await refreshAfterRuntimeAction(roomId,false);
setGMStatus('Game mode selected','gm-ok');
}

async function runRoomGame(action,roomId,confirmHandled){
if(!roomId||!action)throw new Error('Game command is incomplete');
if(!confirmHandled&&action==='stop'&&!confirm('Stop this game session?'))return;
if(!confirmHandled&&action==='reset'&&!confirm('Reset this game session?'))return;
setGMStatus('Updating game...');
const res=await api.room.game(roomId,action);
if(!res.ok){
throw new Error((await res.text().catch(()=>''))||('HTTP '+res.status));
}
clearTransientFieldDirty();
await refreshAfterRuntimeAction(roomId,false);
setGMStatus('Game updated','gm-ok');
}
