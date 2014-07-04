'''
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
'''
import glob
import re

funcs = 0
types = 0;
consts = 0
funcsNow = 0
enums = [];
funcsDict = {};
typesDict = {};
TypesDict = {};
typesList = [];
boxed = {};
funcsText = '';
typesText = '';
dataTexts = '';
inlineMethods = '';
textSerialize = '';
forwards = '';
forwTypedefs = '';
out = open('mtpScheme.h', 'w')
out.write('/*\n');
out.write('Created from \'/SourceFiles/mtproto/scheme.tl\' by \'/SourceFiles/mtproto/generate.py\' script\n\n');
out.write('WARNING! All changes made in this file will be lost!\n\n');
out.write('This file is part of Telegram Desktop,\n');
out.write('an unofficial desktop messaging app, see https://telegram.org\n');
out.write('\n');
out.write('Telegram Desktop is free software: you can redistribute it and/or modify\n');
out.write('it under the terms of the GNU General Public License as published by\n');
out.write('the Free Software Foundation, either version 3 of the License, or\n');
out.write('(at your option) any later version.\n');
out.write('\n');
out.write('It is distributed in the hope that it will be useful,\n');
out.write('but WITHOUT ANY WARRANTY; without even the implied warranty of\n');
out.write('MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the\n');
out.write('GNU General Public License for more details.\n');
out.write('\n');
out.write('Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE\n');
out.write('Copyright (c) 2014 John Preston, https://tdesktop.com\n');
out.write('*/\n');
out.write('#pragma once\n\n#include "mtpCoreTypes.h"\n');
with open('scheme.tl') as f:
  for line in f:
    nocomment = re.match(r'^(.*?)//', line)
    if (nocomment):
      line = nocomment.group(1);
    if (re.match(r'\-\-\-functions\-\-\-', line)):
      funcsNow = 1;
      continue;
    if (re.match(r'\-\-\-types\-\-\-', line)):
      funcsNow = 0;
      continue;
    if (re.match(r'^\s*$', line)):
      continue;

    nametype = re.match(r'([a-zA-Z\.0-9_]+)#([0-9a-f]+)([^=]*)=\s*([a-zA-Z\.<>0-9_]+);', line);
    if (not nametype):
      print('Bad line found: ' + line);
      continue;

    name = nametype.group(1);
    nameInd = name.find('.');
    if (nameInd >= 0):
      Name = name[0:nameInd] + '_' + name[nameInd + 1:nameInd + 2].upper() + name[nameInd + 2:];
      name = name.replace('.', '_');
    else:
      Name = name[0:1].upper() + name[1:];
    typeid = nametype.group(2);
    params = nametype.group(3);
    restype = nametype.group(4);
    if (restype.find('<') >= 0):
      templ = re.match(r'^([vV]ector<)([A-Za-z0-9\._]+)>$', restype);
      if (templ):
        restype = templ.group(1) + 'MTP' + templ.group(2).replace('.', '_') + '>';
      else:
        print('Bad template type: ' + restype);
        continue;
    resType = restype.replace('.', '_');
    if (restype.find('.') >= 0):
      parts = re.match(r'([a-z]+)\.([A-Z][A-Za-z0-9<>\._]+)', restype)
      if (parts):
        restype = parts.group(1) + '_' + parts.group(2)[0:1].lower() + parts.group(2)[1:];
      else:
        print('Bad result type name with dot: ' + restype);
        continue;
    else:
      if (re.match(r'^[A-Z]', restype)):
        restype = restype[:1].lower() + restype[1:];
      else:
        print('Bad result type name: ' + restype);
        continue;

    boxed[resType] = restype;
    boxed[Name] = name;

    enums.append('\tmtpc_' + name + ' = 0x' + typeid);

    paramsList = params.strip().split(' ');
    prms = {};
    prmsList = [];
    isTemplate = '';
    for param in paramsList:
      if (re.match(r'^\s*$', param)):
        continue;
      pnametype = re.match(r'([a-z_][a-z0-9_]*):([A-Za-z0-9<>\._]+)', param);
      if (not pnametype):
        pnametypeX = re.match(r'([a-z_][a-z0-9_]*):!X', param);
        if (not pnametypeX or isTemplate != ''):
          print('Bad param found: "' + param + '" in line: ' + line);
          continue;
        else:
          pname = isTemplate = pnametypeX.group(1);
          ptype = 'TQueryType';
      else:
        pname = pnametype.group(1);
        ptype = pnametype.group(2);
      if (ptype.find('<') >= 0):
        templ = re.match(r'^([vV]ector<)([A-Za-z0-9\._]+)>$', ptype);
        if (templ):
          ptype = templ.group(1) + 'MTP' + templ.group(2).replace('.', '_') + '>';
        else:
          print('Bad template type: ' + ptype);
          continue;
      prmsList.append(pname);
      prms[pname] = ptype.replace('.', '_');

    if (isTemplate == '' and resType == 'X'):
      print('Bad response type "X" in "' + name +'" in line: ' + line);
      continue;

    if funcsNow:
      if (isTemplate != ''):
        funcsText += '\ntemplate <class TQueryType>';
      funcsText += '\nclass MTP' + name + ' { // RPC method \'' + nametype.group(1) + '\'\n'; # class

      funcsText += 'public:\n';

      prmsStr = [];
      prmsInit = [];
      prmsNames = [];
      if (len(prms)):
        for paramName in prmsList:
          paramType = prms[paramName];
          prmsInit.append('v' + paramName + '(_' + paramName + ')');
          prmsNames.append('_' + paramName);
          if (paramName == isTemplate):
            ptypeFull = paramType;
          else:
            ptypeFull = 'MTP' + paramType;
          funcsText += '\t' + ptypeFull + ' v' + paramName + ';\n';
          if (paramType in ['int', 'Int', 'bool', 'Bool']):
            prmsStr.append(ptypeFull + ' _' + paramName);
          else:
            prmsStr.append('const ' + ptypeFull + ' &_' + paramName);
        funcsText += '\n';

      funcsText += '\tMTP' + name + '() {\n\t}\n'; # constructor
      funcsText += '\tMTP' + name + '(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_' + name + ') {\n\t\tread(from, end, cons);\n\t}\n'; # stream constructor
      if (len(prms)):
        funcsText += '\tMTP' + name + '(' + ', '.join(prmsStr) + ') : ' + ', '.join(prmsInit) + ' {\n\t}\n';
      funcsText += '\n';

      funcsText += '\tuint32 size() const {\n'; # count size
      size = [];
      for k in prmsList:
        v = prms[k];
        size.append('v' + k + '.size()');
      if (not len(size)):
        size.append('0');
      funcsText += '\t\treturn ' + ' + '.join(size) + ';\n';
      funcsText += '\t}\n';

      funcsText += '\tmtpTypeId type() const {\n\t\treturn mtpc_' + name + ';\n\t}\n'; # type id

      funcsText += '\tvoid read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_' + name + ') {\n'; # read method
      for k in prmsList:
        v = prms[k];
        funcsText += '\t\tv' + k + '.read(from, end);\n';
      funcsText += '\t}\n';

      funcsText += '\tvoid write(mtpBuffer &to) const {\n'; # write method
      for k in prmsList:
        v = prms[k];
        funcsText += '\t\tv' + k + '.write(to);\n';
      funcsText += '\t}\n';

      if (isTemplate != ''):
        funcsText += '\n\ttypedef typename TQueryType::ResponseType ResponseType;\n';
      else:
        funcsText += '\n\ttypedef MTP' + resType + ' ResponseType;\n'; # method return type

      funcsText += '};\n'; # class ending
      if (isTemplate != ''):
        funcsText += 'template <typename TQueryType>\n';
        funcsText += 'class MTP' + Name + ' : public MTPBoxed<MTP' + name + '<TQueryType> > {\n';
        funcsText += 'public:\n';
        funcsText += '\tMTP' + Name + '() {\n\t}\n';
        funcsText += '\tMTP' + Name + '(const MTP' + name + '<TQueryType> &v) : MTPBoxed<MTP' + name + '<TQueryType> >(v) {\n\t}\n';
        if (len(prms)):
          funcsText += '\tMTP' + Name + '(' + ', '.join(prmsStr) + ') : MTPBoxed<MTP' + name + '<TQueryType> >(MTP' + name + '<TQueryType>(' + ', '.join(prmsNames) + ')) {\n\t}\n';
        funcsText += '};\n';
      else:
        funcsText += 'class MTP' + Name + ' : public MTPBoxed<MTP' + name + '> {\n';
        funcsText += 'public:\n';
        funcsText += '\tMTP' + Name + '() {\n\t}\n';
        funcsText += '\tMTP' + Name + '(const MTP' + name + ' &v) : MTPBoxed<MTP' + name + '>(v) {\n\t}\n';
        funcsText += '\tMTP' + Name + '(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = 0) : MTPBoxed<MTP' + name + '>(from, end, cons) {\n\t}\n';
        if (len(prms)):
          funcsText += '\tMTP' + Name + '(' + ', '.join(prmsStr) + ') : MTPBoxed<MTP' + name + '>(MTP' + name + '(' + ', '.join(prmsNames) + ')) {\n\t}\n';
        funcsText += '};\n';
      funcs = funcs + 1;

      if (not restype in funcsDict):
        funcsDict[restype] = [];
