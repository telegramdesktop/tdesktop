'''
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
'''
import glob
import re
import binascii

# define some checked flag conversions
# the key flag type should be a subset of the value flag type
# with exact the same names, then the key flag can be implicitly
# casted to the value flag type
parentFlags = {};
parentFlagsList = [];
def addChildParentFlags(child, parent):
  parentFlagsList.append(child);
  parentFlags[child] = parent;
addChildParentFlags('MTPDmessageService', 'MTPDmessage');
addChildParentFlags('MTPDupdateShortMessage', 'MTPDmessage');
addChildParentFlags('MTPDupdateShortChatMessage', 'MTPDmessage');
addChildParentFlags('MTPDupdateShortSentMessage', 'MTPDmessage');
addChildParentFlags('MTPDreplyKeyboardHide', 'MTPDreplyKeyboardMarkup');
addChildParentFlags('MTPDreplyKeyboardForceReply', 'MTPDreplyKeyboardMarkup');
addChildParentFlags('MTPDinputPeerNotifySettings', 'MTPDpeerNotifySettings');
addChildParentFlags('MTPDpeerNotifySettings', 'MTPDinputPeerNotifySettings');
addChildParentFlags('MTPDchannelForbidden', 'MTPDchannel');

# this is a map (key flags -> map (flag name -> flag bit))
# each key flag of parentFlags should be a subset of the value flag here
parentFlagsCheck = {};

