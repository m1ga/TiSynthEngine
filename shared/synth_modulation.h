/* Two phase-continuous per-voice LFOs. Plain C, bounded allocation-free work.
 * Capability flags describe the voice's actual source/filter configuration.
 * Depth is normalized; output clamps also protect sums of two modulators. */
#ifndef TI_SYNTH_MODULATION_H
#define TI_SYNTH_MODULATION_H
#include <math.h>
#include <string.h>
#define SYNTH_MOD_SINE 0
#define SYNTH_MOD_TRIANGLE 1
#define SYNTH_MOD_TARGET_CUTOFF 0
#define SYNTH_MOD_TARGET_PWM 1
#define SYNTH_MOD_TARGET_FM 2
#define SYNTH_MOD_CAP_FILTER 1u
#define SYNTH_MOD_CAP_PWM 2u
#define SYNTH_MOD_CAP_FM 4u
#define SYNTH_MOD_CAP_ALL 7u
typedef struct { int enabled, waveform, target; double rateHz, syncBeats, depth; } SynthLFOConfig;
typedef struct { SynthLFOConfig lfo[2]; } SynthModulationConfig;
typedef struct { double cutoffOctaves, pwmOffset, fmOffset; } SynthModulationOutput;
typedef struct {
    SynthModulationConfig config;
    double sampleRate, phase[2], rate[2], sineWeight[2];
    /* Ramp the target contributions separately, avoiding jumps on retarget. */
    double cutoff[2], pwm[2], fm[2];
} SynthModulation;
static inline SynthModulationConfig synth_modulation_default_config(void) {
    SynthModulationConfig c;memset(&c,0,sizeof(c));for(int i=0;i<2;i++)c.lfo[i].rateHz=1;return c;
}
static inline int synth_modulation_validate(const SynthModulationConfig *c,unsigned capabilities) {
    if(!c)return 0;
    for(int i=0;i<2;i++) {const SynthLFOConfig *l=&c->lfo[i];
        if((l->enabled!=0&&l->enabled!=1)||(l->waveform!=SYNTH_MOD_SINE&&l->waveform!=SYNTH_MOD_TRIANGLE)
            ||l->target<0||l->target>2||!isfinite(l->rateHz)||l->rateHz<0||l->rateHz>20
            ||!isfinite(l->depth)||l->depth<0||l->depth>1
            ||!(l->syncBeats==0||l->syncBeats==1||l->syncBeats==.5||l->syncBeats==.25||l->syncBeats==2.0/3||l->syncBeats==1.0/3||l->syncBeats==1.0/6))return 0;
        unsigned cap=l->target==SYNTH_MOD_TARGET_CUTOFF?SYNTH_MOD_CAP_FILTER:l->target==SYNTH_MOD_TARGET_PWM?SYNTH_MOD_CAP_PWM:SYNTH_MOD_CAP_FM;
        if(l->enabled&&!(capabilities&cap))return 0;
    }return 1;
}
static inline void synth_modulation_init(SynthModulation *m,double sampleRate) {
    memset(m,0,sizeof(*m));m->sampleRate=isfinite(sampleRate)&&sampleRate>=16000?sampleRate:48000;
    m->config=synth_modulation_default_config();m->rate[0]=m->rate[1]=1;
    m->sineWeight[0]=m->sineWeight[1]=1;
}
static inline int synth_modulation_set_config(SynthModulation *m,const SynthModulationConfig *c,unsigned capabilities) {
    if(!synth_modulation_validate(c,capabilities))return 0;m->config=*c;return 1;
}
static inline double synth_modulation_clamp(double x,double lo,double hi) {return fmin(hi,fmax(lo,x));}
static inline double synth_modulation_approach(double a,double b,double d) {return a<b?fmin(a+d,b):fmax(a-d,b);}
static inline int synth_modulation_is_active(const SynthModulation *m) {
    for(int i=0;i<2;i++)if(m->config.lfo[i].enabled||m->cutoff[i]!=0||m->pwm[i]!=0||m->fm[i]!=0)return 1;
    return 0;
}
static inline SynthModulationOutput synth_modulation_tick(SynthModulation *m,double tempo) {
    SynthModulationOutput out={0,0,0};double k=1/(m->sampleRate*.01);
    if(!isfinite(tempo)||tempo<20||tempo>300)tempo=120;
    for(int i=0;i<2;i++) {
        const SynthLFOConfig *l=&m->config.lfo[i];double depth=l->enabled?l->depth:0;
        double rate=l->syncBeats>0?tempo/(60*l->syncBeats):l->rateHz;
        m->rate[i]=synth_modulation_approach(m->rate[i],rate,30*k);
        m->cutoff[i]=synth_modulation_approach(m->cutoff[i],l->target==SYNTH_MOD_TARGET_CUTOFF?depth:0,k);
        m->pwm[i]=synth_modulation_approach(m->pwm[i],l->target==SYNTH_MOD_TARGET_PWM?depth:0,k);
        m->fm[i]=synth_modulation_approach(m->fm[i],l->target==SYNTH_MOD_TARGET_FM?depth:0,k);
        m->sineWeight[i]=synth_modulation_approach(m->sineWeight[i],l->waveform==SYNTH_MOD_SINE?1:0,k);
        double value=(1-4*fabs(m->phase[i]-.5))*(1-m->sineWeight[i])+sin(6.283185307179586*m->phase[i])*m->sineWeight[i];
        out.cutoffOctaves+=value*m->cutoff[i]*4;out.pwmOffset+=value*m->pwm[i]*.45;out.fmOffset+=value*m->fm[i]*10;
        m->phase[i]+=m->rate[i]/m->sampleRate;m->phase[i]-=floor(m->phase[i]);
    }
    out.cutoffOctaves=synth_modulation_clamp(out.cutoffOctaves,-4,4);
    out.pwmOffset=synth_modulation_clamp(out.pwmOffset,-.45,.45);
    out.fmOffset=synth_modulation_clamp(out.fmOffset,-10,10);return out;
}
static inline double synth_modulation_cutoff(double base,double octaves,double rate) {return synth_modulation_clamp(base*exp2(octaves),20,rate*.45);}
static inline double synth_modulation_pwm(double base,double offset) {return synth_modulation_clamp(base+offset,.05,.95);}
static inline double synth_modulation_fm(double base,double offset) {return synth_modulation_clamp(base+offset,0,10);}
#endif
