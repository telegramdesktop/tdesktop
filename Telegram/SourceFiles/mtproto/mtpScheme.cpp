/*
Created from '/SourceFiles/mtproto/scheme.tl' by '/SourceFiles/mtproto/generate.py' script

WARNING! All changes made in this file will be lost!

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
*/
#include "stdafx.h"
#include "mtpScheme.h"

#if (defined _DEBUG || defined _WITH_DEBUG)


void mtpTextSerializeType(MTPStringLogger &to, const mtpPrime *&from, const mtpPrime *end, mtpPrime cons, uint32 level, mtpPrime vcons) {
	QString add = QString(" ").repeated(level * 2);

	const mtpPrime *start = from;
	try {
		if (!cons) {
			if (from >= end) {
				throw Exception("from >= 2");
			}
			cons = *from;
			++from;
			++start;
		}

		switch (mtpTypeId(cons)) {
		case mtpc_userProfilePhotoEmpty:
			to.add("{ userProfilePhotoEmpty");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_userProfilePhoto:
			to.add("{ userProfilePhoto");
			to.add("\n").add(add);
			to.add("  photo_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  photo_small: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  photo_big: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_rpc_error:
			to.add("{ rpc_error");
			to.add("\n").add(add);
			to.add("  error_code: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  error_message: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_dh_gen_ok:
			to.add("{ dh_gen_ok");
			to.add("\n").add(add);
			to.add("  nonce: "); mtpTextSerializeType(to, from, end, mtpc_int128, level + 1); to.add(",\n").add(add);
			to.add("  server_nonce: "); mtpTextSerializeType(to, from, end, mtpc_int128, level + 1); to.add(",\n").add(add);
			to.add("  new_nonce_hash1: "); mtpTextSerializeType(to, from, end, mtpc_int128, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_dh_gen_retry:
			to.add("{ dh_gen_retry");
			to.add("\n").add(add);
			to.add("  nonce: "); mtpTextSerializeType(to, from, end, mtpc_int128, level + 1); to.add(",\n").add(add);
			to.add("  server_nonce: "); mtpTextSerializeType(to, from, end, mtpc_int128, level + 1); to.add(",\n").add(add);
			to.add("  new_nonce_hash2: "); mtpTextSerializeType(to, from, end, mtpc_int128, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_dh_gen_fail:
			to.add("{ dh_gen_fail");
			to.add("\n").add(add);
			to.add("  nonce: "); mtpTextSerializeType(to, from, end, mtpc_int128, level + 1); to.add(",\n").add(add);
			to.add("  server_nonce: "); mtpTextSerializeType(to, from, end, mtpc_int128, level + 1); to.add(",\n").add(add);
			to.add("  new_nonce_hash3: "); mtpTextSerializeType(to, from, end, mtpc_int128, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputPeerEmpty:
			to.add("{ inputPeerEmpty");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_inputPeerSelf:
			to.add("{ inputPeerSelf");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_inputPeerContact:
			to.add("{ inputPeerContact");
			to.add("\n").add(add);
			to.add("  user_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputPeerForeign:
			to.add("{ inputPeerForeign");
			to.add("\n").add(add);
			to.add("  user_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  access_hash: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputPeerChat:
			to.add("{ inputPeerChat");
			to.add("\n").add(add);
			to.add("  chat_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_photoEmpty:
			to.add("{ photoEmpty");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_photo:
			to.add("{ photo");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  access_hash: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  user_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  caption: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  geo: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  sizes: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_p_q_inner_data:
			to.add("{ p_q_inner_data");
			to.add("\n").add(add);
			to.add("  pq: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  p: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  q: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  nonce: "); mtpTextSerializeType(to, from, end, mtpc_int128, level + 1); to.add(",\n").add(add);
			to.add("  server_nonce: "); mtpTextSerializeType(to, from, end, mtpc_int128, level + 1); to.add(",\n").add(add);
			to.add("  new_nonce: "); mtpTextSerializeType(to, from, end, mtpc_int256, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_client_DH_inner_data:
			to.add("{ client_DH_inner_data");
			to.add("\n").add(add);
			to.add("  nonce: "); mtpTextSerializeType(to, from, end, mtpc_int128, level + 1); to.add(",\n").add(add);
			to.add("  server_nonce: "); mtpTextSerializeType(to, from, end, mtpc_int128, level + 1); to.add(",\n").add(add);
			to.add("  retry_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  g_b: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_contacts_link:
			to.add("{ contacts_link");
			to.add("\n").add(add);
			to.add("  my_link: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  foreign_link: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  user: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputPhotoCropAuto:
			to.add("{ inputPhotoCropAuto");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_inputPhotoCrop:
			to.add("{ inputPhotoCrop");
			to.add("\n").add(add);
			to.add("  crop_left: "); mtpTextSerializeType(to, from, end, mtpc_double, level + 1); to.add(",\n").add(add);
			to.add("  crop_top: "); mtpTextSerializeType(to, from, end, mtpc_double, level + 1); to.add(",\n").add(add);
			to.add("  crop_width: "); mtpTextSerializeType(to, from, end, mtpc_double, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputFile:
			to.add("{ inputFile");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  parts: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  name: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  md5_checksum: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputFileBig:
			to.add("{ inputFileBig");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  parts: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  name: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messageActionEmpty:
			to.add("{ messageActionEmpty");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_messageActionChatCreate:
			to.add("{ messageActionChatCreate");
			to.add("\n").add(add);
			to.add("  title: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  users: "); mtpTextSerializeType(to, from, end, 0, level + 1, mtpc_int); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messageActionChatEditTitle:
			to.add("{ messageActionChatEditTitle");
			to.add("\n").add(add);
			to.add("  title: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messageActionChatEditPhoto:
			to.add("{ messageActionChatEditPhoto");
			to.add("\n").add(add);
			to.add("  photo: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messageActionChatDeletePhoto:
			to.add("{ messageActionChatDeletePhoto");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_messageActionChatAddUser:
			to.add("{ messageActionChatAddUser");
			to.add("\n").add(add);
			to.add("  user_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messageActionChatDeleteUser:
			to.add("{ messageActionChatDeleteUser");
			to.add("\n").add(add);
			to.add("  user_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messageActionGeoChatCreate:
			to.add("{ messageActionGeoChatCreate");
			to.add("\n").add(add);
			to.add("  title: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  address: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messageActionGeoChatCheckin:
			to.add("{ messageActionGeoChatCheckin");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_inputMessagesFilterEmpty:
			to.add("{ inputMessagesFilterEmpty");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_inputMessagesFilterPhotos:
			to.add("{ inputMessagesFilterPhotos");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_inputMessagesFilterVideo:
			to.add("{ inputMessagesFilterVideo");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_inputMessagesFilterPhotoVideo:
			to.add("{ inputMessagesFilterPhotoVideo");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_inputMessagesFilterDocument:
			to.add("{ inputMessagesFilterDocument");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_inputMessagesFilterAudio:
			to.add("{ inputMessagesFilterAudio");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_help_support:
			to.add("{ help_support");
			to.add("\n").add(add);
			to.add("  phone_number: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  user: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_contactFound:
			to.add("{ contactFound");
			to.add("\n").add(add);
			to.add("  user_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_future_salts:
			to.add("{ future_salts");
			to.add("\n").add(add);
			to.add("  req_msg_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  now: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  salts: "); mtpTextSerializeType(to, from, end, mtpc_vector, level + 1, mtpc_future_salt); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputPhotoEmpty:
			to.add("{ inputPhotoEmpty");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_inputPhoto:
			to.add("{ inputPhoto");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  access_hash: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_chatParticipant:
			to.add("{ chatParticipant");
			to.add("\n").add(add);
			to.add("  user_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  inviter_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_auth_exportedAuthorization:
			to.add("{ auth_exportedAuthorization");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  bytes: "); mtpTextSerializeType(to, from, end, mtpc_bytes, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_contactStatus:
			to.add("{ contactStatus");
			to.add("\n").add(add);
			to.add("  user_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  expires: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_new_session_created:
			to.add("{ new_session_created");
			to.add("\n").add(add);
			to.add("  first_msg_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  unique_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  server_salt: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_geochats_located:
			to.add("{ geochats_located");
			to.add("\n").add(add);
			to.add("  results: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  messages: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  chats: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  users: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_updatesTooLong:
			to.add("{ updatesTooLong");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_updateShortMessage:
			to.add("{ updateShortMessage");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  from_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  message: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  pts: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  seq: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_updateShortChatMessage:
			to.add("{ updateShortChatMessage");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  from_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  chat_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  message: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  pts: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  seq: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_updateShort:
			to.add("{ updateShort");
			to.add("\n").add(add);
			to.add("  update: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_updatesCombined:
			to.add("{ updatesCombined");
			to.add("\n").add(add);
			to.add("  updates: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  users: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  chats: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  seq_start: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  seq: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_updates:
			to.add("{ updates");
			to.add("\n").add(add);
			to.add("  updates: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  users: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  chats: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  seq: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_future_salt:
			to.add("{ future_salt");
			to.add("\n").add(add);
			to.add("  valid_since: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  valid_until: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  salt: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_server_DH_inner_data:
			to.add("{ server_DH_inner_data");
			to.add("\n").add(add);
			to.add("  nonce: "); mtpTextSerializeType(to, from, end, mtpc_int128, level + 1); to.add(",\n").add(add);
			to.add("  server_nonce: "); mtpTextSerializeType(to, from, end, mtpc_int128, level + 1); to.add(",\n").add(add);
			to.add("  g: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  dh_prime: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  g_a: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  server_time: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_resPQ:
			to.add("{ resPQ");
			to.add("\n").add(add);
			to.add("  nonce: "); mtpTextSerializeType(to, from, end, mtpc_int128, level + 1); to.add(",\n").add(add);
			to.add("  server_nonce: "); mtpTextSerializeType(to, from, end, mtpc_int128, level + 1); to.add(",\n").add(add);
			to.add("  pq: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  server_public_key_fingerprints: "); mtpTextSerializeType(to, from, end, 0, level + 1, mtpc_long); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_upload_file:
			to.add("{ upload_file");
			to.add("\n").add(add);
			to.add("  type: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  mtime: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  bytes: "); mtpTextSerializeType(to, from, end, mtpc_bytes, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputMediaEmpty:
			to.add("{ inputMediaEmpty");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_inputMediaUploadedPhoto:
			to.add("{ inputMediaUploadedPhoto");
			to.add("\n").add(add);
			to.add("  file: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputMediaPhoto:
			to.add("{ inputMediaPhoto");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputMediaGeoPoint:
			to.add("{ inputMediaGeoPoint");
			to.add("\n").add(add);
			to.add("  geo_point: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputMediaContact:
			to.add("{ inputMediaContact");
			to.add("\n").add(add);
			to.add("  phone_number: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  first_name: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  last_name: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputMediaUploadedVideo:
			to.add("{ inputMediaUploadedVideo");
			to.add("\n").add(add);
			to.add("  file: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  duration: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  w: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  h: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  mime_type: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputMediaUploadedThumbVideo:
			to.add("{ inputMediaUploadedThumbVideo");
			to.add("\n").add(add);
			to.add("  file: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  thumb: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  duration: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  w: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  h: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  mime_type: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputMediaVideo:
			to.add("{ inputMediaVideo");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputMediaUploadedAudio:
			to.add("{ inputMediaUploadedAudio");
			to.add("\n").add(add);
			to.add("  file: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  duration: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  mime_type: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputMediaAudio:
			to.add("{ inputMediaAudio");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputMediaUploadedDocument:
			to.add("{ inputMediaUploadedDocument");
			to.add("\n").add(add);
			to.add("  file: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  file_name: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  mime_type: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputMediaUploadedThumbDocument:
			to.add("{ inputMediaUploadedThumbDocument");
			to.add("\n").add(add);
			to.add("  file: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  thumb: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  file_name: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  mime_type: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputMediaDocument:
			to.add("{ inputMediaDocument");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_documentEmpty:
			to.add("{ documentEmpty");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_document:
			to.add("{ document");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  access_hash: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  user_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  file_name: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  mime_type: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  size: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  thumb: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  dc_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputEncryptedFileEmpty:
			to.add("{ inputEncryptedFileEmpty");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_inputEncryptedFileUploaded:
			to.add("{ inputEncryptedFileUploaded");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  parts: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  md5_checksum: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  key_fingerprint: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputEncryptedFile:
			to.add("{ inputEncryptedFile");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  access_hash: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputEncryptedFileBigUploaded:
			to.add("{ inputEncryptedFileBigUploaded");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  parts: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  key_fingerprint: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_contacts_found:
			to.add("{ contacts_found");
			to.add("\n").add(add);
			to.add("  results: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  users: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputFileLocation:
			to.add("{ inputFileLocation");
			to.add("\n").add(add);
			to.add("  volume_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  local_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  secret: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputVideoFileLocation:
			to.add("{ inputVideoFileLocation");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  access_hash: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputEncryptedFileLocation:
			to.add("{ inputEncryptedFileLocation");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  access_hash: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputAudioFileLocation:
			to.add("{ inputAudioFileLocation");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  access_hash: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputDocumentFileLocation:
			to.add("{ inputDocumentFileLocation");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  access_hash: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_chatFull:
			to.add("{ chatFull");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  participants: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  chat_photo: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  notify_settings: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_chatParticipantsForbidden:
			to.add("{ chatParticipantsForbidden");
			to.add("\n").add(add);
			to.add("  chat_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_chatParticipants:
			to.add("{ chatParticipants");
			to.add("\n").add(add);
			to.add("  chat_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  admin_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  participants: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  version: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_msgs_ack:
			to.add("{ msgs_ack");
			to.add("\n").add(add);
			to.add("  msg_ids: "); mtpTextSerializeType(to, from, end, 0, level + 1, mtpc_long); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_userFull:
			to.add("{ userFull");
			to.add("\n").add(add);
			to.add("  user: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  link: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  profile_photo: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  notify_settings: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  blocked: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  real_first_name: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  real_last_name: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_videoEmpty:
			to.add("{ videoEmpty");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_video:
			to.add("{ video");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  access_hash: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  user_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  caption: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  duration: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  mime_type: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  size: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  thumb: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  dc_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  w: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  h: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messageEmpty:
			to.add("{ messageEmpty");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_message:
			to.add("{ message");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  from_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  to_id: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  out: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  unread: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  message: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  media: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messageForwarded:
			to.add("{ messageForwarded");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  fwd_from_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  fwd_date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  from_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  to_id: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  out: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  unread: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  message: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  media: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messageService:
			to.add("{ messageService");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  from_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  to_id: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  out: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  unread: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  action: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_notifyPeer:
			to.add("{ notifyPeer");
			to.add("\n").add(add);
			to.add("  peer: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_notifyUsers:
			to.add("{ notifyUsers");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_notifyChats:
			to.add("{ notifyChats");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_notifyAll:
			to.add("{ notifyAll");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_messages_messageEmpty:
			to.add("{ messages_messageEmpty");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_messages_message:
			to.add("{ messages_message");
			to.add("\n").add(add);
			to.add("  message: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  chats: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  users: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputPhoneContact:
			to.add("{ inputPhoneContact");
			to.add("\n").add(add);
			to.add("  client_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  phone: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  first_name: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  last_name: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_rpc_answer_unknown:
			to.add("{ rpc_answer_unknown");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_rpc_answer_dropped_running:
			to.add("{ rpc_answer_dropped_running");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_rpc_answer_dropped:
			to.add("{ rpc_answer_dropped");
			to.add("\n").add(add);
			to.add("  msg_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  seq_no: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  bytes: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputVideoEmpty:
			to.add("{ inputVideoEmpty");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_inputVideo:
			to.add("{ inputVideo");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  access_hash: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_decryptedMessageMediaEmpty:
			to.add("{ decryptedMessageMediaEmpty");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_decryptedMessageMediaPhoto:
			to.add("{ decryptedMessageMediaPhoto");
			to.add("\n").add(add);
			to.add("  thumb: "); mtpTextSerializeType(to, from, end, mtpc_bytes, level + 1); to.add(",\n").add(add);
			to.add("  thumb_w: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  thumb_h: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  w: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  h: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  size: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  key: "); mtpTextSerializeType(to, from, end, mtpc_bytes, level + 1); to.add(",\n").add(add);
			to.add("  iv: "); mtpTextSerializeType(to, from, end, mtpc_bytes, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_decryptedMessageMediaVideo:
			to.add("{ decryptedMessageMediaVideo");
			to.add("\n").add(add);
			to.add("  thumb: "); mtpTextSerializeType(to, from, end, mtpc_bytes, level + 1); to.add(",\n").add(add);
			to.add("  thumb_w: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  thumb_h: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  duration: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  mime_type: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  w: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  h: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  size: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  key: "); mtpTextSerializeType(to, from, end, mtpc_bytes, level + 1); to.add(",\n").add(add);
			to.add("  iv: "); mtpTextSerializeType(to, from, end, mtpc_bytes, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_decryptedMessageMediaGeoPoint:
			to.add("{ decryptedMessageMediaGeoPoint");
			to.add("\n").add(add);
			to.add("  lat: "); mtpTextSerializeType(to, from, end, mtpc_double, level + 1); to.add(",\n").add(add);
			to.add("  long: "); mtpTextSerializeType(to, from, end, mtpc_double, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_decryptedMessageMediaContact:
			to.add("{ decryptedMessageMediaContact");
			to.add("\n").add(add);
			to.add("  phone_number: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  first_name: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  last_name: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  user_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_decryptedMessageMediaDocument:
			to.add("{ decryptedMessageMediaDocument");
			to.add("\n").add(add);
			to.add("  thumb: "); mtpTextSerializeType(to, from, end, mtpc_bytes, level + 1); to.add(",\n").add(add);
			to.add("  thumb_w: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  thumb_h: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  file_name: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  mime_type: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  size: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  key: "); mtpTextSerializeType(to, from, end, mtpc_bytes, level + 1); to.add(",\n").add(add);
			to.add("  iv: "); mtpTextSerializeType(to, from, end, mtpc_bytes, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_decryptedMessageMediaAudio:
			to.add("{ decryptedMessageMediaAudio");
			to.add("\n").add(add);
			to.add("  duration: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  mime_type: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  size: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  key: "); mtpTextSerializeType(to, from, end, mtpc_bytes, level + 1); to.add(",\n").add(add);
			to.add("  iv: "); mtpTextSerializeType(to, from, end, mtpc_bytes, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_geoChatMessageEmpty:
			to.add("{ geoChatMessageEmpty");
			to.add("\n").add(add);
			to.add("  chat_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_geoChatMessage:
			to.add("{ geoChatMessage");
			to.add("\n").add(add);
			to.add("  chat_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  from_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  message: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  media: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_geoChatMessageService:
			to.add("{ geoChatMessageService");
			to.add("\n").add(add);
			to.add("  chat_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  from_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  action: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_geoPointEmpty:
			to.add("{ geoPointEmpty");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_geoPoint:
			to.add("{ geoPoint");
			to.add("\n").add(add);
			to.add("  long: "); mtpTextSerializeType(to, from, end, mtpc_double, level + 1); to.add(",\n").add(add);
			to.add("  lat: "); mtpTextSerializeType(to, from, end, mtpc_double, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_dialogs:
			to.add("{ messages_dialogs");
			to.add("\n").add(add);
			to.add("  dialogs: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  messages: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  chats: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  users: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_dialogsSlice:
			to.add("{ messages_dialogsSlice");
			to.add("\n").add(add);
			to.add("  count: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  dialogs: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  messages: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  chats: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  users: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_dhConfigNotModified:
			to.add("{ messages_dhConfigNotModified");
			to.add("\n").add(add);
			to.add("  random: "); mtpTextSerializeType(to, from, end, mtpc_bytes, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_dhConfig:
			to.add("{ messages_dhConfig");
			to.add("\n").add(add);
			to.add("  g: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  p: "); mtpTextSerializeType(to, from, end, mtpc_bytes, level + 1); to.add(",\n").add(add);
			to.add("  version: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  random: "); mtpTextSerializeType(to, from, end, mtpc_bytes, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_peerUser:
			to.add("{ peerUser");
			to.add("\n").add(add);
			to.add("  user_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_peerChat:
			to.add("{ peerChat");
			to.add("\n").add(add);
			to.add("  chat_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_server_DH_params_fail:
			to.add("{ server_DH_params_fail");
			to.add("\n").add(add);
			to.add("  nonce: "); mtpTextSerializeType(to, from, end, mtpc_int128, level + 1); to.add(",\n").add(add);
			to.add("  server_nonce: "); mtpTextSerializeType(to, from, end, mtpc_int128, level + 1); to.add(",\n").add(add);
			to.add("  new_nonce_hash: "); mtpTextSerializeType(to, from, end, mtpc_int128, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_server_DH_params_ok:
			to.add("{ server_DH_params_ok");
			to.add("\n").add(add);
			to.add("  nonce: "); mtpTextSerializeType(to, from, end, mtpc_int128, level + 1); to.add(",\n").add(add);
			to.add("  server_nonce: "); mtpTextSerializeType(to, from, end, mtpc_int128, level + 1); to.add(",\n").add(add);
			to.add("  encrypted_answer: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputAppEvent:
			to.add("{ inputAppEvent");
			to.add("\n").add(add);
			to.add("  time: "); mtpTextSerializeType(to, from, end, mtpc_double, level + 1); to.add(",\n").add(add);
			to.add("  type: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  peer: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  data: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_photos_photo:
			to.add("{ photos_photo");
			to.add("\n").add(add);
			to.add("  photo: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  users: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_peerNotifyEventsEmpty:
			to.add("{ peerNotifyEventsEmpty");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_peerNotifyEventsAll:
			to.add("{ peerNotifyEventsAll");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_nearestDc:
			to.add("{ nearestDc");
			to.add("\n").add(add);
			to.add("  country: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  this_dc: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  nearest_dc: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_wallPaper:
			to.add("{ wallPaper");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  title: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  sizes: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  color: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_wallPaperSolid:
			to.add("{ wallPaperSolid");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  title: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  bg_color: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  color: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_geochats_messages:
			to.add("{ geochats_messages");
			to.add("\n").add(add);
			to.add("  messages: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  chats: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  users: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_geochats_messagesSlice:
			to.add("{ geochats_messagesSlice");
			to.add("\n").add(add);
			to.add("  count: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  messages: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  chats: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  users: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_contacts_blocked:
			to.add("{ contacts_blocked");
			to.add("\n").add(add);
			to.add("  blocked: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  users: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_contacts_blockedSlice:
			to.add("{ contacts_blockedSlice");
			to.add("\n").add(add);
			to.add("  count: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  blocked: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  users: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_statedMessage:
			to.add("{ messages_statedMessage");
			to.add("\n").add(add);
			to.add("  message: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  chats: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  users: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  pts: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  seq: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_statedMessageLink:
			to.add("{ messages_statedMessageLink");
			to.add("\n").add(add);
			to.add("  message: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  chats: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  users: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  links: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  pts: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  seq: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messageMediaEmpty:
			to.add("{ messageMediaEmpty");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_messageMediaPhoto:
			to.add("{ messageMediaPhoto");
			to.add("\n").add(add);
			to.add("  photo: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messageMediaVideo:
			to.add("{ messageMediaVideo");
			to.add("\n").add(add);
			to.add("  video: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messageMediaGeo:
			to.add("{ messageMediaGeo");
			to.add("\n").add(add);
			to.add("  geo: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messageMediaContact:
			to.add("{ messageMediaContact");
			to.add("\n").add(add);
			to.add("  phone_number: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  first_name: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  last_name: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  user_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messageMediaUnsupported:
			to.add("{ messageMediaUnsupported");
			to.add("\n").add(add);
			to.add("  bytes: "); mtpTextSerializeType(to, from, end, mtpc_bytes, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messageMediaDocument:
			to.add("{ messageMediaDocument");
			to.add("\n").add(add);
			to.add("  document: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messageMediaAudio:
			to.add("{ messageMediaAudio");
			to.add("\n").add(add);
			to.add("  audio: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputGeoChat:
			to.add("{ inputGeoChat");
			to.add("\n").add(add);
			to.add("  chat_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  access_hash: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_help_appUpdate:
			to.add("{ help_appUpdate");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  critical: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  url: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  text: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_help_noAppUpdate:
			to.add("{ help_noAppUpdate");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_updates_differenceEmpty:
			to.add("{ updates_differenceEmpty");
			to.add("\n").add(add);
			to.add("  date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  seq: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_updates_difference:
			to.add("{ updates_difference");
			to.add("\n").add(add);
			to.add("  new_messages: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  new_encrypted_messages: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  other_updates: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  chats: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  users: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  state: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_updates_differenceSlice:
			to.add("{ updates_differenceSlice");
			to.add("\n").add(add);
			to.add("  new_messages: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  new_encrypted_messages: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  other_updates: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  chats: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  users: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  intermediate_state: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_msgs_state_info:
			to.add("{ msgs_state_info");
			to.add("\n").add(add);
			to.add("  req_msg_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  info: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_msgs_state_req:
			to.add("{ msgs_state_req");
			to.add("\n").add(add);
			to.add("  msg_ids: "); mtpTextSerializeType(to, from, end, 0, level + 1, mtpc_long); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_msg_resend_req:
			to.add("{ msg_resend_req");
			to.add("\n").add(add);
			to.add("  msg_ids: "); mtpTextSerializeType(to, from, end, 0, level + 1, mtpc_long); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputDocumentEmpty:
			to.add("{ inputDocumentEmpty");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_inputDocument:
			to.add("{ inputDocument");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  access_hash: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_userStatusEmpty:
			to.add("{ userStatusEmpty");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_userStatusOnline:
			to.add("{ userStatusOnline");
			to.add("\n").add(add);
			to.add("  expires: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_userStatusOffline:
			to.add("{ userStatusOffline");
			to.add("\n").add(add);
			to.add("  was_online: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_photos_photos:
			to.add("{ photos_photos");
			to.add("\n").add(add);
			to.add("  photos: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  users: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_photos_photosSlice:
			to.add("{ photos_photosSlice");
			to.add("\n").add(add);
			to.add("  count: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  photos: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  users: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_decryptedMessage:
			to.add("{ decryptedMessage");
			to.add("\n").add(add);
			to.add("  random_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  random_bytes: "); mtpTextSerializeType(to, from, end, mtpc_bytes, level + 1); to.add(",\n").add(add);
			to.add("  message: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  media: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_decryptedMessageService:
			to.add("{ decryptedMessageService");
			to.add("\n").add(add);
			to.add("  random_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  random_bytes: "); mtpTextSerializeType(to, from, end, mtpc_bytes, level + 1); to.add(",\n").add(add);
			to.add("  action: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_contacts_importedContacts:
			to.add("{ contacts_importedContacts");
			to.add("\n").add(add);
			to.add("  imported: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  retry_contacts: "); mtpTextSerializeType(to, from, end, 0, level + 1, mtpc_long); to.add(",\n").add(add);
			to.add("  users: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_fileLocationUnavailable:
			to.add("{ fileLocationUnavailable");
			to.add("\n").add(add);
			to.add("  volume_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  local_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  secret: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_fileLocation:
			to.add("{ fileLocation");
			to.add("\n").add(add);
			to.add("  dc_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  volume_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  local_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  secret: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_photoSizeEmpty:
			to.add("{ photoSizeEmpty");
			to.add("\n").add(add);
			to.add("  type: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_photoSize:
			to.add("{ photoSize");
			to.add("\n").add(add);
			to.add("  type: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  location: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  w: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  h: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  size: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_photoCachedSize:
			to.add("{ photoCachedSize");
			to.add("\n").add(add);
			to.add("  type: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  location: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  w: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  h: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  bytes: "); mtpTextSerializeType(to, from, end, mtpc_bytes, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_msg_detailed_info:
			to.add("{ msg_detailed_info");
			to.add("\n").add(add);
			to.add("  msg_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  answer_msg_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  bytes: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  status: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_msg_new_detailed_info:
			to.add("{ msg_new_detailed_info");
			to.add("\n").add(add);
			to.add("  answer_msg_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  bytes: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  status: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputChatPhotoEmpty:
			to.add("{ inputChatPhotoEmpty");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_inputChatUploadedPhoto:
			to.add("{ inputChatUploadedPhoto");
			to.add("\n").add(add);
			to.add("  file: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  crop: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputChatPhoto:
			to.add("{ inputChatPhoto");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  crop: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_sentMessage:
			to.add("{ messages_sentMessage");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  pts: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  seq: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_sentMessageLink:
			to.add("{ messages_sentMessageLink");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  pts: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  seq: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  links: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_chatFull:
			to.add("{ messages_chatFull");
			to.add("\n").add(add);
			to.add("  full_chat: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  chats: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  users: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_geochats_statedMessage:
			to.add("{ geochats_statedMessage");
			to.add("\n").add(add);
			to.add("  message: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  chats: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  users: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  seq: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_chatPhotoEmpty:
			to.add("{ chatPhotoEmpty");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_chatPhoto:
			to.add("{ chatPhoto");
			to.add("\n").add(add);
			to.add("  photo_small: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  photo_big: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_encryptedMessage:
			to.add("{ encryptedMessage");
			to.add("\n").add(add);
			to.add("  random_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  chat_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  bytes: "); mtpTextSerializeType(to, from, end, mtpc_bytes, level + 1); to.add(",\n").add(add);
			to.add("  file: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_encryptedMessageService:
			to.add("{ encryptedMessageService");
			to.add("\n").add(add);
			to.add("  random_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  chat_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  bytes: "); mtpTextSerializeType(to, from, end, mtpc_bytes, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_destroy_session_ok:
			to.add("{ destroy_session_ok");
			to.add("\n").add(add);
			to.add("  session_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_destroy_session_none:
			to.add("{ destroy_session_none");
			to.add("\n").add(add);
			to.add("  session_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_http_wait:
			to.add("{ http_wait");
			to.add("\n").add(add);
			to.add("  max_delay: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  wait_after: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  max_wait: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_sentEncryptedMessage:
			to.add("{ messages_sentEncryptedMessage");
			to.add("\n").add(add);
			to.add("  date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_sentEncryptedFile:
			to.add("{ messages_sentEncryptedFile");
			to.add("\n").add(add);
			to.add("  date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  file: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_contacts_myLinkEmpty:
			to.add("{ contacts_myLinkEmpty");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_contacts_myLinkRequested:
			to.add("{ contacts_myLinkRequested");
			to.add("\n").add(add);
			to.add("  contact: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_contacts_myLinkContact:
			to.add("{ contacts_myLinkContact");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_inputEncryptedChat:
			to.add("{ inputEncryptedChat");
			to.add("\n").add(add);
			to.add("  chat_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  access_hash: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_chats:
			to.add("{ messages_chats");
			to.add("\n").add(add);
			to.add("  chats: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  users: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_encryptedChatEmpty:
			to.add("{ encryptedChatEmpty");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_encryptedChatWaiting:
			to.add("{ encryptedChatWaiting");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  access_hash: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  admin_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  participant_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_encryptedChatRequested:
			to.add("{ encryptedChatRequested");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  access_hash: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  admin_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  participant_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  g_a: "); mtpTextSerializeType(to, from, end, mtpc_bytes, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_encryptedChat:
			to.add("{ encryptedChat");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  access_hash: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  admin_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  participant_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  g_a_or_b: "); mtpTextSerializeType(to, from, end, mtpc_bytes, level + 1); to.add(",\n").add(add);
			to.add("  key_fingerprint: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_encryptedChatDiscarded:
			to.add("{ encryptedChatDiscarded");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_messages:
			to.add("{ messages_messages");
			to.add("\n").add(add);
			to.add("  messages: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  chats: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  users: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_messagesSlice:
			to.add("{ messages_messagesSlice");
			to.add("\n").add(add);
			to.add("  count: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  messages: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  chats: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  users: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_auth_checkedPhone:
			to.add("{ auth_checkedPhone");
			to.add("\n").add(add);
			to.add("  phone_registered: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  phone_invited: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_contactSuggested:
			to.add("{ contactSuggested");
			to.add("\n").add(add);
			to.add("  user_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  mutual_contacts: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_contacts_foreignLinkUnknown:
			to.add("{ contacts_foreignLinkUnknown");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_contacts_foreignLinkRequested:
			to.add("{ contacts_foreignLinkRequested");
			to.add("\n").add(add);
			to.add("  has_phone: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_contacts_foreignLinkMutual:
			to.add("{ contacts_foreignLinkMutual");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_inputAudioEmpty:
			to.add("{ inputAudioEmpty");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_inputAudio:
			to.add("{ inputAudio");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  access_hash: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_contacts_contacts:
			to.add("{ contacts_contacts");
			to.add("\n").add(add);
			to.add("  contacts: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  users: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_contacts_contactsNotModified:
			to.add("{ contacts_contactsNotModified");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_chatEmpty:
			to.add("{ chatEmpty");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_chat:
			to.add("{ chat");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  title: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  photo: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  participants_count: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  left: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  version: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_chatForbidden:
			to.add("{ chatForbidden");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  title: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_geoChat:
			to.add("{ geoChat");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  access_hash: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  title: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  address: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  venue: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  geo: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  photo: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  participants_count: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  checked_in: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  version: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_pong:
			to.add("{ pong");
			to.add("\n").add(add);
			to.add("  msg_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  ping_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputPeerNotifyEventsEmpty:
			to.add("{ inputPeerNotifyEventsEmpty");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_inputPeerNotifyEventsAll:
			to.add("{ inputPeerNotifyEventsAll");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_inputPeerNotifySettings:
			to.add("{ inputPeerNotifySettings");
			to.add("\n").add(add);
			to.add("  mute_until: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  sound: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  show_previews: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  events_mask: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_affectedHistory:
			to.add("{ messages_affectedHistory");
			to.add("\n").add(add);
			to.add("  pts: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  seq: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  offset: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputNotifyPeer:
			to.add("{ inputNotifyPeer");
			to.add("\n").add(add);
			to.add("  peer: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputNotifyUsers:
			to.add("{ inputNotifyUsers");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_inputNotifyChats:
			to.add("{ inputNotifyChats");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_inputNotifyAll:
			to.add("{ inputNotifyAll");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_inputNotifyGeoChatPeer:
			to.add("{ inputNotifyGeoChatPeer");
			to.add("\n").add(add);
			to.add("  peer: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_bad_msg_notification:
			to.add("{ bad_msg_notification");
			to.add("\n").add(add);
			to.add("  bad_msg_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  bad_msg_seqno: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  error_code: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_bad_server_salt:
			to.add("{ bad_server_salt");
			to.add("\n").add(add);
			to.add("  bad_msg_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  bad_msg_seqno: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  error_code: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  new_server_salt: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_config:
			to.add("{ config");
			to.add("\n").add(add);
			to.add("  date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  test_mode: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  this_dc: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  dc_options: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  chat_size_max: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  broadcast_size_max: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputGeoPointEmpty:
			to.add("{ inputGeoPointEmpty");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_inputGeoPoint:
			to.add("{ inputGeoPoint");
			to.add("\n").add(add);
			to.add("  lat: "); mtpTextSerializeType(to, from, end, mtpc_double, level + 1); to.add(",\n").add(add);
			to.add("  long: "); mtpTextSerializeType(to, from, end, mtpc_double, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputUserEmpty:
			to.add("{ inputUserEmpty");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_inputUserSelf:
			to.add("{ inputUserSelf");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_inputUserContact:
			to.add("{ inputUserContact");
			to.add("\n").add(add);
			to.add("  user_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_inputUserForeign:
			to.add("{ inputUserForeign");
			to.add("\n").add(add);
			to.add("  user_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  access_hash: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_dialog:
			to.add("{ dialog");
			to.add("\n").add(add);
			to.add("  peer: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  top_message: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  unread_count: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  notify_settings: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_importedContact:
			to.add("{ importedContact");
			to.add("\n").add(add);
			to.add("  user_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  client_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_dcOption:
			to.add("{ dcOption");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  hostname: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  ip_address: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  port: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_updateNewMessage:
			to.add("{ updateNewMessage");
			to.add("\n").add(add);
			to.add("  message: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  pts: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_updateMessageID:
			to.add("{ updateMessageID");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  random_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_updateReadMessages:
			to.add("{ updateReadMessages");
			to.add("\n").add(add);
			to.add("  messages: "); mtpTextSerializeType(to, from, end, 0, level + 1, mtpc_int); to.add(",\n").add(add);
			to.add("  pts: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_updateDeleteMessages:
			to.add("{ updateDeleteMessages");
			to.add("\n").add(add);
			to.add("  messages: "); mtpTextSerializeType(to, from, end, 0, level + 1, mtpc_int); to.add(",\n").add(add);
			to.add("  pts: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_updateRestoreMessages:
			to.add("{ updateRestoreMessages");
			to.add("\n").add(add);
			to.add("  messages: "); mtpTextSerializeType(to, from, end, 0, level + 1, mtpc_int); to.add(",\n").add(add);
			to.add("  pts: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_updateUserTyping:
			to.add("{ updateUserTyping");
			to.add("\n").add(add);
			to.add("  user_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_updateChatUserTyping:
			to.add("{ updateChatUserTyping");
			to.add("\n").add(add);
			to.add("  chat_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  user_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_updateChatParticipants:
			to.add("{ updateChatParticipants");
			to.add("\n").add(add);
			to.add("  participants: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_updateUserStatus:
			to.add("{ updateUserStatus");
			to.add("\n").add(add);
			to.add("  user_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  status: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_updateUserName:
			to.add("{ updateUserName");
			to.add("\n").add(add);
			to.add("  user_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  first_name: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  last_name: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_updateUserPhoto:
			to.add("{ updateUserPhoto");
			to.add("\n").add(add);
			to.add("  user_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  photo: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  previous: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_updateContactRegistered:
			to.add("{ updateContactRegistered");
			to.add("\n").add(add);
			to.add("  user_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_updateContactLink:
			to.add("{ updateContactLink");
			to.add("\n").add(add);
			to.add("  user_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  my_link: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  foreign_link: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_updateActivation:
			to.add("{ updateActivation");
			to.add("\n").add(add);
			to.add("  user_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_updateNewAuthorization:
			to.add("{ updateNewAuthorization");
			to.add("\n").add(add);
			to.add("  auth_key_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  device: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  location: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_updateNewGeoChatMessage:
			to.add("{ updateNewGeoChatMessage");
			to.add("\n").add(add);
			to.add("  message: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_updateNewEncryptedMessage:
			to.add("{ updateNewEncryptedMessage");
			to.add("\n").add(add);
			to.add("  message: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  qts: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_updateEncryptedChatTyping:
			to.add("{ updateEncryptedChatTyping");
			to.add("\n").add(add);
			to.add("  chat_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_updateEncryption:
			to.add("{ updateEncryption");
			to.add("\n").add(add);
			to.add("  chat: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_updateEncryptedMessagesRead:
			to.add("{ updateEncryptedMessagesRead");
			to.add("\n").add(add);
			to.add("  chat_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  max_date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_updateChatParticipantAdd:
			to.add("{ updateChatParticipantAdd");
			to.add("\n").add(add);
			to.add("  chat_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  user_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  inviter_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  version: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_updateChatParticipantDelete:
			to.add("{ updateChatParticipantDelete");
			to.add("\n").add(add);
			to.add("  chat_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  user_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  version: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_updateDcOptions:
			to.add("{ updateDcOptions");
			to.add("\n").add(add);
			to.add("  dc_options: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_updateUserBlocked:
			to.add("{ updateUserBlocked");
			to.add("\n").add(add);
			to.add("  user_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  blocked: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_updateNotifySettings:
			to.add("{ updateNotifySettings");
			to.add("\n").add(add);
			to.add("  peer: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  notify_settings: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_decryptedMessageActionSetMessageTTL:
			to.add("{ decryptedMessageActionSetMessageTTL");
			to.add("\n").add(add);
			to.add("  ttl_seconds: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_decryptedMessageActionReadMessages:
			to.add("{ decryptedMessageActionReadMessages");
			to.add("\n").add(add);
			to.add("  random_ids: "); mtpTextSerializeType(to, from, end, 0, level + 1, mtpc_long); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_decryptedMessageActionDeleteMessages:
			to.add("{ decryptedMessageActionDeleteMessages");
			to.add("\n").add(add);
			to.add("  random_ids: "); mtpTextSerializeType(to, from, end, 0, level + 1, mtpc_long); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_decryptedMessageActionScreenshotMessages:
			to.add("{ decryptedMessageActionScreenshotMessages");
			to.add("\n").add(add);
			to.add("  random_ids: "); mtpTextSerializeType(to, from, end, 0, level + 1, mtpc_long); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_decryptedMessageActionFlushHistory:
			to.add("{ decryptedMessageActionFlushHistory");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_decryptedMessageActionNotifyLayer:
			to.add("{ decryptedMessageActionNotifyLayer");
			to.add("\n").add(add);
			to.add("  layer: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_peerNotifySettingsEmpty:
			to.add("{ peerNotifySettingsEmpty");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_peerNotifySettings:
			to.add("{ peerNotifySettings");
			to.add("\n").add(add);
			to.add("  mute_until: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  sound: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  show_previews: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  events_mask: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_userEmpty:
			to.add("{ userEmpty");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_userSelf:
			to.add("{ userSelf");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  first_name: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  last_name: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  phone: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  photo: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  status: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  inactive: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_userContact:
			to.add("{ userContact");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  first_name: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  last_name: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  access_hash: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  phone: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  photo: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  status: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_userRequest:
			to.add("{ userRequest");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  first_name: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  last_name: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  access_hash: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  phone: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  photo: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  status: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_userForeign:
			to.add("{ userForeign");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  first_name: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  last_name: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  access_hash: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  photo: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  status: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_userDeleted:
			to.add("{ userDeleted");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  first_name: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  last_name: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_contacts_suggested:
			to.add("{ contacts_suggested");
			to.add("\n").add(add);
			to.add("  results: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  users: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_auth_authorization:
			to.add("{ auth_authorization");
			to.add("\n").add(add);
			to.add("  expires: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  user: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_chat:
			to.add("{ messages_chat");
			to.add("\n").add(add);
			to.add("  chat: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  users: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_auth_sentCode:
			to.add("{ auth_sentCode");
			to.add("\n").add(add);
			to.add("  phone_registered: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  phone_code_hash: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  send_call_timeout: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  is_password: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_audioEmpty:
			to.add("{ audioEmpty");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_audio:
			to.add("{ audio");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  access_hash: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  user_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  duration: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  mime_type: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  size: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  dc_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_statedMessages:
			to.add("{ messages_statedMessages");
			to.add("\n").add(add);
			to.add("  messages: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  chats: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  users: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  pts: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  seq: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_statedMessagesLinks:
			to.add("{ messages_statedMessagesLinks");
			to.add("\n").add(add);
			to.add("  messages: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  chats: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  users: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  links: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  pts: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  seq: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_contactBlocked:
			to.add("{ contactBlocked");
			to.add("\n").add(add);
			to.add("  user_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_storage_fileUnknown:
			to.add("{ storage_fileUnknown");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_storage_fileJpeg:
			to.add("{ storage_fileJpeg");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_storage_fileGif:
			to.add("{ storage_fileGif");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_storage_filePng:
			to.add("{ storage_filePng");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_storage_filePdf:
			to.add("{ storage_filePdf");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_storage_fileMp3:
			to.add("{ storage_fileMp3");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_storage_fileMov:
			to.add("{ storage_fileMov");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_storage_filePartial:
			to.add("{ storage_filePartial");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_storage_fileMp4:
			to.add("{ storage_fileMp4");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_storage_fileWebp:
			to.add("{ storage_fileWebp");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_help_inviteText:
			to.add("{ help_inviteText");
			to.add("\n").add(add);
			to.add("  message: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_chatLocated:
			to.add("{ chatLocated");
			to.add("\n").add(add);
			to.add("  chat_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  distance: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_contact:
			to.add("{ contact");
			to.add("\n").add(add);
			to.add("  user_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  mutual: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_decryptedMessageLayer:
			to.add("{ decryptedMessageLayer");
			to.add("\n").add(add);
			to.add("  layer: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  message: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_updates_state:
			to.add("{ updates_state");
			to.add("\n").add(add);
			to.add("  pts: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  qts: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  seq: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  unread_count: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_encryptedFileEmpty:
			to.add("{ encryptedFileEmpty");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_encryptedFile:
			to.add("{ encryptedFile");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  access_hash: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  size: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  dc_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  key_fingerprint: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_msgs_all_info:
			to.add("{ msgs_all_info");
			to.add("\n").add(add);
			to.add("  msg_ids: "); mtpTextSerializeType(to, from, end, 0, level + 1, mtpc_long); to.add(",\n").add(add);
			to.add("  info: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_photos_updateProfilePhoto:
			to.add("{ photos_updateProfilePhoto");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  crop: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_getMessages:
			to.add("{ messages_getMessages");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, 0, level + 1, mtpc_int); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_getHistory:
			to.add("{ messages_getHistory");
			to.add("\n").add(add);
			to.add("  peer: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  offset: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  max_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  limit: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_search:
			to.add("{ messages_search");
			to.add("\n").add(add);
			to.add("  peer: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  q: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  filter: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  min_date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  max_date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  offset: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  max_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  limit: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_set_client_DH_params:
			to.add("{ set_client_DH_params");
			to.add("\n").add(add);
			to.add("  nonce: "); mtpTextSerializeType(to, from, end, mtpc_int128, level + 1); to.add(",\n").add(add);
			to.add("  server_nonce: "); mtpTextSerializeType(to, from, end, mtpc_int128, level + 1); to.add(",\n").add(add);
			to.add("  encrypted_data: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_contacts_getStatuses:
			to.add("{ contacts_getStatuses");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_auth_checkPhone:
			to.add("{ auth_checkPhone");
			to.add("\n").add(add);
			to.add("  phone_number: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_help_getAppUpdate:
			to.add("{ help_getAppUpdate");
			to.add("\n").add(add);
			to.add("  device_model: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  system_version: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  app_version: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  lang_code: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_updates_getDifference:
			to.add("{ updates_getDifference");
			to.add("\n").add(add);
			to.add("  pts: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  qts: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_help_getInviteText:
			to.add("{ help_getInviteText");
			to.add("\n").add(add);
			to.add("  lang_code: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_users_getFullUser:
			to.add("{ users_getFullUser");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_updates_getState:
			to.add("{ updates_getState");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_contacts_getContacts:
			to.add("{ contacts_getContacts");
			to.add("\n").add(add);
			to.add("  hash: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_geochats_checkin:
			to.add("{ geochats_checkin");
			to.add("\n").add(add);
			to.add("  peer: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_geochats_editChatTitle:
			to.add("{ geochats_editChatTitle");
			to.add("\n").add(add);
			to.add("  peer: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  title: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  address: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_geochats_editChatPhoto:
			to.add("{ geochats_editChatPhoto");
			to.add("\n").add(add);
			to.add("  peer: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  photo: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_geochats_sendMessage:
			to.add("{ geochats_sendMessage");
			to.add("\n").add(add);
			to.add("  peer: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  message: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  random_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_geochats_sendMedia:
			to.add("{ geochats_sendMedia");
			to.add("\n").add(add);
			to.add("  peer: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  media: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  random_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_geochats_createGeoChat:
			to.add("{ geochats_createGeoChat");
			to.add("\n").add(add);
			to.add("  title: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  geo_point: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  address: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  venue: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_ping:
			to.add("{ ping");
			to.add("\n").add(add);
			to.add("  ping_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_ping_delay_disconnect:
			to.add("{ ping_delay_disconnect");
			to.add("\n").add(add);
			to.add("  ping_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  disconnect_delay: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_help_getSupport:
			to.add("{ help_getSupport");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_messages_readHistory:
			to.add("{ messages_readHistory");
			to.add("\n").add(add);
			to.add("  peer: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  max_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  offset: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_deleteHistory:
			to.add("{ messages_deleteHistory");
			to.add("\n").add(add);
			to.add("  peer: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  offset: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_deleteMessages:
			to.add("{ messages_deleteMessages");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, 0, level + 1, mtpc_int); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_restoreMessages:
			to.add("{ messages_restoreMessages");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, 0, level + 1, mtpc_int); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_receivedMessages:
			to.add("{ messages_receivedMessages");
			to.add("\n").add(add);
			to.add("  max_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_users_getUsers:
			to.add("{ users_getUsers");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_get_future_salts:
			to.add("{ get_future_salts");
			to.add("\n").add(add);
			to.add("  num: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_photos_getUserPhotos:
			to.add("{ photos_getUserPhotos");
			to.add("\n").add(add);
			to.add("  user_id: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  offset: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  max_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  limit: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_register_saveDeveloperInfo:
			to.add("{ register_saveDeveloperInfo");
			to.add("\n").add(add);
			to.add("  name: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  email: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  phone_number: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  age: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  city: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_auth_sendCall:
			to.add("{ auth_sendCall");
			to.add("\n").add(add);
			to.add("  phone_number: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  phone_code_hash: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_auth_logOut:
			to.add("{ auth_logOut");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_auth_resetAuthorizations:
			to.add("{ auth_resetAuthorizations");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_auth_sendInvites:
			to.add("{ auth_sendInvites");
			to.add("\n").add(add);
			to.add("  phone_numbers: "); mtpTextSerializeType(to, from, end, 0, level + 1, mtpc_string); to.add(",\n").add(add);
			to.add("  message: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_account_registerDevice:
			to.add("{ account_registerDevice");
			to.add("\n").add(add);
			to.add("  token_type: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  token: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  device_model: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  system_version: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  app_version: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  app_sandbox: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  lang_code: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_account_unregisterDevice:
			to.add("{ account_unregisterDevice");
			to.add("\n").add(add);
			to.add("  token_type: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  token: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_account_updateNotifySettings:
			to.add("{ account_updateNotifySettings");
			to.add("\n").add(add);
			to.add("  peer: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  settings: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_account_resetNotifySettings:
			to.add("{ account_resetNotifySettings");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_account_updateStatus:
			to.add("{ account_updateStatus");
			to.add("\n").add(add);
			to.add("  offline: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_contacts_deleteContacts:
			to.add("{ contacts_deleteContacts");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_contacts_block:
			to.add("{ contacts_block");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_contacts_unblock:
			to.add("{ contacts_unblock");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_setTyping:
			to.add("{ messages_setTyping");
			to.add("\n").add(add);
			to.add("  peer: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  typing: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_upload_saveFilePart:
			to.add("{ upload_saveFilePart");
			to.add("\n").add(add);
			to.add("  file_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  file_part: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  bytes: "); mtpTextSerializeType(to, from, end, mtpc_bytes, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_help_saveAppLog:
			to.add("{ help_saveAppLog");
			to.add("\n").add(add);
			to.add("  events: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_geochats_setTyping:
			to.add("{ geochats_setTyping");
			to.add("\n").add(add);
			to.add("  peer: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  typing: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_discardEncryption:
			to.add("{ messages_discardEncryption");
			to.add("\n").add(add);
			to.add("  chat_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_setEncryptedTyping:
			to.add("{ messages_setEncryptedTyping");
			to.add("\n").add(add);
			to.add("  peer: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  typing: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_readEncryptedHistory:
			to.add("{ messages_readEncryptedHistory");
			to.add("\n").add(add);
			to.add("  peer: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  max_date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_upload_saveBigFilePart:
			to.add("{ upload_saveBigFilePart");
			to.add("\n").add(add);
			to.add("  file_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  file_part: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  file_total_parts: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  bytes: "); mtpTextSerializeType(to, from, end, mtpc_bytes, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_req_pq:
			to.add("{ req_pq");
			to.add("\n").add(add);
			to.add("  nonce: "); mtpTextSerializeType(to, from, end, mtpc_int128, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_auth_exportAuthorization:
			to.add("{ auth_exportAuthorization");
			to.add("\n").add(add);
			to.add("  dc_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_contacts_importContacts:
			to.add("{ contacts_importContacts");
			to.add("\n").add(add);
			to.add("  contacts: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  replace: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_rpc_drop_answer:
			to.add("{ rpc_drop_answer");
			to.add("\n").add(add);
			to.add("  req_msg_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_help_getConfig:
			to.add("{ help_getConfig");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_help_getNearestDc:
			to.add("{ help_getNearestDc");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_messages_getDialogs:
			to.add("{ messages_getDialogs");
			to.add("\n").add(add);
			to.add("  offset: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  max_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  limit: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_account_getNotifySettings:
			to.add("{ account_getNotifySettings");
			to.add("\n").add(add);
			to.add("  peer: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_geochats_getLocated:
			to.add("{ geochats_getLocated");
			to.add("\n").add(add);
			to.add("  geo_point: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  radius: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  limit: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_getDhConfig:
			to.add("{ messages_getDhConfig");
			to.add("\n").add(add);
			to.add("  version: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  random_length: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_account_updateProfile:
			to.add("{ account_updateProfile");
			to.add("\n").add(add);
			to.add("  first_name: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  last_name: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_getFullChat:
			to.add("{ messages_getFullChat");
			to.add("\n").add(add);
			to.add("  chat_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_geochats_getFullChat:
			to.add("{ geochats_getFullChat");
			to.add("\n").add(add);
			to.add("  peer: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_req_DH_params:
			to.add("{ req_DH_params");
			to.add("\n").add(add);
			to.add("  nonce: "); mtpTextSerializeType(to, from, end, mtpc_int128, level + 1); to.add(",\n").add(add);
			to.add("  server_nonce: "); mtpTextSerializeType(to, from, end, mtpc_int128, level + 1); to.add(",\n").add(add);
			to.add("  p: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  q: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  public_key_fingerprint: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  encrypted_data: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_contacts_getSuggested:
			to.add("{ contacts_getSuggested");
			to.add("\n").add(add);
			to.add("  limit: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_auth_signUp:
			to.add("{ auth_signUp");
			to.add("\n").add(add);
			to.add("  phone_number: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  phone_code_hash: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  phone_code: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  first_name: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  last_name: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_auth_signIn:
			to.add("{ auth_signIn");
			to.add("\n").add(add);
			to.add("  phone_number: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  phone_code_hash: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  phone_code: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_auth_importAuthorization:
			to.add("{ auth_importAuthorization");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  bytes: "); mtpTextSerializeType(to, from, end, mtpc_bytes, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_upload_getFile:
			to.add("{ upload_getFile");
			to.add("\n").add(add);
			to.add("  location: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  offset: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  limit: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_photos_uploadProfilePhoto:
			to.add("{ photos_uploadProfilePhoto");
			to.add("\n").add(add);
			to.add("  file: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  caption: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  geo_point: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  crop: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_auth_sendCode:
			to.add("{ auth_sendCode");
			to.add("\n").add(add);
			to.add("  phone_number: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  sms_type: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  api_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  api_hash: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  lang_code: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_forwardMessages:
			to.add("{ messages_forwardMessages");
			to.add("\n").add(add);
			to.add("  peer: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, 0, level + 1, mtpc_int); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_sendBroadcast:
			to.add("{ messages_sendBroadcast");
			to.add("\n").add(add);
			to.add("  contacts: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  message: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  media: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_receivedQueue:
			to.add("{ messages_receivedQueue");
			to.add("\n").add(add);
			to.add("  max_qts: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_contacts_search:
			to.add("{ contacts_search");
			to.add("\n").add(add);
			to.add("  q: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  limit: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_sendMessage:
			to.add("{ messages_sendMessage");
			to.add("\n").add(add);
			to.add("  peer: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  message: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  random_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_geochats_getRecents:
			to.add("{ geochats_getRecents");
			to.add("\n").add(add);
			to.add("  offset: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  limit: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_geochats_search:
			to.add("{ geochats_search");
			to.add("\n").add(add);
			to.add("  peer: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  q: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  filter: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  min_date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  max_date: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  offset: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  max_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  limit: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_geochats_getHistory:
			to.add("{ geochats_getHistory");
			to.add("\n").add(add);
			to.add("  peer: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  offset: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  max_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  limit: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_destroy_session:
			to.add("{ destroy_session");
			to.add("\n").add(add);
			to.add("  session_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_account_getWallPapers:
			to.add("{ account_getWallPapers");
			to.add(" ");
			to.add("}");
		break;

		case mtpc_messages_sendEncrypted:
			to.add("{ messages_sendEncrypted");
			to.add("\n").add(add);
			to.add("  peer: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  random_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  data: "); mtpTextSerializeType(to, from, end, mtpc_bytes, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_sendEncryptedFile:
			to.add("{ messages_sendEncryptedFile");
			to.add("\n").add(add);
			to.add("  peer: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  random_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  data: "); mtpTextSerializeType(to, from, end, mtpc_bytes, level + 1); to.add(",\n").add(add);
			to.add("  file: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_sendEncryptedService:
			to.add("{ messages_sendEncryptedService");
			to.add("\n").add(add);
			to.add("  peer: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  random_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  data: "); mtpTextSerializeType(to, from, end, mtpc_bytes, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_contacts_getBlocked:
			to.add("{ contacts_getBlocked");
			to.add("\n").add(add);
			to.add("  offset: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  limit: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_contacts_deleteContact:
			to.add("{ contacts_deleteContact");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_invokeAfterMsg:
			to.add("{ invokeAfterMsg");
			to.add("\n").add(add);
			to.add("  msg_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("  query: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_invokeAfterMsgs:
			to.add("{ invokeAfterMsgs");
			to.add("\n").add(add);
			to.add("  msg_ids: "); mtpTextSerializeType(to, from, end, 0, level + 1, mtpc_long); to.add(",\n").add(add);
			to.add("  query: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_initConnection:
			to.add("{ initConnection");
			to.add("\n").add(add);
			to.add("  api_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  device_model: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  system_version: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  app_version: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  lang_code: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("  query: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_getChats:
			to.add("{ messages_getChats");
			to.add("\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, 0, level + 1, mtpc_int); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_sendMedia:
			to.add("{ messages_sendMedia");
			to.add("\n").add(add);
			to.add("  peer: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  media: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  random_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_editChatTitle:
			to.add("{ messages_editChatTitle");
			to.add("\n").add(add);
			to.add("  chat_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  title: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_editChatPhoto:
			to.add("{ messages_editChatPhoto");
			to.add("\n").add(add);
			to.add("  chat_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  photo: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_addChatUser:
			to.add("{ messages_addChatUser");
			to.add("\n").add(add);
			to.add("  chat_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  user_id: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  fwd_limit: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_deleteChatUser:
			to.add("{ messages_deleteChatUser");
			to.add("\n").add(add);
			to.add("  chat_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  user_id: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_createChat:
			to.add("{ messages_createChat");
			to.add("\n").add(add);
			to.add("  users: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  title: "); mtpTextSerializeType(to, from, end, mtpc_string, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_forwardMessage:
			to.add("{ messages_forwardMessage");
			to.add("\n").add(add);
			to.add("  peer: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  random_id: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_requestEncryption:
			to.add("{ messages_requestEncryption");
			to.add("\n").add(add);
			to.add("  user_id: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  random_id: "); mtpTextSerializeType(to, from, end, mtpc_int, level + 1); to.add(",\n").add(add);
			to.add("  g_a: "); mtpTextSerializeType(to, from, end, mtpc_bytes, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;

		case mtpc_messages_acceptEncryption:
			to.add("{ messages_acceptEncryption");
			to.add("\n").add(add);
			to.add("  peer: "); mtpTextSerializeType(to, from, end, 0, level + 1); to.add(",\n").add(add);
			to.add("  g_b: "); mtpTextSerializeType(to, from, end, mtpc_bytes, level + 1); to.add(",\n").add(add);
			to.add("  key_fingerprint: "); mtpTextSerializeType(to, from, end, mtpc_long, level + 1); to.add(",\n").add(add);
			to.add("}");
		break;


		default:
			mtpTextSerializeCore(to, from, end, cons, level, vcons);
		break;
		}

	} catch (Exception &e) {
		to.add("[ERROR] ");
		to.add("(").add(e.what()).add("), cons: 0x").add(mtpWrapNumber(cons, 16));
		if (vcons) to.add(", vcons: 0x").add(mtpWrapNumber(vcons));
		to.add(", ").add(mb(start, (end - start) * sizeof(mtpPrime)).str());
	}
}

#endif
