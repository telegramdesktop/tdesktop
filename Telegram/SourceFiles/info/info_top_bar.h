/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/effects/animations.h"
#include "ui/effects/numbers_animation.h"
#include "info/info_wrap_widget.h"

namespace style {
struct InfoTopBar;
} // namespace style

namespace Window {
class SessionNavigation;
} // namespace Window

namespace Ui {
class IconButton;
class FlatLabel;
class InputField;
class SearchFieldController;
class LabelWithNumbers;
} // namespace Ui

namespace Info {

class Key;
class Section;

rpl::producer<QString> TitleValue(
	const Section &section,
	Key key,
	bool isStackBottom);

class TopBar : public Ui::RpWidget {
public:
	TopBar(
		QWidget *parent,
		not_null<Window::SessionNavigation*> navigation,
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

	template <typename ButtonWidget>
	ButtonWidget *addButtonWithVisibility(
			base::unique_qptr<ButtonWidget> button,
			rpl::producer<bool> shown) {
		auto result = button.get();
		forceButtonVisibility(
			pushButton(std::move(button)),
			std::move(shown));
		return result;
	}

	void createSearchView(
		not_null<Ui::SearchFieldController*> controller,
		rpl::producer<bool> &&shown,
		bool startsFocused);
	bool focusSearchField();

	void setSelectedItems(SelectedItems &&items);
	SelectedItems takeSelectedItems();

	rpl::producer<> cancelSelectionRequests() const;

	void finishAnimating() {
		updateControlsVisibility(anim::type::instant);
	}

	void showSearch();

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void updateControlsGeometry(int newWidth);
	void updateDefaultControlsGeometry(int newWidth);
	void updateSelectionControlsGeometry(int newWidth);
	Ui::FadeWrap<Ui::RpWidget> *pushButton(
		base::unique_qptr<Ui::RpWidget> button);
	void forceButtonVisibility(
		Ui::FadeWrap<Ui::RpWidget> *button,
		rpl::producer<bool> shown);
	void removeButton(not_null<Ui::RpWidget*> button);
	void startHighlightAnimation();
	void updateControlsVisibility(anim::type animated);

	bool selectionMode() const;
	bool searchMode() const;
	Ui::StringWithNumbers generateSelectedText() const;
	[[nodiscard]] bool computeCanDelete() const;
	[[nodiscard]] bool computeCanForward() const;
	void updateSelectionState();
	void createSelectionControls();
	void clearSelectionControls();

	MessageIdsList collectItems() const;
	void performForward();
	void performDelete();

	void setSearchField(
		base::unique_qptr<Ui::InputField> field,
		rpl::producer<bool> &&shown,
		bool startsFocused);
	void clearSearchField();
	void createSearchView(
		not_null<Ui::InputField*> field,
		rpl::producer<bool> &&shown,
		bool startsFocused);

	template <typename Callback>
	void registerUpdateControlCallback(QObject *guard, Callback &&callback);

	template <typename Widget, typename IsVisible>
	void registerToggleControlCallback(Widget *widget, IsVisible &&callback);

	const not_null<Window::SessionNavigation*> _navigation;

	const style::InfoTopBar &_st;
	Ui::Animations::Simple _a_highlight;
	bool _highlight = false;
	QPointer<Ui::FadeWrap<Ui::IconButton>> _back;
	std::vector<base::unique_qptr<Ui::RpWidget>> _buttons;
	QPointer<Ui::FadeWrap<Ui::FlatLabel>> _title;

	bool _searchModeEnabled = false;
	bool _searchModeAvailable = false;
	base::unique_qptr<Ui::RpWidget> _searchView;
	QPointer<Ui::InputField> _searchField;

	rpl::event_stream<> _backClicks;

	SelectedItems _selectedItems;
	bool _canDelete = false;
	bool _canForward = false;
	QPointer<Ui::FadeWrap<Ui::IconButton>> _cancelSelection;
	QPointer<Ui::FadeWrap<Ui::LabelWithNumbers>> _selectionText;
	QPointer<Ui::FadeWrap<Ui::IconButton>> _forward;
	QPointer<Ui::FadeWrap<Ui::IconButton>> _delete;
	rpl::event_stream<> _cancelSelectionClicks;

	using UpdateCallback = Fn<bool(anim::type)>;
	std::map<QObject*, UpdateCallback> _updateControlCallbacks;

};

} // namespace Info
