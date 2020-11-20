/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "base/timer.h"
#include "base/bytes.h"
#include "mtproto/sender.h"
#include "mtproto/mtproto_auth_key.h"

namespace tgcalls {
class GroupInstanceImpl;
} // namespace tgcalls

namespace Calls {

class GroupCall final : public base::has_weak_ptr {
public:
	class Delegate {
	public:
		virtual ~Delegate() = default;

	};

	GroupCall(
		not_null<Delegate*> delegate,
		not_null<ChannelData*> channel,
		const MTPInputGroupCall &inputCall);
	~GroupCall();

	[[nodiscard]] not_null<ChannelData*> channel() const {
		return _channel;
	}

	void start();
	void join(const MTPInputGroupCall &inputCall);
	bool handleUpdate(const MTPGroupCall &call);

	void setMuted(bool mute);
	[[nodiscard]] bool muted() const {
		return _muted.current();
	}
	[[nodiscard]] rpl::producer<bool> mutedValue() const {
		return _muted.value();
	}

	void setCurrentAudioDevice(bool input, const QString &deviceId);
	void setAudioVolume(bool input, float level);
	void setAudioDuckingEnabled(bool enabled);

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	void handleRequestError(const RPCError &error);
	void handleControllerError(const QString &error);
	void createAndStartController();
	void destroyController();

	[[nodiscard]] MTPInputGroupCall inputCall() const;

	const not_null<Delegate*> _delegate;
	const not_null<ChannelData*> _channel;
	MTP::Sender _api;
	crl::time _startTime = 0;

	rpl::variable<bool> _muted = false;

	uint64 _id = 0;
	uint64 _accessHash = 0;

	std::unique_ptr<tgcalls::GroupInstanceImpl> _instance;

	rpl::lifetime _lifetime;

};

} // namespace Calls
