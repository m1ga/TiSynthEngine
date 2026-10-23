/* Front-plane binaural FIR. Pure C, allocation-free, shared by Android/iOS.
 * Coordinates: x=-1 left/+1 right (60 degrees), y=-1 below/+1 above (40 degrees).
 * Control-thread preparation also supports smooth position changes.
 */
#ifndef TI_SYNTH_SPATIAL_H
#define TI_SYNTH_SPATIAL_H
#include <math.h>
#include <string.h>
#include "synth_hrtf_data.h"
#if defined(__ANDROID__) && defined(__aarch64__)
#include <arm_neon.h>
#endif
#define SYNTH_SPATIAL_MAX_TAPS 640

typedef struct {
 int taps;
 float left[SYNTH_SPATIAL_MAX_TAPS],right[SYNTH_SPATIAL_MAX_TAPS];
} SynthSpatialPrepared;

typedef struct {
  int taps, position,rampRemaining;
  float left[SYNTH_SPATIAL_MAX_TAPS], right[SYNTH_SPATIAL_MAX_TAPS];
  float history[SYNTH_SPATIAL_MAX_TAPS * 2];
  float targetLeft[SYNTH_SPATIAL_MAX_TAPS],targetRight[SYNTH_SPATIAL_MAX_TAPS];
  float deltaLeft[SYNTH_SPATIAL_MAX_TAPS],deltaRight[SYNTH_SPATIAL_MAX_TAPS];
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
/* Prepare all interpolation and resampling before publishing a command. */
static inline void synth_spatial_prepare(SynthSpatialPrepared *p,float x,float y,double rate) {
 SynthSpatial temporary;synth_spatial_init(&temporary,x,y,rate);
 p->taps=temporary.taps;memcpy(p->left,temporary.left,sizeof(p->left));memcpy(p->right,temporary.right,sizeof(p->right));
}
static inline void synth_spatial_set_target(SynthSpatial *s,const SynthSpatialPrepared *p,int frames) {
 if(p->taps!=s->taps)return;
 if(frames<1)frames=1;
 memcpy(s->targetLeft,p->left,sizeof(p->left));memcpy(s->targetRight,p->right,sizeof(p->right));
 for(int i=0;i<s->taps;i++) {
  s->deltaLeft[i]=(p->left[i]-s->left[i])/frames;s->deltaRight[i]=(p->right[i]-s->right[i])/frames;
 }
 s->rampRemaining=frames;
}
static inline void synth_spatial_process(SynthSpatial *s,float input,float *left,float *right) {
  if(s->rampRemaining>0) {
   if(--s->rampRemaining==0) {
    memcpy(s->left,s->targetLeft,sizeof(s->left));memcpy(s->right,s->targetRight,sizeof(s->right));
   }else for(int i=0;i<s->taps;i++) {s->left[i]+=s->deltaLeft[i];s->right[i]+=s->deltaRight[i];}
  }
  int p=s->position;s->history[p]=input;s->history[p+s->taps]=input;
  const float *h=s->history+p;
#if defined(__ANDROID__) && defined(__aarch64__)
  {
#pragma clang fp contract(off)
    /* Preserve the shipping ARM64 kernel's four partial sums and rounding:
     * LLVM used separate products/adds in 32-tap groups, then fused its tail.
     * Explicit lanes avoid the extracted-lane reductions and register spills.
     */
    float32x4_t l=vdupq_n_f32(0),r=vdupq_n_f32(0);
    int i=0,aligned=s->taps&~31;
    for(;i<aligned;i+=4) {
      float32x4_t input4=vld1q_f32(h+i);
      l=vaddq_f32(l,vmulq_f32(input4,vld1q_f32(s->left+i)));
      r=vaddq_f32(r,vmulq_f32(input4,vld1q_f32(s->right+i)));
    }
    for(;i<s->taps;i+=4) {
      float32x4_t input4=vld1q_f32(h+i);
      l=vfmaq_f32(l,input4,vld1q_f32(s->left+i));
      r=vfmaq_f32(r,input4,vld1q_f32(s->right+i));
    }
    *left=(vgetq_lane_f32(l,0)+vgetq_lane_f32(l,1))+(vgetq_lane_f32(l,2)+vgetq_lane_f32(l,3));
    *right=(vgetq_lane_f32(r,0)+vgetq_lane_f32(r,1))+(vgetq_lane_f32(r,2)+vgetq_lane_f32(r,3));
  }
#else
  float l0=0,l1=0,l2=0,l3=0,r0=0,r1=0,r2=0,r3=0;
  for(int i=0;i<s->taps;i+=4) {
    l0+=h[i]*s->left[i];l1+=h[i+1]*s->left[i+1];l2+=h[i+2]*s->left[i+2];l3+=h[i+3]*s->left[i+3];
    r0+=h[i]*s->right[i];r1+=h[i+1]*s->right[i+1];r2+=h[i+2]*s->right[i+2];r3+=h[i+3]*s->right[i+3];
  }
  *left=(l0+l1)+(l2+l3);*right=(r0+r1)+(r2+r3);
#endif
  s->position=p?p-1:s->taps-1;
}
#endif
