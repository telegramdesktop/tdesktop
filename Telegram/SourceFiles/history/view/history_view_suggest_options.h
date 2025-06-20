/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_common.h"

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace HistoryView {

struct SuggestTimeBoxArgs {
	not_null<Main::Session*> session;
	rpl::producer<QString> title;
	rpl::producer<QString> submit;
	Fn<void(TimeId)> done;
	TimeId value = 0;
};
void ChooseSuggestTimeBox(
	not_null<Ui::GenericBox*> box,
	SuggestTimeBoxArgs &&args);

struct SuggestPriceBoxArgs {
	not_null<Main::Session*> session;
	bool updating = false;
	Fn<void(SuggestPostOptions)> done;
	SuggestPostOptions value;
};
void ChooseSuggestPriceBox(
	not_null<Ui::GenericBox*> box,
	SuggestPriceBoxArgs &&args);

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

	[[nodiscard]] rpl::producer<> updates() const;

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void updateTexts();

	[[nodiscard]] TextWithEntities composeText() const;

	const not_null<Window::SessionController*> _controller;
	const not_null<PeerData*> _peer;

	Ui::Text::String _title;
	Ui::Text::String _text;

	SuggestPostOptions _values;
	rpl::event_stream<> _updates;

	rpl::lifetime _lifetime;

};

} // namespace HistoryView