#        TypesDict[restype] = resType;
      funcsDict[restype].append([name, typeid, prmsList, prms]);
    else:
      if (isTemplate != ''):
        print('Template types not allowed: "' + resType + '" in line: ' + line);
        continue;
      if (not restype in typesDict):
        typesList.append(restype);
        typesDict[restype] = [];
      TypesDict[restype] = resType;
      typesDict[restype].append([name, typeid, prmsList, prms]);

      consts = consts + 1;

# text serialization: types and funcs
def addTextSerialize(dct):
  result = '';
  for restype in dct:
    v = dct[restype];
    for data in v:
      name = data[0];
      prmsList = data[2];
      prms = data[3];

      if len(result):
        result += '\n';
      result += '\t\tcase mtpc_' + name + ':\n';
      if (len(prms)):
        result += '\t\t\tresult += "\\n" + add;\n';
        for k in prmsList:
          v = prms[k];
          result += '\t\t\tresult += "  ' + k + ': " + mtpTextSerialize(from, end';
          vtypeget = re.match(r'^[Vv]ector<MTP([A-Za-z0-9\._]+)>', v);
          if (vtypeget):
            if (not re.match(r'^[A-Z]', v)):
              result += ', mtpc_vector';
            else:
              result += ', 0';
            result += ', level + 1';

            restype = vtypeget.group(1);
            try:
              if boxed[restype]:
                restype = 0;
            except KeyError:
              if re.match(r'^[A-Z]', restype):
                restype = 0;
          else:
            restype = v;
            try:
              if boxed[restype]:
                restype = 0;
            except KeyError:
              if re.match(r'^[A-Z]', restype):
                restype = 0;
          if (restype):
            try:
              conses = typesDict[restype];
              if (len(conses) > 1):
                print('Complex bare type found: "' + restype + '" trying to serialize "' + k + '" of type "' + v + '"');
                continue;
              result += ', mtpc_' + conses[0][0];
            except KeyError:
              result += ', mtpc_' + restype;
            if (not vtypeget):
              result += ', level + 1';
          else:
            if (not vtypeget):
              result += ', 0, level + 1';
          result += ') + ",\\n" + add;\n';
      else:
        result += '\t\t\tresult = " ";\n';
      result += '\t\treturn "{ ' + name + '" + result + "}";\n';
  return result;

