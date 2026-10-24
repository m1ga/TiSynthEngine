/* Front-plane binaural FIR. Pure C, allocation-free, shared by Android/iOS.
 * Coordinates: x=-1 left/+1 right (60 degrees), y=-1 below/+1 above (40 degrees).
 * Control-thread preparation also supports smooth position changes.
 */
#ifndef TI_SYNTH_SPATIAL_H
#define TI_SYNTH_SPATIAL_H
#include <math.h>
#include <string.h>
#include "synth_hrtf_data.h"
/* Apple size-optimized Release builds use fused per-lane accumulation.
 * Other Apple build modes retain the original streaming implementation. */
#if defined(__aarch64__) && (defined(__ANDROID__) || (defined(__APPLE__) && defined(__OPTIMIZE_SIZE__)))
#define SYNTH_SPATIAL_TIME_BLOCKS 1
#else
#define SYNTH_SPATIAL_TIME_BLOCKS 0
#endif
#if SYNTH_SPATIAL_TIME_BLOCKS
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
    int i=0;
#if defined(__ANDROID__)
 int aligned=s->taps&~31;
#else
 int aligned=0;
#endif
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

/* Process consecutive samples with the original per-sample partial sums.
 * Reuse coefficients without changing multiplication/addition order.
 * Input and output arrays must be disjoint. */
