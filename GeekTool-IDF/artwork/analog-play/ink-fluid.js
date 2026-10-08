// 浏览器设计样机：速度场投影 + 染料平流。WebGL用于本地预览，不进入ESP32固件。
// 速度/压力网格116×116，染料和最终输出466×466；单位为速度网格的格子/秒。
const INK_PALETTES={
  coast:[[.20,.46,.70],[.30,.70,.68],[.85,.43,.23]],
  mineral:[[.22,.53,.43],[.56,.70,.47],[.78,.59,.32]],
  rose:[[.40,.39,.65],[.70,.45,.61],[.86,.48,.34]]
};
function createInkStudy(canvas,status){
  const gl=canvas.getContext('webgl2',{alpha:false,antialias:false,depth:false,preserveDrawingBuffer:true});
  const resources={textures:[],framebuffers:[],programs:[],shaders:[],buffers:[]};
  const fail=message=>{
    status.hidden=false;status.textContent=message;canvas.setAttribute('data-renderer','unavailable');
    destroy();return {ready:false,render(){},step(){},reset(){},inject(){},nextColor(){},setPalette(){},destroy(){}};
  };
  function destroy(){
    if(!gl)return;
    for(const item of resources.textures)gl.deleteTexture(item);
    for(const item of resources.framebuffers)gl.deleteFramebuffer(item);
    for(const item of resources.programs)gl.deleteProgram(item);
    for(const item of resources.shaders)gl.deleteShader(item);
    for(const item of resources.buffers)gl.deleteBuffer(item);
    for(const group of Object.values(resources))group.length=0;
  }
  if(!gl||!gl.getExtension('EXT_color_buffer_float'))return fail('此浏览器不支持染料预览所需的图形能力，请查看审阅图。');
  const vertex=`#version 300 es
    in vec2 position;out vec2 uv;
    void main(){uv=position*.5+.5;gl_Position=vec4(position,0.,1.);}`;
  const common=`#version 300 es
    precision highp float;
    in vec2 uv;out vec4 result;
    uniform sampler2D source,velocity,pressure,divergence,curl;
    uniform vec2 texel,velocityTexel,point;
    uniform vec3 color;
    uniform float dt,radius,dissipation;
    float inside(vec2 p){return 1.-smoothstep(.455,.467,length(p-.5));}
  `;
  function program(body){
    const compiled=[];
    for(const [type,source] of [[gl.VERTEX_SHADER,vertex],[gl.FRAGMENT_SHADER,common+body]]){
      const shader=gl.createShader(type);if(!shader)throw new Error('无法分配染料着色器');
      resources.shaders.push(shader);gl.shaderSource(shader,source);gl.compileShader(shader);
      if(!gl.getShaderParameter(shader,gl.COMPILE_STATUS))throw new Error(gl.getShaderInfoLog(shader));compiled.push(shader);
    }
    const handle=gl.createProgram();if(!handle)throw new Error('无法分配染料绘制程序');resources.programs.push(handle);
    for(const shader of compiled)gl.attachShader(handle,shader);gl.linkProgram(handle);
    if(!gl.getProgramParameter(handle,gl.LINK_STATUS))throw new Error(gl.getProgramInfoLog(handle));
    const uniforms=Object.fromEntries(['source','velocity','pressure','divergence','curl','texel','velocityTexel','point','color','dt','radius','dissipation'].map(name=>[name,gl.getUniformLocation(handle,name)]));
    return {handle,uniforms,attribute:gl.getAttribLocation(handle,'position')};
  }
  function target(size,linear=true){
    const texture=gl.createTexture(),framebuffer=gl.createFramebuffer();
    if(texture)resources.textures.push(texture);if(framebuffer)resources.framebuffers.push(framebuffer);
    if(!texture||!framebuffer)throw new Error('无法分配染料纹理');
    gl.bindTexture(gl.TEXTURE_2D,texture);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,linear?gl.LINEAR:gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER,linear?gl.LINEAR:gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_S,gl.CLAMP_TO_EDGE);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_T,gl.CLAMP_TO_EDGE);
    gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA16F,size,size,0,gl.RGBA,gl.HALF_FLOAT,null);
    gl.bindFramebuffer(gl.FRAMEBUFFER,framebuffer);gl.framebufferTexture2D(gl.FRAMEBUFFER,gl.COLOR_ATTACHMENT0,gl.TEXTURE_2D,texture,0);
    if(gl.checkFramebufferStatus(gl.FRAMEBUFFER)!==gl.FRAMEBUFFER_COMPLETE)throw new Error('染料浮点帧缓冲不可用');
    return {texture,framebuffer,size};
  }
  function pair(size,linear){let read=target(size,linear),write=target(size,linear);return {get read(){return read;},get write(){return write;},swap(){[read,write]=[write,read];}};}
  let quad,advect,splat,diverge,curlPass,vorticity,jacobi,project,display;
  let flow,dye,pres,div,curls;
  try{
    quad=gl.createBuffer();if(!quad)throw new Error('无法分配染料画布');resources.buffers.push(quad);
    gl.bindBuffer(gl.ARRAY_BUFFER,quad);gl.bufferData(gl.ARRAY_BUFFER,new Float32Array([-1,-1,1,-1,-1,1,1,1]),gl.STATIC_DRAW);
    advect=program(`void main(){vec2 v=texture(velocity,uv).xy;vec2 back=uv-dt*v*velocityTexel;
      result=vec4(texture(source,back).rgb*inside(uv)/(1.+dt*dissipation),1.);}`);
    splat=program(`void main(){vec2 d=uv-point;float amount=exp(-dot(d,d)/radius)*inside(uv);
      result=vec4(texture(source,uv).rgb+color*amount,1.);}`);
    diverge=program(`void main(){vec2 l=texture(velocity,uv-vec2(texel.x,0)).xy;
      vec2 r=texture(velocity,uv+vec2(texel.x,0)).xy;vec2 b=texture(velocity,uv-vec2(0,texel.y)).xy;
      vec2 t=texture(velocity,uv+vec2(0,texel.y)).xy;result=vec4(.5*(r.x-l.x+t.y-b.y),0,0,1);}`);
    curlPass=program(`void main(){float l=texture(velocity,uv-vec2(texel.x,0)).y;
      float r=texture(velocity,uv+vec2(texel.x,0)).y;float b=texture(velocity,uv-vec2(0,texel.y)).x;
      float t=texture(velocity,uv+vec2(0,texel.y)).x;result=vec4(.5*(r-l-t+b),0,0,1);}`);
    vorticity=program(`void main(){float l=abs(texture(curl,uv-vec2(texel.x,0)).r);
      float r=abs(texture(curl,uv+vec2(texel.x,0)).r);float b=abs(texture(curl,uv-vec2(0,texel.y)).r);
      float t=abs(texture(curl,uv+vec2(0,texel.y)).r);float c=texture(curl,uv).r;
      vec2 n=.5*vec2(t-b,l-r);n/=length(n)+.0001;vec2 v=texture(velocity,uv).xy+n*c*dt*2.;
      result=vec4(clamp(v,vec2(-250.),vec2(250.))*inside(uv),0,1);}`);
    jacobi=program(`void main(){float l=texture(pressure,uv-vec2(texel.x,0)).r;
      float r=texture(pressure,uv+vec2(texel.x,0)).r;float b=texture(pressure,uv-vec2(0,texel.y)).r;
      float t=texture(pressure,uv+vec2(0,texel.y)).r;
      result=vec4((l+r+b+t-texture(divergence,uv).r)*.25,0,0,1);}`);
    project=program(`void main(){float l=texture(pressure,uv-vec2(texel.x,0)).r;
      float r=texture(pressure,uv+vec2(texel.x,0)).r;float b=texture(pressure,uv-vec2(0,texel.y)).r;
      float t=texture(pressure,uv+vec2(0,texel.y)).r;
      result=vec4((texture(velocity,uv).xy-.5*vec2(r-l,t-b))*inside(uv),0,1);}`);
    display=program(`void main(){vec3 ink=max(texture(source,uv).rgb,vec3(0.));
      float density=max(ink.r,max(ink.g,ink.b));
      vec3 shaded=pow(ink/(.35+density),vec3(.75))*smoothstep(.025,.19,density);
      result=vec4(shaded*inside(uv),1.);}`);
    flow=pair(116,true);dye=pair(466,true);pres=pair(116,false);div=target(116,false);curls=target(116,false);
  }catch(error){return fail(`染料预览初始化失败：${error.message}`);}

  function draw(pass,to,textures={},values={}){
    gl.useProgram(pass.handle);gl.bindFramebuffer(gl.FRAMEBUFFER,to?to.framebuffer:null);
    gl.viewport(0,0,to?to.size:466,to?to.size:466);gl.bindBuffer(gl.ARRAY_BUFFER,quad);
    gl.enableVertexAttribArray(pass.attribute);gl.vertexAttribPointer(pass.attribute,2,gl.FLOAT,false,0,0);
    let unit=0;
    for(const [name,target] of Object.entries(textures)){
      gl.activeTexture(gl.TEXTURE0+unit);gl.bindTexture(gl.TEXTURE_2D,target.texture);gl.uniform1i(pass.uniforms[name],unit++);
    }
    for(const [name,value] of Object.entries(values)){
      const uniform=pass.uniforms[name];if(!uniform)continue;
      if(Array.isArray(value)){if(value.length===2)gl.uniform2f(uniform,...value);else gl.uniform3f(uniform,...value);}
      else gl.uniform1f(uniform,value);
    }
    gl.drawArrays(gl.TRIANGLE_STRIP,0,4);
  }
  let palette='coast',colorIndex=0,disposed=false;
  function inject(x,y,dx,dy,addDye=true,colorOverride=null){
    if(disposed||Math.hypot(x-.5,y-.5)>.44)return;
    draw(splat,flow.write,{source:flow.read},{point:[x,y],color:[dx,dy,0],radius:.0017});flow.swap();
    if(addDye){const color=colorOverride||INK_PALETTES[palette][colorIndex%3];
      draw(splat,dye.write,{source:dye.read},{point:[x,y],color,radius:.0023});dye.swap();}
  }
  function reset(){
    if(disposed)return;colorIndex=0;gl.clearColor(0,0,0,0);
    for(const framebuffer of resources.framebuffers){gl.bindFramebuffer(gl.FRAMEBUFFER,framebuffer);gl.clear(gl.COLOR_BUFFER_BIT);}
    // 宽色带有交汇区，初始就能看出染料的厚度；不是孤立圆点或固定装饰水滴。
    for(let band=0;band<3;band++)for(let i=0;i<18;i++){
      const t=i/17,angle=t*Math.PI*1.65+band*Math.PI*.66;
      const r=.08+t*.22;
      const color=INK_PALETTES[palette][band];
      inject(.5+Math.cos(angle)*r,.47+Math.sin(angle)*r,-Math.sin(angle)*26,Math.cos(angle)*26,true,color.map(c=>c*.75));
    }
    render();
  }
  function step(dt){
    if(disposed)return;
    const interval=Math.min(dt,.025), texel=[1/116,1/116];
    draw(advect,flow.write,{source:flow.read,velocity:flow.read},{dt:interval,velocityTexel:texel,dissipation:.45});flow.swap();
    draw(curlPass,curls,{velocity:flow.read},{texel});
    draw(vorticity,flow.write,{velocity:flow.read,curl:curls},{texel,dt:interval});flow.swap();
    draw(diverge,div,{velocity:flow.read},{texel});
    gl.bindFramebuffer(gl.FRAMEBUFFER,pres.read.framebuffer);gl.clear(gl.COLOR_BUFFER_BIT);
    for(let i=0;i<20;i++){draw(jacobi,pres.write,{pressure:pres.read,divergence:div},{texel});pres.swap();}
    draw(project,flow.write,{velocity:flow.read,pressure:pres.read},{texel});flow.swap();
    draw(advect,dye.write,{source:dye.read,velocity:flow.read},{dt:interval,velocityTexel:texel,dissipation:.012});dye.swap();
  }
  function render(){if(!disposed)draw(display,null,{source:dye.read});}
  status.hidden=true;canvas.setAttribute('data-renderer','ready');reset();
  const onContextLost=event=>{event.preventDefault();disposed=true;status.hidden=false;status.textContent='图形预览已暂停，请重新打开页面恢复。';canvas.setAttribute('data-renderer','lost');};
  canvas.addEventListener('webglcontextlost',onContextLost);
  return {ready:true,render,step,reset,inject,
    nextColor(){colorIndex++;},setPalette(value){if(INK_PALETTES[value]){palette=value;reset();}},
    destroy(){disposed=true;canvas.removeEventListener('webglcontextlost',onContextLost);destroy();}};
}
