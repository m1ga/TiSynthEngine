#define main legacy_engine_main
#include "engine_render_host.cpp"
#undef main
static std::vector<float> spatial(float x,float y,int capacity=8) {
 Rig rig(capacity,1);Command c=note(rig,500,350,2,80,.4,4);
 c.spatial=true;c.spatialX=x;c.spatialY=y;
 rig.e().enqueue(c);rig.settle();return rig.render(samples(400));
}
int main(){
 auto a=spatial(1,0),b=spatial(-1,0),up=spatial(0,1),down=spatial(0,-1);
 double er=0,el=0,d=0;
 for(size_t i=0;i<a.size();i+=2){CHECK(std::fabs(a[i]-b[i+1])<1e-6,"mirror left/right");CHECK(std::fabs(a[i+1]-b[i])<1e-6,"mirror right/left");el+=a[i]*a[i];er+=a[i+1]*a[i+1];d+=std::fabs(up[i]-down[i]);}
 CHECK(er>el,"right source stronger in right ear");CHECK(d>.1,"elevation changes spectrum");
 Rig r(2,1);for(int i=0;i<4;i++){Command c=note(r,300+i*100,500,2,100,.3,0);c.spatial=true;c.spatialX=(i%2)*2-1;c.spatialY=.5;r.e().enqueue(c);}r.settle();auto initial=r.render(samples(100));
 Command stop;stop.type=2;r.e().enqueue(stop);auto tail=r.render(samples(80));
 for(float s:initial)CHECK(std::isfinite(s),"spatial stealing finite");
 for(float s:tail)CHECK(std::isfinite(s),"spatial stop finite");
 CHECK(rms(tail,samples(60),samples(80))==0,"spatial stop drains to silence");
 Rig shortRig(1,0);Command shortNote=note(shortRig,1000,1,0,0,.1,0);
 shortNote.spatial=true;shortNote.spatialX=1;shortRig.e().enqueue(shortNote);shortRig.settle();
 auto shortOut=shortRig.render(samples(30));SynthSpatial reference;synth_spatial_init(&reference,1,0,48000);
 for(int i=0;i<500;i++){
  float l,r;float input=i<samples(1)?static_cast<float>(.1*std::sin(2*kPi*1000*i/kRate)):0;
  synth_spatial_process(&reference,input,&l,&r);
  CHECK(std::fabs(shortOut[(i+kDelay)*2]-l)<1e-6,"short note retains complete left FIR response");
  CHECK(std::fabs(shortOut[(i+kDelay)*2+1]-r)<1e-6,"short note retains complete right FIR response");
 }
 if(failures)return 1;puts("spatial_engine_host: all checks passed");return 0;
}
