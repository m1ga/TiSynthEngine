/* Shared stereo effects. All allocation belongs to init/destroy on the control
 * thread. Set-config, process, finish and panic are bounded audio-thread work.
 * Caller publishes a validated config atomically at an audio block boundary. */
#ifndef TI_SYNTH_EFFECTS_H
#define TI_SYNTH_EFFECTS_H
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int enabled; double rateHz, depth, mix; } SynthChorusConfig;
typedef struct { int enabled; double timeMs, syncBeats, feedback, mix; } SynthDelayConfig;
typedef struct { int enabled; double decay, damping, mix; } SynthReverbConfig;
typedef struct { int enabled; double drive, mix; } SynthSaturationConfig;
typedef struct {
    double inputGain;
    SynthChorusConfig chorus;
    SynthDelayConfig delay;
    SynthReverbConfig reverb;
    SynthSaturationConfig saturation;
} SynthEffectsConfig;
typedef struct { double *values; uint64_t *tags; size_t size, position; uint64_t epoch; } SynthEffectRing;
typedef struct { double send; uint64_t tail, quiet; int active; } SynthEffectGate;
typedef struct {
    double sampleRate, smoothing;
    SynthEffectsConfig config, current, step;
    SynthEffectRing chorus[2], delay[2], comb[2][4], allpass[2][2];
    SynthEffectGate chorusGate, delayGate, reverbGate;
    double chorusPhase, combLow[2][4], combFeedback[2][4], combFeedbackTarget[2][4], combFeedbackStep[2][4], saturationSend;
    double delayOld, delayNew, delayCrossfade;
    size_t memoryBytes;
} SynthEffects;

