/*
Created from '/SourceFiles/mtproto/scheme.tl' by '/SourceFiles/mtproto/generate.py' script

WARNING! All changes made in this file will be lost!

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

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "mtpScheme.h"

#if (defined _DEBUG || defined _WITH_DEBUG)


void mtpTextSerializeType(MTPStringLogger &to, const mtpPrime *&from, const mtpPrime *end, mtpPrime cons, uint32 level, mtpPrime vcons) {
	QVector<mtpTypeId> types, vtypes;
	QVector<int32> stages, flags;
	types.reserve(20); vtypes.reserve(20); stages.reserve(20); flags.reserve(20);
	types.push_back(mtpTypeId(cons)); vtypes.push_back(mtpTypeId(vcons)); stages.push_back(0); flags.push_back(0);

	const mtpPrime *start = from;
	mtpTypeId type = cons, vtype = vcons;
	int32 stage = 0, flag = 0;
	try {
		while (!types.isEmpty()) {
			type = types.back();
			vtype = vtypes.back();
			stage = stages.back();
			flag = flags.back();
			if (!type) {
				if (from >= end) {
					throw Exception("from >= end");
				} else if (stage) {
					throw Exception("unknown type on stage > 0");
				}
				types.back() = type = *from;
				start = ++from;
			}

			int32 lev = level + types.size() - 1;
			switch (type) {
			case mtpc_resPQ:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ resPQ");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  nonce: "); ++stages.back(); types.push_back(mtpc_int128); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  server_nonce: "); ++stages.back(); types.push_back(mtpc_int128); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  pq: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  server_public_key_fingerprints: "); ++stages.back(); types.push_back(0); vtypes.push_back(mtpc_long); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_p_q_inner_data:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ p_q_inner_data");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  pq: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  p: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  q: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  nonce: "); ++stages.back(); types.push_back(mtpc_int128); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  server_nonce: "); ++stages.back(); types.push_back(mtpc_int128); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 5: to.add("  new_nonce: "); ++stages.back(); types.push_back(mtpc_int256); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_server_DH_params_fail:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ server_DH_params_fail");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  nonce: "); ++stages.back(); types.push_back(mtpc_int128); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  server_nonce: "); ++stages.back(); types.push_back(mtpc_int128); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  new_nonce_hash: "); ++stages.back(); types.push_back(mtpc_int128); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_server_DH_params_ok:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ server_DH_params_ok");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  nonce: "); ++stages.back(); types.push_back(mtpc_int128); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  server_nonce: "); ++stages.back(); types.push_back(mtpc_int128); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  encrypted_answer: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_server_DH_inner_data:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ server_DH_inner_data");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  nonce: "); ++stages.back(); types.push_back(mtpc_int128); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  server_nonce: "); ++stages.back(); types.push_back(mtpc_int128); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  g: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  dh_prime: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  g_a: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 5: to.add("  server_time: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_client_DH_inner_data:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ client_DH_inner_data");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  nonce: "); ++stages.back(); types.push_back(mtpc_int128); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  server_nonce: "); ++stages.back(); types.push_back(mtpc_int128); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  retry_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  g_b: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_dh_gen_ok:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ dh_gen_ok");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  nonce: "); ++stages.back(); types.push_back(mtpc_int128); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  server_nonce: "); ++stages.back(); types.push_back(mtpc_int128); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  new_nonce_hash1: "); ++stages.back(); types.push_back(mtpc_int128); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_dh_gen_retry:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ dh_gen_retry");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  nonce: "); ++stages.back(); types.push_back(mtpc_int128); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  server_nonce: "); ++stages.back(); types.push_back(mtpc_int128); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  new_nonce_hash2: "); ++stages.back(); types.push_back(mtpc_int128); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_dh_gen_fail:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ dh_gen_fail");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  nonce: "); ++stages.back(); types.push_back(mtpc_int128); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  server_nonce: "); ++stages.back(); types.push_back(mtpc_int128); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  new_nonce_hash3: "); ++stages.back(); types.push_back(mtpc_int128); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_msgs_ack:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ msgs_ack");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  msg_ids: "); ++stages.back(); types.push_back(0); vtypes.push_back(mtpc_long); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_bad_msg_notification:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ bad_msg_notification");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  bad_msg_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  bad_msg_seqno: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  error_code: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_bad_server_salt:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ bad_server_salt");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  bad_msg_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  bad_msg_seqno: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  error_code: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  new_server_salt: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_msgs_state_req:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ msgs_state_req");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  msg_ids: "); ++stages.back(); types.push_back(0); vtypes.push_back(mtpc_long); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_msgs_state_info:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ msgs_state_info");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  req_msg_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  info: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_msgs_all_info:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ msgs_all_info");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  msg_ids: "); ++stages.back(); types.push_back(0); vtypes.push_back(mtpc_long); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  info: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_msg_detailed_info:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ msg_detailed_info");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  msg_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  answer_msg_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  bytes: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  status: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_msg_new_detailed_info:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ msg_new_detailed_info");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  answer_msg_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  bytes: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  status: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_msg_resend_req:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ msg_resend_req");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  msg_ids: "); ++stages.back(); types.push_back(0); vtypes.push_back(mtpc_long); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_rpc_error:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ rpc_error");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  error_code: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  error_message: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_rpc_answer_unknown:
				to.add("{ rpc_answer_unknown }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_rpc_answer_dropped_running:
				to.add("{ rpc_answer_dropped_running }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_rpc_answer_dropped:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ rpc_answer_dropped");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  msg_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  seq_no: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  bytes: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_future_salt:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ future_salt");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  valid_since: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  valid_until: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  salt: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_future_salts:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ future_salts");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  req_msg_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  now: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  salts: "); ++stages.back(); types.push_back(mtpc_vector); vtypes.push_back(mtpc_future_salt); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_pong:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ pong");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  msg_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  ping_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_destroy_session_ok:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ destroy_session_ok");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  session_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_destroy_session_none:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ destroy_session_none");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  session_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_new_session_created:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ new_session_created");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  first_msg_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  unique_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  server_salt: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_http_wait:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ http_wait");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  max_delay: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  wait_after: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  max_wait: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_error:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ error");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  code: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  text: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_null:
				to.add("{ null }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_inputPeerEmpty:
				to.add("{ inputPeerEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_inputPeerSelf:
				to.add("{ inputPeerSelf }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_inputPeerContact:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputPeerContact");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputPeerForeign:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputPeerForeign");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  access_hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputPeerChat:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputPeerChat");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  chat_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputUserEmpty:
				to.add("{ inputUserEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_inputUserSelf:
				to.add("{ inputUserSelf }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_inputUserContact:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputUserContact");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputUserForeign:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputUserForeign");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  access_hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputPhoneContact:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputPhoneContact");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  client_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  phone: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  first_name: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  last_name: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputFile:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputFile");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  parts: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  name: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  md5_checksum: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputFileBig:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputFileBig");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  parts: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  name: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputMediaEmpty:
				to.add("{ inputMediaEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_inputMediaUploadedPhoto:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputMediaUploadedPhoto");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  file: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  caption: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputMediaPhoto:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputMediaPhoto");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  caption: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputMediaGeoPoint:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputMediaGeoPoint");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  geo_point: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputMediaContact:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputMediaContact");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  phone_number: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  first_name: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  last_name: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputMediaUploadedVideo:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputMediaUploadedVideo");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  file: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  duration: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  w: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  h: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  caption: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputMediaUploadedThumbVideo:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputMediaUploadedThumbVideo");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  file: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  thumb: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  duration: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  w: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  h: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 5: to.add("  caption: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputMediaVideo:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputMediaVideo");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  caption: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputMediaUploadedAudio:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputMediaUploadedAudio");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  file: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  duration: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  mime_type: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputMediaAudio:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputMediaAudio");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputMediaUploadedDocument:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputMediaUploadedDocument");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  file: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  mime_type: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  attributes: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputMediaUploadedThumbDocument:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputMediaUploadedThumbDocument");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  file: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  thumb: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  mime_type: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  attributes: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputMediaDocument:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputMediaDocument");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputMediaVenue:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputMediaVenue");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  geo_point: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  title: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  address: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  provider: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  venue_id: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputChatPhotoEmpty:
				to.add("{ inputChatPhotoEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_inputChatUploadedPhoto:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputChatUploadedPhoto");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  file: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  crop: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputChatPhoto:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputChatPhoto");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  crop: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputGeoPointEmpty:
				to.add("{ inputGeoPointEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_inputGeoPoint:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputGeoPoint");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  lat: "); ++stages.back(); types.push_back(mtpc_double); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  long: "); ++stages.back(); types.push_back(mtpc_double); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputPhotoEmpty:
				to.add("{ inputPhotoEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_inputPhoto:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputPhoto");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  access_hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputVideoEmpty:
				to.add("{ inputVideoEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_inputVideo:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputVideo");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  access_hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputFileLocation:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputFileLocation");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  volume_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  local_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  secret: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputVideoFileLocation:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputVideoFileLocation");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  access_hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputEncryptedFileLocation:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputEncryptedFileLocation");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  access_hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputAudioFileLocation:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputAudioFileLocation");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  access_hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputDocumentFileLocation:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputDocumentFileLocation");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  access_hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputPhotoCropAuto:
				to.add("{ inputPhotoCropAuto }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_inputPhotoCrop:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputPhotoCrop");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  crop_left: "); ++stages.back(); types.push_back(mtpc_double); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  crop_top: "); ++stages.back(); types.push_back(mtpc_double); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  crop_width: "); ++stages.back(); types.push_back(mtpc_double); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputAppEvent:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputAppEvent");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  time: "); ++stages.back(); types.push_back(mtpc_double); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  type: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  peer: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  data: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_peerUser:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ peerUser");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_peerChat:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ peerChat");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  chat_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_storage_fileUnknown:
				to.add("{ storage_fileUnknown }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_storage_fileJpeg:
				to.add("{ storage_fileJpeg }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_storage_fileGif:
				to.add("{ storage_fileGif }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_storage_filePng:
				to.add("{ storage_filePng }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_storage_filePdf:
				to.add("{ storage_filePdf }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_storage_fileMp3:
				to.add("{ storage_fileMp3 }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_storage_fileMov:
				to.add("{ storage_fileMov }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_storage_filePartial:
				to.add("{ storage_filePartial }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_storage_fileMp4:
				to.add("{ storage_fileMp4 }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_storage_fileWebp:
				to.add("{ storage_fileWebp }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_fileLocationUnavailable:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ fileLocationUnavailable");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  volume_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  local_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  secret: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_fileLocation:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ fileLocation");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  dc_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  volume_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  local_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  secret: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_userEmpty:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ userEmpty");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_userSelf:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ userSelf");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  first_name: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  last_name: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  username: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  phone: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 5: to.add("  photo: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 6: to.add("  status: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_userContact:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ userContact");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  first_name: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  last_name: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  username: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  access_hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 5: to.add("  phone: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 6: to.add("  photo: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 7: to.add("  status: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_userRequest:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ userRequest");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  first_name: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  last_name: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  username: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  access_hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 5: to.add("  phone: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 6: to.add("  photo: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 7: to.add("  status: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_userForeign:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ userForeign");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  first_name: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  last_name: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  username: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  access_hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 5: to.add("  photo: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 6: to.add("  status: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_userDeleted:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ userDeleted");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  first_name: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  last_name: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  username: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_userProfilePhotoEmpty:
				to.add("{ userProfilePhotoEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_userProfilePhoto:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ userProfilePhoto");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  photo_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  photo_small: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  photo_big: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_userStatusEmpty:
				to.add("{ userStatusEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_userStatusOnline:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ userStatusOnline");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  expires: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_userStatusOffline:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ userStatusOffline");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  was_online: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_userStatusRecently:
				to.add("{ userStatusRecently }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_userStatusLastWeek:
				to.add("{ userStatusLastWeek }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_userStatusLastMonth:
				to.add("{ userStatusLastMonth }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_chatEmpty:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ chatEmpty");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_chat:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ chat");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  title: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  photo: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  participants_count: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 5: to.add("  left: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 6: to.add("  version: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_chatForbidden:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ chatForbidden");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  title: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_geoChat:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ geoChat");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  access_hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  title: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  address: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  venue: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 5: to.add("  geo: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 6: to.add("  photo: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 7: to.add("  participants_count: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 8: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 9: to.add("  checked_in: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 10: to.add("  version: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_chatFull:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ chatFull");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  participants: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  chat_photo: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  notify_settings: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  exported_invite: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_chatParticipant:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ chatParticipant");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  inviter_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_chatParticipantsForbidden:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ chatParticipantsForbidden");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  chat_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_chatParticipants:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ chatParticipants");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  chat_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  admin_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  participants: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  version: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_chatPhotoEmpty:
				to.add("{ chatPhotoEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_chatPhoto:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ chatPhoto");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  photo_small: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  photo_big: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messageEmpty:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messageEmpty");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_message:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ message");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  from_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  to_id: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  fwd_from_id: "); ++stages.back(); if (flag & MTPDmessage::flag_fwd_from_id) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 2 IN FIELD flags ]"); } break;
				case 5: to.add("  fwd_date: "); ++stages.back(); if (flag & MTPDmessage::flag_fwd_date) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 2 IN FIELD flags ]"); } break;
				case 6: to.add("  reply_to_msg_id: "); ++stages.back(); if (flag & MTPDmessage::flag_reply_to_msg_id) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 3 IN FIELD flags ]"); } break;
				case 7: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 8: to.add("  message: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 9: to.add("  media: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messageService:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messageService");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  flags: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  from_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  to_id: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 5: to.add("  action: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messageMediaEmpty:
				to.add("{ messageMediaEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_messageMediaPhoto:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messageMediaPhoto");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  photo: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  caption: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messageMediaVideo:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messageMediaVideo");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  video: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  caption: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messageMediaGeo:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messageMediaGeo");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  geo: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messageMediaContact:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messageMediaContact");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  phone_number: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  first_name: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  last_name: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messageMediaUnsupported:
				to.add("{ messageMediaUnsupported }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_messageMediaDocument:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messageMediaDocument");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  document: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messageMediaAudio:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messageMediaAudio");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  audio: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messageMediaWebPage:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messageMediaWebPage");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  webpage: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messageMediaVenue:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messageMediaVenue");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  geo: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  title: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  address: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  provider: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  venue_id: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messageActionEmpty:
				to.add("{ messageActionEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_messageActionChatCreate:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messageActionChatCreate");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  title: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  users: "); ++stages.back(); types.push_back(0); vtypes.push_back(mtpc_int); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messageActionChatEditTitle:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messageActionChatEditTitle");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  title: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messageActionChatEditPhoto:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messageActionChatEditPhoto");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  photo: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messageActionChatDeletePhoto:
				to.add("{ messageActionChatDeletePhoto }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_messageActionChatAddUser:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messageActionChatAddUser");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messageActionChatDeleteUser:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messageActionChatDeleteUser");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messageActionGeoChatCreate:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messageActionGeoChatCreate");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  title: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  address: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messageActionGeoChatCheckin:
				to.add("{ messageActionGeoChatCheckin }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_messageActionChatJoinedByLink:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messageActionChatJoinedByLink");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  inviter_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_dialog:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ dialog");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  top_message: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  read_inbox_max_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  unread_count: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  notify_settings: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_photoEmpty:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ photoEmpty");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_photo:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ photo");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  access_hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  geo: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 5: to.add("  sizes: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_photoSizeEmpty:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ photoSizeEmpty");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  type: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_photoSize:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ photoSize");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  type: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  location: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  w: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  h: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  size: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_photoCachedSize:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ photoCachedSize");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  type: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  location: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  w: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  h: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  bytes: "); ++stages.back(); types.push_back(mtpc_bytes); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_videoEmpty:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ videoEmpty");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_video:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ video");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  access_hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  duration: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 5: to.add("  size: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 6: to.add("  thumb: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 7: to.add("  dc_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 8: to.add("  w: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 9: to.add("  h: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_geoPointEmpty:
				to.add("{ geoPointEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_geoPoint:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ geoPoint");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  long: "); ++stages.back(); types.push_back(mtpc_double); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  lat: "); ++stages.back(); types.push_back(mtpc_double); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_auth_checkedPhone:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ auth_checkedPhone");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  phone_registered: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_auth_sentCode:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ auth_sentCode");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  phone_registered: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  phone_code_hash: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  send_call_timeout: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  is_password: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_auth_sentAppCode:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ auth_sentAppCode");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  phone_registered: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  phone_code_hash: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  send_call_timeout: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  is_password: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_auth_authorization:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ auth_authorization");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  expires: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  user: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_auth_exportedAuthorization:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ auth_exportedAuthorization");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  bytes: "); ++stages.back(); types.push_back(mtpc_bytes); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputNotifyPeer:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputNotifyPeer");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputNotifyUsers:
				to.add("{ inputNotifyUsers }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_inputNotifyChats:
				to.add("{ inputNotifyChats }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_inputNotifyAll:
				to.add("{ inputNotifyAll }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_inputNotifyGeoChatPeer:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputNotifyGeoChatPeer");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputPeerNotifyEventsEmpty:
				to.add("{ inputPeerNotifyEventsEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_inputPeerNotifyEventsAll:
				to.add("{ inputPeerNotifyEventsAll }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_inputPeerNotifySettings:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputPeerNotifySettings");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  mute_until: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  sound: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  show_previews: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  events_mask: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_peerNotifyEventsEmpty:
				to.add("{ peerNotifyEventsEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_peerNotifyEventsAll:
				to.add("{ peerNotifyEventsAll }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_peerNotifySettingsEmpty:
				to.add("{ peerNotifySettingsEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_peerNotifySettings:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ peerNotifySettings");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  mute_until: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  sound: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  show_previews: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  events_mask: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_wallPaper:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ wallPaper");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  title: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  sizes: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  color: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_wallPaperSolid:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ wallPaperSolid");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  title: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  bg_color: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  color: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_userFull:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ userFull");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  user: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  link: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  profile_photo: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  notify_settings: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  blocked: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 5: to.add("  real_first_name: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 6: to.add("  real_last_name: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_contact:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ contact");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  mutual: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_importedContact:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ importedContact");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  client_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_contactBlocked:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ contactBlocked");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_contactSuggested:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ contactSuggested");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  mutual_contacts: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_contactStatus:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ contactStatus");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  status: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_chatLocated:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ chatLocated");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  chat_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  distance: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_contacts_link:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ contacts_link");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  my_link: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  foreign_link: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  user: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_contacts_contactsNotModified:
				to.add("{ contacts_contactsNotModified }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_contacts_contacts:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ contacts_contacts");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  contacts: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  users: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_contacts_importedContacts:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ contacts_importedContacts");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  imported: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  retry_contacts: "); ++stages.back(); types.push_back(0); vtypes.push_back(mtpc_long); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  users: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_contacts_blocked:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ contacts_blocked");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  blocked: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  users: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_contacts_blockedSlice:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ contacts_blockedSlice");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  count: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  blocked: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  users: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_contacts_suggested:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ contacts_suggested");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  results: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  users: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_dialogs:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_dialogs");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  dialogs: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  messages: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  chats: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  users: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_dialogsSlice:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_dialogsSlice");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  count: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  dialogs: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  messages: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  chats: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  users: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_messages:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_messages");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  messages: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  chats: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  users: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_messagesSlice:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_messagesSlice");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  count: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  messages: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  chats: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  users: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_messageEmpty:
				to.add("{ messages_messageEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_messages_sentMessage:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_sentMessage");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  media: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  pts: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  pts_count: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_sentMessageLink:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_sentMessageLink");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  media: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  pts: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  pts_count: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 5: to.add("  links: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 6: to.add("  seq: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_chats:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_chats");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  chats: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_chatFull:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_chatFull");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  full_chat: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  chats: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  users: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_affectedHistory:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_affectedHistory");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  pts: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  pts_count: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  offset: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputMessagesFilterEmpty:
				to.add("{ inputMessagesFilterEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_inputMessagesFilterPhotos:
				to.add("{ inputMessagesFilterPhotos }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_inputMessagesFilterVideo:
				to.add("{ inputMessagesFilterVideo }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_inputMessagesFilterPhotoVideo:
				to.add("{ inputMessagesFilterPhotoVideo }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_inputMessagesFilterPhotoVideoDocuments:
				to.add("{ inputMessagesFilterPhotoVideoDocuments }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_inputMessagesFilterDocument:
				to.add("{ inputMessagesFilterDocument }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_inputMessagesFilterAudio:
				to.add("{ inputMessagesFilterAudio }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_updateNewMessage:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updateNewMessage");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  message: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  pts: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  pts_count: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updateMessageID:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updateMessageID");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  random_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updateDeleteMessages:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updateDeleteMessages");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  messages: "); ++stages.back(); types.push_back(0); vtypes.push_back(mtpc_int); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  pts: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  pts_count: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updateUserTyping:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updateUserTyping");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  action: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updateChatUserTyping:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updateChatUserTyping");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  chat_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  action: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updateChatParticipants:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updateChatParticipants");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  participants: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updateUserStatus:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updateUserStatus");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  status: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updateUserName:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updateUserName");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  first_name: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  last_name: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  username: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updateUserPhoto:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updateUserPhoto");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  photo: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  previous: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updateContactRegistered:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updateContactRegistered");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updateContactLink:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updateContactLink");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  my_link: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  foreign_link: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updateNewAuthorization:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updateNewAuthorization");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  auth_key_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  device: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  location: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updateNewGeoChatMessage:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updateNewGeoChatMessage");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  message: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updateNewEncryptedMessage:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updateNewEncryptedMessage");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  message: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  qts: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updateEncryptedChatTyping:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updateEncryptedChatTyping");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  chat_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updateEncryption:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updateEncryption");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  chat: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updateEncryptedMessagesRead:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updateEncryptedMessagesRead");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  chat_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  max_date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updateChatParticipantAdd:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updateChatParticipantAdd");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  chat_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  inviter_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  version: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updateChatParticipantDelete:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updateChatParticipantDelete");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  chat_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  version: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updateDcOptions:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updateDcOptions");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  dc_options: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updateUserBlocked:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updateUserBlocked");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  blocked: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updateNotifySettings:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updateNotifySettings");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  notify_settings: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updateServiceNotification:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updateServiceNotification");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  type: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  message: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  media: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  popup: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updatePrivacy:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updatePrivacy");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  key: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  rules: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updateUserPhone:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updateUserPhone");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  phone: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updateReadHistoryInbox:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updateReadHistoryInbox");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  max_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  pts: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  pts_count: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updateReadHistoryOutbox:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updateReadHistoryOutbox");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  max_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  pts: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  pts_count: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updateWebPage:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updateWebPage");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  webpage: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updateReadMessagesContents:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updateReadMessagesContents");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  messages: "); ++stages.back(); types.push_back(0); vtypes.push_back(mtpc_int); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  pts: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  pts_count: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updates_state:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updates_state");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  pts: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  qts: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  seq: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  unread_count: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updates_differenceEmpty:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updates_differenceEmpty");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  seq: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updates_difference:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updates_difference");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  new_messages: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  new_encrypted_messages: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  other_updates: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  chats: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  users: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 5: to.add("  state: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updates_differenceSlice:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updates_differenceSlice");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  new_messages: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  new_encrypted_messages: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  other_updates: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  chats: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  users: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 5: to.add("  intermediate_state: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updatesTooLong:
				to.add("{ updatesTooLong }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_updateShortMessage:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updateShortMessage");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  message: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  pts: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 5: to.add("  pts_count: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 6: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 7: to.add("  fwd_from_id: "); ++stages.back(); if (flag & MTPDupdateShortMessage::flag_fwd_from_id) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 2 IN FIELD flags ]"); } break;
				case 8: to.add("  fwd_date: "); ++stages.back(); if (flag & MTPDupdateShortMessage::flag_fwd_date) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 2 IN FIELD flags ]"); } break;
				case 9: to.add("  reply_to_msg_id: "); ++stages.back(); if (flag & MTPDupdateShortMessage::flag_reply_to_msg_id) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 3 IN FIELD flags ]"); } break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updateShortChatMessage:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updateShortChatMessage");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  from_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  chat_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  message: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 5: to.add("  pts: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 6: to.add("  pts_count: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 7: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 8: to.add("  fwd_from_id: "); ++stages.back(); if (flag & MTPDupdateShortChatMessage::flag_fwd_from_id) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 2 IN FIELD flags ]"); } break;
				case 9: to.add("  fwd_date: "); ++stages.back(); if (flag & MTPDupdateShortChatMessage::flag_fwd_date) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 2 IN FIELD flags ]"); } break;
				case 10: to.add("  reply_to_msg_id: "); ++stages.back(); if (flag & MTPDupdateShortChatMessage::flag_reply_to_msg_id) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 3 IN FIELD flags ]"); } break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updateShort:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updateShort");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  update: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updatesCombined:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updatesCombined");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  updates: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  users: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  chats: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  seq_start: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 5: to.add("  seq: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updates:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updates");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  updates: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  users: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  chats: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  seq: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_photos_photos:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ photos_photos");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  photos: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  users: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_photos_photosSlice:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ photos_photosSlice");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  count: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  photos: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  users: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_photos_photo:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ photos_photo");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  photo: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  users: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_upload_file:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ upload_file");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  type: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  mtime: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  bytes: "); ++stages.back(); types.push_back(mtpc_bytes); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_dcOption:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ dcOption");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  hostname: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  ip_address: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  port: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_config:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ config");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  expires: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  test_mode: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  this_dc: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  dc_options: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 5: to.add("  chat_size_max: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 6: to.add("  broadcast_size_max: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 7: to.add("  forwarded_count_max: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 8: to.add("  online_update_period_ms: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 9: to.add("  offline_blur_timeout_ms: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 10: to.add("  offline_idle_timeout_ms: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 11: to.add("  online_cloud_timeout_ms: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 12: to.add("  notify_cloud_delay_ms: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 13: to.add("  notify_default_delay_ms: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 14: to.add("  chat_big_size: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 15: to.add("  push_chat_period_ms: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 16: to.add("  push_chat_limit: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 17: to.add("  disabled_features: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_nearestDc:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ nearestDc");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  country: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  this_dc: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  nearest_dc: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_help_appUpdate:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ help_appUpdate");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  critical: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  url: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  text: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_help_noAppUpdate:
				to.add("{ help_noAppUpdate }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_help_inviteText:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ help_inviteText");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  message: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputGeoChat:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputGeoChat");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  chat_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  access_hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_geoChatMessageEmpty:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ geoChatMessageEmpty");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  chat_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_geoChatMessage:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ geoChatMessage");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  chat_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  from_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  message: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 5: to.add("  media: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_geoChatMessageService:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ geoChatMessageService");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  chat_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  from_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  action: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_geochats_statedMessage:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ geochats_statedMessage");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  message: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  chats: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  users: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  seq: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_geochats_located:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ geochats_located");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  results: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  messages: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  chats: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  users: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_geochats_messages:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ geochats_messages");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  messages: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  chats: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  users: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_geochats_messagesSlice:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ geochats_messagesSlice");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  count: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  messages: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  chats: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  users: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_encryptedChatEmpty:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ encryptedChatEmpty");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_encryptedChatWaiting:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ encryptedChatWaiting");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  access_hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  admin_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  participant_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_encryptedChatRequested:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ encryptedChatRequested");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  access_hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  admin_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  participant_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 5: to.add("  g_a: "); ++stages.back(); types.push_back(mtpc_bytes); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_encryptedChat:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ encryptedChat");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  access_hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  admin_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  participant_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 5: to.add("  g_a_or_b: "); ++stages.back(); types.push_back(mtpc_bytes); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 6: to.add("  key_fingerprint: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_encryptedChatDiscarded:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ encryptedChatDiscarded");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputEncryptedChat:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputEncryptedChat");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  chat_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  access_hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_encryptedFileEmpty:
				to.add("{ encryptedFileEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_encryptedFile:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ encryptedFile");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  access_hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  size: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  dc_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  key_fingerprint: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputEncryptedFileEmpty:
				to.add("{ inputEncryptedFileEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_inputEncryptedFileUploaded:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputEncryptedFileUploaded");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  parts: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  md5_checksum: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  key_fingerprint: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputEncryptedFile:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputEncryptedFile");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  access_hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputEncryptedFileBigUploaded:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputEncryptedFileBigUploaded");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  parts: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  key_fingerprint: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_encryptedMessage:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ encryptedMessage");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  random_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  chat_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  bytes: "); ++stages.back(); types.push_back(mtpc_bytes); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  file: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_encryptedMessageService:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ encryptedMessageService");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  random_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  chat_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  bytes: "); ++stages.back(); types.push_back(mtpc_bytes); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_dhConfigNotModified:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_dhConfigNotModified");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  random: "); ++stages.back(); types.push_back(mtpc_bytes); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_dhConfig:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_dhConfig");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  g: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  p: "); ++stages.back(); types.push_back(mtpc_bytes); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  version: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  random: "); ++stages.back(); types.push_back(mtpc_bytes); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_sentEncryptedMessage:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_sentEncryptedMessage");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_sentEncryptedFile:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_sentEncryptedFile");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  file: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputAudioEmpty:
				to.add("{ inputAudioEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_inputAudio:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputAudio");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  access_hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputDocumentEmpty:
				to.add("{ inputDocumentEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_inputDocument:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputDocument");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  access_hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_audioEmpty:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ audioEmpty");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_audio:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ audio");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  access_hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  duration: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 5: to.add("  mime_type: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 6: to.add("  size: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 7: to.add("  dc_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_documentEmpty:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ documentEmpty");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_document:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ document");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  access_hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  mime_type: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  size: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 5: to.add("  thumb: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 6: to.add("  dc_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 7: to.add("  attributes: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_help_support:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ help_support");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  phone_number: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  user: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_notifyPeer:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ notifyPeer");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_notifyUsers:
				to.add("{ notifyUsers }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_notifyChats:
				to.add("{ notifyChats }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_notifyAll:
				to.add("{ notifyAll }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_sendMessageTypingAction:
				to.add("{ sendMessageTypingAction }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_sendMessageCancelAction:
				to.add("{ sendMessageCancelAction }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_sendMessageRecordVideoAction:
				to.add("{ sendMessageRecordVideoAction }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_sendMessageUploadVideoAction:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ sendMessageUploadVideoAction");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  progress: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_sendMessageRecordAudioAction:
				to.add("{ sendMessageRecordAudioAction }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_sendMessageUploadAudioAction:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ sendMessageUploadAudioAction");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  progress: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_sendMessageUploadPhotoAction:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ sendMessageUploadPhotoAction");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  progress: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_sendMessageUploadDocumentAction:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ sendMessageUploadDocumentAction");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  progress: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_sendMessageGeoLocationAction:
				to.add("{ sendMessageGeoLocationAction }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_sendMessageChooseContactAction:
				to.add("{ sendMessageChooseContactAction }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_contactFound:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ contactFound");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_contacts_found:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ contacts_found");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  results: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  users: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputPrivacyKeyStatusTimestamp:
				to.add("{ inputPrivacyKeyStatusTimestamp }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_privacyKeyStatusTimestamp:
				to.add("{ privacyKeyStatusTimestamp }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_inputPrivacyValueAllowContacts:
				to.add("{ inputPrivacyValueAllowContacts }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_inputPrivacyValueAllowAll:
				to.add("{ inputPrivacyValueAllowAll }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_inputPrivacyValueAllowUsers:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputPrivacyValueAllowUsers");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  users: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_inputPrivacyValueDisallowContacts:
				to.add("{ inputPrivacyValueDisallowContacts }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_inputPrivacyValueDisallowAll:
				to.add("{ inputPrivacyValueDisallowAll }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_inputPrivacyValueDisallowUsers:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ inputPrivacyValueDisallowUsers");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  users: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_privacyValueAllowContacts:
				to.add("{ privacyValueAllowContacts }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_privacyValueAllowAll:
				to.add("{ privacyValueAllowAll }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_privacyValueAllowUsers:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ privacyValueAllowUsers");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  users: "); ++stages.back(); types.push_back(0); vtypes.push_back(mtpc_int); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_privacyValueDisallowContacts:
				to.add("{ privacyValueDisallowContacts }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_privacyValueDisallowAll:
				to.add("{ privacyValueDisallowAll }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_privacyValueDisallowUsers:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ privacyValueDisallowUsers");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  users: "); ++stages.back(); types.push_back(0); vtypes.push_back(mtpc_int); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_account_privacyRules:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ account_privacyRules");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  rules: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  users: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_accountDaysTTL:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ accountDaysTTL");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  days: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_account_sentChangePhoneCode:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ account_sentChangePhoneCode");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  phone_code_hash: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  send_call_timeout: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_documentAttributeImageSize:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ documentAttributeImageSize");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  w: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  h: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_documentAttributeAnimated:
				to.add("{ documentAttributeAnimated }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_documentAttributeSticker:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ documentAttributeSticker");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  alt: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_documentAttributeVideo:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ documentAttributeVideo");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  duration: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  w: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  h: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_documentAttributeAudio:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ documentAttributeAudio");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  duration: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_documentAttributeFilename:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ documentAttributeFilename");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  file_name: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_stickersNotModified:
				to.add("{ messages_stickersNotModified }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_messages_stickers:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_stickers");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  hash: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  stickers: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_stickerPack:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ stickerPack");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  emoticon: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  documents: "); ++stages.back(); types.push_back(0); vtypes.push_back(mtpc_long); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_allStickersNotModified:
				to.add("{ messages_allStickersNotModified }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_messages_allStickers:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_allStickers");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  hash: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  packs: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  documents: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_disabledFeature:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ disabledFeature");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  feature: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  description: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_affectedMessages:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_affectedMessages");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  pts: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  pts_count: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_contactLinkUnknown:
				to.add("{ contactLinkUnknown }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_contactLinkNone:
				to.add("{ contactLinkNone }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_contactLinkHasPhone:
				to.add("{ contactLinkHasPhone }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_contactLinkContact:
				to.add("{ contactLinkContact }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_webPageEmpty:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ webPageEmpty");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_webPagePending:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ webPagePending");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_webPage:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ webPage");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  url: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  display_url: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  type: "); ++stages.back(); if (flag & MTPDwebPage::flag_type) { types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 0 IN FIELD flags ]"); } break;
				case 5: to.add("  site_name: "); ++stages.back(); if (flag & MTPDwebPage::flag_site_name) { types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 1 IN FIELD flags ]"); } break;
				case 6: to.add("  title: "); ++stages.back(); if (flag & MTPDwebPage::flag_title) { types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 2 IN FIELD flags ]"); } break;
				case 7: to.add("  description: "); ++stages.back(); if (flag & MTPDwebPage::flag_description) { types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 3 IN FIELD flags ]"); } break;
				case 8: to.add("  photo: "); ++stages.back(); if (flag & MTPDwebPage::flag_photo) { types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 4 IN FIELD flags ]"); } break;
				case 9: to.add("  embed_url: "); ++stages.back(); if (flag & MTPDwebPage::flag_embed_url) { types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 5 IN FIELD flags ]"); } break;
				case 10: to.add("  embed_type: "); ++stages.back(); if (flag & MTPDwebPage::flag_embed_type) { types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 5 IN FIELD flags ]"); } break;
				case 11: to.add("  embed_width: "); ++stages.back(); if (flag & MTPDwebPage::flag_embed_width) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 6 IN FIELD flags ]"); } break;
				case 12: to.add("  embed_height: "); ++stages.back(); if (flag & MTPDwebPage::flag_embed_height) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 6 IN FIELD flags ]"); } break;
				case 13: to.add("  duration: "); ++stages.back(); if (flag & MTPDwebPage::flag_duration) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 7 IN FIELD flags ]"); } break;
				case 14: to.add("  author: "); ++stages.back(); if (flag & MTPDwebPage::flag_author) { types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 8 IN FIELD flags ]"); } break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_authorization:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ authorization");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  flags: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  device_model: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  platform: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  system_version: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 5: to.add("  api_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 6: to.add("  app_name: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 7: to.add("  app_version: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 8: to.add("  date_created: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 9: to.add("  date_active: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 10: to.add("  ip: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 11: to.add("  country: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 12: to.add("  region: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_account_authorizations:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ account_authorizations");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  authorizations: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_account_noPassword:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ account_noPassword");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  new_salt: "); ++stages.back(); types.push_back(mtpc_bytes); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  email_unconfirmed_pattern: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_account_password:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ account_password");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  current_salt: "); ++stages.back(); types.push_back(mtpc_bytes); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  new_salt: "); ++stages.back(); types.push_back(mtpc_bytes); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  hint: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  has_recovery: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  email_unconfirmed_pattern: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_account_passwordSettings:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ account_passwordSettings");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  email: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_account_passwordInputSettings:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ account_passwordInputSettings");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  new_salt: "); ++stages.back(); if (flag & MTPDaccount_passwordInputSettings::flag_new_salt) { types.push_back(mtpc_bytes); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 0 IN FIELD flags ]"); } break;
				case 2: to.add("  new_password_hash: "); ++stages.back(); if (flag & MTPDaccount_passwordInputSettings::flag_new_password_hash) { types.push_back(mtpc_bytes); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 0 IN FIELD flags ]"); } break;
				case 3: to.add("  hint: "); ++stages.back(); if (flag & MTPDaccount_passwordInputSettings::flag_hint) { types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 0 IN FIELD flags ]"); } break;
				case 4: to.add("  email: "); ++stages.back(); if (flag & MTPDaccount_passwordInputSettings::flag_email) { types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 1 IN FIELD flags ]"); } break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_auth_passwordRecovery:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ auth_passwordRecovery");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  email_pattern: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_receivedNotifyMessage:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ receivedNotifyMessage");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  flags: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_chatInviteEmpty:
				to.add("{ chatInviteEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_chatInviteExported:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ chatInviteExported");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  link: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_chatInviteAlready:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ chatInviteAlready");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  chat: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_chatInvite:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ chatInvite");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  title: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_req_pq:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ req_pq");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  nonce: "); ++stages.back(); types.push_back(mtpc_int128); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_req_DH_params:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ req_DH_params");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  nonce: "); ++stages.back(); types.push_back(mtpc_int128); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  server_nonce: "); ++stages.back(); types.push_back(mtpc_int128); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  p: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  q: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  public_key_fingerprint: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 5: to.add("  encrypted_data: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_set_client_DH_params:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ set_client_DH_params");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  nonce: "); ++stages.back(); types.push_back(mtpc_int128); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  server_nonce: "); ++stages.back(); types.push_back(mtpc_int128); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  encrypted_data: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_rpc_drop_answer:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ rpc_drop_answer");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  req_msg_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_get_future_salts:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ get_future_salts");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  num: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_ping:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ ping");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  ping_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_ping_delay_disconnect:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ ping_delay_disconnect");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  ping_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  disconnect_delay: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_destroy_session:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ destroy_session");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  session_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_register_saveDeveloperInfo:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ register_saveDeveloperInfo");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  name: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  email: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  phone_number: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  age: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  city: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_auth_sendCall:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ auth_sendCall");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  phone_number: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  phone_code_hash: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_auth_logOut:
				to.add("{ auth_logOut }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_auth_resetAuthorizations:
				to.add("{ auth_resetAuthorizations }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_auth_sendInvites:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ auth_sendInvites");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  phone_numbers: "); ++stages.back(); types.push_back(0); vtypes.push_back(mtpc_string); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  message: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_auth_bindTempAuthKey:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ auth_bindTempAuthKey");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  perm_auth_key_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  nonce: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  expires_at: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  encrypted_message: "); ++stages.back(); types.push_back(mtpc_bytes); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_account_registerDevice:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ account_registerDevice");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  token_type: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  token: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  device_model: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  system_version: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  app_version: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 5: to.add("  app_sandbox: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 6: to.add("  lang_code: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_account_unregisterDevice:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ account_unregisterDevice");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  token_type: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  token: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_account_updateNotifySettings:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ account_updateNotifySettings");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  settings: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_account_resetNotifySettings:
				to.add("{ account_resetNotifySettings }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_account_updateStatus:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ account_updateStatus");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  offline: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_contacts_deleteContacts:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ contacts_deleteContacts");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_contacts_block:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ contacts_block");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_contacts_unblock:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ contacts_unblock");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_setTyping:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_setTyping");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  action: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_upload_saveFilePart:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ upload_saveFilePart");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  file_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  file_part: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  bytes: "); ++stages.back(); types.push_back(mtpc_bytes); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_help_saveAppLog:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ help_saveAppLog");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  events: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_geochats_setTyping:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ geochats_setTyping");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  typing: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_discardEncryption:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_discardEncryption");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  chat_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_setEncryptedTyping:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_setEncryptedTyping");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  typing: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_readEncryptedHistory:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_readEncryptedHistory");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  max_date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_upload_saveBigFilePart:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ upload_saveBigFilePart");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  file_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  file_part: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  file_total_parts: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  bytes: "); ++stages.back(); types.push_back(mtpc_bytes); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_auth_sendSms:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ auth_sendSms");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  phone_number: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  phone_code_hash: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_account_checkUsername:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ account_checkUsername");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  username: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_account_deleteAccount:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ account_deleteAccount");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  reason: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_account_setAccountTTL:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ account_setAccountTTL");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  ttl: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_account_updateDeviceLocked:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ account_updateDeviceLocked");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  period: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_account_resetAuthorization:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ account_resetAuthorization");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_account_updatePasswordSettings:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ account_updatePasswordSettings");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  current_password_hash: "); ++stages.back(); types.push_back(mtpc_bytes); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  new_settings: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_invokeAfterMsg:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ invokeAfterMsg");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  msg_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  query: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_invokeAfterMsgs:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ invokeAfterMsgs");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  msg_ids: "); ++stages.back(); types.push_back(0); vtypes.push_back(mtpc_long); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  query: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_initConnection:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ initConnection");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  api_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  device_model: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  system_version: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  app_version: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  lang_code: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 5: to.add("  query: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_invokeWithLayer:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ invokeWithLayer");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  layer: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  query: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_invokeWithoutUpdates:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ invokeWithoutUpdates");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  query: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_auth_checkPhone:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ auth_checkPhone");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  phone_number: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_auth_sendCode:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ auth_sendCode");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  phone_number: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  sms_type: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  api_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  api_hash: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  lang_code: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_auth_signUp:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ auth_signUp");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  phone_number: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  phone_code_hash: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  phone_code: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  first_name: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  last_name: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_auth_signIn:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ auth_signIn");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  phone_number: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  phone_code_hash: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  phone_code: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_auth_importAuthorization:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ auth_importAuthorization");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  bytes: "); ++stages.back(); types.push_back(mtpc_bytes); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_auth_checkPassword:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ auth_checkPassword");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  password_hash: "); ++stages.back(); types.push_back(mtpc_bytes); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_auth_recoverPassword:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ auth_recoverPassword");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  code: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_auth_exportAuthorization:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ auth_exportAuthorization");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  dc_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_account_getNotifySettings:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ account_getNotifySettings");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_account_updateProfile:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ account_updateProfile");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  first_name: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  last_name: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_contacts_importCard:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ contacts_importCard");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  export_card: "); ++stages.back(); types.push_back(0); vtypes.push_back(mtpc_int); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_account_updateUsername:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ account_updateUsername");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  username: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_contacts_resolveUsername:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ contacts_resolveUsername");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  username: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_account_changePhone:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ account_changePhone");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  phone_number: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  phone_code_hash: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  phone_code: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_account_getWallPapers:
				to.add("{ account_getWallPapers }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_users_getUsers:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ users_getUsers");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_users_getFullUser:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ users_getFullUser");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_contacts_getStatuses:
				to.add("{ contacts_getStatuses }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_contacts_getContacts:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ contacts_getContacts");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  hash: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_contacts_importContacts:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ contacts_importContacts");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  contacts: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  replace: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_contacts_getSuggested:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ contacts_getSuggested");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  limit: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_contacts_deleteContact:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ contacts_deleteContact");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_contacts_getBlocked:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ contacts_getBlocked");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  offset: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  limit: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_contacts_exportCard:
				to.add("{ contacts_exportCard }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_messages_getMessages:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_getMessages");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(0); vtypes.push_back(mtpc_int); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_getHistory:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_getHistory");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  offset: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  max_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  limit: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_search:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_search");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  q: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  filter: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  min_date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  max_date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 5: to.add("  offset: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 6: to.add("  max_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 7: to.add("  limit: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_getDialogs:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_getDialogs");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  offset: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  max_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  limit: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_readHistory:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_readHistory");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  max_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  offset: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_deleteHistory:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_deleteHistory");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  offset: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_deleteMessages:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_deleteMessages");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(0); vtypes.push_back(mtpc_int); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_readMessageContents:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_readMessageContents");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(0); vtypes.push_back(mtpc_int); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_receivedMessages:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_receivedMessages");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  max_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_sendMessage:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_sendMessage");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  reply_to_msg_id: "); ++stages.back(); if (flag & MTPmessages_sendMessage::flag_reply_to_msg_id) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 0 IN FIELD flags ]"); } break;
				case 3: to.add("  message: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  random_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_sendMedia:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_sendMedia");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  reply_to_msg_id: "); ++stages.back(); if (flag & MTPmessages_sendMedia::flag_reply_to_msg_id) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 0 IN FIELD flags ]"); } break;
				case 3: to.add("  media: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  random_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_forwardMessages:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_forwardMessages");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  id: "); ++stages.back(); types.push_back(0); vtypes.push_back(mtpc_int); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  random_id: "); ++stages.back(); types.push_back(0); vtypes.push_back(mtpc_long); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_editChatTitle:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_editChatTitle");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  chat_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  title: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_editChatPhoto:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_editChatPhoto");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  chat_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  photo: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_addChatUser:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_addChatUser");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  chat_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  user_id: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  fwd_limit: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_deleteChatUser:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_deleteChatUser");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  chat_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  user_id: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_createChat:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_createChat");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  users: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  title: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_forwardMessage:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_forwardMessage");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  random_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_sendBroadcast:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_sendBroadcast");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  contacts: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  random_id: "); ++stages.back(); types.push_back(0); vtypes.push_back(mtpc_long); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  message: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  media: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_importChatInvite:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_importChatInvite");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  hash: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_getChats:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_getChats");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(0); vtypes.push_back(mtpc_int); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_getFullChat:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_getFullChat");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  chat_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_geochats_getFullChat:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ geochats_getFullChat");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_updates_getState:
				to.add("{ updates_getState }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_updates_getDifference:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ updates_getDifference");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  pts: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  qts: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_photos_updateProfilePhoto:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ photos_updateProfilePhoto");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  crop: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_photos_uploadProfilePhoto:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ photos_uploadProfilePhoto");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  file: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  caption: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  geo_point: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  crop: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_photos_deletePhotos:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ photos_deletePhotos");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  id: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_receivedQueue:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_receivedQueue");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  max_qts: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_upload_getFile:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ upload_getFile");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  location: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  offset: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  limit: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_help_getConfig:
				to.add("{ help_getConfig }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_help_getNearestDc:
				to.add("{ help_getNearestDc }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_help_getAppUpdate:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ help_getAppUpdate");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  device_model: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  system_version: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  app_version: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  lang_code: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_help_getInviteText:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ help_getInviteText");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  lang_code: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_photos_getUserPhotos:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ photos_getUserPhotos");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  user_id: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  offset: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  max_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  limit: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_geochats_getLocated:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ geochats_getLocated");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  geo_point: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  radius: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  limit: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_geochats_getRecents:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ geochats_getRecents");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  offset: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  limit: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_geochats_search:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ geochats_search");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  q: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  filter: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  min_date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 4: to.add("  max_date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 5: to.add("  offset: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 6: to.add("  max_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 7: to.add("  limit: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_geochats_getHistory:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ geochats_getHistory");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  offset: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  max_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  limit: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_geochats_checkin:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ geochats_checkin");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_geochats_editChatTitle:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ geochats_editChatTitle");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  title: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  address: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_geochats_editChatPhoto:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ geochats_editChatPhoto");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  photo: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_geochats_sendMessage:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ geochats_sendMessage");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  message: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  random_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_geochats_sendMedia:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ geochats_sendMedia");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  media: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  random_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_geochats_createGeoChat:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ geochats_createGeoChat");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  title: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  geo_point: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  address: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  venue: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_getDhConfig:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_getDhConfig");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  version: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  random_length: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_requestEncryption:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_requestEncryption");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  user_id: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  random_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  g_a: "); ++stages.back(); types.push_back(mtpc_bytes); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_acceptEncryption:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_acceptEncryption");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  g_b: "); ++stages.back(); types.push_back(mtpc_bytes); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  key_fingerprint: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_sendEncrypted:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_sendEncrypted");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  random_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  data: "); ++stages.back(); types.push_back(mtpc_bytes); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_sendEncryptedFile:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_sendEncryptedFile");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  random_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  data: "); ++stages.back(); types.push_back(mtpc_bytes); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  file: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_sendEncryptedService:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_sendEncryptedService");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  random_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  data: "); ++stages.back(); types.push_back(mtpc_bytes); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_help_getSupport:
				to.add("{ help_getSupport }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_contacts_search:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ contacts_search");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  q: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  limit: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_account_getPrivacy:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ account_getPrivacy");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  key: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_account_setPrivacy:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ account_setPrivacy");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  key: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  rules: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_account_getAccountTTL:
				to.add("{ account_getAccountTTL }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_account_sendChangePhoneCode:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ account_sendChangePhoneCode");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  phone_number: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_getStickers:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_getStickers");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  emoticon: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  hash: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_getAllStickers:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_getAllStickers");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  hash: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_getWebPagePreview:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_getWebPagePreview");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  message: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_account_getAuthorizations:
				to.add("{ account_getAuthorizations }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_account_getPassword:
				to.add("{ account_getPassword }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_account_getPasswordSettings:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ account_getPasswordSettings");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  current_password_hash: "); ++stages.back(); types.push_back(mtpc_bytes); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_auth_requestPasswordRecovery:
				to.add("{ auth_requestPasswordRecovery }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;

			case mtpc_messages_exportChatInvite:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_exportChatInvite");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  chat_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_messages_checkChatInvite:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ messages_checkChatInvite");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  hash: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_rpc_result:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ rpc_result");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  req_msg_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  result: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_msg_container:
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ msg_container");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  messages: "); ++stages.back(); types.push_back(mtpc_vector); vtypes.push_back(mtpc_core_message); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
			break;

			case mtpc_core_message: {
				if (stage) {
					to.add(",\n").addSpaces(lev);
				} else {
					to.add("{ core_message");
					to.add("\n").addSpaces(lev);
				}
				switch (stage) {
				case 0: to.add("  msg_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 1: to.add("  seq_no: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 2: to.add("  bytes: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				case 3: to.add("  body: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
				default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
				}
				} break;

			default:
				mtpTextSerializeCore(to, from, end, type, lev, vtype);
				types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
			break;
			}
		}
	} catch (Exception &e) {
		to.add("[ERROR] ");
		to.add("(").add(e.what()).add("), cons: 0x").add(mtpWrapNumber(type, 16));
		if (vtype) to.add(", vcons: 0x").add(mtpWrapNumber(vtype));
		to.add(", ").add(mb(start, (end - start) * sizeof(mtpPrime)).str());
	}
}

#endif
