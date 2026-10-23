package ti.synthengine;
import java.util.*;
public final class SynthHeldContractRegression {
  static Map<String,Object> m(Object... kv) { Map<String,Object> m=new HashMap<>(); for(int i=0;i<kv.length;i+=2)m.put((String)kv[i],kv[i+1]); return m; }
  static void check(boolean condition,String message) { if(!condition)throw new AssertionError(message); }
  public static void main(String[] args) {
    check(SynthContract.validateNoteOn(m("voiceId",1,"attack",2000,"decay",4000,"sustain",.3,"release",4000),19845).accepted,"independent ADSR times");
    for(Object id:new Object[]{0,-1,1.5,true,"1",2147483648.0,null}) check(!SynthContract.validateNoteOn(m("voiceId",id),19845).accepted,"invalid id "+id);
    check(!SynthContract.validateNoteOff(m()).accepted,"missing id");
    check(SynthContract.validateNoteOff(m("voiceId",1)).accepted,"release");
    for(String key:new String[]{"duration","frequencyEnd","panEnd","noiseRateEnd","pulseWidthEnd","filterHzEnd","fmDepthEnd"})check(!SynthContract.validateNoteOn(m("voiceId",1,key,100),19845).accepted,"held sweep "+key);
    check(SynthContract.validateUpdateVoice(m("voiceId",2,"note","A4","fmDepth",0,"filterQ",2),19845).accepted,"partial update");
    check(!SynthContract.validateUpdateVoice(m("voiceId",2,"note","A4","frequency",440),19845).accepted,"ambiguous pitch");
    check(!SynthContract.validateUpdateVoice(m("voiceId",2,"volume",Double.NaN),19845).accepted,"NaN update");
    for(String key:new String[]{"attack","release","waveType","filterType","waveTable","bitDepth","detune"})check(!SynthContract.validateUpdateVoice(m("voiceId",2,key,1),19845).accepted,"immutable "+key);
    check(!SynthContract.validateUpdateVoice(m("voiceId",2),19845).accepted,"empty update");
    System.out.println("held contract: PASS");
  }
}
