/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

namespace TdE2E {

struct UserId {
	uint64 v = 0;
};

struct PrivateKeyId {
	uint64 v = 0;
};

struct CallId {
	uint64 v = 0;
};

struct PublicKey {
	uint64 a = 0;
	uint64 b = 0;
	uint64 c = 0;
	uint64 d = 0;
};

struct ParticipantState {
	UserId id;
	PublicKey key;
};

struct Block {
	QByteArray data;
};

class Call final {
public:
	explicit Call(UserId myUserId);

	[[nodiscard]] PublicKey myKey() const;

	[[nodiscard]] Block makeZeroBlock() const;

	void create(const Block &last);

	enum class ApplyResult {
		Success,
		BlockSkipped
	};
	[[nodiscard]] ApplyResult apply(const Block &block);

private:
	CallId _id;
	UserId _myUserId;
	PrivateKeyId _myKeyId;
	PublicKey _myKey;

};

} // namespace TdE2E
