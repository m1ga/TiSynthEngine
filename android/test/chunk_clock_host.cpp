// Compare stdout against the original renderer using -DENGINE_CPP. Outputs and
// final clocks/state must match across fractional boundaries and small buffers.
#define main render_fixture_main
#include "../../tests/engine_render_host.cpp"
#undef main
#include <cassert>
static uint64_t stateHash(Engine &e) {
 uint64_t h=1469598103934665603ULL;
 auto add=[&](const auto &v){for(unsigned char c:std::string(reinterpret_cast<const char*>(&v),sizeof(v))){h^=c;h*=1099511628211ULL;}};
 add(e.sampleIndex_);add(e.transport_.beat);add(e.transport_.milliseconds);add(e.transport_.beatCompensation);add(e.transport_.msCompensation);
 for(int i=0;i<4;i++){auto &s=e.transport_.slots[i];add(s.beat);add(s.nextBeat);add(s.beatCompensation);add(s.cursor);add(s.playing);}
 for(auto &v:e.voices_){add(v.active);add(v.phase);add(v.increment);add(v.played);add(v.envelope);add(v.held.releasing);add(v.held.releasePlayed);add(v.sequenceId);add(v.sequenceOffBeat);add(v.modulationFrames);add(v.filter.ic1);add(v.filter.ic2);add(v.spatialState.position);add(v.spatialState.history);}
 return h;
}
int main(){
 for(int rate:{16000,44100,48000,96000,192000})for(int ms:{0,1})for(int effects:{0,1}) {
  auto e=std::make_shared<Engine>(32,1,4);e->renderSampleRate_=rate;e->reportedSampleRate_.store(rate);assert(e->initializeLimiter());
  if(effects){Command fx;fx.type=8;fx.effects=synth_effects_default_config();fx.effects.chorus.enabled=fx.effects.delay.enabled=fx.effects.reverb.enabled=fx.effects.saturation.enabled=1;assert(!e->enqueue(fx));}
  auto publish=[&](uint32_t id,bool restart,int steps){auto *c=synth_transport_begin_write(&e->transport_,id,restart);assert(c);c->timeUnit=ms?SYNTH_SEQUENCE_MS:SYNTH_SEQUENCE_BEATS;c->stepDuration=ms?1.137:.002274;c->swing=.73;c->loop=id!=4;c->stepCount=steps;
   for(int i=0;i<steps;i++){auto &s=c->steps[i];s.rest=i==1;s.noteCount=s.rest?0:8;s.duration=ms?1.871+i*.113:.003742+i*.000226;s.length=i==2?(ms?9.731:.019462):0;s.gate=.34;
    for(int n=0;n<s.noteCount;n++)s.notes[n]=48+id+n;
    s.voice.volume=.01;s.voice.waveType=1;s.voice.spatial=1;s.voice.spatialX=.6;s.voice.spatialY=-.4;s.voice.release=3;s.voice.filterType=1;s.voice.filterHz=1800;
    s.voice.frequencyEnd=660;s.voice.panEnd=0;s.voice.filterHzEnd=2300;s.voice.pulseWidthEnd=.7;
    s.voice.mods[0]=1;s.voice.mods[2]=0;s.voice.mods[3]=1.7;s.voice.mods[4]=.3;s.voice.mods[6]=1;s.voice.mods[7]=1;s.voice.mods[8]=1;s.voice.mods[9]=2.3;s.voice.mods[10]=.3;
   }assert(synth_transport_commit(&e->transport_,c,restart));};
  for(uint32_t id=1;id<=4;id++)publish(id,true,3);
  std::vector<float> audio;
  auto render=[&](int frames){std::vector<float> out(frames*2);e->onAudioReady(nullptr,out.data(),frames);audio.insert(audio.end(),out.begin(),out.end());};
  for(int block=0;block<95;block++){
   if(block==9)assert(e->setTempo(271));
   if(block==17)publish(1,false,2);
   if(block==31)assert(e->stopSequence(2));
   if(block==32){SynthSequenceEnd end;while(e->popSequenceEnd(&end)){}publish(5,true,3);}
   if(block==60){Command update;update.type=SYNTH_CMD_HELD_ON;update.voiceId=991;update.frequency=330;update.volume=.01;update.spatial=true;update.spatialX=-.4;update.spatialY=.6;e->enqueue(update);}
   render(std::array<int,7>{1,3,7,16,17,31,257}[block%7]);
  }
  printf("clock %d %d %d %016llx %016llx\n",rate,ms,effects,(unsigned long long)fnv(audio),(unsigned long long)stateHash(*e));
 }
 return 0;
}
