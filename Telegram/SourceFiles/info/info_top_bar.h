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
#include "ui/wrap/fade_wrap.h"
#include "ui/effects/numbers_animation.h"
#include "info/info_wrap_widget.h"

namespace style {
struct InfoTopBar;
} // namespace style

namespace Ui {
class IconButton;
class FlatLabel;
class InputField;
class SearchFieldController;
class LabelWithNumbers;
} // namespace Ui

namespace Info {

class Section;

rpl::producer<QString> TitleValue(
	const Section &section,
	not_null<PeerData*> peer,
	bool isStackBottom);

class TopBar : public Ui::RpWidget {
public:
	TopBar(
		QWidget *parent,
		const style::InfoTopBar &st,
		SelectedItems &&items);

	auto backRequest() const {
		return _backClicks.events();
	}

	void setTitle(rpl::producer<QString> &&title);
	void enableBackButton();
	void highlight();

	template <typename ButtonWidget>
	ButtonWidget *addButton(base::unique_qptr<ButtonWidget> button) {
		auto result = button.get();
		pushButton(std::move(button));
		return result;
	}

	void createSearchView(
		not_null<Ui::SearchFieldController*> controller,
		rpl::producer<bool> &&shown);

	void setSelectedItems(SelectedItems &&items);
	SelectedItems takeSelectedItems();

	rpl::producer<> cancelSelectionRequests() const;

	void finishAnimating() {
		updateControlsVisibility(anim::type::instant);
	}

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void updateControlsGeometry(int newWidth);
	void updateDefaultControlsGeometry(int newWidth);
	void updateSelectionControlsGeometry(int newWidth);
	Ui::FadeWrap<Ui::RpWidget> *pushButton(base::unique_qptr<Ui::RpWidget> button);
	void removeButton(not_null<Ui::RpWidget*> button);
	void startHighlightAnimation();
	void updateControlsVisibility(anim::type animated);

	bool selectionMode() const;
	bool searchMode() const;
	Ui::StringWithNumbers generateSelectedText() const;
	[[nodiscard]] bool computeCanDelete() const;
	void updateSelectionState();
	void createSelectionControls();
	void clearSelectionControls();

	MessageIdsList collectItems() const;
	void performForward();
	void performDelete();

	void setSearchField(
		base::unique_qptr<Ui::InputField> field,
		rpl::producer<bool> &&shown);
	void createSearchView(
		not_null<Ui::InputField*> field,
		rpl::producer<bool> &&shown);

	template <typename Callback>
	void registerUpdateControlCallback(QObject *guard, Callback &&callback);

	template <typename Widget, typename IsVisible>
	void registerToggleControlCallback(Widget *widget, IsVisible &&callback);

	const style::InfoTopBar &_st;
	Animation _a_highlight;
	bool _highlight = false;
	QPointer<Ui::FadeWrap<Ui::IconButton>> _back;
	std::vector<base::unique_qptr<Ui::RpWidget>> _buttons;
	QPointer<Ui::FadeWrap<Ui::FlatLabel>> _title;

	bool _searchModeEnabled = false;
	bool _searchModeAvailable = false;
	base::unique_qptr<Ui::RpWidget> _searchView;

	rpl::event_stream<> _backClicks;

	SelectedItems _selectedItems;
	bool _canDelete = false;
	QPointer<Ui::FadeWrap<Ui::IconButton>> _cancelSelection;
	QPointer<Ui::FadeWrap<Ui::LabelWithNumbers>> _selectionText;
	QPointer<Ui::FadeWrap<Ui::IconButton>> _forward;
	QPointer<Ui::FadeWrap<Ui::IconButton>> _delete;
	rpl::event_stream<> _cancelSelectionClicks;

	using UpdateCallback = base::lambda<bool(anim::type)>;
	std::map<QObject*, UpdateCallback> _updateControlCallbacks;

};

} // namespace Info
