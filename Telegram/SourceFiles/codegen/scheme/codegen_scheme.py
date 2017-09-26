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
import glob, re, binascii, os, sys

input_file = ''
output_path = ''
next_output_path = False
for arg in sys.argv[1:]:
  if next_output_path:
    next_output_path = False
    output_path = arg
  elif arg == '-o':
    next_output_path = True
  elif re.match(r'^-o(.+)', arg):
    output_path = arg[2:]
  else:
    input_file = arg

if input_file == '':
  print('Input file required.')
  sys.exit(1)
if output_path == '':
  print('Output path required.')
  sys.exit(1)

output_header = output_path + '/scheme.h'
output_source = output_path + '/scheme.cpp'

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
factories = '';
flagOperators = '';
methods = '';
inlineMethods = '';
textSerializeInit = '';
textSerializeMethods = '';
forwards = '';
forwTypedefs = '';

with open(input_file) as f:
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

    nametype = re.match(r'([a-zA-Z\.0-9_]+)(#[0-9a-f]+)?([^=]*)=\s*([a-zA-Z\.<>0-9_]+);', line);
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
    if (typeid and len(typeid) > 0):
      typeid = typeid[1:]; # Skip '#'
    while (typeid and len(typeid) > 0 and typeid[0] == '0'):
      typeid = typeid[1:];

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
    if (typeid and len(typeid) > 0):
      typeid = '0x' + typeid;
      if (typeid != countTypeId):
        print('Warning: counted ' + countTypeId + ' mismatch with provided ' + typeid + ' (' + cleanline + ')');
        continue;
    else:
      typeid = countTypeId;

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
      methodBodies = ''
      if (isTemplate != ''):
        funcsText += '\ntemplate <typename TQueryType>';
      funcsText += '\nclass MTP' + name + ' { // RPC method \'' + nametype.group(1) + '\'\n'; # class

      funcsText += 'public:\n';

      prmsStr = [];
      prmsInit = [];
      prmsNames = [];
      if (hasFlags != ''):
        funcsText += '\tenum class Flag : uint32 {\n';
        maxbit = 0;
        parentFlagsCheck['MTP' + name] = {};
        for paramName in conditionsList:
          funcsText += '\t\tf_' + paramName + ' = (1U << ' + conditions[paramName] + '),\n';
          parentFlagsCheck['MTP' + name][paramName] = conditions[paramName];
          maxbit = max(maxbit, int(conditions[paramName]));
        if (maxbit > 0):
          funcsText += '\n';
        funcsText += '\t\tMAX_FIELD = (1U << ' + str(maxbit) + '),\n';
        funcsText += '\t};\n';
        funcsText += '\tusing Flags = base::flags<Flag>;\n';
        funcsText += '\tfriend inline constexpr auto is_flag_type(Flag) { return true; };\n';
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
      funcsText += '\tuint32 innerLength() const;\n'; # count size
      if (isTemplate != ''):
        methodBodies += 'template <typename TQueryType>\n'
        methodBodies += 'uint32 MTP' + name + '<TQueryType>::innerLength() const {\n';
      else:
        methodBodies += 'uint32 MTP' + name + '::innerLength() const {\n';
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
      methodBodies += '\treturn ' + ' + '.join(size) + ';\n';
      methodBodies += '}\n';

      funcsText += '\tmtpTypeId type() const {\n\t\treturn mtpc_' + name + ';\n\t}\n'; # type id

      funcsText += '\tvoid read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons = mtpc_' + name + ');\n'; # read method
      if (isTemplate != ''):
        methodBodies += 'template <typename TQueryType>\n'
        methodBodies += 'void MTP' + name + '<TQueryType>::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {\n';
      else:
        methodBodies += 'void MTP' + name + '::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {\n';
      for k in prmsList:
        v = prms[k];
        if (k in conditionsList):
          if (not k in trivialConditions):
            methodBodies += '\tif (has_' + k + '()) { v' + k + '.read(from, end); } else { v' + k + ' = MTP' + v + '(); }\n';
        else:
          methodBodies += '\tv' + k + '.read(from, end);\n';
      methodBodies += '}\n';

      funcsText += '\tvoid write(mtpBuffer &to) const;\n'; # write method
      if (isTemplate != ''):
        methodBodies += 'template <typename TQueryType>\n'
        methodBodies += 'void MTP' + name + '<TQueryType>::write(mtpBuffer &to) const {\n';
      else:
        methodBodies += 'void MTP' + name + '::write(mtpBuffer &to) const {\n';
      for k in prmsList:
        v = prms[k];
        if (k in conditionsList):
          if (not k in trivialConditions):
            methodBodies += '\tif (has_' + k + '()) v' + k + '.write(to);\n';
        else:
          methodBodies += '\tv' + k + '.write(to);\n';
      methodBodies += '}\n';

      if (isTemplate != ''):
        funcsText += '\n\tusing ResponseType = typename TQueryType::ResponseType;\n';
        inlineMethods += methodBodies;
      else:
        funcsText += '\n\tusing ResponseType = MTP' + resType + ';\n'; # method return type
        methods += methodBodies;

      funcsText += '};\n'; # class ending
      if (isTemplate != ''):
        funcsText += 'template <typename TQueryType>\n';
        funcsText += 'using MTP' + Name + ' = MTPBoxed<MTP' + name + '<TQueryType>>;\n';
      else:
        funcsText += 'using MTP' + Name + ' = MTPBoxed<MTP' + name + '>;\n';
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

      result += 'void Serialize_' + name + '(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, uint32 iflag) {\n';
      if (len(conditions)):
        result += '\tauto flag = MTP' + dataLetter + name + '::Flags::from_raw(iflag);\n\n';
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
      result += '\tresult.insert(mtpc_' + name + ', Serialize_' + name + ');\n';
  return result;