layer = '';
funcs = 0
types = 0;
consts = 0
funcsNow = 0
enums = [];
funcsDict = {};
funcsList = [];
typesDict = {};
TypesDict = {};
typesList = [];
boxed = {};
funcsText = '';
typesText = '';
dataTexts = '';
creatorProxyText = '';
inlineMethods = '';
textSerializeInit = '';
textSerializeMethods = '';
forwards = '';
forwTypedefs = '';
out = open('scheme_auto.h', 'w')
out.write('/*\n');
out.write('Created from \'/SourceFiles/mtproto/scheme.tl\' by \'/SourceFiles/mtproto/generate.py\' script\n\n');
out.write('WARNING! All changes made in this file will be lost!\n\n');
out.write('This file is part of Telegram Desktop,\n');
out.write('the official desktop version of Telegram messaging app, see https://telegram.org\n');
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
out.write('In addition, as a special exception, the copyright holders give permission\n');
out.write('to link the code of portions of this program with the OpenSSL library.\n');
out.write('\n');
out.write('Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE\n');
out.write('Copyright (c) 2014 John Preston, https://desktop.telegram.org\n');
out.write('*/\n');
out.write('#pragma once\n\n#include "mtproto/core_types.h"\n');
with open('scheme.tl') as f:
  for line in f:
    layerline = re.match(r'// LAYER (\d+)', line)
    if (layerline):
      layer = 'constexpr auto CurrentLayer = mtpPrime(' + layerline.group(1) + ');';
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
      if (not re.match(r'vector#1cb5c415 \{t:Type\} # \[ t \] = Vector t;', line)):
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
    while (len(typeid) > 0 and typeid[0] == '0'):
      typeid = typeid[1:];
    if (len(typeid) == 0):
      typeid = '0';
    typeid = '0x' + typeid;

    cleanline = nametype.group(1) + nametype.group(3) + '= ' + nametype.group(4);
    cleanline = re.sub(r' [a-zA-Z0-9_]+\:flags\.[0-9]+\?true', '', cleanline);
    cleanline = cleanline.replace('<', ' ').replace('>', ' ').replace('  ', ' ');
    cleanline = re.sub(r'^ ', '', cleanline);
    cleanline = re.sub(r' $', '', cleanline);
    cleanline = cleanline.replace(':bytes ', ':string ');
    cleanline = cleanline.replace('?bytes ', '?string ');
    cleanline = cleanline.replace('{', '');
    cleanline = cleanline.replace('}', '');
    countTypeId = binascii.crc32(binascii.a2b_qp(cleanline));
    if (countTypeId < 0):
      countTypeId += 2 ** 32;
    countTypeId = '0x' + re.sub(r'^0x|L$', '', hex(countTypeId));
    if (typeid != countTypeId):
      print('Warning: counted ' + countTypeId + ' mismatch with provided ' + typeid + ' (' + cleanline + ')');
      continue;

    params = nametype.group(3);
    restype = nametype.group(4);
    if (restype.find('<') >= 0):
      templ = re.match(r'^([vV]ector<)([A-Za-z0-9\._]+)>$', restype);
      if (templ):
        vectemplate = templ.group(2);
        if (re.match(r'^[A-Z]', vectemplate) or re.match(r'^[a-zA-Z0-9]+_[A-Z]', vectemplate)):
          restype = templ.group(1) + 'MTP' + vectemplate.replace('.', '_') + '>';
        elif (vectemplate == 'int' or vectemplate == 'long' or vectemplate == 'string'):
          restype = templ.group(1) + 'MTP' + vectemplate.replace('.', '_') + '>';
        else:
          foundmeta = '';
          for metatype in typesDict:
            for typedata in typesDict[metatype]:
              if (typedata[0] == vectemplate):
                foundmeta = metatype;
                break;
            if (len(foundmeta) > 0):
              break;
          if (len(foundmeta) > 0):
            ptype = templ.group(1) + 'MTP' + foundmeta.replace('.', '_') + '>';
          else:
            print('Bad vector param: ' + vectemplate);
            continue;
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

    enums.append('\tmtpc_' + name + ' = ' + typeid);

    paramsList = params.strip().split(' ');
    prms = {};
    conditions = {};
    trivialConditions = {}; # true type
    prmsList = [];
    conditionsList = [];
    isTemplate = hasFlags = hasTemplate = '';
    for param in paramsList:
      if (re.match(r'^\s*$', param)):
        continue;
      templ = re.match(r'^{([A-Za-z]+):Type}$', param);
      if (templ):
        hasTemplate = templ.group(1);
        continue;
      pnametype = re.match(r'([a-z_][a-z0-9_]*):([A-Za-z0-9<>\._]+|![a-zA-Z]+|\#|[a-z_][a-z0-9_]*\.[0-9]+\?[A-Za-z0-9<>\._]+)$', param);
      if (not pnametype):
        print('Bad param found: "' + param + '" in line: ' + line);
        continue;
      pname = pnametype.group(1);
      ptypewide = pnametype.group(2);
      if (re.match(r'^!([a-zA-Z]+)$', ptypewide)):
        if ('!' + hasTemplate == ptypewide):
          isTemplate = pname;
          ptype = 'TQueryType';
        else:
          print('Bad template param name: "' + param + '" in line: ' + line);
          continue;
      elif (ptypewide == '#'):
        hasFlags = pname;
        if funcsNow:
          ptype = 'flags<MTP' + name + '::Flags>';
        else:
          ptype = 'flags<MTPD' + name + '::Flags>';
      else:
        ptype = ptypewide;
        if (ptype.find('?') >= 0):
          pmasktype = re.match(r'([a-z_][a-z0-9_]*)\.([0-9]+)\?([A-Za-z0-9<>\._]+)', ptype);
          if (not pmasktype or pmasktype.group(1) != hasFlags):
            print('Bad param found: "' + param + '" in line: ' + line);
            continue;
          ptype = pmasktype.group(3);
          if (ptype.find('<') >= 0):
            templ = re.match(r'^([vV]ector<)([A-Za-z0-9\._]+)>$', ptype);
            if (templ):
              vectemplate = templ.group(2);
              if (re.match(r'^[A-Z]', vectemplate) or re.match(r'^[a-zA-Z0-9]+_[A-Z]', vectemplate)):
                ptype = templ.group(1) + 'MTP' + vectemplate.replace('.', '_') + '>';
              elif (vectemplate == 'int' or vectemplate == 'long' or vectemplate == 'string'):
                ptype = templ.group(1) + 'MTP' + vectemplate.replace('.', '_') + '>';
              else:
                foundmeta = '';
                for metatype in typesDict:
                  for typedata in typesDict[metatype]:
                    if (typedata[0] == vectemplate):
                      foundmeta = metatype;
                      break;
                  if (len(foundmeta) > 0):
                    break;
                if (len(foundmeta) > 0):
                  ptype = templ.group(1) + 'MTP' + foundmeta.replace('.', '_') + '>';
                else:
                  print('Bad vector param: ' + vectemplate);
                  continue;
            else:
              print('Bad template type: ' + ptype);
              continue;
          if (not pname in conditions):
            conditionsList.append(pname);
            conditions[pname] = pmasktype.group(2);
            if (ptype == 'true'):
              trivialConditions[pname] = 1;
        elif (ptype.find('<') >= 0):
          templ = re.match(r'^([vV]ector<)([A-Za-z0-9\._]+)>$', ptype);
          if (templ):
            vectemplate = templ.group(2);
            if (re.match(r'^[A-Z]', vectemplate) or re.match(r'^[a-zA-Z0-9]+_[A-Z]', vectemplate)):
              ptype = templ.group(1) + 'MTP' + vectemplate.replace('.', '_') + '>';
            elif (vectemplate == 'int' or vectemplate == 'long' or vectemplate == 'string'):
              ptype = templ.group(1) + 'MTP' + vectemplate.replace('.', '_') + '>';
            else:
              foundmeta = '';
              for metatype in typesDict:
                for typedata in typesDict[metatype]:
                  if (typedata[0] == vectemplate):
                    foundmeta = metatype;
                    break;
                if (len(foundmeta) > 0):
                  break;
              if (len(foundmeta) > 0):
                ptype = templ.group(1) + 'MTP' + foundmeta.replace('.', '_') + '>';
              else:
                print('Bad vector param: ' + vectemplate);
                continue;
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
        funcsText += '\ntemplate <typename TQueryType>';
      funcsText += '\nclass MTP' + name + ' { // RPC method \'' + nametype.group(1) + '\'\n'; # class

      funcsText += 'public:\n';

      prmsStr = [];
      prmsInit = [];
      prmsNames = [];
      if (hasFlags != ''):
        funcsText += '\tenum class Flag : int32 {\n';
        maxbit = 0;
        parentFlagsCheck['MTP' + name] = {};
        for paramName in conditionsList:
          funcsText += '\t\tf_' + paramName + ' = (1 << ' + conditions[paramName] + '),\n';
          parentFlagsCheck['MTP' + name][paramName] = conditions[paramName];
          maxbit = max(maxbit, int(conditions[paramName]));
        if (maxbit > 0):
          funcsText += '\n';
        funcsText += '\t\tMAX_FIELD = (1 << ' + str(maxbit) + '),\n';
        funcsText += '\t};\n';
        funcsText += '\tQ_DECLARE_FLAGS(Flags, Flag);\n';
        funcsText += '\tfriend inline Flags operator~(Flag v) { return QFlag(~static_cast<int32>(v)); }\n';
        funcsText += '\n';
        if (len(conditions)):
          for paramName in conditionsList:
            if (paramName in trivialConditions):
              funcsText += '\tbool is_' + paramName + '() const { return v' + hasFlags + '.v & Flag::f_' + paramName + '; }\n';
            else:
              funcsText += '\tbool has_' + paramName + '() const { return v' + hasFlags + '.v & Flag::f_' + paramName + '; }\n';
          funcsText += '\n';

      if (len(prms) > len(trivialConditions)):
        for paramName in prmsList:
          if (paramName in trivialConditions):
            continue;
          paramType = prms[paramName];
          prmsInit.append('v' + paramName + '(_' + paramName + ')');
          prmsNames.append('_' + paramName);
          if (paramName == isTemplate):
            ptypeFull = paramType;
          else:
            ptypeFull = 'MTP' + paramType;
          funcsText += '\t' + ptypeFull + ' v' + paramName + ';\n';
          if (paramType in ['int', 'Int', 'bool', 'Bool', 'flags<Flags>']):
            prmsStr.append(ptypeFull + ' _' + paramName);
          else:
            prmsStr.append('const ' + ptypeFull + ' &_' + paramName);
        funcsText += '\n';

      funcsText += '\tMTP' + name + '() = default;\n'; # constructor
      if (len(prms) > len(trivialConditions)):
        funcsText += '\tMTP' + name + '(' + ', '.join(prmsStr) + ') : ' + ', '.join(prmsInit) + ' {\n\t}\n';

      funcsText += '\n';
      funcsText += '\tuint32 innerLength() const {\n'; # count size
      size = [];
      for k in prmsList:
        v = prms[k];
        if (k in conditionsList):
          if (not k in trivialConditions):
            size.append('(has_' + k + '() ? v' + k + '.innerLength() : 0)');
        else:
          size.append('v' + k + '.innerLength()');
      if (not len(size)):
        size.append('0');
      funcsText += '\t\treturn ' + ' + '.join(size) + ';\n';
      funcsText += '\t}\n';

      funcsText += '\tmtpTypeId type() const {\n\t\treturn mtpc_' + name + ';\n\t}\n'; # type id

      funcsText += '\tvoid read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_' + name + ') {\n'; # read method
      for k in prmsList:
        v = prms[k];
        if (k in conditionsList):
          if (not k in trivialConditions):
            funcsText += '\t\tif (has_' + k + '()) { v' + k + '.read(from, end); } else { v' + k + ' = MTP' + v + '(); }\n';
        else:
          funcsText += '\t\tv' + k + '.read(from, end);\n';
      funcsText += '\t}\n';

      funcsText += '\tvoid write(mtpBuffer &to) const {\n'; # write method
      for k in prmsList:
        v = prms[k];
        if (k in conditionsList):
          if (not k in trivialConditions):
            funcsText += '\t\tif (has_' + k + '()) v' + k + '.write(to);\n';
        else:
          funcsText += '\t\tv' + k + '.write(to);\n';
      funcsText += '\t}\n';

      if (isTemplate != ''):
        funcsText += '\n\ttypedef typename TQueryType::ResponseType ResponseType;\n';
      else:
        funcsText += '\n\ttypedef MTP' + resType + ' ResponseType;\n'; # method return type

      funcsText += '};\n'; # class ending
      if (len(conditionsList)):
        funcsText += 'Q_DECLARE_OPERATORS_FOR_FLAGS(MTP' + name + '::Flags)\n\n';
      if (isTemplate != ''):
        funcsText += 'template <typename TQueryType>\n';
        funcsText += 'class MTP' + Name + ' : public MTPBoxed<MTP' + name + '<TQueryType> > {\n';
        funcsText += 'public:\n';
        funcsText += '\tMTP' + Name + '() = default;\n';
        funcsText += '\tMTP' + Name + '(const MTP' + name + '<TQueryType> &v) : MTPBoxed<MTP' + name + '<TQueryType> >(v) {\n\t}\n';
        if (len(prms) > len(trivialConditions)):
          funcsText += '\tMTP' + Name + '(' + ', '.join(prmsStr) + ') : MTPBoxed<MTP' + name + '<TQueryType> >(MTP' + name + '<TQueryType>(' + ', '.join(prmsNames) + ')) {\n\t}\n';
        funcsText += '};\n';
      else:
        funcsText += 'class MTP' + Name + ' : public MTPBoxed<MTP' + name + '> {\n';
        funcsText += 'public:\n';
        funcsText += '\tMTP' + Name + '() = default;\n';
        funcsText += '\tMTP' + Name + '(const MTP' + name + ' &v) : MTPBoxed<MTP' + name + '>(v) {\n\t}\n';
        if (len(prms) > len(trivialConditions)):
          funcsText += '\tMTP' + Name + '(' + ', '.join(prmsStr) + ') : MTPBoxed<MTP' + name + '>(MTP' + name + '(' + ', '.join(prmsNames) + ')) {\n\t}\n';
        funcsText += '};\n';
      funcs = funcs + 1;

      if (not restype in funcsDict):
        funcsList.append(restype);
        funcsDict[restype] = [];
