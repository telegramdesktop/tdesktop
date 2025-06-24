/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_common.h"

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

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

enum class SuggestMode {
	New,
	Change,
};

struct SuggestTimeBoxArgs {
	not_null<Main::Session*> session;
	Fn<void(TimeId)> done;
	TimeId value = 0;
	SuggestMode mode = SuggestMode::New;
};
void ChooseSuggestTimeBox(
	not_null<Ui::GenericBox*> box,
	SuggestTimeBoxArgs &&args);

struct SuggestPriceBoxArgs {
	not_null<Main::Session*> session;
	bool updating = false;
	Fn<void(SuggestPostOptions)> done;
	SuggestPostOptions value;
	SuggestMode mode = SuggestMode::New;
};
void ChooseSuggestPriceBox(
	not_null<Ui::GenericBox*> box,
	SuggestPriceBoxArgs &&args);

[[nodiscard]] bool CanEditSuggestedMessage(not_null<HistoryItem*> item);

class SuggestOptions final {
public:
	SuggestOptions(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<PeerData*> peer,
		SuggestPostOptions values,
		SuggestMode mode);
	~SuggestOptions();

	void paintBar(QPainter &p, int x, int y, int outerWidth);
	void edit();

	void paintIcon(QPainter &p, int x, int y, int outerWidth);
	void paintLines(QPainter &p, int x, int y, int outerWidth);

	[[nodiscard]] SuggestPostOptions values() const;

	[[nodiscard]] rpl::producer<> updates() const;

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void updateTexts();

	[[nodiscard]] TextWithEntities composeText() const;

	const std::shared_ptr<ChatHelpers::Show> _show;
	const not_null<PeerData*> _peer;
	const SuggestMode _mode = SuggestMode::New;

	Ui::Text::String _title;
	Ui::Text::String _text;

	SuggestPostOptions _values;
	rpl::event_stream<> _updates;

	rpl::lifetime _lifetime;

};

} // namespace HistoryView
