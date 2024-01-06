/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/round_rect.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/effects/animations.h"
#include "ui/effects/numbers_animation.h"
#include "info/info_wrap_widget.h"

namespace style {
struct InfoTopBar;
} // namespace style

namespace Dialogs::Stories {
class List;
struct Content;
} // namespace Dialogs::Stories

namespace Window {
class SessionNavigation;
} // namespace Window

namespace Ui {
class AbstractButton;
class IconButton;
class FlatLabel;
class InputField;
class SearchFieldController;
class LabelWithNumbers;
} // namespace Ui

namespace Info {

class Key;
class Section;

struct TitleDescriptor {
	rpl::producer<QString> title;
	rpl::producer<QString> subtitle;
};

class TopBar : public Ui::RpWidget {
public:
	TopBar(
		QWidget *parent,
		not_null<Window::SessionNavigation*> navigation,
		const style::InfoTopBar &st,
		SelectedItems &&items);

	[[nodiscard]] auto backRequest() const {
		return _backClicks.events();
	}
	[[nodiscard]] auto storyClicks() const {
		return _storyClicks.events();
	}

	void setTitle(TitleDescriptor descriptor);
	void setStories(rpl::producer<Dialogs::Stories::Content> content);
	void setStoriesArchive(bool archive);
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

	[[nodiscard]] auto selectionActionRequests() const
		-> rpl::producer<SelectionAction>;

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
	void updateStoriesGeometry(int newWidth);
	Ui::FadeWrap<Ui::RpWidget> *pushButton(
		base::unique_qptr<Ui::RpWidget> button);
	void forceButtonVisibility(
		Ui::FadeWrap<Ui::RpWidget> *button,
		rpl::producer<bool> shown);
	void removeButton(not_null<Ui::RpWidget*> button);
	void startHighlightAnimation();
	void updateControlsVisibility(anim::type animated);

	[[nodiscard]] bool selectionMode() const;
	[[nodiscard]] bool storiesTitle() const;
	[[nodiscard]] bool searchMode() const;
	[[nodiscard]] Ui::StringWithNumbers generateSelectedText() const;
	[[nodiscard]] bool computeCanDelete() const;
	[[nodiscard]] bool computeCanForward() const;
	[[nodiscard]] bool computeCanToggleStoryPin() const;
	void updateSelectionState();
	void createSelectionControls();

	void performForward();
	void performDelete();
	void performToggleStoryPin();

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
	std::optional<Ui::RoundRect> _roundRect;
	Ui::Animations::Simple _a_highlight;
	bool _highlight = false;
	QPointer<Ui::FadeWrap<Ui::IconButton>> _back;
	std::vector<base::unique_qptr<Ui::RpWidget>> _buttons;
	QPointer<Ui::FadeWrap<Ui::FlatLabel>> _title;
	QPointer<Ui::FadeWrap<Ui::FlatLabel>> _subtitle;

	bool _searchModeEnabled = false;
	bool _searchModeAvailable = false;
	base::unique_qptr<Ui::RpWidget> _searchView;
	QPointer<Ui::InputField> _searchField;

	rpl::event_stream<> _backClicks;
	rpl::event_stream<uint64> _storyClicks;

	SelectedItems _selectedItems;
	bool _canDelete = false;
	bool _canForward = false;
	bool _canToggleStoryPin = false;
	bool _storiesArchive = false;
	QPointer<Ui::FadeWrap<Ui::IconButton>> _cancelSelection;
	QPointer<Ui::FadeWrap<Ui::LabelWithNumbers>> _selectionText;
	QPointer<Ui::FadeWrap<Ui::IconButton>> _forward;
	QPointer<Ui::FadeWrap<Ui::IconButton>> _delete;
	QPointer<Ui::FadeWrap<Ui::IconButton>> _toggleStoryPin;
	rpl::event_stream<SelectionAction> _selectionActionRequests;

	QPointer<Ui::FadeWrap<Ui::AbstractButton>> _storiesWrap;
	QPointer<Dialogs::Stories::List> _stories;
	rpl::lifetime _storiesLifetime;
	int _storiesCount = 0;

	using UpdateCallback = Fn<bool(anim::type)>;
	std::map<QObject*, UpdateCallback> _updateControlCallbacks;

};

} // namespace Info
