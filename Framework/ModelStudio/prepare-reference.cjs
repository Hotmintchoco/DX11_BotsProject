// Cross-check the native port against the previously verified browser parser.
const fs=require('fs'),path=require('path');
const root=process.argv[2],out=process.argv[3];
if(!root||!out)throw Error('Usage: node prepare-reference.cjs RESOURCE_ROOT OUTPUT_DIR');
const F=require(path.join(root,'BoutViewer','formats.js'));
function read(p){let b=fs.readFileSync(p);return b.buffer.slice(b.byteOffset,b.byteOffset+b.byteLength);}
fs.mkdirSync(out,{recursive:true});
for(const name of ['az','gr','hs','BloodKnight']){
 const player=name!=='BloodKnight',dir=player?path.join(root,'플레이어',name[0].toUpperCase()+name[1]):path.join(root,'monster');
 const bon=F.parseBON(read(path.join(dir,player?name+'_bone.bon':name+'.bon')));
 const parts=player?['body','arm','head']:['monster'];let lines=[];
 for(const [i,part] of parts.entries()){
  const model=F.parseBSC(read(path.join(dir,player?`${name}_000_${part}.bsc`:name+'.bsc')));
  for(const [j,m] of model.meshes.entries())for(const frame of [0,100,Math.min(419,bon.frames-1),bon.frames-1]){
   const a=bon.matrix(m.name,frame),v=m.positions;
   const xyz=[0,1,2].map(k=>v[0]*a[k]+v[1]*a[4+k]+v[2]*a[8+k]+a[12+k]);
   lines.push([player?i+1:0,j,frame,...xyz].join(' '));
  }
 }
 fs.writeFileSync(path.join(out,name+'_reference.txt'),lines.join('\n'));
}