textSerializeMethods += addTextSerialize(typesList, typesDict, 'D');
textSerializeInit += addTextSerializeInit(typesList, typesDict) + '\n';
textSerializeMethods += addTextSerialize(funcsList, funcsDict, '');
textSerializeInit += addTextSerializeInit(funcsList, funcsDict) + '\n';

for restype in typesList:
  v = typesDict[restype];
  resType = TypesDict[restype];
  withData = 0;
  creatorsDeclarations = '';
  creatorsBodies = '';
  flagDeclarations = '';
  constructsText = '';
  constructsBodies = '';

  forwards += 'class MTP' + restype + ';\n';
  forwTypedefs += 'using MTP' + resType + ' = MTPBoxed<MTP' + restype + '>;\n';

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
      dataText += '\tenum class Flag : uint32 {\n';
      maxbit = 0;
      parentFlagsCheck['MTPD' + name] = {};
      for paramName in conditionsList:
        dataText += '\t\tf_' + paramName + ' = (1U << ' + conditions[paramName] + '),\n';
        parentFlagsCheck['MTPD' + name][paramName] = conditions[paramName];
        maxbit = max(maxbit, int(conditions[paramName]));
      if (maxbit > 0):
        dataText += '\n';
      dataText += '\t\tMAX_FIELD = (1U << ' + str(maxbit) + '),\n';
      dataText += '\t};\n';
      dataText += '\tusing Flags = base::flags<Flag>;\n';
      dataText += '\tfriend inline constexpr auto is_flag_type(Flag) { return true; };\n';
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
      constructsBodies += 'const MTPD' + name + ' &MTP' + restype + '::c_' + name + '() const {\n';
      if (withType):
        constructsBodies += '\tAssert(_type == mtpc_' + name + ');\n';
      constructsBodies += '\treturn queryData<MTPD' + name + '>();\n';
      constructsBodies += '}\n';

      constructsText += '\texplicit MTP' + restype + '(const MTPD' + name + ' *data);\n'; # by-data type constructor
      constructsBodies += 'MTP' + restype + '::MTP' + restype + '(const MTPD' + name + ' *data) : TypeDataOwner(data)';
      if (withType):
        constructsBodies += ', _type(mtpc_' + name + ')';
      constructsBodies += ' {\n}\n';

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
    creatorsDeclarations += 'MTP' + restype + ' MTP_' + name + '(' + ', '.join(creatorParams) + ');\n';
    creatorsBodies += 'MTP' + restype + ' MTP_' + name + '(' + ', '.join(creatorParams) + ') {\n';
    creatorsBodies += '\treturn MTP::internal::TypeCreator::new_' + name + '(' + ', '.join(creatorParamsList) + ');\n';
    creatorsBodies += '}\n';

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
    methods += '\nMTP' + restype + '::MTP' + restype + '()';
    if (inits):
      methods += ' : ' + ', '.join(inits);
    methods += ' {\n}\n';
  else:
    if (inits):
      typesText += ' : ' + ', '.join(inits);
    typesText += ' {\n\t}\n';

  if (withData):
    typesText += getters;

  typesText += '\n\tuint32 innerLength() const;\n'; # size method
  methods += '\nuint32 MTP' + restype + '::innerLength() const {\n';
  if (withType and sizeCases):
    methods += '\tswitch (_type) {\n';
    methods += sizeCases;
    methods += '\t}\n';
    methods += '\treturn 0;\n';
  else:
    methods += sizeFast;
  methods += '}\n';

  typesText += '\tmtpTypeId type() const;\n'; # type id method
  methods += 'mtpTypeId MTP' + restype + '::type() const {\n';
  if (withType):
    methods += '\tAssert(_type != 0);\n';
    methods += '\treturn _type;\n';
  else:
    methods += '\treturn mtpc_' + v[0][0] + ';\n';
  methods += '}\n';

  typesText += '\tvoid read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons'; # read method
  if (not withType):
    typesText += ' = mtpc_' + name;
  typesText += ');\n';
  methods += 'void MTP' + restype + '::read(const mtpPrime *&from, const mtpPrime *end, mtpTypeId cons) {\n';
  if (withData):
    if not (withType):
      methods += '\tif (cons != mtpc_' + v[0][0] + ') throw mtpErrorUnexpected(cons, "MTP' + restype + '");\n';
  if (withType):
    methods += '\tswitch (cons) {\n'
    methods += reader;
    methods += '\t\tdefault: throw mtpErrorUnexpected(cons, "MTP' + restype + '");\n';
    methods += '\t}\n';
  else:
    methods += reader;
  methods += '}\n';

  typesText += '\tvoid write(mtpBuffer &to) const;\n'; # write method
  methods += 'void MTP' + restype + '::write(mtpBuffer &to) const {\n';
  if (withType and writer != ''):
    methods += '\tswitch (_type) {\n';
    methods += writer;
    methods += '\t}\n';
  else:
    methods += writer;
  methods += '}\n';

  typesText += '\n\tusing ResponseType = void;\n'; # no response types declared

  typesText += '\nprivate:\n'; # private constructors
  if (withType): # by-type-id constructor
    typesText += '\texplicit MTP' + restype + '(mtpTypeId type);\n';
    methods += 'MTP' + restype + '::MTP' + restype + '(mtpTypeId type) : ';
    methods += '_type(type)';
    methods += ' {\n';
    methods += '\tswitch (type) {\n'; # type id check
    methods += switchLines;
    methods += '\t\tdefault: throw mtpErrorBadTypeId(type, "MTP' + restype + '");\n\t}\n';
    methods += '}\n'; # by-type-id constructor end

  if (withData):
    typesText += constructsText;
    methods += constructsBodies;

  if (friendDecl):
    typesText += '\n' + friendDecl;

  if (withType):
    typesText += '\n\tmtpTypeId _type = 0;\n'; # type field var

  typesText += '};\n'; # type class ended

  flagOperators += flagDeclarations;
  factories += creatorsDeclarations;
  methods += creatorsBodies;
  typesText += 'using MTP' + resType + ' = MTPBoxed<MTP' + restype + '>;\n'; # boxed type definition

