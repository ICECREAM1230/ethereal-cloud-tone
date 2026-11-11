import {readFile,realpath} from 'node:fs/promises';
import {resolve,dirname,relative,isAbsolute,sep} from 'node:path';
import {Script} from 'node:vm';
import {spawnSync} from 'node:child_process';

// Project subset: static named relative imports and named const/class/function
// declarations. Imports are hoisted; exported function/class bindings must not
// be rebound. Matching write targets are rejected conservatively, including
// same-name shadowed locals. Bare eval, bare arguments and escaped identifiers
// are unsupported. This is not a general ESM live-binding compiler.
const lineBreak=/[\r\n\u2028\u2029]/;
const assignment=/^(?:=|\+=|-=|\*=|\/=|%=|\*\*=|<<=|>>=|>>>=|&=|\|=|\^=|&&=|\|\|=|\?\?=)$/;

function tokensOf(source) {
  const tokens=[];const parentheses=[];let cursor=0;
  const add=(type,value,start,end=cursor)=>tokens.push({type,value,start,end,index:tokens.length});
  function quoted(quote) {
    const start=cursor++;
    while(cursor<source.length) {
      if(source[cursor]==='\\'){cursor+=2;continue;}
      if(source[cursor++]===quote) {add('string',source.slice(start+1,cursor-1),start);return;}
    }
    throw Error('unterminated string');
  }
  function template() {
    let start=cursor++;
    while(cursor<source.length) {
      if(source[cursor]==='\\'){cursor+=2;continue;}
      if(source[cursor]==='`') {cursor++;add('literal','template',start);return;}
      if(source[cursor]==='$'&&source[cursor+1]==='{') {
        add('literal','template',start,cursor);
        const interpolation=cursor;cursor+=2;add('punct','{',interpolation);
        scan(true);start=cursor;continue;
      }
      cursor++;
    }
    throw Error('unterminated template');
  }
  function regex() {
    const start=cursor++;let characterClass=false;
    while(cursor<source.length) {
      const char=source[cursor++];
      if(char==='\\'){cursor++;continue;}
      if(char==='[')characterClass=true;
      if(char===']')characterClass=false;
      if(char==='/'&&!characterClass) {
        while(/[a-z]/i.test(source[cursor]||'')&&cursor<source.length)cursor++;
        add('literal','regex',start);return;
      }
    }
    throw Error('unterminated regular expression');
  }
  function scan(interpolation=false) {
    let braces=0;
    while(cursor<source.length) {
      const char=source[cursor];
      if(/\s/.test(char)){cursor++;continue;}
      if(source.startsWith('//',cursor)) {
        const rest=source.slice(cursor+2);const end=rest.search(lineBreak);
        cursor=end<0?source.length:cursor+2+end;continue;
      }
      if(source.startsWith('/*',cursor)) {
        const end=source.indexOf('*/',cursor+2);
        if(end<0)throw Error('unterminated comment');cursor=end+2;continue;
      }
      if(char==='"'||char==="'"){quoted(char);continue;}
      if(char==='`'){template();continue;}
      const previous=tokens.at(-1);
      const expressionStart=!previous||previous.controlEnd
        ||(previous.type==='punct'&&/^(?:[=(:,;!\[{}?]|=>|&&|\|\||\?\?)$/.test(previous.value))
        ||(previous.type==='identifier'&&/^(?:return|throw|case|typeof|void|delete|yield|await|else|do)$/.test(previous.value));
      if(char==='/'&&expressionStart) {
        regex();continue;
      }
      const start=cursor;
      if(char==='\\')throw Error('unsupported escaped identifier syntax');
      const identifier=source.slice(cursor).match(/^[$A-Z_a-z][$\w]*/);
      if(identifier){cursor+=identifier[0].length;add('identifier',identifier[0],start);continue;}
      const number=source.slice(cursor).match(/^(?:0[xX][\da-fA-F]+|(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)(?:n)?/);
      if(number){cursor+=number[0].length;add('literal','number',start);continue;}
      const operator=source.slice(cursor).match(/^(?:\.\.\.|>>>=|\*\*=|<<=|>>=|&&=|\|\|=|\?\?=|===|!==|>>>|=>|\?\.|\?\?|&&|\|\||==|!=|>=|<=|\+\+|--|\*\*|<<|>>|[+\-*/%&|^]=)/);
      const value=operator?.[0]||char;cursor+=value.length;add('punct',value,start);
      if(value==='(') {
        const before=tokens.at(-3)?.value;
        const control=previous?.type==='identifier'&&!['.','?.'].includes(before)&&/^(?:if|while|for|with|switch|catch)$/.test(previous.value)?previous.value
          :previous?.value==='await'&&before==='for'?'for':null;
        const functionParameters=(previous?.value==='function'&&!['.','?.'].includes(before))
          ||before==='function'||(before==='*'&&tokens.at(-4)?.value==='function');
        tokens.at(-1).control=control;parentheses.push({control,functionParameters});
      }else if(value===')') {
        const context=parentheses.pop();tokens.at(-1).controlEnd=!!context?.control;
        tokens.at(-1).functionParametersEnd=!!context?.functionParameters;
      }
      if(value==='}') {
        if(interpolation&&braces===0)return;
        braces--;
      }else if(value==='{')braces++;
    }
    if(interpolation)throw Error('unterminated template interpolation');
  }
  scan();return tokens;
}

function expressionEnds(token) {
  return token&&((token.type==='identifier'&&!/^(?:new|typeof|void|delete|await|yield|in|instanceof)$/.test(token.value))
    ||token.type==='string'||token.type==='literal'||(token.type==='punct'&&[')',']','}','++','--'].includes(token.value)));
}
function continuesExpression(token) {
  return (token.type==='identifier'&&['in','instanceof'].includes(token.value))
    ||(token.type==='literal'&&token.value==='template')
    ||(token.type==='punct'&&/^(?:[([.,?:+\-*/%<>&|^=]|\?\.|\?\?|&&|\|\||==|!=|===|!==|>=|<=|\*\*|<<|>>|>>>|=>)$/.test(token.value));
}

function rejectRebinding(tokens,names,id) {
  const unsupported=()=>{throw Error(`unsupported exported binding rebinding: ${id}`);};
  const opening=[];const pairs=[];
  for(let index=0;index<tokens.length;index++) {
    const token=tokens[index];if(token.type!=='punct')continue;
    if(['(','[','{'].includes(token.value))opening.push(index);
    else if([')',']','}'].includes(token.value))pairs.push([opening.pop(),index]);
  }
  const matchingName=token=>token.type==='identifier'&&names.has(token.value)
    &&!['.','?.'].includes(tokens[token.index-1]?.value)&&tokens[token.index+1]?.value!==':';
  for(let index=0;index<tokens.length;index++) {
    const token=tokens[index];if(!matchingName(token))continue;
    const previous=tokens[index-1];const next=tokens[index+1];
    if(previous?.type==='punct'&&['.','?.'].includes(previous.value))continue;
    if((next?.type==='punct'&&(assignment.test(next.value)||['++','--'].includes(next.value)))
      ||(previous?.type==='punct'&&['++','--'].includes(previous.value)))unsupported();
  }
  for(const [start,end] of pairs) {
    const before=tokens[start];const after=tokens[end+1];
    const update=after?.type==='punct'&&(assignment.test(after.value)||['++','--'].includes(after.value))
      ||tokens[start-1]?.type==='punct'&&['++','--'].includes(tokens[start-1].value);
    const member=before.value==='['&&expressionEnds(tokens[start-1]);
    if(['(','[','{'].includes(before.value)&&update&&!member) {
      if(tokens.slice(start+1,end).some(matchingName))unsupported();
    }
    if(before.control==='for') {
      let depth=0;
      for(let index=start+1;index<end;index++) {
        const token=tokens[index];
        if(depth===0&&token.type==='punct'&&token.value===';')break;
        if(depth===0&&token.type==='identifier'&&['of','in'].includes(token.value)) {
          if(tokens.slice(start+1,index).some(matchingName))unsupported();break;
        }
        if(token.type==='punct') {
          if(['(','[','{'].includes(token.value))depth++;
          else if([')',']','}'].includes(token.value))depth--;
        }
      }
    }
  }
}

function parseModule(source,id) {
  // Node parses the original bytes as ESM without evaluating or resolving them.
  // This prevents wrappers from legalizing invalid source (e.g. top-level return).
  const env={...process.env};for(const key of ['NODE_OPTIONS','NODE_V8_COVERAGE','NODE_REDIRECT_WARNINGS'])delete env[key];
  const checked=spawnSync(process.execPath,['--check','--input-type=module'],{input:source,encoding:'utf8',env,timeout:10000,maxBuffer:1024*1024});
  if(checked.error)throw checked.error;
  if(checked.status!==0)throw Error(`invalid module syntax: ${id}\n${checked.stderr}`);
  const tokens=tokensOf(source);const imports=[];const exports=[];const mutableNames=new Set();const edits=[];let depth=0;
  const unsupported=kind=>{throw Error(`unsupported ${kind} syntax: ${id}`);};
  for(let index=0;index<tokens.length;index++) {
    const token=tokens[index];const previous=tokens[index-1];
    const property=previous?.type==='punct'&&['.','?.'].includes(previous.value);
    if(token.type==='identifier'&&!property) {
      if(token.value==='eval')unsupported('bare eval');
      if(token.value==='arguments')unsupported('bare arguments');
    }
    if(token.type==='identifier'&&token.value==='import'&&!property) {
      if(depth!==0||tokens[index+1]?.type!=='punct'||tokens[index+1]?.value!=='{')unsupported('import');
      const bindings=[];let next=index+2;
      while(tokens[next]?.value!=='}') {
        if(tokens[next]?.type!=='identifier')unsupported('import');
        const imported=tokens[next++].value;let local=imported;
        if(tokens[next]?.value==='as') {
          next++;if(tokens[next]?.type!=='identifier')unsupported('import');local=tokens[next++].value;
        }
        bindings.push({imported,local});
        if(tokens[next]?.value===','){next++;continue;}
        if(tokens[next]?.value!=='}')unsupported('import');
      }
      if(tokens[++next]?.value!=='from'||tokens[++next]?.type!=='string')unsupported('import');
      const specifier=tokens[next].value;
      if(/[\\\r\n]/.test(specifier))unsupported('import');
      let end=tokens[next].end;
      if(tokens[next+1]?.value===';')end=tokens[++next].end;
      else if(tokens[next+1]&&!/[\r\n]/.test(source.slice(end,tokens[next+1].start)))unsupported('import');
      if(['with','assert'].includes(tokens[next+1]?.value))unsupported('import');
      imports.push({start:token.start,end,specifier,bindings});index=next;continue;
    }
    if(token.type==='identifier'&&token.value==='export'&&!property) {
      const kind=tokens[index+1]?.value;let name=index+2;
      // Restored AE350 UI sources contain minified identity export lists.
      // Keep the offline bundle's immutable, same-name binding restriction.
      if(depth===0&&kind==='{') {
        while(tokens[name]?.value!=='}') {
          if(tokens[name]?.type!=='identifier')unsupported('export');
          const local=tokens[name++].value;
          if(tokens[name]?.value==='as') {
            name++;if(tokens[name]?.type!=='identifier'||tokens[name++].value!==local)unsupported('export alias');
          }
          exports.push(local);mutableNames.add(local);
          if(tokens[name]?.value===','){name++;continue;}
          if(tokens[name]?.value!=='}')unsupported('export');
        }
        let end=tokens[name].end;
        if(tokens[name+1]?.value===';')end=tokens[++name].end;
        else if(tokens[name+1]&&!/[\r\n]/.test(source.slice(end,tokens[name+1].start)))unsupported('export');
        edits.push({start:token.start,end,text:''});index=name;continue;
      }
      if(depth!==0||!['const','class','function'].includes(kind))unsupported('export');
      if(kind==='function'&&tokens[name]?.value==='*')name++;
      if(tokens[name]?.type!=='identifier')unsupported('export');
      exports.push(tokens[name].value);edits.push({start:token.start,end:token.end,text:''});
      if(kind!=='const')mutableNames.add(tokens[name].value);
      if(kind==='const') {
        if(tokens[name+1]?.value!=='=')unsupported('export');
        let nested=0;let pendingClasses=0;
        for(let next=name+2;next<tokens.length;next++) {
          const value=tokens[next].value;
          const functionBody=value==='{'&&tokens[next-1]?.functionParametersEnd;
          if(nested===0&&!pendingClasses&&!functionBody&&expressionEnds(tokens[next-1])&&lineBreak.test(source.slice(tokens[next-1].end,tokens[next].start))&&!continuesExpression(tokens[next]))break;
          if(nested===0&&tokens[next].type==='identifier'&&value==='class')pendingClasses++;
          if(nested===0&&tokens[next].type==='identifier'&&value==='export')break;
          if(tokens[next].type!=='punct')continue;
          if(nested===0&&value===';')break;
          if(nested===0&&value===',') {
            if(tokens[next+1]?.type!=='identifier'||tokens[next+2]?.value!=='=')unsupported('export');
            exports.push(tokens[next+1].value);next+=2;continue;
          }
          if(nested===0&&value==='{'&&pendingClasses&&!functionBody)pendingClasses--;
          if(['(','[','{'].includes(value))nested++;
          else if([')',']','}'].includes(value))nested--;
        }
      }
    }
    if(token.type==='punct') {
      if(['(','[','{'].includes(token.value))depth++;
      else if([')',']','}'].includes(token.value))depth--;
    }
  }
  rejectRebinding(tokens,mutableNames,id);
  return {source,imports,exports:new Set(exports),edits,identifiers:new Set(tokens.filter(token=>token.type==='identifier').map(token=>token.value))};
}

function inside(root,path) {
  const offset=relative(root,path);
  if(offset==='..'||offset.startsWith('..'+sep)||isAbsolute(offset))throw Error(`source path escape: ${path}`);
}

/** Return an offline classic script; read sources without writing files. */
export async function bundleModules(sourceRoot,entry='app.mjs') {
  const root=await realpath(resolve(sourceRoot));const states=new Map();const modules=[];const stack=[];
  const idOf=path=>relative(root,path).split(sep).join('/');
  async function canonical(path) {
    inside(root,path);const actual=await realpath(path);inside(root,actual);return actual;
  }
  async function visit(path) {
    if(states.get(path)?.status==='done')return states.get(path).module;
    if(states.get(path)?.status==='visiting')throw Error(`dependency cycle: ${[...stack,path].map(idOf).join(' -> ')}`);
    const id=idOf(path);const module=parseModule(await readFile(path,'utf8'),id);
    states.set(path,{status:'visiting',module});stack.push(path);
    for(const request of module.imports) {
      if(!/^\.\.?\//.test(request.specifier)||/[?#]/.test(request.specifier))throw Error(`unsupported external import (relative source path required): ${id} -> ${request.specifier}`);
      const dependency=await canonical(resolve(dirname(path),request.specifier));
      const imported=await visit(dependency);
      for(const binding of request.bindings)if(!imported.exports.has(binding.imported))throw Error(`missing export ${binding.imported}: ${id} -> ${idOf(dependency)}`);
      request.dependency=idOf(dependency);
      module.edits.push({...request,text:''});
    }
    modules.push({id,module});
    stack.pop();states.set(path,{status:'done',module});return module;
  }
  await visit(await canonical(resolve(root,entry)));
  const identifiers=new Set(modules.flatMap(({module})=>[...module.identifiers]));
  let registry='__bundleModules';let suffix=0;
  while(identifiers.has(registry))registry='__bundleModules_'+(++suffix);
  const bundled=modules.map(({id,module})=>{
    let source=module.source;
    for(const edit of module.edits.sort((a,b)=>b.start-a.start))source=source.slice(0,edit.start)+edit.text+source.slice(edit.end);
    const imports=module.imports.map(request=>{
      const bindings=request.bindings.map(({imported,local})=>`${JSON.stringify(imported)}:${local}`).join(',');
      return `const {${bindings}} = ${registry}[${JSON.stringify(request.dependency)}];`;
    }).join('\n');
    // Plain strict invocation supplies undefined this, also to lexical arrows.
    return `${registry}[${JSON.stringify(id)}] = (function () {\n${imports}\n${source}\nreturn {${[...module.exports].join(',')}};\n})();`;
  });
  const script=`'use strict';\n(() => {\nconst ${registry} = Object.create(null);\n${bundled.join('\n')}\n})();`;
  // Compile before the caller publishes an artifact; never execute build inputs.
  new Script(script,{filename:'offline-module-bundle'});
  return script;
}
