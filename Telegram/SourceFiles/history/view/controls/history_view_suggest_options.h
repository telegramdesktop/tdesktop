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
class VerticalLayout;
class NumberInput;
class InputField;
} // namespace Ui

namespace Ui::Text {
class CustomEmojiHelper;
} // namespace Ui::Text

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
	Publish,
	Gift,
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

struct StarsInputFieldArgs {
	std::optional<int64> value;
	int64 max = 0;
};
[[nodiscard]] not_null<Ui::NumberInput*> AddStarsInputField(
	not_null<Ui::VerticalLayout*> container,
	StarsInputFieldArgs &&args);

struct TonInputFieldArgs {
	int64 value = 0;
};
[[nodiscard]] not_null<Ui::InputField*> AddTonInputField(
	not_null<Ui::VerticalLayout*> container,
	TonInputFieldArgs &&args);

struct StarsTonPriceInput {
	Fn<void()> focusCallback;
	Fn<std::optional<CreditsAmount>()> computeResult;
	rpl::producer<> submits;
	rpl::producer<> updates;
	rpl::producer<CreditsAmount> result;
};

struct StarsTonPriceArgs {
	not_null<Main::Session*> session;
	rpl::producer<bool> showTon;
	CreditsAmount price;
	int starsMin = 0;
	int starsMax = 0;
	int64 nanoTonMin = 0;
	int64 nanoTonMax = 0;
	bool allowEmpty = false;
	Fn<void(CreditsAmount)> errorHook;
	rpl::producer<TextWithEntities> starsAbout;
	rpl::producer<TextWithEntities> tonAbout;
};

[[nodiscard]] StarsTonPriceInput AddStarsTonPriceInput(
	not_null<Ui::VerticalLayout*> container,
	StarsTonPriceArgs &&args);

struct SuggestPriceBoxArgs {
	not_null<PeerData*> peer;
	bool updating = false;
	Fn<void(SuggestOptions)> done;
	SuggestOptions value;
	SuggestMode mode = SuggestMode::New;
	QString giftName;
};
void ChooseSuggestPriceBox(
	not_null<Ui::GenericBox*> box,
	SuggestPriceBoxArgs &&args);

[[nodiscard]] bool CanEditSuggestedMessage(not_null<HistoryItem*> item);

[[nodiscard]] bool CanAddOfferToMessage(not_null<HistoryItem*> item);

[[nodiscard]] CreditsAmount PriceAfterCommission(
	not_null<Main::Session*> session,
	CreditsAmount price);
[[nodiscard]] QString FormatAfterCommissionPercent(
	not_null<Main::Session*> session,
	CreditsAmount price);

void InsufficientTonBox(
	not_null<Ui::GenericBox*> box,
	not_null<PeerData*> peer,
	CreditsAmount required);

class SuggestOptionsBar final {
public:
	SuggestOptionsBar(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<PeerData*> peer,
		SuggestOptions values,
		SuggestMode mode);
	~SuggestOptionsBar();

	void paintBar(QPainter &p, int x, int y, int outerWidth);
	void edit();

	void paintIcon(QPainter &p, int x, int y, int outerWidth);
	void paintLines(QPainter &p, int x, int y, int outerWidth);

	[[nodiscard]] SuggestOptions values() const;

	[[nodiscard]] rpl::producer<> updates() const;

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void updateTexts();

	[[nodiscard]] TextWithEntities composeText(
		Ui::Text::CustomEmojiHelper &helper) const;

	const std::shared_ptr<ChatHelpers::Show> _show;
	const not_null<PeerData*> _peer;
	const SuggestMode _mode = SuggestMode::New;

	Ui::Text::String _title;
	Ui::Text::String _text;

	SuggestOptions _values;
	rpl::event_stream<> _updates;

	rpl::lifetime _lifetime;

};

} // namespace HistoryView
