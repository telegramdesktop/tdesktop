/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/business/data_business_common.h"
#include "settings/settings_common_session.h"

class FilterChatsPreview;

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Settings {

template <typename SectionType>
class BusinessSection : public Section<SectionType> {
public:
	BusinessSection(
		QWidget *parent,
		not_null<Window::SessionController*> controller)
	: Section<SectionType>(parent)
	, _controller(controller) {
	}

	[[nodiscard]] not_null<Window::SessionController*> controller() const {
		return _controller;
	}
	[[nodiscard]] rpl::producer<> showFinishes() const {
		return _showFinished.events();
	}

private:
	void showFinished() override {
		_showFinished.fire({});
	}

	const not_null<Window::SessionController*> _controller;
	rpl::event_stream<> _showFinished;

};

struct BusinessChatsDescriptor {
	Data::BusinessChats current;
	Fn<void(const Data::BusinessChats&)> save;
	bool include = false;
};
void EditBusinessChats(
	not_null<Window::SessionController*> window,
	BusinessChatsDescriptor &&descriptor);

not_null<FilterChatsPreview*> SetupBusinessChatsPreview(
	not_null<Ui::VerticalLayout*> container,
	not_null<rpl::variable<Data::BusinessChats>*> data);

struct BusinessRecipientsSelectorDescriptor {
	not_null<Window::SessionController*> controller;
	rpl::producer<QString> title;
	not_null<rpl::variable<Data::BusinessRecipients>*> data;
};
void AddBusinessRecipientsSelector(
	not_null<Ui::VerticalLayout*> container,
	BusinessRecipientsSelectorDescriptor &&descriptor);

[[nodiscard]] int ShortcutsCount(not_null<Main::Session*> session);
[[nodiscard]] rpl::producer<int> ShortcutsCountValue(
	not_null<Main::Session*> session);
[[nodiscard]] int ShortcutMessagesCount(
	not_null<Main::Session*> session,
	const QString &name);
[[nodiscard]] rpl::producer<int> ShortcutMessagesCountValue(
	not_null<Main::Session*> session,
	const QString &name);
[[nodiscard]] bool ShortcutExists(
	not_null<Main::Session*> session,
	const QString &name);
[[nodiscard]] rpl::producer<bool> ShortcutExistsValue(
	not_null<Main::Session*> session,
	const QString &name);
[[nodiscard]] int ShortcutsLimit(not_null<Main::Session*> session);
[[nodiscard]] rpl::producer<int> ShortcutsLimitValue(
	not_null<Main::Session*> session);
[[nodiscard]] int ShortcutMessagesLimit(not_null<Main::Session*> session);
[[nodiscard]] rpl::producer<int> ShortcutMessagesLimitValue(
	not_null<Main::Session*> session);

[[nodiscard]] BusinessShortcutId LookupShortcutId(
	not_null<Main::Session*> session,
	const QString &name);

} // namespace Settings