#        TypesDict[restype] = resType;
      funcsDict[restype].append([name, typeid, prmsList, prms, hasFlags, conditionsList, conditions, trivialConditions]);
    else:
      if (isTemplate != ''):
        print('Template types not allowed: "' + resType + '" in line: ' + line);
        continue;
      if (not restype in typesDict):
        typesList.append(restype);
        typesDict[restype] = [];
      TypesDict[restype] = resType;
      typesDict[restype].append([name, typeid, prmsList, prms, hasFlags, conditionsList, conditions, trivialConditions]);

      consts = consts + 1;

# text serialization: types and funcs
def addTextSerialize(lst, dct, dataLetter):
  result = '';
  for restype in lst:
    v = dct[restype];
    for data in v:
      name = data[0];
      prmsList = data[2];
      prms = data[3];
      hasFlags = data[4];
      conditionsList = data[5];
      conditions = data[6];
      trivialConditions = data[7];

      result += 'void _serialize_' + name + '(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 iflag) {\n';
      if (len(conditions)):
        result += '\tMTP' + dataLetter + name + '::Flags flag(iflag);\n\n';
      if (len(prms)):
        result += '\tif (stage) {\n';
        result += '\t\tto.add(",\\n").addSpaces(lev);\n';
        result += '\t} else {\n';
        result += '\t\tto.add("{ ' + name + '");\n';
        result += '\t\tto.add("\\n").addSpaces(lev);\n';
        result += '\t}\n';
        result += '\tswitch (stage) {\n';
        stage = 0;
        for k in prmsList:
          v = prms[k];
          result += '\tcase ' + str(stage) + ': to.add("  ' + k + ': "); ++stages.back(); ';
          if (k == hasFlags):
            result += 'if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; ';
          if (k in trivialConditions):
            result += 'if (flag & MTP' + dataLetter + name + '::Flag::f_' + k + ') { ';
            result += 'to.add("YES [ BY BIT ' + conditions[k] + ' IN FIELD ' + hasFlags + ' ]"); ';
            result += '} else { to.add("[ SKIPPED BY BIT ' + conditions[k] + ' IN FIELD ' + hasFlags + ' ]"); } ';
          else:
            if (k in conditions):
              result += 'if (flag & MTP' + dataLetter + name + '::Flag::f_' + k + ') { ';
            result += 'types.push_back(';
            vtypeget = re.match(r'^[Vv]ector<MTP([A-Za-z0-9\._]+)>', v);
            if (vtypeget):
              if (not re.match(r'^[A-Z]', v)):
                result += 'mtpc_vector';
              else:
                result += '0';
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
                if (vtypeget):
                  result += '); vtypes.push_back(';
                result += 'mtpc_' + conses[0][0];
                if (not vtypeget):
                  result += '); vtypes.push_back(0';
              except KeyError:
                if (vtypeget):
                  result += '); vtypes.push_back(';
                if (re.match(r'^flags<', restype)):
                  result += 'mtpc_flags';
                else:
                  result += 'mtpc_' + restype + '+0';
                if (not vtypeget):
                  result += '); vtypes.push_back(0';
            else:
              result += '0); vtypes.push_back(0';
            result += '); stages.push_back(0); flags.push_back(0); ';
            if (k in conditions):
              result += '} else { to.add("[ SKIPPED BY BIT ' + conditions[k] + ' IN FIELD ' + hasFlags + ' ]"); } ';
          result += 'break;\n';
          stage = stage + 1;
        result += '\tdefault: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;\n';
        result += '\t}\n';
      else:
        result += '\tto.add("{ ' + name + ' }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();\n';
      result += '}\n\n';
  return result;

