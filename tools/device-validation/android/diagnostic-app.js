'use strict';
var synth=require('ti.synthengine');
var dir=Ti.Filesystem.applicationDataDirectory;
var configFile=Ti.Filesystem.getFile(dir,'physical-config.json');
var config=configFile.exists()?JSON.parse(configFile.read().text):{durationMs:30000,phase:'feasibility'};
var result={schemaVersion:1,phase:config.phase,durationRequestedMs:config.durationMs,device:{model:Ti.Platform.model,os:Ti.Platform.version},config:config,phaseLog:[],samples:[],sequenceIds:[],accepted:true,complete:false,interrupted:false};
var modeNames=['off','full period','half period'];
var modeLabel=typeof config.hintMode==='number'?' · mode '+(modeNames[config.hintMode]||config.hintMode):'';
var diagnosticTitle='Audio check: '+(config.phase||'diagnostic')+modeLabel+' · '+Math.round(config.durationMs/1000)+' s';
var label=Ti.UI.createLabel({text:diagnosticTitle+'\nPreparing this run…',color:'#ddd',font:{fontSize:18},textAlign:'center'});
var win=Ti.UI.createWindow({backgroundColor:'#111',keepScreenOn:true});win.add(label);win.open();
Ti.API.info('PHYSICAL_DIAGNOSTIC_DIR '+dir);
function save(name,value){Ti.Filesystem.getFile(dir,name).write(JSON.stringify(value));}
function log(phase){var entry={phase:phase,time:Date.now()};result.phaseLog.push(entry);Ti.API.info('PHYSICAL_DIAGNOSTIC '+JSON.stringify(entry));save('physical-progress.json',{phase:phase,time:entry.time,result:result});}
function finish(error){
 if(result.complete||result.finishing)return;result.finishing=true;if(error)result.error=String(error);
 function close(){result.complete=true;result.finishedAt=Date.now();try{result.finalStats=synth.getStats();synth.stopAll();synth.shutdown();}catch(e){result.cleanupError=String(e);}save('physical-result.json',result);label.text=diagnosticTitle+'\nFinished — this run has ended.\n'+(result.error||('Peak render load '+result.finalStats.peakRenderLoad))+'\nA new run starts only when launched.';log('finished');}
 if(typeof synth.getPhysicalStats==='function'&&result.started&&config.physicalTelemetry!==false){
  try{synth.getPhysicalStats({freeze:true});var attempt=0;function readFrozen(){var p=synth.getPhysicalStats({freeze:false});if(p&&p.frozen){result.physicalTelemetry=p;close();}else if(++attempt<100)setTimeout(readFrozen,10);else{result.error=result.error||'Histogram freeze not acknowledged';close();}}setTimeout(readFrozen,20);}catch(e){result.error=result.error||String(e);close();}
 }else close();
}
Ti.App.addEventListener('pause',function(){result.interrupted=true;finish('App paused during controlled validation');});
function begin(){
 try{
  result.beforeStartStats=synth.getStats();result.defaults=synth.getDefaults();
  result.started=synth.start({maxVoices:32,maxSequences:4,mixingProfile:'conservative',audioSession:'ambient'});
  if(!result.started)throw Error('start rejected');synth.setVolume({volume:.15});result.masterVolume=.15;
  result.processing={applyTo:['music','effects','ui','ambience'],inputGain:1,chorus:{enabled:true,rate:1.2,depth:.6,mix:.4},delay:{enabled:true,timeMs:333,feedback:.6,mix:.4},reverb:{enabled:true,decay:4,damping:.5,mix:.4},saturation:{enabled:true,drive:2,mix:.4}};
  if(config.effects===false){['chorus','delay','reverb','saturation'].forEach(function(key){result.processing[key].enabled=false;});}if(!synth.setMasterProcessing(result.processing))throw Error('processing rejected');
  result.lfos=[{wave:'sine',target:'filterHz',rate:1.7,depth:.3,sync:'off'},{wave:'triangle',target:'pulseWidth',rate:2.3,depth:.3,sync:'off'}];
  if(config.lfos===false)result.lfos=[];
  for(var k=0;k<4;k++){
   var notes=[];for(var n=0;n<8;n++)notes.push(44+k*8+n);
   var payload={timeUnit:'beats',stepDuration:1,loop:true,steps:[{notes:notes,gate:1}],voice:{bus:['music','effects','ui','ambience'][k],effectSend:1,volume:.01,waveType:1,attack:2,release:30,filterType:'lowpass',filterHz:1800,filterQ:.7071,spatialX:.6,spatialY:-.4,modulations:result.lfos}};
   if(config.spatial===false){delete payload.voice.spatialX;delete payload.voice.spatialY;}if(config.longSteps)payload.stepDuration=120;
   var id=synth.playSequence(payload);if(!(id>0))throw Error('sequence publication rejected');result.sequenceIds.push(id);
  }
  synth.setStatsEnabled(true);log('warmup');
  setTimeout(function(){
   result.warmStats=synth.getStats();synth.resetStats();
   setTimeout(function(){
    result.startedAt=Date.now();result.initialStats=synth.getStats();log('measurement');
    var timer=setInterval(function(){
     try{
      var elapsed=Date.now()-result.startedAt,stats=synth.getStats(),states=result.sequenceIds.map(function(id){return synth.getSequenceState({sequenceId:id});});
      result.samples.push({elapsedMs:elapsed,stats:stats,states:states});label.text=diagnosticTitle+'\nRunning: '+Math.floor(elapsed/1000)+' / '+Math.floor(config.durationMs/1000)+' s\n32 binaural voices / 4 sequences';
      save('physical-progress.json',{phase:'measurement',time:Date.now(),elapsedMs:elapsed,stats:stats,states:states});
      if(elapsed>=config.durationMs){clearInterval(timer);result.elapsedMs=elapsed;finish();}
     }catch(e){clearInterval(timer);finish(e);}
    },1000);
   },100);
  },config.warmupMs||5000);
 }catch(e){finish(e);}
}
function memoryPhase(){
 try{
  result.started=synth.start({maxVoices:32,maxSequences:4,mixingProfile:'conservative',audioSession:'ambient'});if(!result.started)throw Error('memory start rejected');synth.setVolume({volume:.15});
  var processing={applyTo:['music','effects','ui','ambience'],inputGain:1,chorus:{enabled:true,rate:1.2,depth:.6,mix:.4},delay:{enabled:true,timeMs:333,feedback:.6,mix:.4},reverb:{enabled:true,decay:4,damping:.5,mix:.4},saturation:{enabled:true,drive:2,mix:.4}};
  if(!synth.setMasterProcessing(processing))throw Error('memory FX rejected');
  for(var k=0;k<4;k++){var initial=[];for(var n=0;n<8;n++)initial.push(44+k*8+n);var id=synth.playSequence({timeUnit:'beats',stepDuration:120,loop:true,steps:[{notes:initial,gate:1}],voice:{volume:.01,waveType:1,spatialX:.6,spatialY:-.4}});if(!id)throw Error('memory initial score rejected');result.sequenceIds.push(id);}
  log('memory-loaded-warm');
  setTimeout(function(){try{
   var scores=[];for(var k=0;k<4;k++){var steps=[];for(var i=0;i<512;i++){var notes=[];for(var n=0;n<32;n++)notes.push(36+n);steps.push({notes:notes,gate:1});}scores.push({sequenceId:result.sequenceIds[k],steps:steps,stepDuration:120,voice:{volume:.01,waveType:1,spatialX:.6,spatialY:-.4}});}
   result.denseInputSteps=2048;result.denseInputNotes=65536;log('memory-js-inputs');
   setTimeout(function(){try{
    result.denseAccepted=[];for(var k=0;k<4;k++)result.denseAccepted.push(synth.updateSequence(scores[k]));log('memory-dense-publications');
    var round=0;result.publicationAttempts=0;result.publicationAccepted=0;
    var pressure=setInterval(function(){try{
     for(var k=0;k<4;k++){scores[k].voice.volume=round%2?.009:.01;result.publicationAttempts++;if(synth.updateSequence(scores[k]))result.publicationAccepted++;}round++;
     if(round>=20){clearInterval(pressure);log('memory-bank-pressure-complete');setTimeout(function(){var cycle=0;function recover(){if(cycle>=5){finish();return;}synth.shutdown();log('memory-shutdown-'+cycle);setTimeout(function(){result.recoveryStarted=synth.start({maxVoices:32,maxSequences:4,mixingProfile:'conservative',audioSession:'ambient'});if(!result.recoveryStarted){finish('recovery start rejected');return;}synth.setVolume({volume:.15});cycle++;log('memory-recovery-'+cycle);setTimeout(recover,2000);},500);}recover();},5000);}
    }catch(e){clearInterval(pressure);finish(e);}},250);
   }catch(e){finish(e);}},2000);
  }catch(e){finish(e);}},5000);
 }catch(e){finish(e);}
}
log('baseline');setTimeout(config.phase==='memory'?memoryPhase:begin,config.baselineMs||15000);