flagOperators += '\n'

for childName in parentFlagsList:
  parentName = parentFlags[childName];
  for flag in parentFlagsCheck[childName]:
#
# 'channelForbidden' has 'until_date' flag and 'channel' doesn't have it.
# But as long as flags don't collide this is not a problem.
#
#    if (not flag in parentFlagsCheck[parentName]):
#      print('Flag ' + flag + ' not found in ' + parentName + ' which should be a flags-parent of ' + childName);
#      error
#
    if (flag in parentFlagsCheck[parentName]):
      if (parentFlagsCheck[childName][flag] != parentFlagsCheck[parentName][flag]):
        print('Flag ' + flag + ' has different value in ' + parentName + ' which should be a flags-parent of ' + childName);
        error
    else:
      parentFlagsCheck[parentName][flag] = parentFlagsCheck[childName][flag];
  flagOperators += 'inline ' + parentName + '::Flags mtpCastFlags(' + childName + '::Flags flags) { return static_cast<' + parentName + '::Flag>(flags.value()); }\n';
  flagOperators += 'inline ' + parentName + '::Flags mtpCastFlags(MTPflags<' + childName + '::Flags> flags) { return mtpCastFlags(flags.v); }\n';

# manual types added here
textSerializeMethods += '\
void _serialize_rpc_result(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, uint32 iflag) {\n\
	if (stage) {\n\
		to.add(",\\n").addSpaces(lev);\n\
	} else {\n\
		to.add("{ rpc_result");\n\
		to.add("\\n").addSpaces(lev);\n\
	}\n\
	switch (stage) {\n\
	case 0: to.add("  req_msg_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;\n\
	case 1: to.add("  result: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;\n\
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;\n\
	}\n\
}\n\
\n\
void _serialize_msg_container(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, uint32 iflag) {\n\
	if (stage) {\n\
		to.add(",\\n").addSpaces(lev);\n\
	} else {\n\
		to.add("{ msg_container");\n\
		to.add("\\n").addSpaces(lev);\n\
	}\n\
	switch (stage) {\n\
	case 0: to.add("  messages: "); ++stages.back(); types.push_back(mtpc_vector); vtypes.push_back(mtpc_core_message); stages.push_back(0); flags.push_back(0); break;\n\
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;\n\
	}\n\
}\n\
\n\
void _serialize_core_message(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, uint32 iflag) {\n\
	if (stage) {\n\
		to.add(",\\n").addSpaces(lev);\n\
	} else {\n\
		to.add("{ core_message");\n\
		to.add("\\n").addSpaces(lev);\n\
	}\n\
	switch (stage) {\n\
	case 0: to.add("  msg_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;\n\
	case 1: to.add("  seq_no: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;\n\
	case 2: to.add("  bytes: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;\n\
	case 3: to.add("  body: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;\n\
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;\n\
	}\n\
}\n\
\n';