# text serialization: types and funcs
def addTextSerializeInit(lst, dct):
  result = '';
  for restype in lst:
    v = dct[restype];
    for data in v:
      name = data[0];
      result += '\t\t_serializers.insert(mtpc_' + name + ', _serialize_' + name + ');\n';
  return result;

textSerializeMethods += addTextSerialize(typesList, typesDict, 'D');
textSerializeInit += addTextSerializeInit(typesList, typesDict) + '\n';
textSerializeMethods += addTextSerialize(funcsList, funcsDict, '');
textSerializeInit += addTextSerializeInit(funcsList, funcsDict) + '\n';

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
    hasFlags = data[4];
    conditionsList = data[5];
    conditions = data[6];
    trivialConditions = data[7];

    dataText = '';
    dataText += '\nclass MTPD' + name + ' : public MTP::internal::TypeData {\n'; # data class
    dataText += 'public:\n';

    sizeList = [];
    creatorParams = [];
    creatorParamsList = [];
    readText = '';
    writeText = '';

    if (hasFlags != ''):
      dataText += '\tenum class Flag : int32 {\n';
      maxbit = 0;
      parentFlagsCheck['MTPD' + name] = {};
      for paramName in conditionsList:
        dataText += '\t\tf_' + paramName + ' = (1 << ' + conditions[paramName] + '),\n';
        parentFlagsCheck['MTPD' + name][paramName] = conditions[paramName];
        maxbit = max(maxbit, int(conditions[paramName]));
      if (maxbit > 0):
        dataText += '\n';
      dataText += '\t\tMAX_FIELD = (1 << ' + str(maxbit) + '),\n';
      dataText += '\t};\n';
      dataText += '\tQ_DECLARE_FLAGS(Flags, Flag);\n';
      dataText += '\tfriend inline Flags operator~(Flag v) { return QFlag(~static_cast<int32>(v)); }\n';
      dataText += '\n';
      if (len(conditions)):
        for paramName in conditionsList:
          if (paramName in trivialConditions):
            dataText += '\tbool is_' + paramName + '() const { return v' + hasFlags + '.v & Flag::f_' + paramName + '; }\n';
          else:
            dataText += '\tbool has_' + paramName + '() const { return v' + hasFlags + '.v & Flag::f_' + paramName + '; }\n';
        dataText += '\n';

    dataText += '\tMTPD' + name + '() = default;\n'; # default constructor
    switchLines += '\t\tcase mtpc_' + name + ': '; # for by-type-id type constructor
    if (len(prms) > len(trivialConditions)):
      switchLines += 'setData(new MTPD' + name + '()); ';
      withData = 1;

      getters += '\tconst MTPD' + name + ' &c_' + name + '() const;\n'; # const getter
      constructsInline += 'inline const MTPD' + name + ' &MTP' + restype + '::c_' + name + '() const {\n';
      if (withType):
        constructsInline += '\tt_assert(_type == mtpc_' + name + ');\n';
      constructsInline += '\treturn queryData<MTPD' + name + '>();\n';
      constructsInline += '}\n';

      constructsText += '\texplicit MTP' + restype + '(const MTPD' + name + ' *data);\n'; # by-data type constructor
      constructsInline += 'inline MTP' + restype + '::MTP' + restype + '(const MTPD' + name + ' *data) : TypeDataOwner(data)';
      if (withType):
        constructsInline += ', _type(mtpc_' + name + ')';
      constructsInline += ' {\n}\n';

      dataText += '\tMTPD' + name + '('; # params constructor
      prmsStr = [];
      prmsInit = [];
      for paramName in prmsList:
        if (paramName in trivialConditions):
          continue;
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
        if (paramName in conditions):
          readText += '\tif (v->has_' + paramName + '()) { v->v' + paramName + '.read(from, end); } else { v->v' + paramName + ' = MTP' + paramType + '(); }\n';
          writeText += '\tif (v.has_' + paramName + '()) v.v' + paramName + '.write(to);\n';
          sizeList.append('(v.has_' + paramName + '() ? v.v' + paramName + '.innerLength() : 0)');
        else:
          readText += '\tv->v' + paramName + '.read(from, end);\n';
          writeText += '\tv.v' + paramName + '.write(to);\n';
          sizeList.append('v.v' + paramName + '.innerLength()');

      forwards += 'class MTPD' + name + ';\n'; # data class forward declaration

      dataText += ', '.join(prmsStr) + ') : ' + ', '.join(prmsInit) + ' {\n\t}\n';

      dataText += '\n';
      for paramName in prmsList: # fields declaration
        if (paramName in trivialConditions):
          continue;
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

    if (len(prms) > len(trivialConditions)):
      dataTexts += dataText; # add data class

    if (not friendDecl):
      friendDecl += '\tfriend class MTP::internal::TypeCreator;\n';
    creatorProxyText += '\tinline static MTP' + restype + ' new_' + name + '(' + ', '.join(creatorParams) + ') {\n';
    if (len(prms) > len(trivialConditions)): # creator with params
      creatorProxyText += '\t\treturn MTP' + restype + '(new MTPD' + name + '(' + ', '.join(creatorParamsList) + '));\n';
    else:
      if (withType): # creator by type
        creatorProxyText += '\t\treturn MTP' + restype + '(mtpc_' + name + ');\n';
      else: # single creator
        creatorProxyText += '\t\treturn MTP' + restype + '();\n';
    creatorProxyText += '\t}\n';
    if (len(conditionsList)):
      creatorsText += 'Q_DECLARE_OPERATORS_FOR_FLAGS(MTPD' + name + '::Flags)\n';
    creatorsText += 'inline MTP' + restype + ' MTP_' + name + '(' + ', '.join(creatorParams) + ') {\n';
    creatorsText += '\treturn MTP::internal::TypeCreator::new_' + name + '(' + ', '.join(creatorParamsList) + ');\n';
    creatorsText += '}\n';

    if (withType):
      reader += '\t\tcase mtpc_' + name + ': _type = cons; '; # read switch line
      if (len(prms) > len(trivialConditions)):
        reader += '{\n';
        reader += '\t\t\tauto v = new MTPD' + name + '();\n';
        reader += '\t\t\tsetData(v);\n';
        reader += readText;
        reader += '\t\t} break;\n';

        writer += '\t\tcase mtpc_' + name + ': {\n'; # write switch line
        writer += '\t\t\tauto &v = c_' + name + '();\n';
        writer += writeText;
        writer += '\t\t} break;\n';
      else:
        reader += 'break;\n';
    else:
      if (len(prms) > len(trivialConditions)):
        reader += '\n\tauto v = new MTPD' + name + '();\n';
        reader += '\tsetData(v);\n';
        reader += readText;

        writer += '\tauto &v = c_' + name + '();\n';
        writer += writeText;

  forwards += '\n';

  typesText += '\nclass MTP' + restype; # type class declaration
  if (withData):
    typesText += ' : private MTP::internal::TypeDataOwner'; # if has data fields
  typesText += ' {\n';
  typesText += 'public:\n';
  typesText += '\tMTP' + restype + '()'; # default constructor
  inits = [];
  if not (withType):
    if (withData):
      inits.append('TypeDataOwner(' + newFast + ')');
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

  if (withData):
    typesText += getters;

  typesText += '\n\tuint32 innerLength() const;\n'; # size method
  inlineMethods += '\ninline uint32 MTP' + restype + '::innerLength() const {\n';
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
    inlineMethods += '\tt_assert(_type != 0);\n';
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
    if not (withType):
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
  if (withType and writer != ''):
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
    typesText += '\n\tmtpTypeId _type = 0;\n'; # type field var

  typesText += '};\n'; # type class ended

  inlineMethods += creatorsText;
  typesText += 'typedef MTPBoxed<MTP' + restype + '> MTP' + resType + ';\n'; # boxed type definition

