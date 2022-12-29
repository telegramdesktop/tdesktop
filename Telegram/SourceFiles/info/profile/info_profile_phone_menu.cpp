/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_phone_menu.h"

#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_chat.h" // expandedMenuSeparator.

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

	setMinWidth(st::historyMessagesTTLLabel.minWidth
		+ st.itemPadding.left());

	sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		_label->moveToLeft(
			st.itemPadding.left(),
			(s.height() - _label->height()) / 2);
	}, lifetime());

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

void AddPhoneMenu(not_null<Ui::PopupMenu*> menu, not_null<UserData*> user) {
	if (user->isSelf()) {
		return;
	}
	using Strings = std::vector<QString>;
	const auto prefixes = user->session().account().appConfig().get<Strings>(
		u"fragment_prefixes"_q,
		std::vector<QString>());
	{
		const auto proj = [&phone = user->phone()](const QString &p) {
			return phone.startsWith(p);
		};
		if (ranges::none_of(prefixes, proj)) {
			return;
		}
	}
	const auto domains = user->session().account().appConfig().get<Strings>(
		u"whitelisted_domains"_q,
		std::vector<QString>());
	const auto proj = [&, domain = u"fragment"_q](const QString &p) {
		return p.contains(domain);
	};
	const auto it = ranges::find_if(domains, proj);
	if (it == end(domains)) {
		return;
	}

	menu->addSeparator(&st::expandedMenuSeparator);
	const auto link = Ui::Text::Link(
		tr::lng_info_mobile_context_menu_fragment_about_link(tr::now),
		*it);
	menu->addAction(base::make_unique_q<TextItem>(
		menu->menu(),
		st::reactionMenu.menu,
		tr::lng_info_mobile_context_menu_fragment_about(
			lt_link,
			rpl::single(link),
			Ui::Text::RichLangValue)));
}

} // namespace Profile
} // namespace Info