textSerialize += addTextSerialize(typesDict) + '\n';
textSerialize += addTextSerialize(funcsDict);

for restype in typesList:
  v = typesDict[restype];
  resType = TypesDict[restype];
  withData = 0;
  creatorsText = '';
  constructsText = '';
  constructsInline = '';

  forwards += 'class MTP' + restype + ';\n';
  forwTypedefs += 'typedef MTPBoxed<MTP' + restype + '> MTP' + resType + ';\n';

  withType = (len(v) > 1);
  switchLines = '';
  friendDecl = '';
  getters = '';
  reader = '';
  writer = '';
  sizeList = [];
  sizeFast = '';
  newFast = '';
  sizeCases = '';
  for data in v:
    name = data[0];
    typeid = data[1];
    prmsList = data[2];
    prms = data[3];

    dataText = '';
    dataText += '\nclass MTPD' + name + ' : public mtpDataImpl<MTPD' + name + '> {\n'; # data class
    dataText += 'public:\n';

    sizeList = [];
    creatorParams = [];
    creatorParamsList = [];
    readText = '';
    writeText = '';
    dataText += '\tMTPD' + name + '() {\n\t}\n'; # default constructor
    switchLines += '\t\tcase mtpc_' + name + ': '; # for by-type-id type constructor
    if (len(prms)):
      switchLines += 'setData(new MTPD' + name + '()); ';
      withData = 1;

      getters += '\n\tMTPD' + name + ' &_' + name + '() {\n'; # splitting getter
      getters += '\t\tif (!data) throw mtpErrorUninitialized();\n';
      if (withType):
        getters += '\t\tif (_type != mtpc_' + name + ') throw mtpErrorWrongTypeId(_type, mtpc_' + name + ');\n';
      getters += '\t\tsplit();\n';
      getters += '\t\treturn *(MTPD' + name + '*)data;\n';
      getters += '\t}\n';

      getters += '\tconst MTPD' + name + ' &c_' + name + '() const {\n'; # const getter
      getters += '\t\tif (!data) throw mtpErrorUninitialized();\n';
      if (withType):
        getters += '\t\tif (_type != mtpc_' + name + ') throw mtpErrorWrongTypeId(_type, mtpc_' + name + ');\n';
      getters += '\t\treturn *(const MTPD' + name + '*)data;\n';
      getters += '\t}\n';

      constructsText += '\texplicit MTP' + restype + '(MTPD' + name + ' *_data);\n'; # by-data type constructor
      constructsInline += 'inline MTP' + restype + '::MTP' + restype + '(MTPD' + name + ' *_data) : mtpDataOwner(_data)';
      if (withType):
        constructsInline += ', _type(mtpc_' + name + ')';
      constructsInline += ' {\n}\n';

      dataText += '\tMTPD' + name + '('; # params constructor
      prmsStr = [];
      prmsInit = [];
      for paramName in prmsList:
        paramType = prms[paramName];

        if (paramType in ['int', 'Int', 'bool', 'Bool']):
          prmsStr.append('MTP' + paramType + ' _' + paramName);
          creatorParams.append('MTP' + paramType + ' _' + paramName);
        else:
          prmsStr.append('const MTP' + paramType + ' &_' + paramName);
          creatorParams.append('const MTP' + paramType + ' &_' + paramName);
        creatorParamsList.append('_' + paramName);
        prmsInit.append('v' + paramName + '(_' + paramName + ')');
        if (withType):
          readText += '\t\t';
          writeText += '\t\t';
        readText += '\tv.v' + paramName + '.read(from, end);\n';
        writeText += '\tv.v' + paramName + '.write(to);\n';
        sizeList.append('v.v' + paramName + '.size()');

      forwards += 'class MTPD' + name + ';\n'; # data class forward declaration

      dataText += ', '.join(prmsStr) + ') : ' + ', '.join(prmsInit) + ' {\n\t}\n';

      dataText += '\n';
      for paramName in prmsList: # fields declaration
        paramType = prms[paramName];
        dataText += '\tMTP' + paramType + ' v' + paramName + ';\n';
      sizeCases += '\t\tcase mtpc_' + name + ': {\n';
      sizeCases += '\t\t\tconst MTPD' + name + ' &v(c_' + name + '());\n';
      sizeCases += '\t\t\treturn ' + ' + '.join(sizeList) + ';\n';
      sizeCases += '\t\t}\n';
      sizeFast = '\tconst MTPD' + name + ' &v(c_' + name + '());\n\treturn ' + ' + '.join(sizeList) + ';\n';
      newFast = 'new MTPD' + name + '()';
    else:
      sizeFast = '\treturn 0;\n';

    switchLines += 'break;\n';
    dataText += '};\n'; # class ending

    if (len(prms)):
      dataTexts += dataText; # add data class

    friendDecl += '\tfriend MTP' + restype + ' MTP_' + name + '(' + ', '.join(creatorParams) + ');\n';
    creatorsText += 'inline MTP' + restype + ' MTP_' + name + '(' + ', '.join(creatorParams) + ') {\n';
    if (len(prms)): # creator with params
      creatorsText += '\treturn MTP' + restype + '(new MTPD' + name + '(' + ', '.join(creatorParamsList) + '));\n';
    else:
      if (withType): # creator by type
        creatorsText += '\treturn MTP' + restype + '(mtpc_' + name + ');\n';
      else: # single creator
        creatorsText += '\treturn MTP' + restype + '();\n';
    creatorsText += '}\n';

    if (withType):
      reader += '\t\tcase mtpc_' + name + ': _type = cons; '; # read switch line
      if (len(prms)):
        reader += '{\n';
        reader += '\t\t\tif (!data) setData(new MTPD' + name + '());\n';
        reader += '\t\t\tMTPD' + name + ' &v(_' + name + '());\n';
        reader += readText;
        reader += '\t\t} break;\n';

        writer += '\t\tcase mtpc_' + name + ': {\n'; # write switch line
        writer += '\t\t\tconst MTPD' + name + ' &v(c_' + name + '());\n';
        writer += writeText;
        writer += '\t\t} break;\n';
      else:
        reader += 'break;\n';
    else:
      if (len(prms)):
        reader += '\n\tif (!data) setData(new MTPD' + name + '());\n';
        reader += '\tMTPD' + name + ' &v(_' + name + '());\n';
        reader += readText;

        writer += '\tconst MTPD' + name + ' &v(c_' + name + '());\n';
        writer += writeText;

  forwards += '\n';

  typesText += '\nclass MTP' + restype; # type class declaration
  if (withData):
    typesText += ' : private mtpDataOwner'; # if has data fields
  typesText += ' {\n';
  typesText += 'public:\n';
  typesText += '\tMTP' + restype + '()'; # default constructor
  inits = [];
  if (withType):
    if (withData):
      inits.append('mtpDataOwner(0)');
    inits.append('_type(0)');
  else:
    if (withData):
      inits.append('mtpDataOwner(' + newFast + ')');
  if (withData and not withType):
    typesText += ';\n';
    inlineMethods += '\ninline MTP' + restype + '::MTP' + restype + '()';
    if (inits):
      inlineMethods += ' : ' + ', '.join(inits);
    inlineMethods += ' {\n}\n';
  else:
    if (inits):
      typesText += ' : ' + ', '.join(inits);
    typesText += ' {\n\t}\n';

  inits = [];
  if (withData):
    inits.append('mtpDataOwner(0)');
  if (withType):
    inits.append('_type(0)');
  typesText += '\tMTP' + restype + '(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons';
  if (not withType):
    typesText += ' = mtpc_' + name;
  typesText += ')'; # read constructor
  if (inits):
    typesText += ' : ' + ', '.join(inits);
  typesText += ' {\n\t\tread(from, end, cons);\n\t}\n';

  if (withData):
    typesText += getters;

  typesText += '\n\tuint32 size() const;\n'; # size method
  inlineMethods += '\ninline uint32 MTP' + restype + '::size() const {\n';
  if (withType and sizeCases):
    inlineMethods += '\tswitch (_type) {\n';
    inlineMethods += sizeCases;
    inlineMethods += '\t}\n';
    inlineMethods += '\treturn 0;\n';
  else:
    inlineMethods += sizeFast;
  inlineMethods += '}\n';

  typesText += '\tmtpTypeId type() const;\n'; # type id method
  inlineMethods += 'inline mtpTypeId MTP' + restype + '::type() const {\n';
  if (withType):
    inlineMethods += '\tif (!_type) throw mtpErrorUninitialized();\n';
    inlineMethods += '\treturn _type;\n';
  else:
    inlineMethods += '\treturn mtpc_' + v[0][0] + ';\n';
  inlineMethods += '}\n';

  typesText += '\tvoid read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons'; # read method
  if (not withType):
    typesText += ' = mtpc_' + name;
  typesText += ');\n';
  inlineMethods += 'inline void MTP' + restype + '::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {\n';
  if (withData):
    if (withType):
      inlineMethods += '\tif (cons != _type) setData(0);\n';
    else:
      inlineMethods += '\tif (cons != mtpc_' + v[0][0] + ') throw mtpErrorUnexpected(cons, "MTP' + restype + '");\n';
  if (withType):
    inlineMethods += '\tswitch (cons) {\n'
    inlineMethods += reader;
    inlineMethods += '\t\tdefault: throw mtpErrorUnexpected(cons, "MTP' + restype + '");\n';
    inlineMethods += '\t}\n';
  else:
    inlineMethods += reader;
  inlineMethods += '}\n';

  typesText += '\tvoid write(mtpBuffer &to) const;\n'; # write method
  inlineMethods += 'inline void MTP' + restype + '::write(mtpBuffer &to) const {\n';
  if (withType):
    inlineMethods += '\tswitch (_type) {\n';
    inlineMethods += writer;
    inlineMethods += '\t}\n';
  else:
    inlineMethods += writer;
  inlineMethods += '}\n';

  typesText += '\n\ttypedef void ResponseType;\n'; # no response types declared

  typesText += '\nprivate:\n'; # private constructors
  if (withType): # by-type-id constructor
    typesText += '\texplicit MTP' + restype + '(mtpTypeId type);\n';
    inlineMethods += 'inline MTP' + restype + '::MTP' + restype + '(mtpTypeId type) : ';
    if (withData):
      inlineMethods += 'mtpDataOwner(0), ';
    inlineMethods += '_type(type)';
    inlineMethods += ' {\n';
    inlineMethods += '\tswitch (type) {\n'; # type id check
    inlineMethods += switchLines;
    inlineMethods += '\t\tdefault: throw mtpErrorBadTypeId(type, "MTP' + restype + '");\n\t}\n';
    inlineMethods += '}\n'; # by-type-id constructor end

  if (withData):
    typesText += constructsText;
    inlineMethods += constructsInline;

  if (friendDecl):
    typesText += '\n' + friendDecl;

  if (withType):
    typesText += '\n\tmtpTypeId _type;\n'; # type field var

  typesText += '};\n'; # type class ended

  inlineMethods += creatorsText;
  typesText += 'typedef MTPBoxed<MTP' + restype + '> MTP' + resType + ';\n'; # boxed type definition