for childName in parentFlagsList:
  parentName = parentFlags[childName];
  for flag in parentFlagsCheck[childName]:
    if (not flag in parentFlagsCheck[parentName]):
      print('Flag ' + flag + ' not found in ' + parentName + ' which should be a flags-parent of ' + childName);
      error
    elif (parentFlagsCheck[childName][flag] != parentFlagsCheck[parentName][flag]):
      print('Flag ' + flag + ' has different value in ' + parentName + ' which should be a flags-parent of ' + childName);
      error
  inlineMethods += 'inline ' + parentName + '::Flags mtpCastFlags(' + childName + '::Flags flags) { return ' + parentName + '::Flags(QFlag(flags)); }\n';
  inlineMethods += 'inline ' + parentName + '::Flags mtpCastFlags(MTPflags<' + childName + '::Flags> flags) { return mtpCastFlags(flags.v); }\n';

# manual types added here
textSerializeMethods += 'void _serialize_rpc_result(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 iflag) {\n';
textSerializeMethods += '\tif (stage) {\n';
textSerializeMethods += '\t\tto.add(",\\n").addSpaces(lev);\n';
textSerializeMethods += '\t} else {\n';
textSerializeMethods += '\t\tto.add("{ rpc_result");\n';
textSerializeMethods += '\t\tto.add("\\n").addSpaces(lev);\n';
textSerializeMethods += '\t}\n';
textSerializeMethods += '\tswitch (stage) {\n';
textSerializeMethods += '\tcase 0: to.add("  req_msg_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;\n';
textSerializeMethods += '\tcase 1: to.add("  result: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;\n';
textSerializeMethods += '\tdefault: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;\n';
textSerializeMethods += '\t}\n';
textSerializeMethods += '}\n\n';
textSerializeInit += '\t\t_serializers.insert(mtpc_rpc_result, _serialize_rpc_result);\n';

