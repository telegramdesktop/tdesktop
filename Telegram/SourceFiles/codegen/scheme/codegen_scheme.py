'''
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
'''
import glob, re, binascii, os, sys

sys.dont_write_bytecode = True
scriptPath = os.path.dirname(os.path.realpath(__file__))
sys.path.append(scriptPath + '/../../../lib_tl/tl')
from generate_tl import generate

generate({
  'namespaces': {
    'creator': 'MTP::details',
  },
  'prefixes': {
    'type': 'MTP',
    'data': 'MTPD',
    'id': 'mtpc',
    'construct': 'MTP_',
  },
  'types': {
    'prime': 'mtpPrime',
    'typeId': 'mtpTypeId',
    'buffer': 'mtpBuffer',
  },
  'sections': [
    'read-write',
  ],

  # define some checked flag conversions
  # the key flag type should be a subset of the value flag type
  # with exact the same names, then the key flag can be implicitly
  # casted to the value flag type
  'flagInheritance': {
    'messageService': 'message',
    'updateShortMessage': 'message',
    'updateShortChatMessage': 'message',
    'updateShortSentMessage': 'message',
    'replyKeyboardHide': 'replyKeyboardMarkup',
    'replyKeyboardForceReply': 'replyKeyboardMarkup',
    'inputPeerNotifySettings': 'peerNotifySettings',
    'peerNotifySettings': 'inputPeerNotifySettings',
    'channelForbidden': 'channel',
    'dialogFolder': 'dialog',
  },

  'typeIdExceptions': [
    'channel#c88974ac',
    'ipPortSecret#37982646',
    'accessPointRule#4679b65f',
    'help.configSimple#5a592a6c',
    'messageReplies#81834865',
  ],

  'renamedTypes': {
    'passwordKdfAlgoSHA256SHA256PBKDF2HMACSHA512iter100000SHA256ModPow': 'passwordKdfAlgoModPow',
   },

  'skip': [
    'int ? = Int;',
    'long ? = Long;',
    'double ? = Double;',
    'string ? = String;',

    'vector {t:Type} # [ t ] = Vector t;',

    'int128 4*[ int ] = Int128;',
    'int256 8*[ int ] = Int256;',

    'vector#1cb5c415 {t:Type} # [ t ] = Vector t;',
  ],
  'builtin': [
    'int',
    'long',
    'double',
    'string',
    'bytes',
    'int128',
    'int256',
  ],
  'builtinTemplates': [
    'vector',
    'flags',
  ],
  'synonyms': {
    'bytes': 'string',
  },
  'builtinInclude': 'mtproto/core_types.h',

  'dumpToText': {
    'include': 'mtproto/details/mtproto_dump_to_text.h',
  },

})
