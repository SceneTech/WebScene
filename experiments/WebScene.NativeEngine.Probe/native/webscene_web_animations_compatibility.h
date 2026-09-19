#pragma once
#include <string_view>

namespace webscene_native {
inline constexpr std::string_view webAnimationsCompatibilityScript=R"JS(
(() => {
  const createNative=globalThis.__webSceneCreateWebAnimation;
  const controlNative=globalThis.__webSceneControlWebAnimation;
  const statusNative=globalThis.__webSceneWebAnimationStatus;
  const idsNative=globalThis.__webSceneElementWebAnimationIds;
  delete globalThis.__webSceneCreateWebAnimation;
  delete globalThis.__webSceneControlWebAnimation;
  delete globalThis.__webSceneWebAnimationStatus;
  delete globalThis.__webSceneElementWebAnimationIds;
  const animations=new Map();
  const directions=new Map([['normal',0],['reverse',1],['alternate',2],['alternate-reverse',3]]);
  const fills=new Map([['none',0],['forwards',1],['backwards',2],['both',3]]);
  const easings=new Set(['linear','ease','ease-in','ease-out','ease-in-out']);
  const deferred=()=>{let resolve,reject;const promise=new Promise((yes,no)=>{resolve=yes;reject=no;});return{promise,resolve,reject};};
  const normalizeTiming=options=>{
    if(typeof options==='number')options={duration:options};
    options=options==null?{}:Object(options);
    const timing={duration:Number(options.duration??0),delay:Number(options.delay??0),
      iterations:Number(options.iterations??1),direction:String(options.direction??'normal'),
      fill:String(options.fill??'none'),easing:String(options.easing??'linear')};
    if((options.iterationStart!=null&&Number(options.iterationStart)!==0)
      ||(options.endDelay!=null&&Number(options.endDelay)!==0)
      ||(options.composite!=null&&String(options.composite)!=='replace'))
      throw new TypeError('Unsupported bounded animation timing member');
    if(!Number.isFinite(timing.duration)||timing.duration<0||timing.duration>3600000
      ||!Number.isFinite(timing.delay)||Math.abs(timing.delay)>3600000
      ||(!(Number.isFinite(timing.iterations)||timing.iterations===Infinity))
      ||timing.iterations<0||timing.iterations>1000000||!directions.has(timing.direction)
      ||!fills.has(timing.fill)||!easings.has(timing.easing))
      throw new TypeError('Unsupported bounded animation timing');
    return timing;
  };
  const normalizeKeyframes=input=>{
    if(!Array.isArray(input)||input.length<2||input.length>16)
      throw new TypeError('Keyframes must be an array of 2 to 16 entries');
    const frames=input.map(value=>{const frame=Object(value);
      for(const name of Object.keys(frame))if(!['offset','opacity','transform'].includes(name))
        throw new TypeError(`Unsupported bounded keyframe property: ${name}`);
      const result={
      offset:frame.offset==null?null:Number(frame.offset),
      opacity:frame.opacity==null?null:Number(frame.opacity),
      transform:frame.transform==null?null:String(frame.transform)};
      if(result.offset!==null&&(!Number.isFinite(result.offset)||result.offset<0||result.offset>1)
        ||result.opacity!==null&&!Number.isFinite(result.opacity))
        throw new TypeError('Invalid bounded animation keyframe');return result;});
    const explicit=frames.every(frame=>frame.offset!==null),implicit=frames.every(frame=>frame.offset===null);
    if(!explicit&&!implicit)throw new TypeError('Offsets must be all explicit or all implicit');
    if(implicit)frames.forEach((frame,index)=>{frame.offset=index/(frames.length-1);});
    for(let index=1;index<frames.length;++index)
      if(frames[index].offset<frames[index-1].offset)throw new TypeError('Offsets must be nondecreasing');
    const opacity=frames.every(frame=>frame.opacity!==null),transform=frames.every(frame=>frame.transform!==null);
    if((!opacity&&frames.some(frame=>frame.opacity!==null))
      ||(!transform&&frames.some(frame=>frame.transform!==null))||(!opacity&&!transform))
      throw new TypeError('Each supported property requires every keyframe');
    return frames;
  };
  class KeyframeEffect{
    constructor(target,keyframes,options={}){
      if(!(target instanceof Element))throw new TypeError('KeyframeEffect target must be an Element');
      this.target=target;this._keyframes=normalizeKeyframes(keyframes);
      this._timing=normalizeTiming(options);this._animation=null;
    }
    getKeyframes(){return this._keyframes.map(frame=>({...frame}));}
    getTiming(){return{...this._timing};}
  }
  const nativeCreate=animation=>{const timing=animation.effect._timing;
    const id=createNative(animation.effect.target,animation.effect._keyframes,
      timing.duration,timing.delay,timing.iterations,directions.get(timing.direction),
      fills.get(timing.fill),timing.easing);if(!id)return false;animation._id=id;return true;};
  const renew=animation=>{animation._settled=false;animation._deferred=deferred();
    animation.finished=animation._deferred.promise;};
  class Animation extends EventTarget{
    constructor(effect,animationTimeline=null){super();if(!(effect instanceof KeyframeEffect)||effect._animation)
      throw new TypeError('Animation requires an unused KeyframeEffect');
      if(animationTimeline!==null&&animationTimeline!==document.timeline)
        throw new DOMException('Only the document timeline is supported','NotSupportedError');
      this.effect=effect;effect._animation=this;this.onfinish=null;this.oncancel=null;
      this._id=0;this._settled=false;this._deferred=deferred();
      this.finished=this._deferred.promise;this.ready=Promise.resolve(this);this.timeline=animationTimeline;
    }
    get currentTime(){return statusNative(this._id)?.currentTime??null;}
    set currentTime(value){value=Number(value);if(!Number.isFinite(value))
      throw new TypeError('currentTime must be finite');
      const settled=this._settled;if(!controlNative(this._id,4,value))
        throw new DOMException('Animation is idle','InvalidStateError');
      if(settled)renew(this);}
    get playState(){const state=statusNative(this._id);return!state?'idle':state.finished?'finished':state.paused?'paused':'running';}
    play(){if(this.timeline!==document.timeline)throw new DOMException(
        'A document timeline is required','NotSupportedError');
      if(!statusNative(this._id)){if(this._id&&animations.get(this._id)===this&&!this._settled)
          settle(this._id,'cancel');
        animations.delete(this._id);if(this._settled)renew(this);
        if(!nativeCreate(this))throw new DOMException('Animation capacity unavailable','NotSupportedError');
        animations.set(this._id,this);return this.ready;}
      if(this._settled)renew(this);controlNative(this._id,0);return this.ready;}
    pause(){controlNative(this._id,1);}
    cancel(){if(controlNative(this._id,2))settle(this._id,'cancel');}
    finish(){if(!controlNative(this._id,3))throw new DOMException(
      'Infinite animations cannot finish','InvalidStateError');}
  }
  const settle=(id,kind)=>{const animation=animations.get(id);if(!animation)return;
    if(animation._settled){if(kind==='cancel')animations.delete(id);return;}
    animation._settled=true;const event=new Event(kind);
    if(kind==='finish'){animation._deferred.resolve(animation);animation.dispatchEvent(event);}
    else{animation._deferred.reject(new DOMException('The animation was canceled','AbortError'));
      animation.dispatchEvent(event);animations.delete(id);}};
  Object.defineProperty(globalThis,'__webSceneSettleWebAnimation',{value:settle,configurable:true});
  Object.defineProperty(Element.prototype,'animate',{value(keyframes,options){
    const animation=new Animation(new KeyframeEffect(this,keyframes,options),document.timeline);
    animation.play();return animation;},configurable:true,writable:true});
  Object.defineProperty(Element.prototype,'getAnimations',{value(){return Array.from(
    idsNative(this),id=>animations.get(id)).filter(Boolean);},configurable:true,writable:true});
  Object.defineProperty(Document.prototype,'getAnimations',{value(){return Array.from(
    animations.values()).filter(animation=>animation.playState!=='idle');},configurable:true,writable:true});
  const timeline=Object.freeze({get currentTime(){return performance.now();}});
  Object.defineProperty(Document.prototype,'timeline',{get(){return timeline;},configurable:true});
  Object.defineProperties(globalThis,{Animation:{value:Animation,configurable:true,writable:true},
    KeyframeEffect:{value:KeyframeEffect,configurable:true,writable:true}});
})();
)JS";
}
