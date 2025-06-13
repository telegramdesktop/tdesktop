/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_common.h"

namespace Window {
class SessionController;
} // namespace Window

namespace HistoryView {

class SuggestOptions final {
public:
	SuggestOptions(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		SuggestPostOptions values);
	~SuggestOptions();

	void paintBar(QPainter &p, int x, int y, int outerWidth);
	void edit();

	[[nodiscard]] SuggestPostOptions values() const;

	[[nodiscard]] rpl::producer<> repaints() const;

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void updateTexts();

	[[nodiscard]] TextWithEntities composeText() const;

	const not_null<Window::SessionController*> _controller;
	const not_null<PeerData*> _peer;

	Ui::Text::String _title;
	Ui::Text::String _text;

	SuggestPostOptions _values;
	rpl::event_stream<> _repaints;

	rpl::lifetime _lifetime;

};

} // namespace HistoryView
