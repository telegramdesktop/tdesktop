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

class Ringtones final {
public:
	explicit Ringtones(not_null<ApiWrap*> api);

	void upload(
		const QString &filename,
		const QString &filemime,
		const QByteArray &content);

private:
	struct UploadedData {
		QString filename;
		QString filemime;
	};
	void ready(const FullMsgId &msgId, const MTPInputFile &file);

	const not_null<Main::Session*> _session;
	MTP::Sender _api;

	base::flat_map<FullMsgId, UploadedData> _uploads;

};

} // namespace Api