textSerializeInit += '\
	result.insert(mtpc_rpc_result, _serialize_rpc_result);\n\
	result.insert(mtpc_msg_container, _serialize_msg_container);\n\
	result.insert(mtpc_core_message, _serialize_core_message);\n';

# module itself

header = '\
/*\n\
WARNING! All changes made in this file will be lost!\n\
Created from \'' + os.path.basename(input_file) + '\' by \'codegen_scheme\'\n\
\n\
This file is part of Telegram Desktop,\n\
the official desktop version of Telegram messaging app, see https://telegram.org\n\
\n\
Telegram Desktop is free software: you can redistribute it and/or modify\n\
it under the terms of the GNU General Public License as published by\n\
the Free Software Foundation, either version 3 of the License, or\n\
(at your option) any later version.\n\
\n\
It is distributed in the hope that it will be useful,\n\
but WITHOUT ANY WARRANTY; without even the implied warranty of\n\
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the\n\
GNU General Public License for more details.\n\
\n\
In addition, as a special exception, the copyright holders give permission\n\
to link the code of portions of this program with the OpenSSL library.\n\
\n\
Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE\n\
Copyright (c) 2014 John Preston, https://desktop.telegram.org\n\
*/\n\
#pragma once\n\
\n\
#include "mtproto/core_types.h"\n\
#include "base/flags.h"\n\
\n\
// Creator current layer and proxy class declaration\n\
namespace MTP {\n\
namespace internal {\n\
\n\
' + layer + '\n\
\n\
class TypeCreator;\n\
\n\
} // namespace internal\n\
} // namespace MTP\n\
\n\
// Type id constants\n\
enum {\n\
' + ',\n'.join(enums) + '\n\
};\n\
\n\
// Type forward declarations\n\
' + forwards + '\n\
// Boxed types definitions\n\
' + forwTypedefs + '\n\
// Type classes definitions\n\
' + typesText + '\n\
// Type constructors with data\n\
' + dataTexts + '\n\
// RPC methods\n\
' + funcsText + '\n\
// Template methods definition\n\
' + inlineMethods + '\n\
// Flag operators definition\n\
' + flagOperators + '\n\
// Factory methods declaration\n\
' + factories + '\n\
// Human-readable text serialization\n\
void mtpTextSerializeType(MTPStringLogger &to, const mtpPrime *&from, const mtpPrime *end, mtpPrime cons, uint32 level, mtpPrime vcons);\n'

