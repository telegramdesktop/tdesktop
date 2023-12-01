/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "mtproto/sender.h"

class ApiWrap;

namespace Ui {
struct ColorIndicesCompressed;
} // namespace Ui

namespace Api {

class PeerColors final {
public:
	explicit PeerColors(not_null<ApiWrap*> api);
	~PeerColors();

	[[nodiscard]] std::vector<uint8> suggested() const;
	[[nodiscard]] rpl::producer<std::vector<uint8>> suggestedValue() const;
	[[nodiscard]] auto indicesValue() const
		-> rpl::producer<Ui::ColorIndicesCompressed>;

private:
	void request();
	void apply(const MTPDhelp_peerColors &data);

	MTP::Sender _api;
	int32 _hash = 0;

	mtpRequestId _requestId = 0;
	base::Timer _timer;
	rpl::variable<std::vector<uint8>> _suggested;
	rpl::event_stream<> _colorIndicesChanged;
	std::unique_ptr<Ui::ColorIndicesCompressed> _colorIndicesCurrent;

};

} // namespace Api