textSerializeMethods += 'void _serialize_msg_container(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 iflag) {\n';
textSerializeMethods += '\tif (stage) {\n';
textSerializeMethods += '\t\tto.add(",\\n").addSpaces(lev);\n';
textSerializeMethods += '\t} else {\n';
textSerializeMethods += '\t\tto.add("{ msg_container");\n';
textSerializeMethods += '\t\tto.add("\\n").addSpaces(lev);\n';
textSerializeMethods += '\t}\n';
textSerializeMethods += '\tswitch (stage) {\n';
textSerializeMethods += '\tcase 0: to.add("  messages: "); ++stages.back(); types.push_back(mtpc_vector); vtypes.push_back(mtpc_core_message); stages.push_back(0); flags.push_back(0); break;\n';
textSerializeMethods += '\tdefault: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;\n';
textSerializeMethods += '\t}\n';
textSerializeMethods += '}\n\n';
textSerializeInit += '\t\t_serializers.insert(mtpc_msg_container, _serialize_msg_container);\n';

textSerializeMethods += 'void _serialize_core_message(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 iflag) {\n';
textSerializeMethods += '\tif (stage) {\n';
textSerializeMethods += '\t\tto.add(",\\n").addSpaces(lev);\n';
textSerializeMethods += '\t} else {\n';
textSerializeMethods += '\t\tto.add("{ core_message");\n';
textSerializeMethods += '\t\tto.add("\\n").addSpaces(lev);\n';
textSerializeMethods += '\t}\n';
textSerializeMethods += '\tswitch (stage) {\n';
textSerializeMethods += '\tcase 0: to.add("  msg_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;\n';
textSerializeMethods += '\tcase 1: to.add("  seq_no: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;\n';
textSerializeMethods += '\tcase 2: to.add("  bytes: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;\n';
textSerializeMethods += '\tcase 3: to.add("  body: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;\n';
textSerializeMethods += '\tdefault: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;\n';
textSerializeMethods += '\t}\n';
textSerializeMethods += '}\n\n';
textSerializeInit += '\t\t_serializers.insert(mtpc_core_message, _serialize_core_message);\n';