source = '\
/*\n\
WARNING! All changes made in this file will be lost!\n\
Created from \'' + os.path.basename(input_file) + '\' by \'codegen_scheme\'\n\
\n\
This file is part of Telegram Desktop,\n\
the official desktop version of Telegram messaging app, see https://telegram.org\n\
\n\
Telegram Desktop is free software: you can redistribute it and/or modify\n\
it under the terms of the GNU General Public License as published by\n\
the Free Software Foundation, either version 3 of the License, or\n\
(at your option) any later version.\n\
\n\
It is distributed in the hope that it will be useful,\n\
but WITHOUT ANY WARRANTY; without even the implied warranty of\n\
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the\n\
GNU General Public License for more details.\n\
\n\
Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE\n\
Copyright (c) 2014 John Preston, https://desktop.telegram.org\n\
*/\n\
#include "scheme.h"\n\
\n\
// Creator proxy class definition\n\
namespace MTP {\n\
namespace internal {\n\
\n\
class TypeCreator {\n\
public:\n\
' + creatorProxyText + '\n\
};\n\
\n\
} // namespace internal\n\
} // namespace MTP\n\
\n\
// Methods definition\n\
' + methods + '\n\
\n\
using Types = QVector<mtpTypeId>;\n\
using StagesFlags = QVector<int32>;\n\
\n\
' + textSerializeMethods + '\n\
namespace {\n\
\n\
using TextSerializer = void (*)(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, uint32 iflag);\n\
using TextSerializers = QMap<mtpTypeId, TextSerializer>;\n\
\n\
QMap<mtpTypeId, TextSerializer> createTextSerializers() {\n\
	auto result = QMap<mtpTypeId, TextSerializer>();\n\
\n\
' + textSerializeInit + '\n\
\n\
	return result;\n\
}\n\
\n\
} // namespace\n\
\n\
void mtpTextSerializeType(MTPStringLogger &to, const mtpPrime *&from, const mtpPrime *end, mtpPrime cons, uint32 level, mtpPrime vcons) {\n\
	static auto serializers = createTextSerializers();\n\
\n\
	QVector<mtpTypeId> types, vtypes;\n\
	QVector<int32> stages, flags;\n\
	types.reserve(20); vtypes.reserve(20); stages.reserve(20); flags.reserve(20);\n\
	types.push_back(mtpTypeId(cons)); vtypes.push_back(mtpTypeId(vcons)); stages.push_back(0); flags.push_back(0);\n\
\n\
	const mtpPrime *start = from;\n\
	mtpTypeId type = cons, vtype = vcons;\n\
	int32 stage = 0, flag = 0;\n\
\n\
	while (!types.isEmpty()) {\n\
		type = types.back();\n\
		vtype = vtypes.back();\n\
		stage = stages.back();\n\
		flag = flags.back();\n\
		if (!type) {\n\
			if (from >= end) {\n\
				throw Exception("from >= end");\n\
			} else if (stage) {\n\
				throw Exception("unknown type on stage > 0");\n\
			}\n\
			types.back() = type = *from;\n\
			start = ++from;\n\
		}\n\
\n\
		int32 lev = level + types.size() - 1;\n\
		auto it = serializers.constFind(type);\n\
		if (it != serializers.cend()) {\n\
			(*it.value())(to, stage, lev, types, vtypes, stages, flags, start, end, flag);\n\
		} else {\n\
			mtpTextSerializeCore(to, from, end, type, lev, vtype);\n\
			types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();\n\
		}\n\
	}\n\
}\n';

already_header = ''
if os.path.isfile(output_header):
  with open(output_header, 'r') as already:
    already_header = already.read()
if already_header != header:
  with open(output_header, 'w') as out:
    out.write(header)

already_source = ''
if os.path.isfile(output_source):
  with open(output_source, 'r') as already:
    already_source = already.read()
if already_source != source:
  with open(output_source, 'w') as out:
    out.write(source)
