/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "ui/rp_widget.h"

namespace style {
struct InfoTopBar;
} // namespace style

namespace Ui {
class IconButton;
class FlatLabel;
class InputField;
class SearchFieldController;
} // namespace Ui

namespace Info {

class Section;

rpl::producer<QString> TitleValue(
	const Section &section,
	not_null<PeerData*> peer);

class TopBar : public Ui::RpWidget {
public:
	TopBar(QWidget *parent, const style::InfoTopBar &st);

	auto backRequest() const {
		return _backClicks.events();
	}

	void setTitle(rpl::producer<QString> &&title);
	void enableBackButton(bool enable);

	template <typename ButtonWidget>
	ButtonWidget *addButton(base::unique_qptr<ButtonWidget> button) {
		auto result = button.get();
		pushButton(std::move(button));
		return result;
	}

	void createSearchView(
		not_null<Ui::SearchFieldController*> controller);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void updateControlsGeometry(int newWidth);
	void pushButton(base::unique_qptr<Ui::RpWidget> button);
	void removeButton(not_null<Ui::RpWidget*> button);

	void setSearchField(base::unique_qptr<Ui::InputField> field);
	void createSearchView(not_null<Ui::InputField*> field);

	const style::InfoTopBar &_st;
	object_ptr<Ui::IconButton> _back = { nullptr };
	std::vector<base::unique_qptr<Ui::RpWidget>> _buttons;
	object_ptr<Ui::FlatLabel> _title = { nullptr };

	base::unique_qptr<Ui::RpWidget> _searchView;

	rpl::event_stream<> _backClicks;

};

} // namespace Info