textSerializeFull = '\nvoid mtpTextSerializeType(MTPStringLogger &to, const mtpPrime *&from, const mtpPrime *end, mtpPrime cons, uint32 level, mtpPrime vcons) {\n';
textSerializeFull += '\tif (_serializers.isEmpty()) initTextSerializers();\n\n';
textSerializeFull += '\tQVector<mtpTypeId> types, vtypes;\n';
textSerializeFull += '\tQVector<int32> stages, flags;\n';
textSerializeFull += '\ttypes.reserve(20); vtypes.reserve(20); stages.reserve(20); flags.reserve(20);\n';
textSerializeFull += '\ttypes.push_back(mtpTypeId(cons)); vtypes.push_back(mtpTypeId(vcons)); stages.push_back(0); flags.push_back(0);\n\n';
textSerializeFull += '\tconst mtpPrime *start = from;\n';
textSerializeFull += '\tmtpTypeId type = cons, vtype = vcons;\n';
textSerializeFull += '\tint32 stage = 0, flag = 0;\n\n';
textSerializeFull += '\twhile (!types.isEmpty()) {\n';
textSerializeFull += '\t\ttype = types.back();\n';
textSerializeFull += '\t\tvtype = vtypes.back();\n';
textSerializeFull += '\t\tstage = stages.back();\n';
textSerializeFull += '\t\tflag = flags.back();\n';
textSerializeFull += '\t\tif (!type) {\n';
textSerializeFull += '\t\t\tif (from >= end) {\n';
textSerializeFull += '\t\t\t\tthrow Exception("from >= end");\n';
textSerializeFull += '\t\t\t} else if (stage) {\n';
textSerializeFull += '\t\t\t\tthrow Exception("unknown type on stage > 0");\n';
textSerializeFull += '\t\t\t}\n';
textSerializeFull += '\t\t\ttypes.back() = type = *from;\n';
textSerializeFull += '\t\t\tstart = ++from;\n';
textSerializeFull += '\t\t}\n\n';
textSerializeFull += '\t\tint32 lev = level + types.size() - 1;\n';
textSerializeFull += '\t\tTextSerializers::const_iterator it = _serializers.constFind(type);\n';
textSerializeFull += '\t\tif (it != _serializers.cend()) {\n';
textSerializeFull += '\t\t\t(*it.value())(to, stage, lev, types, vtypes, stages, flags, start, end, flag);\n';
textSerializeFull += '\t\t} else {\n';
textSerializeFull += '\t\t\tmtpTextSerializeCore(to, from, end, type, lev, vtype);\n';
textSerializeFull += '\t\t\ttypes.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();\n';
textSerializeFull += '\t\t}\n';
textSerializeFull += '\t}\n';
textSerializeFull += '}\n';