static inline SynthEffectsConfig synth_effects_default_config(void) {
    SynthEffectsConfig c; memset(&c,0,sizeof(c)); c.inputGain=1;
    c.chorus.rateHz=.8; c.chorus.depth=.4; c.chorus.mix=.25;
    c.delay.timeMs=250; c.delay.feedback=.3; c.delay.mix=.25;
    c.reverb.decay=1.5; c.reverb.damping=.5; c.reverb.mix=.2;
    c.saturation.drive=1; c.saturation.mix=1; return c;
}
static inline int synth_effects_range(double x,double a,double b) { return isfinite(x)&&x>=a&&x<=b; }
static inline int synth_effects_validate(const SynthEffectsConfig *c) {
    return c && (c->chorus.enabled==0||c->chorus.enabled==1) && (c->delay.enabled==0||c->delay.enabled==1)
      && (c->reverb.enabled==0||c->reverb.enabled==1) && (c->saturation.enabled==0||c->saturation.enabled==1)
      && synth_effects_range(c->inputGain,0,4)
      && synth_effects_range(c->chorus.rateHz,.05,5) && synth_effects_range(c->chorus.depth,0,1) && synth_effects_range(c->chorus.mix,0,1)
      && synth_effects_range(c->delay.timeMs,20,2000) && synth_effects_range(c->delay.feedback,0,.85) && synth_effects_range(c->delay.mix,0,1)
      && (c->delay.syncBeats==0||c->delay.syncBeats==1||c->delay.syncBeats==.5||c->delay.syncBeats==.25||c->delay.syncBeats==2.0/3||c->delay.syncBeats==1.0/3||c->delay.syncBeats==1.0/6)
      && synth_effects_range(c->reverb.decay,.2,8) && synth_effects_range(c->reverb.damping,0,1) && synth_effects_range(c->reverb.mix,0,1)
      && synth_effects_range(c->saturation.drive,1,4) && synth_effects_range(c->saturation.mix,0,1);
}
static inline int synth_effect_ring_init(SynthEffects *s,SynthEffectRing *r,size_t n) {
    r->size=n;r->position=0;r->epoch=1;
    r->values=(double*)calloc(n,sizeof(double));r->tags=(uint64_t*)calloc(n,sizeof(uint64_t));
    s->memoryBytes+=n*(sizeof(double)+sizeof(uint64_t));return r->values&&r->tags;
}
static inline void synth_effect_ring_destroy(SynthEffectRing *r) { free(r->values);free(r->tags);memset(r,0,sizeof(*r)); }
static inline void synth_effects_destroy(SynthEffects *s) {
    for(int ch=0;ch<2;ch++) { synth_effect_ring_destroy(&s->chorus[ch]);synth_effect_ring_destroy(&s->delay[ch]);
        for(int j=0;j<4;j++)synth_effect_ring_destroy(&s->comb[ch][j]);
        for(int j=0;j<2;j++)synth_effect_ring_destroy(&s->allpass[ch][j]); }
    s->memoryBytes=0;
}
static inline int synth_effects_init(SynthEffects *s,double rate) {
    if(!s||!isfinite(rate)||rate<16000||rate>192000)return 0;
    memset(s,0,sizeof(*s));s->sampleRate=rate;s->smoothing=1.0/(rate*.01);
    s->config=s->current=synth_effects_default_config();s->delayOld=s->delayNew=rate*.25;s->delayCrossfade=1;
    const double comb[]={.0297,.0371,.0411,.0437}, ap[]={.005,.0017};
    for(int ch=0;ch<2;ch++) {
        if(!synth_effect_ring_init(s,&s->chorus[ch],(size_t)(rate*.05)+4)||!synth_effect_ring_init(s,&s->delay[ch],(size_t)(rate*4)+4))goto fail;
        for(int j=0;j<4;j++) {
            if(!synth_effect_ring_init(s,&s->comb[ch][j],(size_t)(rate*(comb[j]+ch*.0007))+1))goto fail;
            s->combFeedback[ch][j]=s->combFeedbackTarget[ch][j]=pow(.001,(double)s->comb[ch][j].size/(rate*s->config.reverb.decay));
        }
        for(int j=0;j<2;j++)if(!synth_effect_ring_init(s,&s->allpass[ch][j],(size_t)(rate*(ap[j]+ch*.0003))+1))goto fail;
    }return 1;
fail:synth_effects_destroy(s);return 0;
}
static inline size_t synth_effects_memory_bytes(const SynthEffects *s) { return s->memoryBytes+sizeof(*s); }
static inline void synth_effects_set_config(SynthEffects *s,const SynthEffectsConfig *c) {
    if(!synth_effects_validate(c))return;
    if(c->reverb.decay!=s->config.reverb.decay)
        for(int ch=0;ch<2;ch++)for(int j=0;j<4;j++) {
            s->combFeedbackTarget[ch][j]=pow(.001,(double)s->comb[ch][j].size/(s->sampleRate*c->reverb.decay));
            s->combFeedbackStep[ch][j]=fabs(s->combFeedbackTarget[ch][j]-s->combFeedback[ch][j])*s->smoothing;
        }
#define SYNTH_EFFECT_RAMP(field) s->step.field=fabs(c->field-s->current.field)*s->smoothing
    SYNTH_EFFECT_RAMP(inputGain);
    SYNTH_EFFECT_RAMP(chorus.rateHz);SYNTH_EFFECT_RAMP(chorus.depth);SYNTH_EFFECT_RAMP(chorus.mix);
    SYNTH_EFFECT_RAMP(delay.feedback);SYNTH_EFFECT_RAMP(delay.mix);
    SYNTH_EFFECT_RAMP(reverb.decay);SYNTH_EFFECT_RAMP(reverb.damping);SYNTH_EFFECT_RAMP(reverb.mix);
    SYNTH_EFFECT_RAMP(saturation.drive);SYNTH_EFFECT_RAMP(saturation.mix);
#undef SYNTH_EFFECT_RAMP
    s->config=*c;
}
static inline void synth_effect_ring_reset(SynthEffectRing *r) { ++r->epoch;r->position=0; }
static inline void synth_effects_panic(SynthEffects *s) {
    for(int ch=0;ch<2;ch++) {synth_effect_ring_reset(&s->chorus[ch]);synth_effect_ring_reset(&s->delay[ch]);
        for(int j=0;j<4;j++){synth_effect_ring_reset(&s->comb[ch][j]);s->combLow[ch][j]=0;}
        for(int j=0;j<2;j++)synth_effect_ring_reset(&s->allpass[ch][j]);}
    s->chorusGate.tail=s->delayGate.tail=s->reverbGate.tail=0;
    s->chorusGate.quiet=s->delayGate.quiet=s->reverbGate.quiet=0;
}
static inline double synth_effect_ring_at(const SynthEffectRing *r,size_t i) { return r->tags[i]==r->epoch?r->values[i]:0; }
static inline double synth_effect_ring_read(const SynthEffectRing *r,double distance) {
    distance=fmax(1,fmin((double)r->size-2,distance));
    double pos=(double)r->position-distance;if(pos<0)pos+=r->size;
    size_t i=(size_t)pos,j=i+1;if(j==r->size)j=0;
    double f=pos-i;return synth_effect_ring_at(r,i)*(1-f)+synth_effect_ring_at(r,j)*f;
}
static inline void synth_effect_ring_write(SynthEffectRing *r,double v) {
    r->values[r->position]=fabs(v)<1e-25?0:v;r->tags[r->position]=r->epoch;
    if(++r->position==r->size)r->position=0;
}
static inline double synth_effect_approach(double a,double b,double step) {
    return a<b?fmin(a+step,b):fmax(a-step,b);
}
static inline double synth_effect_gate_tick(SynthEffectGate *g,int enabled,double rate,double step) {
    g->send=synth_effect_approach(g->send,enabled?1:0,step);
    if(enabled){g->active=1;g->tail=0;g->quiet=0;}
    else if(g->active) {if(++g->tail>=(uint64_t)(rate*30)){g->active=0;g->send=0;}}
    if(!g->active)return 0;
    return g->tail>rate*29.9?fmax(0,(rate*30-g->tail)/(rate*.1)):1;
}
static inline void synth_effect_gate_quiet(SynthEffectGate *g,double l,double r,double rate) {
    if(g->send==0&&fabs(l)+fabs(r)<1e-9){if(++g->quiet>(uint64_t)(rate*.2))g->active=0;}else g->quiet=0;
}
static inline void synth_effects_clear_chorus(SynthEffects *s) {for(int ch=0;ch<2;ch++)synth_effect_ring_reset(&s->chorus[ch]);}
static inline void synth_effects_clear_delay(SynthEffects *s) {for(int ch=0;ch<2;ch++)synth_effect_ring_reset(&s->delay[ch]);}
static inline void synth_effects_clear_reverb(SynthEffects *s) {
    for(int ch=0;ch<2;ch++) {
        for(int j=0;j<4;j++){synth_effect_ring_reset(&s->comb[ch][j]);s->combLow[ch][j]=0;}
        for(int j=0;j<2;j++)synth_effect_ring_reset(&s->allpass[ch][j]);
    }
}
static inline void synth_effects_process(SynthEffects *s,double tempo,double *left,double *right) {
    SynthEffectsConfig *a=&s->current,*b=&s->config;double k=s->smoothing;
    if(!b->chorus.enabled&&!b->delay.enabled&&!b->reverb.enabled&&
        !s->chorusGate.active&&!s->delayGate.active&&!s->reverbGate.active) {
        a->inputGain=synth_effect_approach(a->inputGain,b->inputGain,s->step.inputGain);
        if(a->inputGain!=1){*left*=a->inputGain;*right*=a->inputGain;}
        return;
    }
    a->inputGain=synth_effect_approach(a->inputGain,b->inputGain,s->step.inputGain);
    double l=*left*a->inputGain,r=*right*a->inputGain;
    int wasActive=s->chorusGate.active;
    double fade=synth_effect_gate_tick(&s->chorusGate,b->chorus.enabled,s->sampleRate,k);
    if(wasActive&&!s->chorusGate.active)synth_effects_clear_chorus(s);
    if(s->chorusGate.active) {
        a->chorus.rateHz=synth_effect_approach(a->chorus.rateHz,b->chorus.rateHz,s->step.chorus.rateHz);
        a->chorus.depth=synth_effect_approach(a->chorus.depth,b->chorus.depth,s->step.chorus.depth);a->chorus.mix=synth_effect_approach(a->chorus.mix,b->chorus.mix,s->step.chorus.mix);
        double dl=(.018+.012*a->chorus.depth*sin(s->chorusPhase))*s->sampleRate;
        double dr=(.018+.012*a->chorus.depth*cos(s->chorusPhase))*s->sampleRate;
        double wl=synth_effect_ring_read(&s->chorus[0],dl),wr=synth_effect_ring_read(&s->chorus[1],dr);
        synth_effect_ring_write(&s->chorus[0],l*s->chorusGate.send);synth_effect_ring_write(&s->chorus[1],r*s->chorusGate.send);
        l+=(wl*fade-l*s->chorusGate.send)*a->chorus.mix;r+=(wr*fade-r*s->chorusGate.send)*a->chorus.mix;
        s->chorusPhase+=6.283185307179586*a->chorus.rateHz/s->sampleRate;if(s->chorusPhase>=6.283185307179586)s->chorusPhase-=6.283185307179586;
        synth_effect_gate_quiet(&s->chorusGate,wl,wr,s->sampleRate);
        if(!s->chorusGate.active)synth_effects_clear_chorus(s);
    }
    wasActive=s->delayGate.active;
    fade=synth_effect_gate_tick(&s->delayGate,b->delay.enabled,s->sampleRate,k);
    if(wasActive&&!s->delayGate.active)synth_effects_clear_delay(s);
    if(s->delayGate.active) {
        a->delay.feedback=synth_effect_approach(a->delay.feedback,b->delay.feedback,s->step.delay.feedback);a->delay.mix=synth_effect_approach(a->delay.mix,b->delay.mix,s->step.delay.mix);
        if(!isfinite(tempo)||tempo<20||tempo>300)tempo=120;
        double time=b->delay.syncBeats>0?60*b->delay.syncBeats/tempo:b->delay.timeMs*.001;
        double distance=fmin(4*s->sampleRate,time*s->sampleRate);
        if(fabs(distance-s->delayNew)>.01&&s->delayCrossfade>=1){s->delayOld=s->delayNew;s->delayNew=distance;s->delayCrossfade=0;}
        double t=s->delayCrossfade,wl=synth_effect_ring_read(&s->delay[0],s->delayOld)*(1-t)+synth_effect_ring_read(&s->delay[0],s->delayNew)*t;
        double wr=synth_effect_ring_read(&s->delay[1],s->delayOld)*(1-t)+synth_effect_ring_read(&s->delay[1],s->delayNew)*t;
        s->delayCrossfade=fmin(1,t+k);
        synth_effect_ring_write(&s->delay[0],l*s->delayGate.send+wr*a->delay.feedback);synth_effect_ring_write(&s->delay[1],r*s->delayGate.send+wl*a->delay.feedback);
        l+=wl*fade*a->delay.mix;r+=wr*fade*a->delay.mix;
        /* Delay can remain silent until its first echo (up to four seconds). */
        if(s->delayGate.send==0&&fabs(wl)+fabs(wr)<1e-9) {
            if(++s->delayGate.quiet>(uint64_t)(fmax(s->delayOld,s->delayNew)+s->sampleRate*.2))s->delayGate.active=0;
        }else s->delayGate.quiet=0;
        if(!s->delayGate.active)synth_effects_clear_delay(s);
    }
    wasActive=s->reverbGate.active;
    fade=synth_effect_gate_tick(&s->reverbGate,b->reverb.enabled,s->sampleRate,k);
    if(wasActive&&!s->reverbGate.active)synth_effects_clear_reverb(s);
    if(s->reverbGate.active) {
        a->reverb.decay=synth_effect_approach(a->reverb.decay,b->reverb.decay,s->step.reverb.decay);a->reverb.damping=synth_effect_approach(a->reverb.damping,b->reverb.damping,s->step.reverb.damping);a->reverb.mix=synth_effect_approach(a->reverb.mix,b->reverb.mix,s->step.reverb.mix);
        double out[2]={0,0};
        for(int ch=0;ch<2;ch++) {
            double input=(ch?r:l)*s->reverbGate.send;
            for(int j=0;j<4;j++) {
                SynthEffectRing *ring=&s->comb[ch][j];double v=synth_effect_ring_at(ring,ring->position);
                double damp=.05+.9*a->reverb.damping;
                s->combLow[ch][j]=(1-damp)*v+damp*s->combLow[ch][j];
                s->combFeedback[ch][j]=synth_effect_approach(s->combFeedback[ch][j],s->combFeedbackTarget[ch][j],s->combFeedbackStep[ch][j]);
                double feedback=s->combFeedback[ch][j];
                synth_effect_ring_write(ring,input*.25+s->combLow[ch][j]*feedback);out[ch]+=v;
            }
            for(int j=0;j<2;j++){SynthEffectRing *ring=&s->allpass[ch][j];double v=synth_effect_ring_at(ring,ring->position),x=out[ch];out[ch]=v-.5*x;synth_effect_ring_write(ring,x+.5*out[ch]);}
        }
        l+=out[0]*fade*a->reverb.mix;r+=out[1]*fade*a->reverb.mix;
        synth_effect_gate_quiet(&s->reverbGate,out[0],out[1],s->sampleRate);
        if(!s->reverbGate.active)synth_effects_clear_reverb(s);
    }
    *left=l;*right=r;
}
/* Called after the existing master gain, immediately before the peak limiter.
 * masterGain=1 when the caller already applied that gain. */
static inline void synth_effects_finish(SynthEffects *s,double masterGain,double *left,double *right) {
    double k=s->smoothing;
    s->saturationSend=synth_effect_approach(s->saturationSend,s->config.saturation.enabled?1:0,k);
    double l=*left*masterGain,r=*right*masterGain;
    if(s->saturationSend>0) {
        s->current.saturation.drive=synth_effect_approach(s->current.saturation.drive,s->config.saturation.drive,s->step.saturation.drive);
        s->current.saturation.mix=synth_effect_approach(s->current.saturation.mix,s->config.saturation.mix,s->step.saturation.mix);
        double drive=s->current.saturation.drive,mix=s->current.saturation.mix*s->saturationSend;
        l+=(tanh(l*drive)/tanh(drive)-l)*mix;r+=(tanh(r*drive)/tanh(drive)-r)*mix;
    }*left=l;*right=r;
}
#endif