textSerializeFull = '\ninline QString mtpTextSerialize(const mtpPrime *&from, const mtpPrime *end, mtpPrime cons, uint32 level, mtpPrime vcons) {\n';
textSerializeFull += '\tQString add = QString(" ").repeated(level * 2);\n\n';
textSerializeFull += '\tconst mtpPrime *start = from;\n';
textSerializeFull += '\ttry {\n';
textSerializeFull += '\t\tif (!cons) {\n';
textSerializeFull += '\t\t\tif (from >= end) {\n';
textSerializeFull += '\t\t\t\tthrow Exception("from >= 2");\n';
textSerializeFull += '\t\t\t}\n';
textSerializeFull += '\t\t\tcons = *from;\n';
textSerializeFull += '\t\t\t++from;\n';
textSerializeFull += '\t\t\t++start;\n';
textSerializeFull += '\t\t}\n\n';
textSerializeFull += '\t\tQString result;\n';
textSerializeFull += '\t\tswitch (mtpTypeId(cons)) {\n' + textSerialize + '\t\t}\n\n';
textSerializeFull += '\t\treturn mtpTextSerializeCore(from, end, cons, level, vcons);\n';
textSerializeFull += '\t} catch (Exception &e) {\n';
textSerializeFull += '\t\tQString result = "(" + QString(e.what()) + QString("), cons: %1").arg(cons);\n';
textSerializeFull += '\t\tif (vcons) result += QString(", vcons: %1").arg(vcons);\n';
textSerializeFull += '\t\tresult += ", " + mb(start, (end - start) * sizeof(mtpPrime)).str();\n';
textSerializeFull += '\t\treturn "[ERROR] " + result;\n';
textSerializeFull += '\t}\n';
textSerializeFull += '}\n';

out.write('\n// Type id constants\nenum {\n' + ',\n'.join(enums) + '\n};\n');
out.write('\n// Type forward declarations\n' + forwards);
out.write('\n// Boxed types definitions\n' + forwTypedefs);
out.write('\n// Type classes definitions\n' + typesText);
out.write('\n// Type constructors with data\n' + dataTexts);
out.write('\n// RPC methods\n' + funcsText);
out.write('\n// Inline methods definition\n' + inlineMethods);
out.write('\n// Human-readable text serialization\n#if (defined _DEBUG || defined _WITH_DEBUG)\n' + textSerializeFull + '\n#endif\n');

print('Done, written {0} constructors, {1} functions.'.format(consts, funcs));
