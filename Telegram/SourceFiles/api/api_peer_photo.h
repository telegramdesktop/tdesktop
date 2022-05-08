/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"

class ApiWrap;
class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace Api {

class PeerPhoto final {
public:
	explicit PeerPhoto(not_null<ApiWrap*> api);

	void upload(not_null<PeerData*> peer, QImage &&image);
	void clear(not_null<PhotoData*> photo);
	void set(not_null<PeerData*> peer, not_null<PhotoData*> photo);

private:
	void ready(const FullMsgId &msgId, const MTPInputFile &file);

	const not_null<Main::Session*> _session;
	MTP::Sender _api;

	base::flat_map<FullMsgId, not_null<PeerData*>> _uploads;

};

} // namespace Api
