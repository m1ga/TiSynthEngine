#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../shared/synth_effects.h"

static void test_delay_tail(void) {
    SynthEffects fx;assert(synth_effects_init(&fx,16000));
    SynthEffectsConfig c=synth_effects_default_config();c.delay.enabled=1;c.delay.timeMs=2000;c.delay.feedback=.8;c.delay.mix=1;
    synth_effects_set_config(&fx,&c);
    for(int i=0;i<1600;i++){double l=0,r=0;synth_effects_process(&fx,120,&l,&r);}
    double l=1,r=0;synth_effects_process(&fx,120,&l,&r);
    c.delay.enabled=0;synth_effects_set_config(&fx,&c);
    double first=0,second=0;
    for(int i=1;i<80000;i++) {
        l=r=0;synth_effects_process(&fx,120,&l,&r);
        if(i==32000) {
            first=l;
        }
        if(i==64000) {
            second=r;
        }
    }
    assert(fabs(first-1)<1e-9&&fabs(second-.8)<1e-9);
    synth_effects_panic(&fx);
    for(int i=0;i<80000;i++){l=r=0;synth_effects_process(&fx,120,&l,&r);assert(l==0&&r==0);}
    synth_effects_destroy(&fx);
}
static void test_validation_and_ramp(void) {
    SynthEffects fx;assert(synth_effects_init(&fx,48000));
    SynthEffectsConfig c=synth_effects_default_config();c.delay.feedback=.851;assert(!synth_effects_validate(&c));
    c=synth_effects_default_config();c.chorus.rateHz=NAN;assert(!synth_effects_validate(&c));
    c=synth_effects_default_config();c.inputGain=.5;synth_effects_set_config(&fx,&c);
    for(int i=0;i<240;i++){double l=1,r=1;synth_effects_process(&fx,120,&l,&r);}
    assert(fabs(fx.current.inputGain-.75)<1e-10);
    for(int i=0;i<241;i++){double l=1,r=1;synth_effects_process(&fx,120,&l,&r);}
    assert(fx.current.inputGain==.5);
    synth_effects_destroy(&fx);
}
static void test_rate(double rate) {
    SynthEffects fx;
    assert(synth_effects_init(&fx, rate));
    SynthEffectsConfig c = synth_effects_default_config();
    assert(synth_effects_validate(&c));
    for (int i=0;i<1000;i++) {
        double l=sin(i*.17)*.3, r=cos(i*.13)*.2, a=l,b=r;
        synth_effects_process(&fx,120,&l,&r);
        synth_effects_finish(&fx,1,&l,&r);
        assert(l==a && r==b);
    }
    c.chorus.enabled=c.delay.enabled=c.reverb.enabled=c.saturation.enabled=1;
    c.chorus.mix=.5;c.delay.mix=.5;c.reverb.mix=.4;c.saturation.mix=.5;
    c.delay.feedback=.85;c.reverb.decay=8;
    synth_effects_set_config(&fx,&c);
    double energy=0;
    for (int i=0;i<(int)rate*3;i++) {
        double l=i<1000 ? .2*sin(i*.1) : 0, r=l;
        if(i==(int)rate){ c.delay.timeMs=30;c.chorus.rateHz=5;synth_effects_set_config(&fx,&c); }
        synth_effects_process(&fx,120,&l,&r);synth_effects_finish(&fx,1,&l,&r);
        assert(isfinite(l)&&isfinite(r));assert(fabs(l)<4&&fabs(r)<4);
        if(i>rate) energy+=l*l+r*r;
    }
    assert(energy>0);
    synth_effects_panic(&fx);
    for(int i=0;i<1000;i++){double l=0,r=0;synth_effects_process(&fx,120,&l,&r);synth_effects_finish(&fx,1,&l,&r);assert(l==0&&r==0);}
    c.chorus.enabled=c.delay.enabled=c.reverb.enabled=c.saturation.enabled=0;
    synth_effects_set_config(&fx,&c);
    for(int i=0;i<(int)(rate*31);i++){double l=0,r=0;synth_effects_process(&fx,120,&l,&r);synth_effects_finish(&fx,1,&l,&r);assert(isfinite(l)&&isfinite(r));}
    double l=.123,r=-.233;synth_effects_process(&fx,120,&l,&r);synth_effects_finish(&fx,1,&l,&r);assert(l==.123&&r==-.233);
    assert(synth_effects_memory_bytes(&fx)<32u*1024u*1024u);
    synth_effects_destroy(&fx);
}
int main(void){test_validation_and_ramp();test_delay_tail();double rates[]={16000,44100,48000,96000,192000};for(int i=0;i<5;i++)test_rate(rates[i]);puts("effects core regression passed");}
