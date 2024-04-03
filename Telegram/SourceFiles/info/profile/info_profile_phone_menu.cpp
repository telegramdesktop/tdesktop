/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_phone_menu.h"

#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_app_config_values.h"
#include "main/main_session.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_chat.h" // expandedMenuSeparator.
#include "styles/style_chat_helpers.h"

namespace Info {
namespace Profile {
namespace {

class TextItem final : public Ui::Menu::ItemBase {
public:
	TextItem(
		not_null<Ui::RpWidget*> parent,
		const style::Menu &st,
		rpl::producer<TextWithEntities> &&text);

	not_null<QAction*> action() const override;
	bool isEnabled() const override;

protected:
	int contentHeight() const override;

private:
	const base::unique_qptr<Ui::FlatLabel> _label;
	const not_null<QAction*> _dummyAction;

};

[[nodiscard]] int CountMinWidthForHeight(
		not_null<Ui::FlatLabel*> label,
		int basicWidth,
		int heightLimit) {
	const auto height = [&](int width) {
		label->resizeToWidth(width);
		return label->height();
	};
	auto widthMin = basicWidth;
	auto widthMax = label->textMaxWidth();
	if (height(widthMin) <= heightLimit || height(widthMax) > heightLimit) {
		return basicWidth;
	}
	while (widthMin + 1 < widthMax) {
		const auto middle = (widthMin + widthMax) / 2;
		if (height(middle) > heightLimit) {
			widthMin = middle;
		} else {
			widthMax = middle;
		}
	}
	return widthMax;
}

TextItem::TextItem(
	not_null<Ui::RpWidget*> parent,
	const style::Menu &st,
	rpl::producer<TextWithEntities> &&text)
: ItemBase(parent, st)
, _label(base::make_unique_q<Ui::FlatLabel>(
	this,
	std::move(text),
	st::historyMessagesTTLLabel))
, _dummyAction(Ui::CreateChild<QAction>(parent.get())) {
	// Try to fit the phrase in two lines.
	const auto limit = st::historyMessagesTTLLabel.style.font->height * 2;
	const auto min1 = st::historyMessagesTTLLabel.minWidth;
	const auto min2 = CountMinWidthForHeight(_label.get(), min1, limit);
	const auto added = st.itemPadding.left() + st.itemPadding.right();
	setMinWidth(std::max(min1, min2) + added);

	sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		if (s.width() <= added) {
			return;
		}
		_label->resizeToWidth(s.width() - added);
		_label->moveToLeft(
			st.itemPadding.left(),
			(s.height() - _label->height()) / 2);
	}, lifetime());

	_label->resizeToWidth(parent->width() - added);
	initResizeHook(parent->sizeValue());
}

not_null<QAction*> TextItem::action() const {
	return _dummyAction;
}

bool TextItem::isEnabled() const {
	return false;
}

int TextItem::contentHeight() const {
	return _label->height();
}

} // namespace

bool IsCollectiblePhone(not_null<UserData*> user) {
	using Strings = std::vector<QString>;
	const auto prefixes = user->session().appConfig().get<Strings>(
		u"fragment_prefixes"_q,
		Strings{ u"888"_q });
	const auto phone = user->phone();
	const auto proj = [&](const QString &p) {
		return phone.startsWith(p);
	};
	return ranges::any_of(prefixes, proj);
}

void AddPhoneMenu(not_null<Ui::PopupMenu*> menu, not_null<UserData*> user) {
	if (user->isSelf() || !IsCollectiblePhone(user)) {
		return;
	} else if (const auto url = AppConfig::FragmentLink(&user->session())) {
		menu->addSeparator(&st::expandedMenuSeparator);
		const auto link = Ui::Text::Link(
			tr::lng_info_mobile_context_menu_fragment_about_link(tr::now),
			*url);
		menu->addAction(base::make_unique_q<TextItem>(
			menu->menu(),
			st::reactionMenu.menu,
			tr::lng_info_mobile_context_menu_fragment_about(
				lt_link,
				rpl::single(link),
				Ui::Text::RichLangValue)));
	}
}

} // namespace Profile
} // namespace Info