out.write('\n// Creator current layer and proxy class declaration\n');
out.write('namespace MTP {\nnamespace internal {\n\n' + layer + '\n\n');
out.write('class TypeCreator;\n\n} // namespace internal\n} // namespace MTP\n');
out.write('\n// Type id constants\nenum {\n' + ',\n'.join(enums) + '\n};\n');
out.write('\n// Type forward declarations\n' + forwards);
out.write('\n// Boxed types definitions\n' + forwTypedefs);
out.write('\n// Type classes definitions\n' + typesText);
out.write('\n// Type constructors with data\n' + dataTexts);
out.write('\n// RPC methods\n' + funcsText);
out.write('\n// Creator proxy class definition\nnamespace MTP {\nnamespace internal {\n\nclass TypeCreator {\npublic:\n' + creatorProxyText + '\t};\n\n} // namespace internal\n} // namespace MTP\n');
out.write('\n// Inline methods definition\n' + inlineMethods);
out.write('\n// Human-readable text serialization\nvoid mtpTextSerializeType(MTPStringLogger &to, const mtpPrime *&from, const mtpPrime *end, mtpPrime cons, uint32 level, mtpPrime vcons);\n');

outCpp = open('scheme_auto.cpp', 'w');
outCpp.write('/*\n');
outCpp.write('Created from \'/SourceFiles/mtproto/scheme.tl\' by \'/SourceFiles/mtproto/generate.py\' script\n\n');
outCpp.write('WARNING! All changes made in this file will be lost!\n\n');
outCpp.write('This file is part of Telegram Desktop,\n');
outCpp.write('the official desktop version of Telegram messaging app, see https://telegram.org\n');
outCpp.write('\n');
outCpp.write('Telegram Desktop is free software: you can redistribute it and/or modify\n');
outCpp.write('it under the terms of the GNU General Public License as published by\n');
outCpp.write('the Free Software Foundation, either version 3 of the License, or\n');
outCpp.write('(at your option) any later version.\n');
outCpp.write('\n');
outCpp.write('It is distributed in the hope that it will be useful,\n');
outCpp.write('but WITHOUT ANY WARRANTY; without even the implied warranty of\n');
outCpp.write('MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the\n');
outCpp.write('GNU General Public License for more details.\n');
outCpp.write('\n');
outCpp.write('Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE\n');
outCpp.write('Copyright (c) 2014 John Preston, https://desktop.telegram.org\n');
outCpp.write('*/\n');
outCpp.write('#include "mtproto/scheme_auto.h"\n\n');
outCpp.write('typedef QVector<mtpTypeId> Types;\ntypedef QVector<int32> StagesFlags;\n\n');
outCpp.write(textSerializeMethods);
outCpp.write('namespace {\n');
outCpp.write('\ttypedef void(*mtpTextSerializer)(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 iflag);\n');
outCpp.write('\ttypedef QMap<mtpTypeId, mtpTextSerializer> TextSerializers;\n\tTextSerializers _serializers;\n\n');
outCpp.write('\tvoid initTextSerializers() {\n');
outCpp.write(textSerializeInit);
outCpp.write('\t}\n}\n');
outCpp.write(textSerializeFull + '\n');

print('Done, written {0} constructors, {1} functions.'.format(consts, funcs));
