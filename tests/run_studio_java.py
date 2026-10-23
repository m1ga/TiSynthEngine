#!/usr/bin/env python3
"""Replay the shared Studio acceptance corpus against the actual Java contracts."""
from pathlib import Path
import json,subprocess,tempfile,sys
root=Path(__file__).resolve().parents[1]
def literal(v):
 if v is None:return 'null'
 if isinstance(v,bool):return 'Boolean.TRUE' if v else 'Boolean.FALSE'
 if isinstance(v,str):return json.dumps(v)
 if isinstance(v,(int,float)):return str(v)
 if isinstance(v,list):return 'new Object[]{'+','.join(map(literal,v))+'}'
 return 'm('+','.join(x for k,w in v.items() for x in (json.dumps(k),literal(w)))+')'
memory_test=root/'android/test/ti/synthengine/SynthSequenceMemoryRegression.java'
if len(sys.argv)>1 and sys.argv[1]=='--memory-only':
 with tempfile.TemporaryDirectory() as work:
  base=root/'android/src/ti/synthengine'
  subprocess.run(['javac','-d',work,str(base/'SynthContract.java'),str(base/'SynthStudioContract.java'),str(memory_test)],check=True)
  subprocess.run(['java','-cp',work,'ti.synthengine.SynthSequenceMemoryRegression'],check=True)
 sys.exit(0)
with tempfile.TemporaryDirectory() as work:
 p=Path(work);calls=[]
 for i,line in enumerate((Path(sys.argv[1]) if len(sys.argv)>1 else root/'tests/studio_contract_corpus.jsonl').read_text().splitlines()):
  r=json.loads(line)
  calls.append(f'check({i+1},{literal(r["api"])},{literal(r["options"])},{literal(r.get("previous"))},{str(r["accepted"]).lower()},{str(r.get("update",False)).lower()},{r.get("maxFrequency",20000)});')
 code='''package ti.synthengine;
import java.util.*;
class StudioCorpusRegression {
 static Map<String,Object> m(Object... a){Map<String,Object> o=new HashMap<>();for(int i=0;i<a.length;i+=2)o.put((String)a[i],a[i+1]);return o;}
 static void check(int line,String api,Map<String,Object> options,Map<String,Object> previous,boolean expected,boolean update,double maxHz){
  boolean accepted=false;
  try {
   if(api.equals("sequence")){
    SynthStudioContract.Sequence before=previous==null?null:SynthStudioContract.sequence(previous,null,true,maxHz);
    SynthStudioContract.sequence(options,before,previous==null&&!update,maxHz);accepted=true;
   } else if(api.equals("masterProcessing")){
    double[] before=previous==null?null:SynthStudioContract.effects(previous,null);
    SynthStudioContract.effects(options,before);accepted=true;
   } else if(api.equals("noteOn"))accepted=SynthContract.validateNoteOn(options,maxHz).accepted;
   else accepted=SynthContract.validateUpdateVoice(options,maxHz).accepted;
  } catch(IllegalArgumentException|NullPointerException|ClassCastException rejected){}
  if(accepted!=expected)throw new AssertionError("corpus line "+line+" "+api+" expected "+expected+" actual "+accepted);
 }
 public static void main(String[] args){
'''+ '\n'.join(calls)+f'\nSystem.out.println("Studio Java corpus: {len(calls)} cases PASS");'+'\n}}'
 source=p/'StudioCorpusRegression.java';source.write_text(code)
 base=root/'android/src/ti/synthengine'
 subprocess.run(['javac','-d',work,str(source),str(base/'SynthContract.java'),str(base/'SynthStudioContract.java'),str(root/'tests/studio_contract_java.java'),str(memory_test)],check=True)
 for main in (['StudioCorpusRegression'] if len(sys.argv)>1 else ['StudioCorpusRegression','StudioContractRegression','SynthSequenceMemoryRegression']):
  subprocess.run(['java','-cp',work,'ti.synthengine.'+main],check=True)
