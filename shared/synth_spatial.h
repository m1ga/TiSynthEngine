/* Front-plane binaural FIR. Pure C, allocation-free, shared by Android/iOS.
 * Coordinates: x=-1 left/+1 right (60 degrees), y=-1 below/+1 above (40 degrees).
 * Fixed source position per event. Sample-rate conversion happens once at onset.
 */
#ifndef TI_SYNTH_SPATIAL_H
#define TI_SYNTH_SPATIAL_H
#include <math.h>
#include <string.h>
#include "synth_hrtf_data.h"
#define SYNTH_SPATIAL_MAX_TAPS 640

typedef struct {
  int taps, position;
  float left[SYNTH_SPATIAL_MAX_TAPS], right[SYNTH_SPATIAL_MAX_TAPS];
  float history[SYNTH_SPATIAL_MAX_TAPS * 2];
} SynthSpatial;

static inline float synth_spatial_clamp(float v) { return fmaxf(-1, fminf(1, v)); }
static inline float synth_spatial_coefficient(float x, float y, int ear, int tap) {
  if(tap<0 || tap>=128) return 0;
  x=synth_spatial_clamp(x)+1; y=synth_spatial_clamp(y)+1;
  int ix=(int)x, iy=(int)y;if(ix==2)ix=1;if(iy==2)iy=1;
  float fx=x-ix,fy=y-iy;
  float a=synth_hrtf_grid[iy][ix][ear][tap]*(1-fx)+synth_hrtf_grid[iy][ix+1][ear][tap]*fx;
  float b=synth_hrtf_grid[iy+1][ix][ear][tap]*(1-fx)+synth_hrtf_grid[iy+1][ix+1][ear][tap]*fx;
  return (a*(1-fy)+b*fy)*(SYNTH_HRTF_GAIN/32768.0f);
}
static inline void synth_spatial_init(SynthSpatial *s,float x,float y,double rate) {
  memset(s,0,sizeof(*s));
  if(!isfinite(rate)||rate<8000)rate=44100;
  double ratio=rate/44100.0;
  int taps=(int)ceil(128*ratio);taps=(taps+3)&~3;
  s->taps=taps>SYNTH_SPATIAL_MAX_TAPS?SYNTH_SPATIAL_MAX_TAPS:taps;
  for(int i=0;i<s->taps;i++) {
    double p=i/ratio;int k=(int)p;float t=(float)(p-k);
    s->left[i]=((1-t)*synth_spatial_coefficient(x,y,0,k)+t*synth_spatial_coefficient(x,y,0,k+1))/(float)ratio;
    s->right[i]=((1-t)*synth_spatial_coefficient(x,y,1,k)+t*synth_spatial_coefficient(x,y,1,k+1))/(float)ratio;
  }
}
static inline void synth_spatial_process(SynthSpatial *s,float input,float *left,float *right) {
  int p=s->position;s->history[p]=input;s->history[p+s->taps]=input;
  const float *h=s->history+p;
  float l0=0,l1=0,l2=0,l3=0,r0=0,r1=0,r2=0,r3=0;
  for(int i=0;i<s->taps;i+=4) {
    l0+=h[i]*s->left[i];l1+=h[i+1]*s->left[i+1];l2+=h[i+2]*s->left[i+2];l3+=h[i+3]*s->left[i+3];
    r0+=h[i]*s->right[i];r1+=h[i+1]*s->right[i+1];r2+=h[i+2]*s->right[i+2];r3+=h[i+3]*s->right[i+3];
  }
  *left=(l0+l1)+(l2+l3);*right=(r0+r1)+(r2+r3);
  s->position=p?p-1:s->taps-1;
}
#endif
