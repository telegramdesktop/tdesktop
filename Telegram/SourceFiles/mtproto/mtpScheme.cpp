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

typedef QVector<mtpTypeId> Types;
typedef QVector<int32> StagesFlags;

void _serialize_resPQ(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_p_q_inner_data(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_server_DH_params_fail(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_server_DH_params_ok(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_server_DH_inner_data(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_client_DH_inner_data(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_dh_gen_ok(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_dh_gen_retry(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_dh_gen_fail(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_msgs_ack(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_bad_msg_notification(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_bad_server_salt(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_msgs_state_req(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_msgs_state_info(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_msgs_all_info(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_msg_detailed_info(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_msg_new_detailed_info(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_msg_resend_req(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_rpc_error(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_rpc_answer_unknown(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ rpc_answer_unknown }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_rpc_answer_dropped_running(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ rpc_answer_dropped_running }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_rpc_answer_dropped(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_future_salt(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_future_salts(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_pong(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_destroy_session_ok(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_destroy_session_none(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_new_session_created(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_http_wait(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_boolFalse(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ boolFalse }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_boolTrue(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ boolTrue }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_true(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ true }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_error(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_null(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ null }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputPeerEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputPeerEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputPeerSelf(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputPeerSelf }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputPeerChat(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_inputPeerUser(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ inputPeerUser");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  access_hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_inputPeerChannel(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ inputPeerChannel");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  channel_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  access_hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_inputUserEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputUserEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputUserSelf(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputUserSelf }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputUser(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ inputUser");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  access_hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_inputPhoneContact(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_inputFile(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_inputFileBig(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_inputMediaEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputMediaEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputMediaUploadedPhoto(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_inputMediaPhoto(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_inputMediaGeoPoint(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_inputMediaContact(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_inputMediaUploadedVideo(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
	case 4: to.add("  mime_type: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 5: to.add("  caption: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_inputMediaUploadedThumbVideo(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
	case 5: to.add("  mime_type: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 6: to.add("  caption: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_inputMediaVideo(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_inputMediaUploadedAudio(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_inputMediaAudio(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_inputMediaUploadedDocument(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
	case 3: to.add("  caption: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_inputMediaUploadedThumbDocument(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
	case 4: to.add("  caption: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_inputMediaDocument(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ inputMediaDocument");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  id: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  caption: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_inputMediaVenue(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_inputMediaGifExternal(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ inputMediaGifExternal");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  url: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  q: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_inputChatPhotoEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputChatPhotoEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputChatUploadedPhoto(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_inputChatPhoto(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_inputGeoPointEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputGeoPointEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputGeoPoint(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_inputPhotoEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputPhotoEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputPhoto(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_inputVideoEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputVideoEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputVideo(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_inputFileLocation(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_inputVideoFileLocation(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_inputEncryptedFileLocation(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_inputAudioFileLocation(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_inputDocumentFileLocation(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_inputPhotoCropAuto(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputPhotoCropAuto }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputPhotoCrop(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_inputAppEvent(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_peerUser(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_peerChat(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_peerChannel(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ peerChannel");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  channel_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_storage_fileUnknown(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ storage_fileUnknown }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_storage_fileJpeg(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ storage_fileJpeg }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_storage_fileGif(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ storage_fileGif }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_storage_filePng(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ storage_filePng }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_storage_filePdf(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ storage_filePdf }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_storage_fileMp3(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ storage_fileMp3 }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_storage_fileMov(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ storage_fileMov }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_storage_filePartial(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ storage_filePartial }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_storage_fileMp4(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ storage_fileMp4 }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_storage_fileWebp(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ storage_fileWebp }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_fileLocationUnavailable(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_fileLocation(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_userEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_user(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ user");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  self: "); ++stages.back(); if (flag & MTPDuser::flag_self) { to.add("YES [ BY BIT 10 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 10 IN FIELD flags ]"); } break;
	case 2: to.add("  contact: "); ++stages.back(); if (flag & MTPDuser::flag_contact) { to.add("YES [ BY BIT 11 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 11 IN FIELD flags ]"); } break;
	case 3: to.add("  mutual_contact: "); ++stages.back(); if (flag & MTPDuser::flag_mutual_contact) { to.add("YES [ BY BIT 12 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 12 IN FIELD flags ]"); } break;
	case 4: to.add("  deleted: "); ++stages.back(); if (flag & MTPDuser::flag_deleted) { to.add("YES [ BY BIT 13 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 13 IN FIELD flags ]"); } break;
	case 5: to.add("  bot: "); ++stages.back(); if (flag & MTPDuser::flag_bot) { to.add("YES [ BY BIT 14 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 14 IN FIELD flags ]"); } break;
	case 6: to.add("  bot_chat_history: "); ++stages.back(); if (flag & MTPDuser::flag_bot_chat_history) { to.add("YES [ BY BIT 15 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 15 IN FIELD flags ]"); } break;
	case 7: to.add("  bot_nochats: "); ++stages.back(); if (flag & MTPDuser::flag_bot_nochats) { to.add("YES [ BY BIT 16 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 16 IN FIELD flags ]"); } break;
	case 8: to.add("  verified: "); ++stages.back(); if (flag & MTPDuser::flag_verified) { to.add("YES [ BY BIT 17 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 17 IN FIELD flags ]"); } break;
	case 9: to.add("  restricted: "); ++stages.back(); if (flag & MTPDuser::flag_restricted) { to.add("YES [ BY BIT 18 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 18 IN FIELD flags ]"); } break;
	case 10: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 11: to.add("  access_hash: "); ++stages.back(); if (flag & MTPDuser::flag_access_hash) { types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 0 IN FIELD flags ]"); } break;
	case 12: to.add("  first_name: "); ++stages.back(); if (flag & MTPDuser::flag_first_name) { types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 1 IN FIELD flags ]"); } break;
	case 13: to.add("  last_name: "); ++stages.back(); if (flag & MTPDuser::flag_last_name) { types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 2 IN FIELD flags ]"); } break;
	case 14: to.add("  username: "); ++stages.back(); if (flag & MTPDuser::flag_username) { types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 3 IN FIELD flags ]"); } break;
	case 15: to.add("  phone: "); ++stages.back(); if (flag & MTPDuser::flag_phone) { types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 4 IN FIELD flags ]"); } break;
	case 16: to.add("  photo: "); ++stages.back(); if (flag & MTPDuser::flag_photo) { types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 5 IN FIELD flags ]"); } break;
	case 17: to.add("  status: "); ++stages.back(); if (flag & MTPDuser::flag_status) { types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 6 IN FIELD flags ]"); } break;
	case 18: to.add("  bot_info_version: "); ++stages.back(); if (flag & MTPDuser::flag_bot_info_version) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 14 IN FIELD flags ]"); } break;
	case 19: to.add("  restriction_reason: "); ++stages.back(); if (flag & MTPDuser::flag_restriction_reason) { types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 18 IN FIELD flags ]"); } break;
	case 20: to.add("  bot_inline_placeholder: "); ++stages.back(); if (flag & MTPDuser::flag_bot_inline_placeholder) { types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 19 IN FIELD flags ]"); } break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_userProfilePhotoEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ userProfilePhotoEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_userProfilePhoto(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_userStatusEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ userStatusEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_userStatusOnline(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_userStatusOffline(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_userStatusRecently(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ userStatusRecently }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_userStatusLastWeek(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ userStatusLastWeek }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_userStatusLastMonth(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ userStatusLastMonth }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_chatEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_chat(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ chat");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  creator: "); ++stages.back(); if (flag & MTPDchat::flag_creator) { to.add("YES [ BY BIT 0 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 0 IN FIELD flags ]"); } break;
	case 2: to.add("  kicked: "); ++stages.back(); if (flag & MTPDchat::flag_kicked) { to.add("YES [ BY BIT 1 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 1 IN FIELD flags ]"); } break;
	case 3: to.add("  left: "); ++stages.back(); if (flag & MTPDchat::flag_left) { to.add("YES [ BY BIT 2 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 2 IN FIELD flags ]"); } break;
	case 4: to.add("  admins_enabled: "); ++stages.back(); if (flag & MTPDchat::flag_admins_enabled) { to.add("YES [ BY BIT 3 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 3 IN FIELD flags ]"); } break;
	case 5: to.add("  admin: "); ++stages.back(); if (flag & MTPDchat::flag_admin) { to.add("YES [ BY BIT 4 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 4 IN FIELD flags ]"); } break;
	case 6: to.add("  deactivated: "); ++stages.back(); if (flag & MTPDchat::flag_deactivated) { to.add("YES [ BY BIT 5 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 5 IN FIELD flags ]"); } break;
	case 7: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 8: to.add("  title: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 9: to.add("  photo: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 10: to.add("  participants_count: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 11: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 12: to.add("  version: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 13: to.add("  migrated_to: "); ++stages.back(); if (flag & MTPDchat::flag_migrated_to) { types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 6 IN FIELD flags ]"); } break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_chatForbidden(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ chatForbidden");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  title: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_channel(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ channel");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  creator: "); ++stages.back(); if (flag & MTPDchannel::flag_creator) { to.add("YES [ BY BIT 0 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 0 IN FIELD flags ]"); } break;
	case 2: to.add("  kicked: "); ++stages.back(); if (flag & MTPDchannel::flag_kicked) { to.add("YES [ BY BIT 1 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 1 IN FIELD flags ]"); } break;
	case 3: to.add("  left: "); ++stages.back(); if (flag & MTPDchannel::flag_left) { to.add("YES [ BY BIT 2 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 2 IN FIELD flags ]"); } break;
	case 4: to.add("  editor: "); ++stages.back(); if (flag & MTPDchannel::flag_editor) { to.add("YES [ BY BIT 3 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 3 IN FIELD flags ]"); } break;
	case 5: to.add("  moderator: "); ++stages.back(); if (flag & MTPDchannel::flag_moderator) { to.add("YES [ BY BIT 4 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 4 IN FIELD flags ]"); } break;
	case 6: to.add("  broadcast: "); ++stages.back(); if (flag & MTPDchannel::flag_broadcast) { to.add("YES [ BY BIT 5 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 5 IN FIELD flags ]"); } break;
	case 7: to.add("  verified: "); ++stages.back(); if (flag & MTPDchannel::flag_verified) { to.add("YES [ BY BIT 7 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 7 IN FIELD flags ]"); } break;
	case 8: to.add("  megagroup: "); ++stages.back(); if (flag & MTPDchannel::flag_megagroup) { to.add("YES [ BY BIT 8 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 8 IN FIELD flags ]"); } break;
	case 9: to.add("  restricted: "); ++stages.back(); if (flag & MTPDchannel::flag_restricted) { to.add("YES [ BY BIT 9 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 9 IN FIELD flags ]"); } break;
	case 10: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 11: to.add("  access_hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 12: to.add("  title: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 13: to.add("  username: "); ++stages.back(); if (flag & MTPDchannel::flag_username) { types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 6 IN FIELD flags ]"); } break;
	case 14: to.add("  photo: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 15: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 16: to.add("  version: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 17: to.add("  restriction_reason: "); ++stages.back(); if (flag & MTPDchannel::flag_restriction_reason) { types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 9 IN FIELD flags ]"); } break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_channelForbidden(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ channelForbidden");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  access_hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  title: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_chatFull(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
	case 5: to.add("  bot_info: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_channelFull(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ channelFull");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  can_view_participants: "); ++stages.back(); if (flag & MTPDchannelFull::flag_can_view_participants) { to.add("YES [ BY BIT 3 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 3 IN FIELD flags ]"); } break;
	case 2: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 3: to.add("  about: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 4: to.add("  participants_count: "); ++stages.back(); if (flag & MTPDchannelFull::flag_participants_count) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 0 IN FIELD flags ]"); } break;
	case 5: to.add("  admins_count: "); ++stages.back(); if (flag & MTPDchannelFull::flag_admins_count) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 1 IN FIELD flags ]"); } break;
	case 6: to.add("  kicked_count: "); ++stages.back(); if (flag & MTPDchannelFull::flag_kicked_count) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 2 IN FIELD flags ]"); } break;
	case 7: to.add("  read_inbox_max_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 8: to.add("  unread_count: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 9: to.add("  unread_important_count: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 10: to.add("  chat_photo: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 11: to.add("  notify_settings: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 12: to.add("  exported_invite: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 13: to.add("  bot_info: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 14: to.add("  migrated_from_chat_id: "); ++stages.back(); if (flag & MTPDchannelFull::flag_migrated_from_chat_id) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 4 IN FIELD flags ]"); } break;
	case 15: to.add("  migrated_from_max_id: "); ++stages.back(); if (flag & MTPDchannelFull::flag_migrated_from_max_id) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 4 IN FIELD flags ]"); } break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_chatParticipant(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_chatParticipantCreator(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ chatParticipantCreator");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_chatParticipantAdmin(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ chatParticipantAdmin");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  inviter_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_chatParticipantsForbidden(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ chatParticipantsForbidden");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  chat_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  self_participant: "); ++stages.back(); if (flag & MTPDchatParticipantsForbidden::flag_self_participant) { types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 0 IN FIELD flags ]"); } break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_chatParticipants(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ chatParticipants");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  chat_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  participants: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  version: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_chatPhotoEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ chatPhotoEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_chatPhoto(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messageEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_message(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ message");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  unread: "); ++stages.back(); if (flag & MTPDmessage::flag_unread) { to.add("YES [ BY BIT 0 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 0 IN FIELD flags ]"); } break;
	case 2: to.add("  out: "); ++stages.back(); if (flag & MTPDmessage::flag_out) { to.add("YES [ BY BIT 1 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 1 IN FIELD flags ]"); } break;
	case 3: to.add("  mentioned: "); ++stages.back(); if (flag & MTPDmessage::flag_mentioned) { to.add("YES [ BY BIT 4 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 4 IN FIELD flags ]"); } break;
	case 4: to.add("  media_unread: "); ++stages.back(); if (flag & MTPDmessage::flag_media_unread) { to.add("YES [ BY BIT 5 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 5 IN FIELD flags ]"); } break;
	case 5: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 6: to.add("  from_id: "); ++stages.back(); if (flag & MTPDmessage::flag_from_id) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 8 IN FIELD flags ]"); } break;
	case 7: to.add("  to_id: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 8: to.add("  fwd_from_id: "); ++stages.back(); if (flag & MTPDmessage::flag_fwd_from_id) { types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 2 IN FIELD flags ]"); } break;
	case 9: to.add("  fwd_date: "); ++stages.back(); if (flag & MTPDmessage::flag_fwd_date) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 2 IN FIELD flags ]"); } break;
	case 10: to.add("  via_bot_id: "); ++stages.back(); if (flag & MTPDmessage::flag_via_bot_id) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 11 IN FIELD flags ]"); } break;
	case 11: to.add("  reply_to_msg_id: "); ++stages.back(); if (flag & MTPDmessage::flag_reply_to_msg_id) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 3 IN FIELD flags ]"); } break;
	case 12: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 13: to.add("  message: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 14: to.add("  media: "); ++stages.back(); if (flag & MTPDmessage::flag_media) { types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 9 IN FIELD flags ]"); } break;
	case 15: to.add("  reply_markup: "); ++stages.back(); if (flag & MTPDmessage::flag_reply_markup) { types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 6 IN FIELD flags ]"); } break;
	case 16: to.add("  entities: "); ++stages.back(); if (flag & MTPDmessage::flag_entities) { types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 7 IN FIELD flags ]"); } break;
	case 17: to.add("  views: "); ++stages.back(); if (flag & MTPDmessage::flag_views) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 10 IN FIELD flags ]"); } break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messageService(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messageService");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  unread: "); ++stages.back(); if (flag & MTPDmessageService::flag_unread) { to.add("YES [ BY BIT 0 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 0 IN FIELD flags ]"); } break;
	case 2: to.add("  out: "); ++stages.back(); if (flag & MTPDmessageService::flag_out) { to.add("YES [ BY BIT 1 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 1 IN FIELD flags ]"); } break;
	case 3: to.add("  mentioned: "); ++stages.back(); if (flag & MTPDmessageService::flag_mentioned) { to.add("YES [ BY BIT 4 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 4 IN FIELD flags ]"); } break;
	case 4: to.add("  media_unread: "); ++stages.back(); if (flag & MTPDmessageService::flag_media_unread) { to.add("YES [ BY BIT 5 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 5 IN FIELD flags ]"); } break;
	case 5: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 6: to.add("  from_id: "); ++stages.back(); if (flag & MTPDmessageService::flag_from_id) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 8 IN FIELD flags ]"); } break;
	case 7: to.add("  to_id: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 8: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 9: to.add("  action: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messageMediaEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ messageMediaEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_messageMediaPhoto(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messageMediaVideo(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messageMediaGeo(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messageMediaContact(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messageMediaUnsupported(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ messageMediaUnsupported }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_messageMediaDocument(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messageMediaDocument");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  document: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  caption: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messageMediaAudio(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messageMediaWebPage(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messageMediaVenue(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messageActionEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ messageActionEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_messageActionChatCreate(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messageActionChatEditTitle(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messageActionChatEditPhoto(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messageActionChatDeletePhoto(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ messageActionChatDeletePhoto }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_messageActionChatAddUser(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messageActionChatAddUser");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  users: "); ++stages.back(); types.push_back(0); vtypes.push_back(mtpc_int); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messageActionChatDeleteUser(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messageActionChatJoinedByLink(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messageActionChannelCreate(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messageActionChannelCreate");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  title: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messageActionChatMigrateTo(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messageActionChatMigrateTo");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  channel_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messageActionChannelMigrateFrom(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messageActionChannelMigrateFrom");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  title: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  chat_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_dialog(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_dialogChannel(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ dialogChannel");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  top_message: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  top_important_message: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 3: to.add("  read_inbox_max_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 4: to.add("  unread_count: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 5: to.add("  unread_important_count: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 6: to.add("  notify_settings: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 7: to.add("  pts: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_photoEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_photo(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ photo");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  access_hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 3: to.add("  sizes: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_photoSizeEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_photoSize(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_photoCachedSize(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_videoEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_video(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ video");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  access_hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 3: to.add("  duration: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 4: to.add("  mime_type: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 5: to.add("  size: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 6: to.add("  thumb: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 7: to.add("  dc_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 8: to.add("  w: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 9: to.add("  h: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_geoPointEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ geoPointEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_geoPoint(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_auth_checkedPhone(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_auth_sentCode(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_auth_sentAppCode(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_auth_authorization(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ auth_authorization");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  user: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_auth_exportedAuthorization(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_inputNotifyPeer(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_inputNotifyUsers(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputNotifyUsers }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputNotifyChats(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputNotifyChats }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputNotifyAll(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputNotifyAll }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputPeerNotifyEventsEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputPeerNotifyEventsEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputPeerNotifyEventsAll(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputPeerNotifyEventsAll }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputPeerNotifySettings(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_peerNotifyEventsEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ peerNotifyEventsEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_peerNotifyEventsAll(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ peerNotifyEventsAll }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_peerNotifySettingsEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ peerNotifySettingsEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_peerNotifySettings(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_wallPaper(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_wallPaperSolid(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_inputReportReasonSpam(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputReportReasonSpam }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputReportReasonViolence(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputReportReasonViolence }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputReportReasonPornography(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputReportReasonPornography }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputReportReasonOther(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ inputReportReasonOther");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  text: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_userFull(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
	case 5: to.add("  bot_info: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_contact(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_importedContact(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_contactBlocked(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_contactSuggested(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_contactStatus(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_contacts_link(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_contacts_contactsNotModified(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ contacts_contactsNotModified }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_contacts_contacts(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_contacts_importedContacts(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_contacts_blocked(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_contacts_blockedSlice(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_contacts_suggested(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_dialogs(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_dialogsSlice(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_messages(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_messagesSlice(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_channelMessages(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messages_channelMessages");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  pts: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  count: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 3: to.add("  messages: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 4: to.add("  collapsed: "); ++stages.back(); if (flag & MTPDmessages_channelMessages::flag_collapsed) { types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 0 IN FIELD flags ]"); } break;
	case 5: to.add("  chats: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 6: to.add("  users: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messages_chats(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_chatFull(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_affectedHistory(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_inputMessagesFilterEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputMessagesFilterEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputMessagesFilterPhotos(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputMessagesFilterPhotos }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputMessagesFilterVideo(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputMessagesFilterVideo }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputMessagesFilterPhotoVideo(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputMessagesFilterPhotoVideo }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputMessagesFilterPhotoVideoDocuments(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputMessagesFilterPhotoVideoDocuments }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputMessagesFilterDocument(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputMessagesFilterDocument }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputMessagesFilterAudio(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputMessagesFilterAudio }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputMessagesFilterAudioDocuments(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputMessagesFilterAudioDocuments }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputMessagesFilterUrl(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputMessagesFilterUrl }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputMessagesFilterGif(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputMessagesFilterGif }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_updateNewMessage(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_updateMessageID(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_updateDeleteMessages(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_updateUserTyping(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_updateChatUserTyping(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_updateChatParticipants(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_updateUserStatus(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_updateUserName(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_updateUserPhoto(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_updateContactRegistered(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_updateContactLink(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_updateNewAuthorization(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_updateNewEncryptedMessage(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_updateEncryptedChatTyping(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_updateEncryption(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_updateEncryptedMessagesRead(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_updateChatParticipantAdd(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
	case 3: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 4: to.add("  version: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_updateChatParticipantDelete(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_updateDcOptions(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_updateUserBlocked(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_updateNotifySettings(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_updateServiceNotification(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_updatePrivacy(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_updateUserPhone(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_updateReadHistoryInbox(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_updateReadHistoryOutbox(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_updateWebPage(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ updateWebPage");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  webpage: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  pts: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  pts_count: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_updateReadMessagesContents(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_updateChannelTooLong(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ updateChannelTooLong");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  channel_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_updateChannel(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ updateChannel");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  channel_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_updateChannelGroup(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ updateChannelGroup");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  channel_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  group: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_updateNewChannelMessage(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ updateNewChannelMessage");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  message: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  pts: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  pts_count: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_updateReadChannelInbox(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ updateReadChannelInbox");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  channel_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  max_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_updateDeleteChannelMessages(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ updateDeleteChannelMessages");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  channel_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  messages: "); ++stages.back(); types.push_back(0); vtypes.push_back(mtpc_int); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  pts: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 3: to.add("  pts_count: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_updateChannelMessageViews(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ updateChannelMessageViews");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  channel_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  views: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_updateChatAdmins(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ updateChatAdmins");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  chat_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  enabled: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  version: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_updateChatParticipantAdmin(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ updateChatParticipantAdmin");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  chat_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  is_admin: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 3: to.add("  version: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_updateNewStickerSet(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ updateNewStickerSet");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  stickerset: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_updateStickerSetsOrder(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ updateStickerSetsOrder");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  order: "); ++stages.back(); types.push_back(0); vtypes.push_back(mtpc_long); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_updateStickerSets(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ updateStickerSets }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_updateSavedGifs(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ updateSavedGifs }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_updateBotInlineQuery(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ updateBotInlineQuery");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  query_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  query: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 3: to.add("  offset: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_updates_state(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_updates_differenceEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_updates_difference(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_updates_differenceSlice(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_updatesTooLong(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ updatesTooLong }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_updateShortMessage(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ updateShortMessage");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  unread: "); ++stages.back(); if (flag & MTPDupdateShortMessage::flag_unread) { to.add("YES [ BY BIT 0 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 0 IN FIELD flags ]"); } break;
	case 2: to.add("  out: "); ++stages.back(); if (flag & MTPDupdateShortMessage::flag_out) { to.add("YES [ BY BIT 1 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 1 IN FIELD flags ]"); } break;
	case 3: to.add("  mentioned: "); ++stages.back(); if (flag & MTPDupdateShortMessage::flag_mentioned) { to.add("YES [ BY BIT 4 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 4 IN FIELD flags ]"); } break;
	case 4: to.add("  media_unread: "); ++stages.back(); if (flag & MTPDupdateShortMessage::flag_media_unread) { to.add("YES [ BY BIT 5 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 5 IN FIELD flags ]"); } break;
	case 5: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 6: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 7: to.add("  message: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 8: to.add("  pts: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 9: to.add("  pts_count: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 10: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 11: to.add("  fwd_from_id: "); ++stages.back(); if (flag & MTPDupdateShortMessage::flag_fwd_from_id) { types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 2 IN FIELD flags ]"); } break;
	case 12: to.add("  fwd_date: "); ++stages.back(); if (flag & MTPDupdateShortMessage::flag_fwd_date) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 2 IN FIELD flags ]"); } break;
	case 13: to.add("  via_bot_id: "); ++stages.back(); if (flag & MTPDupdateShortMessage::flag_via_bot_id) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 11 IN FIELD flags ]"); } break;
	case 14: to.add("  reply_to_msg_id: "); ++stages.back(); if (flag & MTPDupdateShortMessage::flag_reply_to_msg_id) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 3 IN FIELD flags ]"); } break;
	case 15: to.add("  entities: "); ++stages.back(); if (flag & MTPDupdateShortMessage::flag_entities) { types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 7 IN FIELD flags ]"); } break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_updateShortChatMessage(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ updateShortChatMessage");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  unread: "); ++stages.back(); if (flag & MTPDupdateShortChatMessage::flag_unread) { to.add("YES [ BY BIT 0 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 0 IN FIELD flags ]"); } break;
	case 2: to.add("  out: "); ++stages.back(); if (flag & MTPDupdateShortChatMessage::flag_out) { to.add("YES [ BY BIT 1 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 1 IN FIELD flags ]"); } break;
	case 3: to.add("  mentioned: "); ++stages.back(); if (flag & MTPDupdateShortChatMessage::flag_mentioned) { to.add("YES [ BY BIT 4 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 4 IN FIELD flags ]"); } break;
	case 4: to.add("  media_unread: "); ++stages.back(); if (flag & MTPDupdateShortChatMessage::flag_media_unread) { to.add("YES [ BY BIT 5 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 5 IN FIELD flags ]"); } break;
	case 5: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 6: to.add("  from_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 7: to.add("  chat_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 8: to.add("  message: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 9: to.add("  pts: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 10: to.add("  pts_count: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 11: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 12: to.add("  fwd_from_id: "); ++stages.back(); if (flag & MTPDupdateShortChatMessage::flag_fwd_from_id) { types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 2 IN FIELD flags ]"); } break;
	case 13: to.add("  fwd_date: "); ++stages.back(); if (flag & MTPDupdateShortChatMessage::flag_fwd_date) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 2 IN FIELD flags ]"); } break;
	case 14: to.add("  via_bot_id: "); ++stages.back(); if (flag & MTPDupdateShortChatMessage::flag_via_bot_id) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 11 IN FIELD flags ]"); } break;
	case 15: to.add("  reply_to_msg_id: "); ++stages.back(); if (flag & MTPDupdateShortChatMessage::flag_reply_to_msg_id) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 3 IN FIELD flags ]"); } break;
	case 16: to.add("  entities: "); ++stages.back(); if (flag & MTPDupdateShortChatMessage::flag_entities) { types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 7 IN FIELD flags ]"); } break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_updateShort(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_updatesCombined(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_updates(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_updateShortSentMessage(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ updateShortSentMessage");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  unread: "); ++stages.back(); if (flag & MTPDupdateShortSentMessage::flag_unread) { to.add("YES [ BY BIT 0 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 0 IN FIELD flags ]"); } break;
	case 2: to.add("  out: "); ++stages.back(); if (flag & MTPDupdateShortSentMessage::flag_out) { to.add("YES [ BY BIT 1 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 1 IN FIELD flags ]"); } break;
	case 3: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 4: to.add("  pts: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 5: to.add("  pts_count: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 6: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 7: to.add("  media: "); ++stages.back(); if (flag & MTPDupdateShortSentMessage::flag_media) { types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 9 IN FIELD flags ]"); } break;
	case 8: to.add("  entities: "); ++stages.back(); if (flag & MTPDupdateShortSentMessage::flag_entities) { types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 7 IN FIELD flags ]"); } break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_photos_photos(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_photos_photosSlice(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_photos_photo(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_upload_file(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_dcOption(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ dcOption");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  ipv6: "); ++stages.back(); if (flag & MTPDdcOption::flag_ipv6) { to.add("YES [ BY BIT 0 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 0 IN FIELD flags ]"); } break;
	case 2: to.add("  media_only: "); ++stages.back(); if (flag & MTPDdcOption::flag_media_only) { to.add("YES [ BY BIT 1 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 1 IN FIELD flags ]"); } break;
	case 3: to.add("  id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 4: to.add("  ip_address: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 5: to.add("  port: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_config(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
	case 6: to.add("  megagroup_size_max: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
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
	case 17: to.add("  saved_gifs_limit: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 18: to.add("  disabled_features: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_nearestDc(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_help_appUpdate(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_help_noAppUpdate(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ help_noAppUpdate }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_help_inviteText(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_encryptedChatEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_encryptedChatWaiting(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_encryptedChatRequested(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_encryptedChat(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_encryptedChatDiscarded(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_inputEncryptedChat(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_encryptedFileEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ encryptedFileEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_encryptedFile(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_inputEncryptedFileEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputEncryptedFileEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputEncryptedFileUploaded(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_inputEncryptedFile(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_inputEncryptedFileBigUploaded(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_encryptedMessage(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_encryptedMessageService(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_dhConfigNotModified(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_dhConfig(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_sentEncryptedMessage(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_sentEncryptedFile(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_inputAudioEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputAudioEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputAudio(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_inputDocumentEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputDocumentEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputDocument(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_audioEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_audio(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ audio");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  access_hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 3: to.add("  duration: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 4: to.add("  mime_type: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 5: to.add("  size: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 6: to.add("  dc_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_documentEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_document(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_help_support(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_notifyPeer(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_notifyUsers(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ notifyUsers }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_notifyChats(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ notifyChats }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_notifyAll(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ notifyAll }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_sendMessageTypingAction(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ sendMessageTypingAction }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_sendMessageCancelAction(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ sendMessageCancelAction }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_sendMessageRecordVideoAction(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ sendMessageRecordVideoAction }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_sendMessageUploadVideoAction(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_sendMessageRecordAudioAction(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ sendMessageRecordAudioAction }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_sendMessageUploadAudioAction(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_sendMessageUploadPhotoAction(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_sendMessageUploadDocumentAction(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_sendMessageGeoLocationAction(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ sendMessageGeoLocationAction }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_sendMessageChooseContactAction(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ sendMessageChooseContactAction }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_contacts_found(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ contacts_found");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  results: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  chats: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  users: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_inputPrivacyKeyStatusTimestamp(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputPrivacyKeyStatusTimestamp }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_privacyKeyStatusTimestamp(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ privacyKeyStatusTimestamp }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputPrivacyValueAllowContacts(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputPrivacyValueAllowContacts }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputPrivacyValueAllowAll(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputPrivacyValueAllowAll }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputPrivacyValueAllowUsers(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_inputPrivacyValueDisallowContacts(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputPrivacyValueDisallowContacts }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputPrivacyValueDisallowAll(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputPrivacyValueDisallowAll }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputPrivacyValueDisallowUsers(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_privacyValueAllowContacts(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ privacyValueAllowContacts }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_privacyValueAllowAll(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ privacyValueAllowAll }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_privacyValueAllowUsers(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_privacyValueDisallowContacts(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ privacyValueDisallowContacts }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_privacyValueDisallowAll(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ privacyValueDisallowAll }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_privacyValueDisallowUsers(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_account_privacyRules(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_accountDaysTTL(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_account_sentChangePhoneCode(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_documentAttributeImageSize(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_documentAttributeAnimated(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ documentAttributeAnimated }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_documentAttributeSticker(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ documentAttributeSticker");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  alt: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  stickerset: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_documentAttributeVideo(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_documentAttributeAudio(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ documentAttributeAudio");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  duration: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  title: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  performer: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_documentAttributeFilename(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_stickersNotModified(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ messages_stickersNotModified }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_messages_stickers(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_stickerPack(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_allStickersNotModified(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ messages_allStickersNotModified }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_messages_allStickers(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messages_allStickers");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  hash: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  sets: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_disabledFeature(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_affectedMessages(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_contactLinkUnknown(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ contactLinkUnknown }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_contactLinkNone(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ contactLinkNone }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_contactLinkHasPhone(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ contactLinkHasPhone }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_contactLinkContact(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ contactLinkContact }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_webPageEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_webPagePending(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_webPage(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
	case 15: to.add("  document: "); ++stages.back(); if (flag & MTPDwebPage::flag_document) { types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 9 IN FIELD flags ]"); } break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_authorization(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_account_authorizations(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_account_noPassword(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_account_password(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_account_passwordSettings(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_account_passwordInputSettings(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_auth_passwordRecovery(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_receivedNotifyMessage(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_chatInviteEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ chatInviteEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_chatInviteExported(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_chatInviteAlready(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_chatInvite(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ chatInvite");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  channel: "); ++stages.back(); if (flag & MTPDchatInvite::flag_channel) { to.add("YES [ BY BIT 0 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 0 IN FIELD flags ]"); } break;
	case 2: to.add("  broadcast: "); ++stages.back(); if (flag & MTPDchatInvite::flag_broadcast) { to.add("YES [ BY BIT 1 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 1 IN FIELD flags ]"); } break;
	case 3: to.add("  public: "); ++stages.back(); if (flag & MTPDchatInvite::flag_public) { to.add("YES [ BY BIT 2 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 2 IN FIELD flags ]"); } break;
	case 4: to.add("  megagroup: "); ++stages.back(); if (flag & MTPDchatInvite::flag_megagroup) { to.add("YES [ BY BIT 3 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 3 IN FIELD flags ]"); } break;
	case 5: to.add("  title: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_inputStickerSetEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputStickerSetEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputStickerSetID(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ inputStickerSetID");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  access_hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_inputStickerSetShortName(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ inputStickerSetShortName");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  short_name: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_stickerSet(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ stickerSet");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  installed: "); ++stages.back(); if (flag & MTPDstickerSet::flag_installed) { to.add("YES [ BY BIT 0 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 0 IN FIELD flags ]"); } break;
	case 2: to.add("  disabled: "); ++stages.back(); if (flag & MTPDstickerSet::flag_disabled) { to.add("YES [ BY BIT 1 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 1 IN FIELD flags ]"); } break;
	case 3: to.add("  official: "); ++stages.back(); if (flag & MTPDstickerSet::flag_official) { to.add("YES [ BY BIT 2 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 2 IN FIELD flags ]"); } break;
	case 4: to.add("  id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 5: to.add("  access_hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 6: to.add("  title: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 7: to.add("  short_name: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 8: to.add("  count: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 9: to.add("  hash: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messages_stickerSet(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messages_stickerSet");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  set: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  packs: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  documents: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_botCommand(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ botCommand");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  command: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  description: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_botInfoEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ botInfoEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_botInfo(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ botInfo");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  version: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  share_text: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 3: to.add("  description: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 4: to.add("  commands: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_keyboardButton(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ keyboardButton");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  text: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_keyboardButtonRow(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ keyboardButtonRow");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  buttons: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_replyKeyboardHide(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ replyKeyboardHide");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  selective: "); ++stages.back(); if (flag & MTPDreplyKeyboardHide::flag_selective) { to.add("YES [ BY BIT 2 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 2 IN FIELD flags ]"); } break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_replyKeyboardForceReply(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ replyKeyboardForceReply");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  single_use: "); ++stages.back(); if (flag & MTPDreplyKeyboardForceReply::flag_single_use) { to.add("YES [ BY BIT 1 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 1 IN FIELD flags ]"); } break;
	case 2: to.add("  selective: "); ++stages.back(); if (flag & MTPDreplyKeyboardForceReply::flag_selective) { to.add("YES [ BY BIT 2 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 2 IN FIELD flags ]"); } break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_replyKeyboardMarkup(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ replyKeyboardMarkup");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  resize: "); ++stages.back(); if (flag & MTPDreplyKeyboardMarkup::flag_resize) { to.add("YES [ BY BIT 0 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 0 IN FIELD flags ]"); } break;
	case 2: to.add("  single_use: "); ++stages.back(); if (flag & MTPDreplyKeyboardMarkup::flag_single_use) { to.add("YES [ BY BIT 1 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 1 IN FIELD flags ]"); } break;
	case 3: to.add("  selective: "); ++stages.back(); if (flag & MTPDreplyKeyboardMarkup::flag_selective) { to.add("YES [ BY BIT 2 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 2 IN FIELD flags ]"); } break;
	case 4: to.add("  rows: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_help_appChangelogEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ help_appChangelogEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_help_appChangelog(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ help_appChangelog");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  text: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messageEntityUnknown(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messageEntityUnknown");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  offset: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  length: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messageEntityMention(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messageEntityMention");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  offset: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  length: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messageEntityHashtag(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messageEntityHashtag");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  offset: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  length: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messageEntityBotCommand(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messageEntityBotCommand");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  offset: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  length: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messageEntityUrl(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messageEntityUrl");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  offset: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  length: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messageEntityEmail(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messageEntityEmail");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  offset: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  length: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messageEntityBold(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messageEntityBold");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  offset: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  length: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messageEntityItalic(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messageEntityItalic");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  offset: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  length: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messageEntityCode(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messageEntityCode");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  offset: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  length: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messageEntityPre(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messageEntityPre");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  offset: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  length: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  language: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messageEntityTextUrl(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messageEntityTextUrl");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  offset: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  length: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  url: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_inputChannelEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ inputChannelEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_inputChannel(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ inputChannel");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  channel_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  access_hash: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_contacts_resolvedPeer(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ contacts_resolvedPeer");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  chats: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  users: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messageRange(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messageRange");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  min_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  max_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messageGroup(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messageGroup");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  min_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  max_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  count: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 3: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_updates_channelDifferenceEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ updates_channelDifferenceEmpty");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  final: "); ++stages.back(); if (flag & MTPDupdates_channelDifferenceEmpty::flag_final) { to.add("YES [ BY BIT 0 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 0 IN FIELD flags ]"); } break;
	case 2: to.add("  pts: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 3: to.add("  timeout: "); ++stages.back(); if (flag & MTPDupdates_channelDifferenceEmpty::flag_timeout) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 1 IN FIELD flags ]"); } break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_updates_channelDifferenceTooLong(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ updates_channelDifferenceTooLong");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  final: "); ++stages.back(); if (flag & MTPDupdates_channelDifferenceTooLong::flag_final) { to.add("YES [ BY BIT 0 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 0 IN FIELD flags ]"); } break;
	case 2: to.add("  pts: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 3: to.add("  timeout: "); ++stages.back(); if (flag & MTPDupdates_channelDifferenceTooLong::flag_timeout) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 1 IN FIELD flags ]"); } break;
	case 4: to.add("  top_message: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 5: to.add("  top_important_message: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 6: to.add("  read_inbox_max_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 7: to.add("  unread_count: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 8: to.add("  unread_important_count: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 9: to.add("  messages: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 10: to.add("  chats: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 11: to.add("  users: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_updates_channelDifference(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ updates_channelDifference");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  final: "); ++stages.back(); if (flag & MTPDupdates_channelDifference::flag_final) { to.add("YES [ BY BIT 0 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 0 IN FIELD flags ]"); } break;
	case 2: to.add("  pts: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 3: to.add("  timeout: "); ++stages.back(); if (flag & MTPDupdates_channelDifference::flag_timeout) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 1 IN FIELD flags ]"); } break;
	case 4: to.add("  new_messages: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 5: to.add("  other_updates: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 6: to.add("  chats: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 7: to.add("  users: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_channelMessagesFilterEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ channelMessagesFilterEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_channelMessagesFilter(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ channelMessagesFilter");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  important_only: "); ++stages.back(); if (flag & MTPDchannelMessagesFilter::flag_important_only) { to.add("YES [ BY BIT 0 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 0 IN FIELD flags ]"); } break;
	case 2: to.add("  exclude_new_messages: "); ++stages.back(); if (flag & MTPDchannelMessagesFilter::flag_exclude_new_messages) { to.add("YES [ BY BIT 1 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 1 IN FIELD flags ]"); } break;
	case 3: to.add("  ranges: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_channelMessagesFilterCollapsed(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ channelMessagesFilterCollapsed }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_channelParticipant(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ channelParticipant");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_channelParticipantSelf(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ channelParticipantSelf");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  inviter_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_channelParticipantModerator(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ channelParticipantModerator");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  inviter_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_channelParticipantEditor(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ channelParticipantEditor");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  inviter_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_channelParticipantKicked(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ channelParticipantKicked");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  kicked_by: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_channelParticipantCreator(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ channelParticipantCreator");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  user_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_channelParticipantsRecent(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ channelParticipantsRecent }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_channelParticipantsAdmins(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ channelParticipantsAdmins }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_channelParticipantsKicked(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ channelParticipantsKicked }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_channelParticipantsBots(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ channelParticipantsBots }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_channelRoleEmpty(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ channelRoleEmpty }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_channelRoleModerator(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ channelRoleModerator }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_channelRoleEditor(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ channelRoleEditor }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_channels_channelParticipants(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ channels_channelParticipants");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  count: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  participants: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  users: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_channels_channelParticipant(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ channels_channelParticipant");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  participant: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  users: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_help_termsOfService(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ help_termsOfService");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  text: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_foundGif(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ foundGif");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  url: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  thumb_url: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  content_url: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 3: to.add("  content_type: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 4: to.add("  w: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 5: to.add("  h: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_foundGifCached(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ foundGifCached");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  url: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  photo: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  document: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messages_foundGifs(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messages_foundGifs");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  next_offset: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  results: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messages_savedGifsNotModified(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ messages_savedGifsNotModified }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_messages_savedGifs(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messages_savedGifs");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  hash: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  gifs: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_inputBotInlineMessageMediaAuto(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ inputBotInlineMessageMediaAuto");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  caption: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_inputBotInlineMessageText(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ inputBotInlineMessageText");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  no_webpage: "); ++stages.back(); if (flag & MTPDinputBotInlineMessageText::flag_no_webpage) { to.add("YES [ BY BIT 0 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 0 IN FIELD flags ]"); } break;
	case 2: to.add("  message: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 3: to.add("  entities: "); ++stages.back(); if (flag & MTPDinputBotInlineMessageText::flag_entities) { types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 1 IN FIELD flags ]"); } break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_inputBotInlineResult(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ inputBotInlineResult");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  id: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  type: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 3: to.add("  title: "); ++stages.back(); if (flag & MTPDinputBotInlineResult::flag_title) { types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 1 IN FIELD flags ]"); } break;
	case 4: to.add("  description: "); ++stages.back(); if (flag & MTPDinputBotInlineResult::flag_description) { types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 2 IN FIELD flags ]"); } break;
	case 5: to.add("  url: "); ++stages.back(); if (flag & MTPDinputBotInlineResult::flag_url) { types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 3 IN FIELD flags ]"); } break;
	case 6: to.add("  thumb_url: "); ++stages.back(); if (flag & MTPDinputBotInlineResult::flag_thumb_url) { types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 4 IN FIELD flags ]"); } break;
	case 7: to.add("  content_url: "); ++stages.back(); if (flag & MTPDinputBotInlineResult::flag_content_url) { types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 5 IN FIELD flags ]"); } break;
	case 8: to.add("  content_type: "); ++stages.back(); if (flag & MTPDinputBotInlineResult::flag_content_type) { types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 5 IN FIELD flags ]"); } break;
	case 9: to.add("  w: "); ++stages.back(); if (flag & MTPDinputBotInlineResult::flag_w) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 6 IN FIELD flags ]"); } break;
	case 10: to.add("  h: "); ++stages.back(); if (flag & MTPDinputBotInlineResult::flag_h) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 6 IN FIELD flags ]"); } break;
	case 11: to.add("  duration: "); ++stages.back(); if (flag & MTPDinputBotInlineResult::flag_duration) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 7 IN FIELD flags ]"); } break;
	case 12: to.add("  send_message: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_botInlineMessageMediaAuto(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ botInlineMessageMediaAuto");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  caption: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_botInlineMessageText(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ botInlineMessageText");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  no_webpage: "); ++stages.back(); if (flag & MTPDbotInlineMessageText::flag_no_webpage) { to.add("YES [ BY BIT 0 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 0 IN FIELD flags ]"); } break;
	case 2: to.add("  message: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 3: to.add("  entities: "); ++stages.back(); if (flag & MTPDbotInlineMessageText::flag_entities) { types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 1 IN FIELD flags ]"); } break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_botInlineMediaResultDocument(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ botInlineMediaResultDocument");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  type: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  document: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 3: to.add("  send_message: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_botInlineMediaResultPhoto(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ botInlineMediaResultPhoto");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  id: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  type: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  photo: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 3: to.add("  send_message: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_botInlineResult(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ botInlineResult");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  id: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  type: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 3: to.add("  title: "); ++stages.back(); if (flag & MTPDbotInlineResult::flag_title) { types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 1 IN FIELD flags ]"); } break;
	case 4: to.add("  description: "); ++stages.back(); if (flag & MTPDbotInlineResult::flag_description) { types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 2 IN FIELD flags ]"); } break;
	case 5: to.add("  url: "); ++stages.back(); if (flag & MTPDbotInlineResult::flag_url) { types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 3 IN FIELD flags ]"); } break;
	case 6: to.add("  thumb_url: "); ++stages.back(); if (flag & MTPDbotInlineResult::flag_thumb_url) { types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 4 IN FIELD flags ]"); } break;
	case 7: to.add("  content_url: "); ++stages.back(); if (flag & MTPDbotInlineResult::flag_content_url) { types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 5 IN FIELD flags ]"); } break;
	case 8: to.add("  content_type: "); ++stages.back(); if (flag & MTPDbotInlineResult::flag_content_type) { types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 5 IN FIELD flags ]"); } break;
	case 9: to.add("  w: "); ++stages.back(); if (flag & MTPDbotInlineResult::flag_w) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 6 IN FIELD flags ]"); } break;
	case 10: to.add("  h: "); ++stages.back(); if (flag & MTPDbotInlineResult::flag_h) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 6 IN FIELD flags ]"); } break;
	case 11: to.add("  duration: "); ++stages.back(); if (flag & MTPDbotInlineResult::flag_duration) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 7 IN FIELD flags ]"); } break;
	case 12: to.add("  send_message: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messages_botResults(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messages_botResults");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  gallery: "); ++stages.back(); if (flag & MTPDmessages_botResults::flag_gallery) { to.add("YES [ BY BIT 0 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 0 IN FIELD flags ]"); } break;
	case 2: to.add("  query_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 3: to.add("  next_offset: "); ++stages.back(); if (flag & MTPDmessages_botResults::flag_next_offset) { types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 1 IN FIELD flags ]"); } break;
	case 4: to.add("  results: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_req_pq(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_req_DH_params(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_set_client_DH_params(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_rpc_drop_answer(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_get_future_salts(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_ping(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_ping_delay_disconnect(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_destroy_session(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_register_saveDeveloperInfo(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_auth_sendCall(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_auth_logOut(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ auth_logOut }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_auth_resetAuthorizations(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ auth_resetAuthorizations }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_auth_sendInvites(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_auth_bindTempAuthKey(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_auth_sendSms(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_account_registerDevice(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_account_unregisterDevice(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_account_updateNotifySettings(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_account_resetNotifySettings(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ account_resetNotifySettings }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_account_updateStatus(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_account_reportPeer(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ account_reportPeer");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  reason: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_account_checkUsername(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_account_deleteAccount(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_account_setAccountTTL(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_account_updateDeviceLocked(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_account_resetAuthorization(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_account_updatePasswordSettings(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_contacts_deleteContacts(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_contacts_block(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_contacts_unblock(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_setTyping(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_reportSpam(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messages_reportSpam");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messages_discardEncryption(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_setEncryptedTyping(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_readEncryptedHistory(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_installStickerSet(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messages_installStickerSet");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  stickerset: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  disabled: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messages_uninstallStickerSet(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messages_uninstallStickerSet");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  stickerset: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messages_editChatAdmin(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messages_editChatAdmin");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  chat_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  user_id: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  is_admin: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messages_reorderStickerSets(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messages_reorderStickerSets");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  order: "); ++stages.back(); types.push_back(0); vtypes.push_back(mtpc_long); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messages_saveGif(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messages_saveGif");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  id: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  unsave: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messages_setInlineBotResults(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messages_setInlineBotResults");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  gallery: "); ++stages.back(); if (flag & MTPmessages_setInlineBotResults::flag_gallery) { to.add("YES [ BY BIT 0 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 0 IN FIELD flags ]"); } break;
	case 2: to.add("  private: "); ++stages.back(); if (flag & MTPmessages_setInlineBotResults::flag_private) { to.add("YES [ BY BIT 1 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 1 IN FIELD flags ]"); } break;
	case 3: to.add("  query_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 4: to.add("  results: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 5: to.add("  cache_time: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 6: to.add("  next_offset: "); ++stages.back(); if (flag & MTPmessages_setInlineBotResults::flag_next_offset) { types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 2 IN FIELD flags ]"); } break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_upload_saveFilePart(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_upload_saveBigFilePart(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_help_saveAppLog(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_channels_readHistory(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ channels_readHistory");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  channel: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  max_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_channels_reportSpam(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ channels_reportSpam");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  channel: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  user_id: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  id: "); ++stages.back(); types.push_back(0); vtypes.push_back(mtpc_int); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_channels_editAbout(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ channels_editAbout");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  channel: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  about: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_channels_checkUsername(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ channels_checkUsername");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  channel: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  username: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_channels_updateUsername(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ channels_updateUsername");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  channel: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  username: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_invokeAfterMsg(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_invokeAfterMsgs(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_initConnection(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_invokeWithLayer(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_invokeWithoutUpdates(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_auth_checkPhone(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_auth_sendCode(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_auth_signUp(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_auth_signIn(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_auth_importAuthorization(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_auth_importBotAuthorization(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ auth_importBotAuthorization");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  flags: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  api_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  api_hash: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 3: to.add("  bot_auth_token: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_auth_checkPassword(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_auth_recoverPassword(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_auth_exportAuthorization(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_auth_requestPasswordRecovery(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ auth_requestPasswordRecovery }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_account_getNotifySettings(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_account_updateProfile(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_account_updateUsername(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_account_changePhone(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_contacts_importCard(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_account_getWallPapers(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ account_getWallPapers }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_account_getPrivacy(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_account_setPrivacy(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_account_getAccountTTL(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ account_getAccountTTL }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_account_sendChangePhoneCode(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_account_getAuthorizations(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ account_getAuthorizations }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_account_getPassword(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ account_getPassword }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_account_getPasswordSettings(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_users_getUsers(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_users_getFullUser(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_contacts_getStatuses(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ contacts_getStatuses }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_contacts_getContacts(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_contacts_importContacts(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_contacts_getSuggested(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_contacts_deleteContact(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_contacts_getBlocked(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_contacts_exportCard(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ contacts_exportCard }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_messages_getMessagesViews(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messages_getMessagesViews");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  id: "); ++stages.back(); types.push_back(0); vtypes.push_back(mtpc_int); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  increment: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_contacts_search(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_contacts_resolveUsername(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_getMessages(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_getHistory(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messages_getHistory");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  offset_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  add_offset: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 3: to.add("  limit: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 4: to.add("  max_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 5: to.add("  min_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messages_search(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messages_search");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  important_only: "); ++stages.back(); if (flag & MTPmessages_search::flag_important_only) { to.add("YES [ BY BIT 0 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 0 IN FIELD flags ]"); } break;
	case 2: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 3: to.add("  q: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 4: to.add("  filter: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 5: to.add("  min_date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 6: to.add("  max_date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 7: to.add("  offset: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 8: to.add("  max_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 9: to.add("  limit: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messages_searchGlobal(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messages_searchGlobal");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  q: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  offset_date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  offset_peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 3: to.add("  offset_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 4: to.add("  limit: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_channels_getImportantHistory(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ channels_getImportantHistory");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  channel: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  offset_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  add_offset: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 3: to.add("  limit: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 4: to.add("  max_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 5: to.add("  min_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_channels_getMessages(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ channels_getMessages");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  channel: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  id: "); ++stages.back(); types.push_back(0); vtypes.push_back(mtpc_int); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messages_getDialogs(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messages_getDialogs");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  offset_date: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  offset_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  offset_peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 3: to.add("  limit: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_channels_getDialogs(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ channels_getDialogs");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  offset: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  limit: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messages_readHistory(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messages_readHistory");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  max_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messages_deleteMessages(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_readMessageContents(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_channels_deleteMessages(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ channels_deleteMessages");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  channel: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  id: "); ++stages.back(); types.push_back(0); vtypes.push_back(mtpc_int); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messages_deleteHistory(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messages_deleteHistory");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  max_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_channels_deleteUserHistory(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ channels_deleteUserHistory");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  channel: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  user_id: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messages_receivedMessages(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_sendMessage(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messages_sendMessage");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  no_webpage: "); ++stages.back(); if (flag & MTPmessages_sendMessage::flag_no_webpage) { to.add("YES [ BY BIT 1 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 1 IN FIELD flags ]"); } break;
	case 2: to.add("  broadcast: "); ++stages.back(); if (flag & MTPmessages_sendMessage::flag_broadcast) { to.add("YES [ BY BIT 4 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 4 IN FIELD flags ]"); } break;
	case 3: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 4: to.add("  reply_to_msg_id: "); ++stages.back(); if (flag & MTPmessages_sendMessage::flag_reply_to_msg_id) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 0 IN FIELD flags ]"); } break;
	case 5: to.add("  message: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 6: to.add("  random_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 7: to.add("  reply_markup: "); ++stages.back(); if (flag & MTPmessages_sendMessage::flag_reply_markup) { types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 2 IN FIELD flags ]"); } break;
	case 8: to.add("  entities: "); ++stages.back(); if (flag & MTPmessages_sendMessage::flag_entities) { types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 3 IN FIELD flags ]"); } break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messages_sendMedia(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messages_sendMedia");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  broadcast: "); ++stages.back(); if (flag & MTPmessages_sendMedia::flag_broadcast) { to.add("YES [ BY BIT 4 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 4 IN FIELD flags ]"); } break;
	case 2: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 3: to.add("  reply_to_msg_id: "); ++stages.back(); if (flag & MTPmessages_sendMedia::flag_reply_to_msg_id) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 0 IN FIELD flags ]"); } break;
	case 4: to.add("  media: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 5: to.add("  random_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 6: to.add("  reply_markup: "); ++stages.back(); if (flag & MTPmessages_sendMedia::flag_reply_markup) { types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 2 IN FIELD flags ]"); } break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messages_forwardMessages(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messages_forwardMessages");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  broadcast: "); ++stages.back(); if (flag & MTPmessages_forwardMessages::flag_broadcast) { to.add("YES [ BY BIT 4 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 4 IN FIELD flags ]"); } break;
	case 2: to.add("  from_peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 3: to.add("  id: "); ++stages.back(); types.push_back(0); vtypes.push_back(mtpc_int); stages.push_back(0); flags.push_back(0); break;
	case 4: to.add("  random_id: "); ++stages.back(); types.push_back(0); vtypes.push_back(mtpc_long); stages.push_back(0); flags.push_back(0); break;
	case 5: to.add("  to_peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messages_editChatTitle(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_editChatPhoto(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_addChatUser(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_deleteChatUser(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_createChat(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_forwardMessage(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_sendBroadcast(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_importChatInvite(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_startBot(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messages_startBot");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  bot: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  random_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 3: to.add("  start_param: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messages_toggleChatAdmins(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messages_toggleChatAdmins");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  chat_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  enabled: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messages_migrateChat(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messages_migrateChat");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  chat_id: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messages_sendInlineBotResult(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messages_sendInlineBotResult");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  broadcast: "); ++stages.back(); if (flag & MTPmessages_sendInlineBotResult::flag_broadcast) { to.add("YES [ BY BIT 4 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 4 IN FIELD flags ]"); } break;
	case 2: to.add("  peer: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 3: to.add("  reply_to_msg_id: "); ++stages.back(); if (flag & MTPmessages_sendInlineBotResult::flag_reply_to_msg_id) { types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); } else { to.add("[ SKIPPED BY BIT 0 IN FIELD flags ]"); } break;
	case 4: to.add("  random_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 5: to.add("  query_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 6: to.add("  id: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_channels_createChannel(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ channels_createChannel");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  flags: "); ++stages.back(); if (start >= end) throw Exception("start >= end in flags"); else flags.back() = *start; types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  broadcast: "); ++stages.back(); if (flag & MTPchannels_createChannel::flag_broadcast) { to.add("YES [ BY BIT 0 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 0 IN FIELD flags ]"); } break;
	case 2: to.add("  megagroup: "); ++stages.back(); if (flag & MTPchannels_createChannel::flag_megagroup) { to.add("YES [ BY BIT 1 IN FIELD flags ]"); } else { to.add("[ SKIPPED BY BIT 1 IN FIELD flags ]"); } break;
	case 3: to.add("  title: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 4: to.add("  about: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_channels_editAdmin(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ channels_editAdmin");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  channel: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  user_id: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  role: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_channels_editTitle(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ channels_editTitle");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  channel: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  title: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_channels_editPhoto(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ channels_editPhoto");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  channel: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  photo: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_channels_toggleComments(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ channels_toggleComments");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  channel: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  enabled: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_channels_joinChannel(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ channels_joinChannel");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  channel: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_channels_leaveChannel(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ channels_leaveChannel");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  channel: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_channels_inviteToChannel(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ channels_inviteToChannel");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  channel: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  users: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_channels_kickFromChannel(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ channels_kickFromChannel");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  channel: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  user_id: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  kicked: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_channels_deleteChannel(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ channels_deleteChannel");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  channel: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messages_getChats(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_channels_getChannels(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ channels_getChannels");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  id: "); ++stages.back(); types.push_back(00); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messages_getFullChat(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_channels_getFullChannel(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ channels_getFullChannel");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  channel: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messages_getDhConfig(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_requestEncryption(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_acceptEncryption(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_sendEncrypted(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_sendEncryptedFile(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_sendEncryptedService(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_receivedQueue(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_photos_deletePhotos(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_getStickers(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_getAllStickers(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messages_getAllStickers");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  hash: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messages_getWebPagePreview(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_exportChatInvite(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_channels_exportInvite(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ channels_exportInvite");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  channel: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messages_checkChatInvite(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_messages_getStickerSet(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messages_getStickerSet");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  stickerset: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messages_getDocumentByHash(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messages_getDocumentByHash");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  sha256: "); ++stages.back(); types.push_back(mtpc_bytes); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  size: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  mime_type: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messages_searchGifs(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messages_searchGifs");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  q: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  offset: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messages_getSavedGifs(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messages_getSavedGifs");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  hash: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_messages_getInlineBotResults(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ messages_getInlineBotResults");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  bot: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  query: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  offset: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_updates_getState(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ updates_getState }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_updates_getDifference(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_updates_getChannelDifference(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ updates_getChannelDifference");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  channel: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  filter: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  pts: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 3: to.add("  limit: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_photos_updateProfilePhoto(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_photos_uploadProfilePhoto(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_photos_getUserPhotos(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ photos_getUserPhotos");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  user_id: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  offset: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  max_id: "); ++stages.back(); types.push_back(mtpc_long); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 3: to.add("  limit: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_upload_getFile(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_help_getConfig(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ help_getConfig }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_help_getNearestDc(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ help_getNearestDc }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_help_getAppUpdate(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_help_getInviteText(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_help_getSupport(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	to.add("{ help_getSupport }"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
}

void _serialize_help_getAppChangelog(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ help_getAppChangelog");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  device_model: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  system_version: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  app_version: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 3: to.add("  lang_code: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_help_getTermsOfService(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ help_getTermsOfService");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  lang_code: "); ++stages.back(); types.push_back(mtpc_string); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_channels_getParticipants(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ channels_getParticipants");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  channel: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  filter: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 2: to.add("  offset: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 3: to.add("  limit: "); ++stages.back(); types.push_back(mtpc_int); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_channels_getParticipant(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
	if (stage) {
		to.add(",\n").addSpaces(lev);
	} else {
		to.add("{ channels_getParticipant");
		to.add("\n").addSpaces(lev);
	}
	switch (stage) {
	case 0: to.add("  channel: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	case 1: to.add("  user_id: "); ++stages.back(); types.push_back(0); vtypes.push_back(0); stages.push_back(0); flags.push_back(0); break;
	default: to.add("}"); types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back(); break;
	}
}

void _serialize_rpc_result(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_msg_container(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

void _serialize_core_message(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag) {
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
}

namespace {
	typedef void(*mtpTextSerializer)(MTPStringLogger &to, int32 stage, int32 lev, Types &types, Types &vtypes, StagesFlags &stages, StagesFlags &flags, const mtpPrime *start, const mtpPrime *end, int32 flag);
	typedef QMap<mtpTypeId, mtpTextSerializer> TextSerializers;
	TextSerializers _serializers;

	void initTextSerializers() {
		_serializers.insert(mtpc_resPQ, _serialize_resPQ);
		_serializers.insert(mtpc_p_q_inner_data, _serialize_p_q_inner_data);
		_serializers.insert(mtpc_server_DH_params_fail, _serialize_server_DH_params_fail);
		_serializers.insert(mtpc_server_DH_params_ok, _serialize_server_DH_params_ok);
		_serializers.insert(mtpc_server_DH_inner_data, _serialize_server_DH_inner_data);
		_serializers.insert(mtpc_client_DH_inner_data, _serialize_client_DH_inner_data);
		_serializers.insert(mtpc_dh_gen_ok, _serialize_dh_gen_ok);
		_serializers.insert(mtpc_dh_gen_retry, _serialize_dh_gen_retry);
		_serializers.insert(mtpc_dh_gen_fail, _serialize_dh_gen_fail);
		_serializers.insert(mtpc_msgs_ack, _serialize_msgs_ack);
		_serializers.insert(mtpc_bad_msg_notification, _serialize_bad_msg_notification);
		_serializers.insert(mtpc_bad_server_salt, _serialize_bad_server_salt);
		_serializers.insert(mtpc_msgs_state_req, _serialize_msgs_state_req);
		_serializers.insert(mtpc_msgs_state_info, _serialize_msgs_state_info);
		_serializers.insert(mtpc_msgs_all_info, _serialize_msgs_all_info);
		_serializers.insert(mtpc_msg_detailed_info, _serialize_msg_detailed_info);
		_serializers.insert(mtpc_msg_new_detailed_info, _serialize_msg_new_detailed_info);
		_serializers.insert(mtpc_msg_resend_req, _serialize_msg_resend_req);
		_serializers.insert(mtpc_rpc_error, _serialize_rpc_error);
		_serializers.insert(mtpc_rpc_answer_unknown, _serialize_rpc_answer_unknown);
		_serializers.insert(mtpc_rpc_answer_dropped_running, _serialize_rpc_answer_dropped_running);
		_serializers.insert(mtpc_rpc_answer_dropped, _serialize_rpc_answer_dropped);
		_serializers.insert(mtpc_future_salt, _serialize_future_salt);
		_serializers.insert(mtpc_future_salts, _serialize_future_salts);
		_serializers.insert(mtpc_pong, _serialize_pong);
		_serializers.insert(mtpc_destroy_session_ok, _serialize_destroy_session_ok);
		_serializers.insert(mtpc_destroy_session_none, _serialize_destroy_session_none);
		_serializers.insert(mtpc_new_session_created, _serialize_new_session_created);
		_serializers.insert(mtpc_http_wait, _serialize_http_wait);
		_serializers.insert(mtpc_boolFalse, _serialize_boolFalse);
		_serializers.insert(mtpc_boolTrue, _serialize_boolTrue);
		_serializers.insert(mtpc_true, _serialize_true);
		_serializers.insert(mtpc_error, _serialize_error);
		_serializers.insert(mtpc_null, _serialize_null);
		_serializers.insert(mtpc_inputPeerEmpty, _serialize_inputPeerEmpty);
		_serializers.insert(mtpc_inputPeerSelf, _serialize_inputPeerSelf);
		_serializers.insert(mtpc_inputPeerChat, _serialize_inputPeerChat);
		_serializers.insert(mtpc_inputPeerUser, _serialize_inputPeerUser);
		_serializers.insert(mtpc_inputPeerChannel, _serialize_inputPeerChannel);
		_serializers.insert(mtpc_inputUserEmpty, _serialize_inputUserEmpty);
		_serializers.insert(mtpc_inputUserSelf, _serialize_inputUserSelf);
		_serializers.insert(mtpc_inputUser, _serialize_inputUser);
		_serializers.insert(mtpc_inputPhoneContact, _serialize_inputPhoneContact);
		_serializers.insert(mtpc_inputFile, _serialize_inputFile);
		_serializers.insert(mtpc_inputFileBig, _serialize_inputFileBig);
		_serializers.insert(mtpc_inputMediaEmpty, _serialize_inputMediaEmpty);
		_serializers.insert(mtpc_inputMediaUploadedPhoto, _serialize_inputMediaUploadedPhoto);
		_serializers.insert(mtpc_inputMediaPhoto, _serialize_inputMediaPhoto);
		_serializers.insert(mtpc_inputMediaGeoPoint, _serialize_inputMediaGeoPoint);
		_serializers.insert(mtpc_inputMediaContact, _serialize_inputMediaContact);
		_serializers.insert(mtpc_inputMediaUploadedVideo, _serialize_inputMediaUploadedVideo);
		_serializers.insert(mtpc_inputMediaUploadedThumbVideo, _serialize_inputMediaUploadedThumbVideo);
		_serializers.insert(mtpc_inputMediaVideo, _serialize_inputMediaVideo);
		_serializers.insert(mtpc_inputMediaUploadedAudio, _serialize_inputMediaUploadedAudio);
		_serializers.insert(mtpc_inputMediaAudio, _serialize_inputMediaAudio);
		_serializers.insert(mtpc_inputMediaUploadedDocument, _serialize_inputMediaUploadedDocument);
		_serializers.insert(mtpc_inputMediaUploadedThumbDocument, _serialize_inputMediaUploadedThumbDocument);
		_serializers.insert(mtpc_inputMediaDocument, _serialize_inputMediaDocument);
		_serializers.insert(mtpc_inputMediaVenue, _serialize_inputMediaVenue);
		_serializers.insert(mtpc_inputMediaGifExternal, _serialize_inputMediaGifExternal);
		_serializers.insert(mtpc_inputChatPhotoEmpty, _serialize_inputChatPhotoEmpty);
		_serializers.insert(mtpc_inputChatUploadedPhoto, _serialize_inputChatUploadedPhoto);
		_serializers.insert(mtpc_inputChatPhoto, _serialize_inputChatPhoto);
		_serializers.insert(mtpc_inputGeoPointEmpty, _serialize_inputGeoPointEmpty);
		_serializers.insert(mtpc_inputGeoPoint, _serialize_inputGeoPoint);
		_serializers.insert(mtpc_inputPhotoEmpty, _serialize_inputPhotoEmpty);
		_serializers.insert(mtpc_inputPhoto, _serialize_inputPhoto);
		_serializers.insert(mtpc_inputVideoEmpty, _serialize_inputVideoEmpty);
		_serializers.insert(mtpc_inputVideo, _serialize_inputVideo);
		_serializers.insert(mtpc_inputFileLocation, _serialize_inputFileLocation);
		_serializers.insert(mtpc_inputVideoFileLocation, _serialize_inputVideoFileLocation);
		_serializers.insert(mtpc_inputEncryptedFileLocation, _serialize_inputEncryptedFileLocation);
		_serializers.insert(mtpc_inputAudioFileLocation, _serialize_inputAudioFileLocation);
		_serializers.insert(mtpc_inputDocumentFileLocation, _serialize_inputDocumentFileLocation);
		_serializers.insert(mtpc_inputPhotoCropAuto, _serialize_inputPhotoCropAuto);
		_serializers.insert(mtpc_inputPhotoCrop, _serialize_inputPhotoCrop);
		_serializers.insert(mtpc_inputAppEvent, _serialize_inputAppEvent);
		_serializers.insert(mtpc_peerUser, _serialize_peerUser);
		_serializers.insert(mtpc_peerChat, _serialize_peerChat);
		_serializers.insert(mtpc_peerChannel, _serialize_peerChannel);
		_serializers.insert(mtpc_storage_fileUnknown, _serialize_storage_fileUnknown);
		_serializers.insert(mtpc_storage_fileJpeg, _serialize_storage_fileJpeg);
		_serializers.insert(mtpc_storage_fileGif, _serialize_storage_fileGif);
		_serializers.insert(mtpc_storage_filePng, _serialize_storage_filePng);
		_serializers.insert(mtpc_storage_filePdf, _serialize_storage_filePdf);
		_serializers.insert(mtpc_storage_fileMp3, _serialize_storage_fileMp3);
		_serializers.insert(mtpc_storage_fileMov, _serialize_storage_fileMov);
		_serializers.insert(mtpc_storage_filePartial, _serialize_storage_filePartial);
		_serializers.insert(mtpc_storage_fileMp4, _serialize_storage_fileMp4);
		_serializers.insert(mtpc_storage_fileWebp, _serialize_storage_fileWebp);
		_serializers.insert(mtpc_fileLocationUnavailable, _serialize_fileLocationUnavailable);
		_serializers.insert(mtpc_fileLocation, _serialize_fileLocation);
		_serializers.insert(mtpc_userEmpty, _serialize_userEmpty);
		_serializers.insert(mtpc_user, _serialize_user);
		_serializers.insert(mtpc_userProfilePhotoEmpty, _serialize_userProfilePhotoEmpty);
		_serializers.insert(mtpc_userProfilePhoto, _serialize_userProfilePhoto);
		_serializers.insert(mtpc_userStatusEmpty, _serialize_userStatusEmpty);
		_serializers.insert(mtpc_userStatusOnline, _serialize_userStatusOnline);
		_serializers.insert(mtpc_userStatusOffline, _serialize_userStatusOffline);
		_serializers.insert(mtpc_userStatusRecently, _serialize_userStatusRecently);
		_serializers.insert(mtpc_userStatusLastWeek, _serialize_userStatusLastWeek);
		_serializers.insert(mtpc_userStatusLastMonth, _serialize_userStatusLastMonth);
		_serializers.insert(mtpc_chatEmpty, _serialize_chatEmpty);
		_serializers.insert(mtpc_chat, _serialize_chat);
		_serializers.insert(mtpc_chatForbidden, _serialize_chatForbidden);
		_serializers.insert(mtpc_channel, _serialize_channel);
		_serializers.insert(mtpc_channelForbidden, _serialize_channelForbidden);
		_serializers.insert(mtpc_chatFull, _serialize_chatFull);
		_serializers.insert(mtpc_channelFull, _serialize_channelFull);
		_serializers.insert(mtpc_chatParticipant, _serialize_chatParticipant);
		_serializers.insert(mtpc_chatParticipantCreator, _serialize_chatParticipantCreator);
		_serializers.insert(mtpc_chatParticipantAdmin, _serialize_chatParticipantAdmin);
		_serializers.insert(mtpc_chatParticipantsForbidden, _serialize_chatParticipantsForbidden);
		_serializers.insert(mtpc_chatParticipants, _serialize_chatParticipants);
		_serializers.insert(mtpc_chatPhotoEmpty, _serialize_chatPhotoEmpty);
		_serializers.insert(mtpc_chatPhoto, _serialize_chatPhoto);
		_serializers.insert(mtpc_messageEmpty, _serialize_messageEmpty);
		_serializers.insert(mtpc_message, _serialize_message);
		_serializers.insert(mtpc_messageService, _serialize_messageService);
		_serializers.insert(mtpc_messageMediaEmpty, _serialize_messageMediaEmpty);
		_serializers.insert(mtpc_messageMediaPhoto, _serialize_messageMediaPhoto);
		_serializers.insert(mtpc_messageMediaVideo, _serialize_messageMediaVideo);
		_serializers.insert(mtpc_messageMediaGeo, _serialize_messageMediaGeo);
		_serializers.insert(mtpc_messageMediaContact, _serialize_messageMediaContact);
		_serializers.insert(mtpc_messageMediaUnsupported, _serialize_messageMediaUnsupported);
		_serializers.insert(mtpc_messageMediaDocument, _serialize_messageMediaDocument);
		_serializers.insert(mtpc_messageMediaAudio, _serialize_messageMediaAudio);
		_serializers.insert(mtpc_messageMediaWebPage, _serialize_messageMediaWebPage);
		_serializers.insert(mtpc_messageMediaVenue, _serialize_messageMediaVenue);
		_serializers.insert(mtpc_messageActionEmpty, _serialize_messageActionEmpty);
		_serializers.insert(mtpc_messageActionChatCreate, _serialize_messageActionChatCreate);
		_serializers.insert(mtpc_messageActionChatEditTitle, _serialize_messageActionChatEditTitle);
		_serializers.insert(mtpc_messageActionChatEditPhoto, _serialize_messageActionChatEditPhoto);
		_serializers.insert(mtpc_messageActionChatDeletePhoto, _serialize_messageActionChatDeletePhoto);
		_serializers.insert(mtpc_messageActionChatAddUser, _serialize_messageActionChatAddUser);
		_serializers.insert(mtpc_messageActionChatDeleteUser, _serialize_messageActionChatDeleteUser);
		_serializers.insert(mtpc_messageActionChatJoinedByLink, _serialize_messageActionChatJoinedByLink);
		_serializers.insert(mtpc_messageActionChannelCreate, _serialize_messageActionChannelCreate);
		_serializers.insert(mtpc_messageActionChatMigrateTo, _serialize_messageActionChatMigrateTo);
		_serializers.insert(mtpc_messageActionChannelMigrateFrom, _serialize_messageActionChannelMigrateFrom);
		_serializers.insert(mtpc_dialog, _serialize_dialog);
		_serializers.insert(mtpc_dialogChannel, _serialize_dialogChannel);
		_serializers.insert(mtpc_photoEmpty, _serialize_photoEmpty);
		_serializers.insert(mtpc_photo, _serialize_photo);
		_serializers.insert(mtpc_photoSizeEmpty, _serialize_photoSizeEmpty);
		_serializers.insert(mtpc_photoSize, _serialize_photoSize);
		_serializers.insert(mtpc_photoCachedSize, _serialize_photoCachedSize);
		_serializers.insert(mtpc_videoEmpty, _serialize_videoEmpty);
		_serializers.insert(mtpc_video, _serialize_video);
		_serializers.insert(mtpc_geoPointEmpty, _serialize_geoPointEmpty);
		_serializers.insert(mtpc_geoPoint, _serialize_geoPoint);
		_serializers.insert(mtpc_auth_checkedPhone, _serialize_auth_checkedPhone);
		_serializers.insert(mtpc_auth_sentCode, _serialize_auth_sentCode);
		_serializers.insert(mtpc_auth_sentAppCode, _serialize_auth_sentAppCode);
		_serializers.insert(mtpc_auth_authorization, _serialize_auth_authorization);
		_serializers.insert(mtpc_auth_exportedAuthorization, _serialize_auth_exportedAuthorization);
		_serializers.insert(mtpc_inputNotifyPeer, _serialize_inputNotifyPeer);
		_serializers.insert(mtpc_inputNotifyUsers, _serialize_inputNotifyUsers);
		_serializers.insert(mtpc_inputNotifyChats, _serialize_inputNotifyChats);
		_serializers.insert(mtpc_inputNotifyAll, _serialize_inputNotifyAll);
		_serializers.insert(mtpc_inputPeerNotifyEventsEmpty, _serialize_inputPeerNotifyEventsEmpty);
		_serializers.insert(mtpc_inputPeerNotifyEventsAll, _serialize_inputPeerNotifyEventsAll);
		_serializers.insert(mtpc_inputPeerNotifySettings, _serialize_inputPeerNotifySettings);
		_serializers.insert(mtpc_peerNotifyEventsEmpty, _serialize_peerNotifyEventsEmpty);
		_serializers.insert(mtpc_peerNotifyEventsAll, _serialize_peerNotifyEventsAll);
		_serializers.insert(mtpc_peerNotifySettingsEmpty, _serialize_peerNotifySettingsEmpty);
		_serializers.insert(mtpc_peerNotifySettings, _serialize_peerNotifySettings);
		_serializers.insert(mtpc_wallPaper, _serialize_wallPaper);
		_serializers.insert(mtpc_wallPaperSolid, _serialize_wallPaperSolid);
		_serializers.insert(mtpc_inputReportReasonSpam, _serialize_inputReportReasonSpam);
		_serializers.insert(mtpc_inputReportReasonViolence, _serialize_inputReportReasonViolence);
		_serializers.insert(mtpc_inputReportReasonPornography, _serialize_inputReportReasonPornography);
		_serializers.insert(mtpc_inputReportReasonOther, _serialize_inputReportReasonOther);
		_serializers.insert(mtpc_userFull, _serialize_userFull);
		_serializers.insert(mtpc_contact, _serialize_contact);
		_serializers.insert(mtpc_importedContact, _serialize_importedContact);
		_serializers.insert(mtpc_contactBlocked, _serialize_contactBlocked);
		_serializers.insert(mtpc_contactSuggested, _serialize_contactSuggested);
		_serializers.insert(mtpc_contactStatus, _serialize_contactStatus);
		_serializers.insert(mtpc_contacts_link, _serialize_contacts_link);
		_serializers.insert(mtpc_contacts_contactsNotModified, _serialize_contacts_contactsNotModified);
		_serializers.insert(mtpc_contacts_contacts, _serialize_contacts_contacts);
		_serializers.insert(mtpc_contacts_importedContacts, _serialize_contacts_importedContacts);
		_serializers.insert(mtpc_contacts_blocked, _serialize_contacts_blocked);
		_serializers.insert(mtpc_contacts_blockedSlice, _serialize_contacts_blockedSlice);
		_serializers.insert(mtpc_contacts_suggested, _serialize_contacts_suggested);
		_serializers.insert(mtpc_messages_dialogs, _serialize_messages_dialogs);
		_serializers.insert(mtpc_messages_dialogsSlice, _serialize_messages_dialogsSlice);
		_serializers.insert(mtpc_messages_messages, _serialize_messages_messages);
		_serializers.insert(mtpc_messages_messagesSlice, _serialize_messages_messagesSlice);
		_serializers.insert(mtpc_messages_channelMessages, _serialize_messages_channelMessages);
		_serializers.insert(mtpc_messages_chats, _serialize_messages_chats);
		_serializers.insert(mtpc_messages_chatFull, _serialize_messages_chatFull);
		_serializers.insert(mtpc_messages_affectedHistory, _serialize_messages_affectedHistory);
		_serializers.insert(mtpc_inputMessagesFilterEmpty, _serialize_inputMessagesFilterEmpty);
		_serializers.insert(mtpc_inputMessagesFilterPhotos, _serialize_inputMessagesFilterPhotos);
		_serializers.insert(mtpc_inputMessagesFilterVideo, _serialize_inputMessagesFilterVideo);
		_serializers.insert(mtpc_inputMessagesFilterPhotoVideo, _serialize_inputMessagesFilterPhotoVideo);
		_serializers.insert(mtpc_inputMessagesFilterPhotoVideoDocuments, _serialize_inputMessagesFilterPhotoVideoDocuments);
		_serializers.insert(mtpc_inputMessagesFilterDocument, _serialize_inputMessagesFilterDocument);
		_serializers.insert(mtpc_inputMessagesFilterAudio, _serialize_inputMessagesFilterAudio);
		_serializers.insert(mtpc_inputMessagesFilterAudioDocuments, _serialize_inputMessagesFilterAudioDocuments);
		_serializers.insert(mtpc_inputMessagesFilterUrl, _serialize_inputMessagesFilterUrl);
		_serializers.insert(mtpc_inputMessagesFilterGif, _serialize_inputMessagesFilterGif);
		_serializers.insert(mtpc_updateNewMessage, _serialize_updateNewMessage);
		_serializers.insert(mtpc_updateMessageID, _serialize_updateMessageID);
		_serializers.insert(mtpc_updateDeleteMessages, _serialize_updateDeleteMessages);
		_serializers.insert(mtpc_updateUserTyping, _serialize_updateUserTyping);
		_serializers.insert(mtpc_updateChatUserTyping, _serialize_updateChatUserTyping);
		_serializers.insert(mtpc_updateChatParticipants, _serialize_updateChatParticipants);
		_serializers.insert(mtpc_updateUserStatus, _serialize_updateUserStatus);
		_serializers.insert(mtpc_updateUserName, _serialize_updateUserName);
		_serializers.insert(mtpc_updateUserPhoto, _serialize_updateUserPhoto);
		_serializers.insert(mtpc_updateContactRegistered, _serialize_updateContactRegistered);
		_serializers.insert(mtpc_updateContactLink, _serialize_updateContactLink);
		_serializers.insert(mtpc_updateNewAuthorization, _serialize_updateNewAuthorization);
		_serializers.insert(mtpc_updateNewEncryptedMessage, _serialize_updateNewEncryptedMessage);
		_serializers.insert(mtpc_updateEncryptedChatTyping, _serialize_updateEncryptedChatTyping);
		_serializers.insert(mtpc_updateEncryption, _serialize_updateEncryption);
		_serializers.insert(mtpc_updateEncryptedMessagesRead, _serialize_updateEncryptedMessagesRead);
		_serializers.insert(mtpc_updateChatParticipantAdd, _serialize_updateChatParticipantAdd);
		_serializers.insert(mtpc_updateChatParticipantDelete, _serialize_updateChatParticipantDelete);
		_serializers.insert(mtpc_updateDcOptions, _serialize_updateDcOptions);
		_serializers.insert(mtpc_updateUserBlocked, _serialize_updateUserBlocked);
		_serializers.insert(mtpc_updateNotifySettings, _serialize_updateNotifySettings);
		_serializers.insert(mtpc_updateServiceNotification, _serialize_updateServiceNotification);
		_serializers.insert(mtpc_updatePrivacy, _serialize_updatePrivacy);
		_serializers.insert(mtpc_updateUserPhone, _serialize_updateUserPhone);
		_serializers.insert(mtpc_updateReadHistoryInbox, _serialize_updateReadHistoryInbox);
		_serializers.insert(mtpc_updateReadHistoryOutbox, _serialize_updateReadHistoryOutbox);
		_serializers.insert(mtpc_updateWebPage, _serialize_updateWebPage);
		_serializers.insert(mtpc_updateReadMessagesContents, _serialize_updateReadMessagesContents);
		_serializers.insert(mtpc_updateChannelTooLong, _serialize_updateChannelTooLong);
		_serializers.insert(mtpc_updateChannel, _serialize_updateChannel);
		_serializers.insert(mtpc_updateChannelGroup, _serialize_updateChannelGroup);
		_serializers.insert(mtpc_updateNewChannelMessage, _serialize_updateNewChannelMessage);
		_serializers.insert(mtpc_updateReadChannelInbox, _serialize_updateReadChannelInbox);
		_serializers.insert(mtpc_updateDeleteChannelMessages, _serialize_updateDeleteChannelMessages);
		_serializers.insert(mtpc_updateChannelMessageViews, _serialize_updateChannelMessageViews);
		_serializers.insert(mtpc_updateChatAdmins, _serialize_updateChatAdmins);
		_serializers.insert(mtpc_updateChatParticipantAdmin, _serialize_updateChatParticipantAdmin);
		_serializers.insert(mtpc_updateNewStickerSet, _serialize_updateNewStickerSet);
		_serializers.insert(mtpc_updateStickerSetsOrder, _serialize_updateStickerSetsOrder);
		_serializers.insert(mtpc_updateStickerSets, _serialize_updateStickerSets);
		_serializers.insert(mtpc_updateSavedGifs, _serialize_updateSavedGifs);
		_serializers.insert(mtpc_updateBotInlineQuery, _serialize_updateBotInlineQuery);
		_serializers.insert(mtpc_updates_state, _serialize_updates_state);
		_serializers.insert(mtpc_updates_differenceEmpty, _serialize_updates_differenceEmpty);
		_serializers.insert(mtpc_updates_difference, _serialize_updates_difference);
		_serializers.insert(mtpc_updates_differenceSlice, _serialize_updates_differenceSlice);
		_serializers.insert(mtpc_updatesTooLong, _serialize_updatesTooLong);
		_serializers.insert(mtpc_updateShortMessage, _serialize_updateShortMessage);
		_serializers.insert(mtpc_updateShortChatMessage, _serialize_updateShortChatMessage);
		_serializers.insert(mtpc_updateShort, _serialize_updateShort);
		_serializers.insert(mtpc_updatesCombined, _serialize_updatesCombined);
		_serializers.insert(mtpc_updates, _serialize_updates);
		_serializers.insert(mtpc_updateShortSentMessage, _serialize_updateShortSentMessage);
		_serializers.insert(mtpc_photos_photos, _serialize_photos_photos);
		_serializers.insert(mtpc_photos_photosSlice, _serialize_photos_photosSlice);
		_serializers.insert(mtpc_photos_photo, _serialize_photos_photo);
		_serializers.insert(mtpc_upload_file, _serialize_upload_file);
		_serializers.insert(mtpc_dcOption, _serialize_dcOption);
		_serializers.insert(mtpc_config, _serialize_config);
		_serializers.insert(mtpc_nearestDc, _serialize_nearestDc);
		_serializers.insert(mtpc_help_appUpdate, _serialize_help_appUpdate);
		_serializers.insert(mtpc_help_noAppUpdate, _serialize_help_noAppUpdate);
		_serializers.insert(mtpc_help_inviteText, _serialize_help_inviteText);
		_serializers.insert(mtpc_encryptedChatEmpty, _serialize_encryptedChatEmpty);
		_serializers.insert(mtpc_encryptedChatWaiting, _serialize_encryptedChatWaiting);
		_serializers.insert(mtpc_encryptedChatRequested, _serialize_encryptedChatRequested);
		_serializers.insert(mtpc_encryptedChat, _serialize_encryptedChat);
		_serializers.insert(mtpc_encryptedChatDiscarded, _serialize_encryptedChatDiscarded);
		_serializers.insert(mtpc_inputEncryptedChat, _serialize_inputEncryptedChat);
		_serializers.insert(mtpc_encryptedFileEmpty, _serialize_encryptedFileEmpty);
		_serializers.insert(mtpc_encryptedFile, _serialize_encryptedFile);
		_serializers.insert(mtpc_inputEncryptedFileEmpty, _serialize_inputEncryptedFileEmpty);
		_serializers.insert(mtpc_inputEncryptedFileUploaded, _serialize_inputEncryptedFileUploaded);
		_serializers.insert(mtpc_inputEncryptedFile, _serialize_inputEncryptedFile);
		_serializers.insert(mtpc_inputEncryptedFileBigUploaded, _serialize_inputEncryptedFileBigUploaded);
		_serializers.insert(mtpc_encryptedMessage, _serialize_encryptedMessage);
		_serializers.insert(mtpc_encryptedMessageService, _serialize_encryptedMessageService);
		_serializers.insert(mtpc_messages_dhConfigNotModified, _serialize_messages_dhConfigNotModified);
		_serializers.insert(mtpc_messages_dhConfig, _serialize_messages_dhConfig);
		_serializers.insert(mtpc_messages_sentEncryptedMessage, _serialize_messages_sentEncryptedMessage);
		_serializers.insert(mtpc_messages_sentEncryptedFile, _serialize_messages_sentEncryptedFile);
		_serializers.insert(mtpc_inputAudioEmpty, _serialize_inputAudioEmpty);
		_serializers.insert(mtpc_inputAudio, _serialize_inputAudio);
		_serializers.insert(mtpc_inputDocumentEmpty, _serialize_inputDocumentEmpty);
		_serializers.insert(mtpc_inputDocument, _serialize_inputDocument);
		_serializers.insert(mtpc_audioEmpty, _serialize_audioEmpty);
		_serializers.insert(mtpc_audio, _serialize_audio);
		_serializers.insert(mtpc_documentEmpty, _serialize_documentEmpty);
		_serializers.insert(mtpc_document, _serialize_document);
		_serializers.insert(mtpc_help_support, _serialize_help_support);
		_serializers.insert(mtpc_notifyPeer, _serialize_notifyPeer);
		_serializers.insert(mtpc_notifyUsers, _serialize_notifyUsers);
		_serializers.insert(mtpc_notifyChats, _serialize_notifyChats);
		_serializers.insert(mtpc_notifyAll, _serialize_notifyAll);
		_serializers.insert(mtpc_sendMessageTypingAction, _serialize_sendMessageTypingAction);
		_serializers.insert(mtpc_sendMessageCancelAction, _serialize_sendMessageCancelAction);
		_serializers.insert(mtpc_sendMessageRecordVideoAction, _serialize_sendMessageRecordVideoAction);
		_serializers.insert(mtpc_sendMessageUploadVideoAction, _serialize_sendMessageUploadVideoAction);
		_serializers.insert(mtpc_sendMessageRecordAudioAction, _serialize_sendMessageRecordAudioAction);
		_serializers.insert(mtpc_sendMessageUploadAudioAction, _serialize_sendMessageUploadAudioAction);
		_serializers.insert(mtpc_sendMessageUploadPhotoAction, _serialize_sendMessageUploadPhotoAction);
		_serializers.insert(mtpc_sendMessageUploadDocumentAction, _serialize_sendMessageUploadDocumentAction);
		_serializers.insert(mtpc_sendMessageGeoLocationAction, _serialize_sendMessageGeoLocationAction);
		_serializers.insert(mtpc_sendMessageChooseContactAction, _serialize_sendMessageChooseContactAction);
		_serializers.insert(mtpc_contacts_found, _serialize_contacts_found);
		_serializers.insert(mtpc_inputPrivacyKeyStatusTimestamp, _serialize_inputPrivacyKeyStatusTimestamp);
		_serializers.insert(mtpc_privacyKeyStatusTimestamp, _serialize_privacyKeyStatusTimestamp);
		_serializers.insert(mtpc_inputPrivacyValueAllowContacts, _serialize_inputPrivacyValueAllowContacts);
		_serializers.insert(mtpc_inputPrivacyValueAllowAll, _serialize_inputPrivacyValueAllowAll);
		_serializers.insert(mtpc_inputPrivacyValueAllowUsers, _serialize_inputPrivacyValueAllowUsers);
		_serializers.insert(mtpc_inputPrivacyValueDisallowContacts, _serialize_inputPrivacyValueDisallowContacts);
		_serializers.insert(mtpc_inputPrivacyValueDisallowAll, _serialize_inputPrivacyValueDisallowAll);
		_serializers.insert(mtpc_inputPrivacyValueDisallowUsers, _serialize_inputPrivacyValueDisallowUsers);
		_serializers.insert(mtpc_privacyValueAllowContacts, _serialize_privacyValueAllowContacts);
		_serializers.insert(mtpc_privacyValueAllowAll, _serialize_privacyValueAllowAll);
		_serializers.insert(mtpc_privacyValueAllowUsers, _serialize_privacyValueAllowUsers);
		_serializers.insert(mtpc_privacyValueDisallowContacts, _serialize_privacyValueDisallowContacts);
		_serializers.insert(mtpc_privacyValueDisallowAll, _serialize_privacyValueDisallowAll);
		_serializers.insert(mtpc_privacyValueDisallowUsers, _serialize_privacyValueDisallowUsers);
		_serializers.insert(mtpc_account_privacyRules, _serialize_account_privacyRules);
		_serializers.insert(mtpc_accountDaysTTL, _serialize_accountDaysTTL);
		_serializers.insert(mtpc_account_sentChangePhoneCode, _serialize_account_sentChangePhoneCode);
		_serializers.insert(mtpc_documentAttributeImageSize, _serialize_documentAttributeImageSize);
		_serializers.insert(mtpc_documentAttributeAnimated, _serialize_documentAttributeAnimated);
		_serializers.insert(mtpc_documentAttributeSticker, _serialize_documentAttributeSticker);
		_serializers.insert(mtpc_documentAttributeVideo, _serialize_documentAttributeVideo);
		_serializers.insert(mtpc_documentAttributeAudio, _serialize_documentAttributeAudio);
		_serializers.insert(mtpc_documentAttributeFilename, _serialize_documentAttributeFilename);
		_serializers.insert(mtpc_messages_stickersNotModified, _serialize_messages_stickersNotModified);
		_serializers.insert(mtpc_messages_stickers, _serialize_messages_stickers);
		_serializers.insert(mtpc_stickerPack, _serialize_stickerPack);
		_serializers.insert(mtpc_messages_allStickersNotModified, _serialize_messages_allStickersNotModified);
		_serializers.insert(mtpc_messages_allStickers, _serialize_messages_allStickers);
		_serializers.insert(mtpc_disabledFeature, _serialize_disabledFeature);
		_serializers.insert(mtpc_messages_affectedMessages, _serialize_messages_affectedMessages);
		_serializers.insert(mtpc_contactLinkUnknown, _serialize_contactLinkUnknown);
		_serializers.insert(mtpc_contactLinkNone, _serialize_contactLinkNone);
		_serializers.insert(mtpc_contactLinkHasPhone, _serialize_contactLinkHasPhone);
		_serializers.insert(mtpc_contactLinkContact, _serialize_contactLinkContact);
		_serializers.insert(mtpc_webPageEmpty, _serialize_webPageEmpty);
		_serializers.insert(mtpc_webPagePending, _serialize_webPagePending);
		_serializers.insert(mtpc_webPage, _serialize_webPage);
		_serializers.insert(mtpc_authorization, _serialize_authorization);
		_serializers.insert(mtpc_account_authorizations, _serialize_account_authorizations);
		_serializers.insert(mtpc_account_noPassword, _serialize_account_noPassword);
		_serializers.insert(mtpc_account_password, _serialize_account_password);
		_serializers.insert(mtpc_account_passwordSettings, _serialize_account_passwordSettings);
		_serializers.insert(mtpc_account_passwordInputSettings, _serialize_account_passwordInputSettings);
		_serializers.insert(mtpc_auth_passwordRecovery, _serialize_auth_passwordRecovery);
		_serializers.insert(mtpc_receivedNotifyMessage, _serialize_receivedNotifyMessage);
		_serializers.insert(mtpc_chatInviteEmpty, _serialize_chatInviteEmpty);
		_serializers.insert(mtpc_chatInviteExported, _serialize_chatInviteExported);
		_serializers.insert(mtpc_chatInviteAlready, _serialize_chatInviteAlready);
		_serializers.insert(mtpc_chatInvite, _serialize_chatInvite);
		_serializers.insert(mtpc_inputStickerSetEmpty, _serialize_inputStickerSetEmpty);
		_serializers.insert(mtpc_inputStickerSetID, _serialize_inputStickerSetID);
		_serializers.insert(mtpc_inputStickerSetShortName, _serialize_inputStickerSetShortName);
		_serializers.insert(mtpc_stickerSet, _serialize_stickerSet);
		_serializers.insert(mtpc_messages_stickerSet, _serialize_messages_stickerSet);
		_serializers.insert(mtpc_botCommand, _serialize_botCommand);
		_serializers.insert(mtpc_botInfoEmpty, _serialize_botInfoEmpty);
		_serializers.insert(mtpc_botInfo, _serialize_botInfo);
		_serializers.insert(mtpc_keyboardButton, _serialize_keyboardButton);
		_serializers.insert(mtpc_keyboardButtonRow, _serialize_keyboardButtonRow);
		_serializers.insert(mtpc_replyKeyboardHide, _serialize_replyKeyboardHide);
		_serializers.insert(mtpc_replyKeyboardForceReply, _serialize_replyKeyboardForceReply);
		_serializers.insert(mtpc_replyKeyboardMarkup, _serialize_replyKeyboardMarkup);
		_serializers.insert(mtpc_help_appChangelogEmpty, _serialize_help_appChangelogEmpty);
		_serializers.insert(mtpc_help_appChangelog, _serialize_help_appChangelog);
		_serializers.insert(mtpc_messageEntityUnknown, _serialize_messageEntityUnknown);
		_serializers.insert(mtpc_messageEntityMention, _serialize_messageEntityMention);
		_serializers.insert(mtpc_messageEntityHashtag, _serialize_messageEntityHashtag);
		_serializers.insert(mtpc_messageEntityBotCommand, _serialize_messageEntityBotCommand);
		_serializers.insert(mtpc_messageEntityUrl, _serialize_messageEntityUrl);
		_serializers.insert(mtpc_messageEntityEmail, _serialize_messageEntityEmail);
		_serializers.insert(mtpc_messageEntityBold, _serialize_messageEntityBold);
		_serializers.insert(mtpc_messageEntityItalic, _serialize_messageEntityItalic);
		_serializers.insert(mtpc_messageEntityCode, _serialize_messageEntityCode);
		_serializers.insert(mtpc_messageEntityPre, _serialize_messageEntityPre);
		_serializers.insert(mtpc_messageEntityTextUrl, _serialize_messageEntityTextUrl);
		_serializers.insert(mtpc_inputChannelEmpty, _serialize_inputChannelEmpty);
		_serializers.insert(mtpc_inputChannel, _serialize_inputChannel);
		_serializers.insert(mtpc_contacts_resolvedPeer, _serialize_contacts_resolvedPeer);
		_serializers.insert(mtpc_messageRange, _serialize_messageRange);
		_serializers.insert(mtpc_messageGroup, _serialize_messageGroup);
		_serializers.insert(mtpc_updates_channelDifferenceEmpty, _serialize_updates_channelDifferenceEmpty);
		_serializers.insert(mtpc_updates_channelDifferenceTooLong, _serialize_updates_channelDifferenceTooLong);
		_serializers.insert(mtpc_updates_channelDifference, _serialize_updates_channelDifference);
		_serializers.insert(mtpc_channelMessagesFilterEmpty, _serialize_channelMessagesFilterEmpty);
		_serializers.insert(mtpc_channelMessagesFilter, _serialize_channelMessagesFilter);
		_serializers.insert(mtpc_channelMessagesFilterCollapsed, _serialize_channelMessagesFilterCollapsed);
		_serializers.insert(mtpc_channelParticipant, _serialize_channelParticipant);
		_serializers.insert(mtpc_channelParticipantSelf, _serialize_channelParticipantSelf);
		_serializers.insert(mtpc_channelParticipantModerator, _serialize_channelParticipantModerator);
		_serializers.insert(mtpc_channelParticipantEditor, _serialize_channelParticipantEditor);
		_serializers.insert(mtpc_channelParticipantKicked, _serialize_channelParticipantKicked);
		_serializers.insert(mtpc_channelParticipantCreator, _serialize_channelParticipantCreator);
		_serializers.insert(mtpc_channelParticipantsRecent, _serialize_channelParticipantsRecent);
		_serializers.insert(mtpc_channelParticipantsAdmins, _serialize_channelParticipantsAdmins);
		_serializers.insert(mtpc_channelParticipantsKicked, _serialize_channelParticipantsKicked);
		_serializers.insert(mtpc_channelParticipantsBots, _serialize_channelParticipantsBots);
		_serializers.insert(mtpc_channelRoleEmpty, _serialize_channelRoleEmpty);
		_serializers.insert(mtpc_channelRoleModerator, _serialize_channelRoleModerator);
		_serializers.insert(mtpc_channelRoleEditor, _serialize_channelRoleEditor);
		_serializers.insert(mtpc_channels_channelParticipants, _serialize_channels_channelParticipants);
		_serializers.insert(mtpc_channels_channelParticipant, _serialize_channels_channelParticipant);
		_serializers.insert(mtpc_help_termsOfService, _serialize_help_termsOfService);
		_serializers.insert(mtpc_foundGif, _serialize_foundGif);
		_serializers.insert(mtpc_foundGifCached, _serialize_foundGifCached);
		_serializers.insert(mtpc_messages_foundGifs, _serialize_messages_foundGifs);
		_serializers.insert(mtpc_messages_savedGifsNotModified, _serialize_messages_savedGifsNotModified);
		_serializers.insert(mtpc_messages_savedGifs, _serialize_messages_savedGifs);
		_serializers.insert(mtpc_inputBotInlineMessageMediaAuto, _serialize_inputBotInlineMessageMediaAuto);
		_serializers.insert(mtpc_inputBotInlineMessageText, _serialize_inputBotInlineMessageText);
		_serializers.insert(mtpc_inputBotInlineResult, _serialize_inputBotInlineResult);
		_serializers.insert(mtpc_botInlineMessageMediaAuto, _serialize_botInlineMessageMediaAuto);
		_serializers.insert(mtpc_botInlineMessageText, _serialize_botInlineMessageText);
		_serializers.insert(mtpc_botInlineMediaResultDocument, _serialize_botInlineMediaResultDocument);
		_serializers.insert(mtpc_botInlineMediaResultPhoto, _serialize_botInlineMediaResultPhoto);
		_serializers.insert(mtpc_botInlineResult, _serialize_botInlineResult);
		_serializers.insert(mtpc_messages_botResults, _serialize_messages_botResults);

		_serializers.insert(mtpc_req_pq, _serialize_req_pq);
		_serializers.insert(mtpc_req_DH_params, _serialize_req_DH_params);
		_serializers.insert(mtpc_set_client_DH_params, _serialize_set_client_DH_params);
		_serializers.insert(mtpc_rpc_drop_answer, _serialize_rpc_drop_answer);
		_serializers.insert(mtpc_get_future_salts, _serialize_get_future_salts);
		_serializers.insert(mtpc_ping, _serialize_ping);
		_serializers.insert(mtpc_ping_delay_disconnect, _serialize_ping_delay_disconnect);
		_serializers.insert(mtpc_destroy_session, _serialize_destroy_session);
		_serializers.insert(mtpc_register_saveDeveloperInfo, _serialize_register_saveDeveloperInfo);
		_serializers.insert(mtpc_auth_sendCall, _serialize_auth_sendCall);
		_serializers.insert(mtpc_auth_logOut, _serialize_auth_logOut);
		_serializers.insert(mtpc_auth_resetAuthorizations, _serialize_auth_resetAuthorizations);
		_serializers.insert(mtpc_auth_sendInvites, _serialize_auth_sendInvites);
		_serializers.insert(mtpc_auth_bindTempAuthKey, _serialize_auth_bindTempAuthKey);
		_serializers.insert(mtpc_auth_sendSms, _serialize_auth_sendSms);
		_serializers.insert(mtpc_account_registerDevice, _serialize_account_registerDevice);
		_serializers.insert(mtpc_account_unregisterDevice, _serialize_account_unregisterDevice);
		_serializers.insert(mtpc_account_updateNotifySettings, _serialize_account_updateNotifySettings);
		_serializers.insert(mtpc_account_resetNotifySettings, _serialize_account_resetNotifySettings);
		_serializers.insert(mtpc_account_updateStatus, _serialize_account_updateStatus);
		_serializers.insert(mtpc_account_reportPeer, _serialize_account_reportPeer);
		_serializers.insert(mtpc_account_checkUsername, _serialize_account_checkUsername);
		_serializers.insert(mtpc_account_deleteAccount, _serialize_account_deleteAccount);
		_serializers.insert(mtpc_account_setAccountTTL, _serialize_account_setAccountTTL);
		_serializers.insert(mtpc_account_updateDeviceLocked, _serialize_account_updateDeviceLocked);
		_serializers.insert(mtpc_account_resetAuthorization, _serialize_account_resetAuthorization);
		_serializers.insert(mtpc_account_updatePasswordSettings, _serialize_account_updatePasswordSettings);
		_serializers.insert(mtpc_contacts_deleteContacts, _serialize_contacts_deleteContacts);
		_serializers.insert(mtpc_contacts_block, _serialize_contacts_block);
		_serializers.insert(mtpc_contacts_unblock, _serialize_contacts_unblock);
		_serializers.insert(mtpc_messages_setTyping, _serialize_messages_setTyping);
		_serializers.insert(mtpc_messages_reportSpam, _serialize_messages_reportSpam);
		_serializers.insert(mtpc_messages_discardEncryption, _serialize_messages_discardEncryption);
		_serializers.insert(mtpc_messages_setEncryptedTyping, _serialize_messages_setEncryptedTyping);
		_serializers.insert(mtpc_messages_readEncryptedHistory, _serialize_messages_readEncryptedHistory);
		_serializers.insert(mtpc_messages_installStickerSet, _serialize_messages_installStickerSet);
		_serializers.insert(mtpc_messages_uninstallStickerSet, _serialize_messages_uninstallStickerSet);
		_serializers.insert(mtpc_messages_editChatAdmin, _serialize_messages_editChatAdmin);
		_serializers.insert(mtpc_messages_reorderStickerSets, _serialize_messages_reorderStickerSets);
		_serializers.insert(mtpc_messages_saveGif, _serialize_messages_saveGif);
		_serializers.insert(mtpc_messages_setInlineBotResults, _serialize_messages_setInlineBotResults);
		_serializers.insert(mtpc_upload_saveFilePart, _serialize_upload_saveFilePart);
		_serializers.insert(mtpc_upload_saveBigFilePart, _serialize_upload_saveBigFilePart);
		_serializers.insert(mtpc_help_saveAppLog, _serialize_help_saveAppLog);
		_serializers.insert(mtpc_channels_readHistory, _serialize_channels_readHistory);
		_serializers.insert(mtpc_channels_reportSpam, _serialize_channels_reportSpam);
		_serializers.insert(mtpc_channels_editAbout, _serialize_channels_editAbout);
		_serializers.insert(mtpc_channels_checkUsername, _serialize_channels_checkUsername);
		_serializers.insert(mtpc_channels_updateUsername, _serialize_channels_updateUsername);
		_serializers.insert(mtpc_invokeAfterMsg, _serialize_invokeAfterMsg);
		_serializers.insert(mtpc_invokeAfterMsgs, _serialize_invokeAfterMsgs);
		_serializers.insert(mtpc_initConnection, _serialize_initConnection);
		_serializers.insert(mtpc_invokeWithLayer, _serialize_invokeWithLayer);
		_serializers.insert(mtpc_invokeWithoutUpdates, _serialize_invokeWithoutUpdates);
		_serializers.insert(mtpc_auth_checkPhone, _serialize_auth_checkPhone);
		_serializers.insert(mtpc_auth_sendCode, _serialize_auth_sendCode);
		_serializers.insert(mtpc_auth_signUp, _serialize_auth_signUp);
		_serializers.insert(mtpc_auth_signIn, _serialize_auth_signIn);
		_serializers.insert(mtpc_auth_importAuthorization, _serialize_auth_importAuthorization);
		_serializers.insert(mtpc_auth_importBotAuthorization, _serialize_auth_importBotAuthorization);
		_serializers.insert(mtpc_auth_checkPassword, _serialize_auth_checkPassword);
		_serializers.insert(mtpc_auth_recoverPassword, _serialize_auth_recoverPassword);
		_serializers.insert(mtpc_auth_exportAuthorization, _serialize_auth_exportAuthorization);
		_serializers.insert(mtpc_auth_requestPasswordRecovery, _serialize_auth_requestPasswordRecovery);
		_serializers.insert(mtpc_account_getNotifySettings, _serialize_account_getNotifySettings);
		_serializers.insert(mtpc_account_updateProfile, _serialize_account_updateProfile);
		_serializers.insert(mtpc_account_updateUsername, _serialize_account_updateUsername);
		_serializers.insert(mtpc_account_changePhone, _serialize_account_changePhone);
		_serializers.insert(mtpc_contacts_importCard, _serialize_contacts_importCard);
		_serializers.insert(mtpc_account_getWallPapers, _serialize_account_getWallPapers);
		_serializers.insert(mtpc_account_getPrivacy, _serialize_account_getPrivacy);
		_serializers.insert(mtpc_account_setPrivacy, _serialize_account_setPrivacy);
		_serializers.insert(mtpc_account_getAccountTTL, _serialize_account_getAccountTTL);
		_serializers.insert(mtpc_account_sendChangePhoneCode, _serialize_account_sendChangePhoneCode);
		_serializers.insert(mtpc_account_getAuthorizations, _serialize_account_getAuthorizations);
		_serializers.insert(mtpc_account_getPassword, _serialize_account_getPassword);
		_serializers.insert(mtpc_account_getPasswordSettings, _serialize_account_getPasswordSettings);
		_serializers.insert(mtpc_users_getUsers, _serialize_users_getUsers);
		_serializers.insert(mtpc_users_getFullUser, _serialize_users_getFullUser);
		_serializers.insert(mtpc_contacts_getStatuses, _serialize_contacts_getStatuses);
		_serializers.insert(mtpc_contacts_getContacts, _serialize_contacts_getContacts);
		_serializers.insert(mtpc_contacts_importContacts, _serialize_contacts_importContacts);
		_serializers.insert(mtpc_contacts_getSuggested, _serialize_contacts_getSuggested);
		_serializers.insert(mtpc_contacts_deleteContact, _serialize_contacts_deleteContact);
		_serializers.insert(mtpc_contacts_getBlocked, _serialize_contacts_getBlocked);
		_serializers.insert(mtpc_contacts_exportCard, _serialize_contacts_exportCard);
		_serializers.insert(mtpc_messages_getMessagesViews, _serialize_messages_getMessagesViews);
		_serializers.insert(mtpc_contacts_search, _serialize_contacts_search);
		_serializers.insert(mtpc_contacts_resolveUsername, _serialize_contacts_resolveUsername);
		_serializers.insert(mtpc_messages_getMessages, _serialize_messages_getMessages);
		_serializers.insert(mtpc_messages_getHistory, _serialize_messages_getHistory);
		_serializers.insert(mtpc_messages_search, _serialize_messages_search);
		_serializers.insert(mtpc_messages_searchGlobal, _serialize_messages_searchGlobal);
		_serializers.insert(mtpc_channels_getImportantHistory, _serialize_channels_getImportantHistory);
		_serializers.insert(mtpc_channels_getMessages, _serialize_channels_getMessages);
		_serializers.insert(mtpc_messages_getDialogs, _serialize_messages_getDialogs);
		_serializers.insert(mtpc_channels_getDialogs, _serialize_channels_getDialogs);
		_serializers.insert(mtpc_messages_readHistory, _serialize_messages_readHistory);
		_serializers.insert(mtpc_messages_deleteMessages, _serialize_messages_deleteMessages);
		_serializers.insert(mtpc_messages_readMessageContents, _serialize_messages_readMessageContents);
		_serializers.insert(mtpc_channels_deleteMessages, _serialize_channels_deleteMessages);
		_serializers.insert(mtpc_messages_deleteHistory, _serialize_messages_deleteHistory);
		_serializers.insert(mtpc_channels_deleteUserHistory, _serialize_channels_deleteUserHistory);
		_serializers.insert(mtpc_messages_receivedMessages, _serialize_messages_receivedMessages);
		_serializers.insert(mtpc_messages_sendMessage, _serialize_messages_sendMessage);
		_serializers.insert(mtpc_messages_sendMedia, _serialize_messages_sendMedia);
		_serializers.insert(mtpc_messages_forwardMessages, _serialize_messages_forwardMessages);
		_serializers.insert(mtpc_messages_editChatTitle, _serialize_messages_editChatTitle);
		_serializers.insert(mtpc_messages_editChatPhoto, _serialize_messages_editChatPhoto);
		_serializers.insert(mtpc_messages_addChatUser, _serialize_messages_addChatUser);
		_serializers.insert(mtpc_messages_deleteChatUser, _serialize_messages_deleteChatUser);
		_serializers.insert(mtpc_messages_createChat, _serialize_messages_createChat);
		_serializers.insert(mtpc_messages_forwardMessage, _serialize_messages_forwardMessage);
		_serializers.insert(mtpc_messages_sendBroadcast, _serialize_messages_sendBroadcast);
		_serializers.insert(mtpc_messages_importChatInvite, _serialize_messages_importChatInvite);
		_serializers.insert(mtpc_messages_startBot, _serialize_messages_startBot);
		_serializers.insert(mtpc_messages_toggleChatAdmins, _serialize_messages_toggleChatAdmins);
		_serializers.insert(mtpc_messages_migrateChat, _serialize_messages_migrateChat);
		_serializers.insert(mtpc_messages_sendInlineBotResult, _serialize_messages_sendInlineBotResult);
		_serializers.insert(mtpc_channels_createChannel, _serialize_channels_createChannel);
		_serializers.insert(mtpc_channels_editAdmin, _serialize_channels_editAdmin);
		_serializers.insert(mtpc_channels_editTitle, _serialize_channels_editTitle);
		_serializers.insert(mtpc_channels_editPhoto, _serialize_channels_editPhoto);
		_serializers.insert(mtpc_channels_toggleComments, _serialize_channels_toggleComments);
		_serializers.insert(mtpc_channels_joinChannel, _serialize_channels_joinChannel);
		_serializers.insert(mtpc_channels_leaveChannel, _serialize_channels_leaveChannel);
		_serializers.insert(mtpc_channels_inviteToChannel, _serialize_channels_inviteToChannel);
		_serializers.insert(mtpc_channels_kickFromChannel, _serialize_channels_kickFromChannel);
		_serializers.insert(mtpc_channels_deleteChannel, _serialize_channels_deleteChannel);
		_serializers.insert(mtpc_messages_getChats, _serialize_messages_getChats);
		_serializers.insert(mtpc_channels_getChannels, _serialize_channels_getChannels);
		_serializers.insert(mtpc_messages_getFullChat, _serialize_messages_getFullChat);
		_serializers.insert(mtpc_channels_getFullChannel, _serialize_channels_getFullChannel);
		_serializers.insert(mtpc_messages_getDhConfig, _serialize_messages_getDhConfig);
		_serializers.insert(mtpc_messages_requestEncryption, _serialize_messages_requestEncryption);
		_serializers.insert(mtpc_messages_acceptEncryption, _serialize_messages_acceptEncryption);
		_serializers.insert(mtpc_messages_sendEncrypted, _serialize_messages_sendEncrypted);
		_serializers.insert(mtpc_messages_sendEncryptedFile, _serialize_messages_sendEncryptedFile);
		_serializers.insert(mtpc_messages_sendEncryptedService, _serialize_messages_sendEncryptedService);
		_serializers.insert(mtpc_messages_receivedQueue, _serialize_messages_receivedQueue);
		_serializers.insert(mtpc_photos_deletePhotos, _serialize_photos_deletePhotos);
		_serializers.insert(mtpc_messages_getStickers, _serialize_messages_getStickers);
		_serializers.insert(mtpc_messages_getAllStickers, _serialize_messages_getAllStickers);
		_serializers.insert(mtpc_messages_getWebPagePreview, _serialize_messages_getWebPagePreview);
		_serializers.insert(mtpc_messages_exportChatInvite, _serialize_messages_exportChatInvite);
		_serializers.insert(mtpc_channels_exportInvite, _serialize_channels_exportInvite);
		_serializers.insert(mtpc_messages_checkChatInvite, _serialize_messages_checkChatInvite);
		_serializers.insert(mtpc_messages_getStickerSet, _serialize_messages_getStickerSet);
		_serializers.insert(mtpc_messages_getDocumentByHash, _serialize_messages_getDocumentByHash);
		_serializers.insert(mtpc_messages_searchGifs, _serialize_messages_searchGifs);
		_serializers.insert(mtpc_messages_getSavedGifs, _serialize_messages_getSavedGifs);
		_serializers.insert(mtpc_messages_getInlineBotResults, _serialize_messages_getInlineBotResults);
		_serializers.insert(mtpc_updates_getState, _serialize_updates_getState);
		_serializers.insert(mtpc_updates_getDifference, _serialize_updates_getDifference);
		_serializers.insert(mtpc_updates_getChannelDifference, _serialize_updates_getChannelDifference);
		_serializers.insert(mtpc_photos_updateProfilePhoto, _serialize_photos_updateProfilePhoto);
		_serializers.insert(mtpc_photos_uploadProfilePhoto, _serialize_photos_uploadProfilePhoto);
		_serializers.insert(mtpc_photos_getUserPhotos, _serialize_photos_getUserPhotos);
		_serializers.insert(mtpc_upload_getFile, _serialize_upload_getFile);
		_serializers.insert(mtpc_help_getConfig, _serialize_help_getConfig);
		_serializers.insert(mtpc_help_getNearestDc, _serialize_help_getNearestDc);
		_serializers.insert(mtpc_help_getAppUpdate, _serialize_help_getAppUpdate);
		_serializers.insert(mtpc_help_getInviteText, _serialize_help_getInviteText);
		_serializers.insert(mtpc_help_getSupport, _serialize_help_getSupport);
		_serializers.insert(mtpc_help_getAppChangelog, _serialize_help_getAppChangelog);
		_serializers.insert(mtpc_help_getTermsOfService, _serialize_help_getTermsOfService);
		_serializers.insert(mtpc_channels_getParticipants, _serialize_channels_getParticipants);
		_serializers.insert(mtpc_channels_getParticipant, _serialize_channels_getParticipant);

		_serializers.insert(mtpc_rpc_result, _serialize_rpc_result);
		_serializers.insert(mtpc_msg_container, _serialize_msg_container);
		_serializers.insert(mtpc_core_message, _serialize_core_message);
	}
}

void mtpTextSerializeType(MTPStringLogger &to, const mtpPrime *&from, const mtpPrime *end, mtpPrime cons, uint32 level, mtpPrime vcons) {
	if (_serializers.isEmpty()) initTextSerializers();

	QVector<mtpTypeId> types, vtypes;
	QVector<int32> stages, flags;
	types.reserve(20); vtypes.reserve(20); stages.reserve(20); flags.reserve(20);
	types.push_back(mtpTypeId(cons)); vtypes.push_back(mtpTypeId(vcons)); stages.push_back(0); flags.push_back(0);

	const mtpPrime *start = from;
	mtpTypeId type = cons, vtype = vcons;
	int32 stage = 0, flag = 0;

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
		TextSerializers::const_iterator it = _serializers.constFind(type);
		if (it != _serializers.cend()) {
			(*it.value())(to, stage, lev, types, vtypes, stages, flags, start, end, flag);
		} else {
			mtpTextSerializeCore(to, from, end, type, lev, vtype);
			types.pop_back(); vtypes.pop_back(); stages.pop_back(); flags.pop_back();
		}
	}
}

