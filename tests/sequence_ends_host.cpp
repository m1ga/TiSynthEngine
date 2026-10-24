// Exercise the actual JNI end-event producer, including its allocation boundary.
#define main original_render_main
#include "engine_render_host.cpp"
#undef main
#include <cassert>
static int allocations;
static jsize length;
static double values[8];
static jdoubleArray JNICALL allocate(JNIEnv *,jsize count) {
 ++allocations;length=count;return reinterpret_cast<jdoubleArray>(values);
}
static void JNICALL writeValues(JNIEnv *,jdoubleArray,jsize start,jsize count,const jdouble *input) {
 assert(start==0&&count==length&&count<=8);std::copy(input,input+count,values);
}
int main() {
 JNINativeInterface_ table{};table.NewDoubleArray=allocate;table.SetDoubleArrayRegion=writeValues;
 JNIEnv env{&table};auto engine=std::make_shared<Engine>(8,1,4);auto handle=reinterpret_cast<jlong>(&engine);
 for(int i=0;i<100;i++)assert(!Java_ti_synthengine_SynthThread_nativeSequenceEnds(&env,nullptr,handle));
 assert(allocations==0);
 auto *config=synth_transport_begin_write(&engine->transport_,42,1);assert(config);
 config->stepCount=1;config->steps[0].noteCount=1;config->steps[0].notes[0]=60;
 assert(synth_transport_commit(&engine->transport_,config,1));
 synth_transport_begin_block(&engine->transport_);assert(synth_transport_request_stop(&engine->transport_,42));
 synth_transport_begin_block(&engine->transport_);
 assert(Java_ti_synthengine_SynthThread_nativeSequenceEnds(&env,nullptr,handle));
 assert(allocations==1&&length==2&&values[0]==42&&values[1]==SYNTH_SEQUENCE_STOPPED);
 assert(!Java_ti_synthengine_SynthThread_nativeSequenceEnds(&env,nullptr,handle));
 assert(allocations==1);
 puts("JNI sequence ends: idle allocates nothing; durable stop delivered once PASS");
}