#if SYNTH_SPATIAL_TIME_BLOCKS
static inline float synth_spatial_reduce_four(float32x4_t v) {
 return (vgetq_lane_f32(v,0)+vgetq_lane_f32(v,1))+(vgetq_lane_f32(v,2)+vgetq_lane_f32(v,3));
}
/* Copy the old window before future inputs overwrite its oldest samples. */
static inline void synth_spatial_process_four(SynthSpatial *s,const float *input,float *left,float *right){
 if(s->rampRemaining>0){for(int f=0;f<4;f++)synth_spatial_process(s,input[f],left+f,right+f);return;}
 float h[SYNTH_SPATIAL_MAX_TAPS+3];
 memcpy(h+4,s->history+s->position+1,(s->taps-1)*sizeof(float));
 for(int f=0;f<4;f++)h[3-f]=input[f];
 float32x4_t l0=vdupq_n_f32(0),l1=l0,l2=l0,l3=l0,r0=l0,r1=l0,r2=l0,r3=l0;
 int i=0;
#if defined(__ANDROID__)
 int aligned=s->taps&~31;
#else
 int aligned=0;
#endif
 {
#pragma clang fp contract(off)
 for(;i<aligned;i+=4){
  float32x4_t cl=vld1q_f32(s->left+i),cr=vld1q_f32(s->right+i);
  float32x4_t x0=vld1q_f32(h+3+i),x1=vld1q_f32(h+2+i),x2=vld1q_f32(h+1+i),x3=vld1q_f32(h+i);
  l0=vaddq_f32(l0,vmulq_f32(x0,cl));r0=vaddq_f32(r0,vmulq_f32(x0,cr));
  l1=vaddq_f32(l1,vmulq_f32(x1,cl));r1=vaddq_f32(r1,vmulq_f32(x1,cr));
  l2=vaddq_f32(l2,vmulq_f32(x2,cl));r2=vaddq_f32(r2,vmulq_f32(x2,cr));
  l3=vaddq_f32(l3,vmulq_f32(x3,cl));r3=vaddq_f32(r3,vmulq_f32(x3,cr));
 }
 }
 for(;i<s->taps;i+=4){
  float32x4_t cl=vld1q_f32(s->left+i),cr=vld1q_f32(s->right+i);
  float32x4_t x0=vld1q_f32(h+3+i),x1=vld1q_f32(h+2+i),x2=vld1q_f32(h+1+i),x3=vld1q_f32(h+i);
  l0=vfmaq_f32(l0,x0,cl);r0=vfmaq_f32(r0,x0,cr);
  l1=vfmaq_f32(l1,x1,cl);r1=vfmaq_f32(r1,x1,cr);
  l2=vfmaq_f32(l2,x2,cl);r2=vfmaq_f32(r2,x2,cr);
  l3=vfmaq_f32(l3,x3,cl);r3=vfmaq_f32(r3,x3,cr);
 }
 left[0]=synth_spatial_reduce_four(l0);right[0]=synth_spatial_reduce_four(r0);left[1]=synth_spatial_reduce_four(l1);right[1]=synth_spatial_reduce_four(r1);
 left[2]=synth_spatial_reduce_four(l2);right[2]=synth_spatial_reduce_four(r2);left[3]=synth_spatial_reduce_four(l3);right[3]=synth_spatial_reduce_four(r3);
 for(int f=0;f<4;f++){int p=s->position;s->history[p]=input[f];s->history[p+s->taps]=input[f];s->position=p?p-1:s->taps-1;}
}
static inline void synth_spatial_process_eight(SynthSpatial *s,const float *input,float *left,float *right){
 if(s->rampRemaining>0){for(int f=0;f<8;f++)synth_spatial_process(s,input[f],left+f,right+f);return;}
 float h[SYNTH_SPATIAL_MAX_TAPS+7];
 memcpy(h+8,s->history+s->position+1,(s->taps-1)*sizeof(float));
 for(int f=0;f<8;f++)h[7-f]=input[f];
 float32x4_t l0=vdupq_n_f32(0),l1=vdupq_n_f32(0),l2=vdupq_n_f32(0),l3=vdupq_n_f32(0),l4=vdupq_n_f32(0),l5=vdupq_n_f32(0),l6=vdupq_n_f32(0),l7=vdupq_n_f32(0),r0=vdupq_n_f32(0),r1=vdupq_n_f32(0),r2=vdupq_n_f32(0),r3=vdupq_n_f32(0),r4=vdupq_n_f32(0),r5=vdupq_n_f32(0),r6=vdupq_n_f32(0),r7=vdupq_n_f32(0);
 int i=0;
#if defined(__ANDROID__)
 int aligned=s->taps&~31;
#else
 int aligned=0;
#endif
 {
#pragma clang fp contract(off)
 for(;i<aligned;i+=4){
  float32x4_t cl=vld1q_f32(s->left+i),cr=vld1q_f32(s->right+i);
  float32x4_t x0=vld1q_f32(h+7+i);
  l0=vaddq_f32(l0,vmulq_f32(x0,cl));r0=vaddq_f32(r0,vmulq_f32(x0,cr));
  float32x4_t x1=vld1q_f32(h+6+i);
  l1=vaddq_f32(l1,vmulq_f32(x1,cl));r1=vaddq_f32(r1,vmulq_f32(x1,cr));
  float32x4_t x2=vld1q_f32(h+5+i);
  l2=vaddq_f32(l2,vmulq_f32(x2,cl));r2=vaddq_f32(r2,vmulq_f32(x2,cr));
  float32x4_t x3=vld1q_f32(h+4+i);
  l3=vaddq_f32(l3,vmulq_f32(x3,cl));r3=vaddq_f32(r3,vmulq_f32(x3,cr));
  float32x4_t x4=vld1q_f32(h+3+i);
  l4=vaddq_f32(l4,vmulq_f32(x4,cl));r4=vaddq_f32(r4,vmulq_f32(x4,cr));
  float32x4_t x5=vld1q_f32(h+2+i);
  l5=vaddq_f32(l5,vmulq_f32(x5,cl));r5=vaddq_f32(r5,vmulq_f32(x5,cr));
  float32x4_t x6=vld1q_f32(h+1+i);
  l6=vaddq_f32(l6,vmulq_f32(x6,cl));r6=vaddq_f32(r6,vmulq_f32(x6,cr));
  float32x4_t x7=vld1q_f32(h+0+i);
  l7=vaddq_f32(l7,vmulq_f32(x7,cl));r7=vaddq_f32(r7,vmulq_f32(x7,cr));
 }
 }
 for(;i<s->taps;i+=4){
  float32x4_t cl=vld1q_f32(s->left+i),cr=vld1q_f32(s->right+i);
  float32x4_t x0=vld1q_f32(h+7+i);
  l0=vfmaq_f32(l0,x0,cl);r0=vfmaq_f32(r0,x0,cr);
  float32x4_t x1=vld1q_f32(h+6+i);
  l1=vfmaq_f32(l1,x1,cl);r1=vfmaq_f32(r1,x1,cr);
  float32x4_t x2=vld1q_f32(h+5+i);
  l2=vfmaq_f32(l2,x2,cl);r2=vfmaq_f32(r2,x2,cr);
  float32x4_t x3=vld1q_f32(h+4+i);
  l3=vfmaq_f32(l3,x3,cl);r3=vfmaq_f32(r3,x3,cr);
  float32x4_t x4=vld1q_f32(h+3+i);
  l4=vfmaq_f32(l4,x4,cl);r4=vfmaq_f32(r4,x4,cr);
  float32x4_t x5=vld1q_f32(h+2+i);
  l5=vfmaq_f32(l5,x5,cl);r5=vfmaq_f32(r5,x5,cr);
  float32x4_t x6=vld1q_f32(h+1+i);
  l6=vfmaq_f32(l6,x6,cl);r6=vfmaq_f32(r6,x6,cr);
  float32x4_t x7=vld1q_f32(h+0+i);
  l7=vfmaq_f32(l7,x7,cl);r7=vfmaq_f32(r7,x7,cr);
 }
 left[0]=synth_spatial_reduce_four(l0);right[0]=synth_spatial_reduce_four(r0);
 left[1]=synth_spatial_reduce_four(l1);right[1]=synth_spatial_reduce_four(r1);
 left[2]=synth_spatial_reduce_four(l2);right[2]=synth_spatial_reduce_four(r2);
 left[3]=synth_spatial_reduce_four(l3);right[3]=synth_spatial_reduce_four(r3);
 left[4]=synth_spatial_reduce_four(l4);right[4]=synth_spatial_reduce_four(r4);
 left[5]=synth_spatial_reduce_four(l5);right[5]=synth_spatial_reduce_four(r5);
 left[6]=synth_spatial_reduce_four(l6);right[6]=synth_spatial_reduce_four(r6);
 left[7]=synth_spatial_reduce_four(l7);right[7]=synth_spatial_reduce_four(r7);
 for(int f=0;f<8;f++){int p=s->position;s->history[p]=input[f];s->history[p+s->taps]=input[f];s->position=p?p-1:s->taps-1;}
}
#endif
static inline void synth_spatial_process_block(SynthSpatial *s,const float *input,float *left,float *right,int frames) {
 int i=0;
#if SYNTH_SPATIAL_TIME_BLOCKS
 for(;i+7<frames;i+=8)synth_spatial_process_eight(s,input+i,left+i,right+i);
 for(;i+3<frames;i+=4)synth_spatial_process_four(s,input+i,left+i,right+i);
#endif
 for(;i<frames;i++)synth_spatial_process(s,input[i],left+i,right+i);
}
#endif
