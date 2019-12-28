/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QReadWriteLock>

namespace MTP {

class Instance;
class AuthKey;
using AuthKeyPtr = std::shared_ptr<AuthKey>;
enum class DcType;

namespace details {

enum class TemporaryKeyType {
	Regular,
	MediaCluster
};

enum class CreatingKeyType {
	None,
	Persistent,
	TemporaryRegular,
	TemporaryMediaCluster
};

[[nodiscard]] TemporaryKeyType TemporaryKeyTypeByDcType(DcType type);

class Dcenter : public QObject {
public:
	// Main thread.
	Dcenter(DcId dcId, AuthKeyPtr &&key);

	// Thread-safe.
	[[nodiscard]] DcId id() const;
	[[nodiscard]] AuthKeyPtr getPersistentKey() const;
	[[nodiscard]] AuthKeyPtr getTemporaryKey(TemporaryKeyType type) const;
	[[nodiscard]] CreatingKeyType acquireKeyCreation(DcType type);
	bool releaseKeyCreationOnDone(
		CreatingKeyType type,
		const AuthKeyPtr &temporaryKey,
		const AuthKeyPtr &persistentKeyUsedForBind);
	void releaseKeyCreationOnFail(CreatingKeyType type);
	bool destroyTemporaryKey(uint64 keyId);
	bool destroyConfirmedForgottenKey(uint64 keyId);

	[[nodiscard]] bool connectionInited() const;
	void setConnectionInited(bool connectionInited = true);

private:
	static constexpr auto kTemporaryKeysCount = 2;

	const DcId _id = 0;
	mutable QReadWriteLock _mutex;

	AuthKeyPtr _temporaryKeys[kTemporaryKeysCount];
	AuthKeyPtr _persistentKey;
	bool _connectionInited = false;
	std::atomic<bool> _creatingKeys[kTemporaryKeysCount] = { false };

};

} // namespace details
} // namespace MTP
