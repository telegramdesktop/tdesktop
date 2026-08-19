/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_common_session.h"

namespace Ui {
class InputField;
class SearchFieldController;
} // namespace Ui

namespace Settings {

class Experimental : public Section<Experimental> {
public:
	Experimental(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~Experimental();

	[[nodiscard]] rpl::producer<QString> title() override;
	void fillTopBarMenu(const Ui::Menu::MenuCallback &addAction) override;
	void setInnerFocus() override;
	void showFinished() override;
	[[nodiscard]] base::weak_qptr<Ui::RpWidget> createPinnedToTop(
		not_null<QWidget*> parent) override;

private:
	void setupContent();

	rpl::event_stream<> _reloadOptionsRequests;
	rpl::variable<QString> _query;
	std::unique_ptr<Ui::SearchFieldController> _searchController;
	QPointer<Ui::InputField> _searchField;
	std::vector<std::pair<QString, QPointer<QWidget>>> _highlights;

};

} // namespace Settings
