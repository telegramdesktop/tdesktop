/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "window/section_widget.h"
#include "ui/effects/animations.h"

namespace Storage {
enum class SharedMediaType : signed char;
} // namespace Storage

namespace Ui {
class FadeShadow;
class PlainShadow;
class PopupMenu;
class IconButton;
class RoundRect;
struct StringWithNumbers;
} // namespace Ui

namespace Window {
enum class SlideDirection;
} // namespace Window

namespace Info {
namespace Profile {
class Widget;
} // namespace Profile

namespace Media {
class Widget;
} // namespace Media

class Key;
class Controller;
class Section;
class Memento;
class MoveMemento;
class ContentMemento;
class ContentWidget;
class TopBar;

enum class Wrap {
	Layer,
	Narrow,
	Side,
};

struct SelectedItem {
	explicit SelectedItem(GlobalMsgId globalId) : globalId(globalId) {
	}

	GlobalMsgId globalId;
	bool canDelete = false;
	bool canForward = false;
	bool canToggleStoryPin = false;
	bool canUnpinStory = false;
};

struct SelectedItems {
	SelectedItems() = default;
	explicit SelectedItems(Storage::SharedMediaType type);

	Fn<Ui::StringWithNumbers(int)> title;
	std::vector<SelectedItem> list;
};

enum class SelectionAction {
	Clear,
	Forward,
	Delete,
	ToggleStoryPin,
	ToggleStoryInProfile,
};

class WrapWidget final : public Window::SectionWidget {
public:
	WrapWidget(
		QWidget *parent,
		not_null<Window::SessionController*> window,
		Wrap wrap,
		not_null<Memento*> memento);

	Key key() const;
	Dialogs::RowDescriptor activeChat() const override;
	Wrap wrap() const {
		return _wrap.current();
	}
	rpl::producer<Wrap> wrapValue() const;
	void setWrap(Wrap wrap);

	rpl::producer<> contentChanged() const;

	not_null<Controller*> controller() {
		return _controller.get();
	}

	bool hasTopBarShadow() const override;
	QPixmap grabForShowAnimation(
		const Window::SectionSlideParams &params) override;

	void forceContentRepaint();

	bool showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) override;
	bool showBackFromStackInternal(const Window::SectionShow &params);
	void removeFromStack(const std::vector<Section> &sections);
	std::shared_ptr<Window::SectionMemento> createMemento() override;

	rpl::producer<int> desiredHeightValue() const override;

	// Float player interface.
	bool floatPlayerHandleWheelEvent(QEvent *e) override;
	QRect floatPlayerAvailableRect() override;

	object_ptr<Ui::RpWidget> createTopBarSurrogate(QWidget *parent);

	[[nodiscard]] bool closeByOutsideClick() const;

	void updateGeometry(
		QRect newGeometry,
		bool expanding,
		int additionalScroll,
		int maxVisibleHeight);
	[[nodiscard]] int scrollBottomSkip() const;
	[[nodiscard]] int scrollTillBottom(int forHeight) const;
	[[nodiscard]] rpl::producer<int> scrollTillBottomChanges() const;
	[[nodiscard]] rpl::producer<bool> grabbingForExpanding() const;
	[[nodiscard]] const Ui::RoundRect *bottomSkipRounding() const;

	[[nodiscard]] rpl::producer<> removeRequests() const override {
		return _removeRequests.events();
	}

	~WrapWidget();

protected:
	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

	void doSetInnerFocus() override;
	void showFinishedHook() override;

	void showAnimatedHook(
		const Window::SectionSlideParams &params) override;

private:
	using SlideDirection = Window::SlideDirection;
	using SectionSlideParams = Window::SectionSlideParams;
	struct StackItem;

	void startInjectingActivePeerProfiles();
	void injectActiveProfile(Dialogs::Key key);
	void injectActivePeerProfile(not_null<PeerData*> peer);
	void injectActiveProfileMemento(
		std::shared_ptr<ContentMemento> memento);
	void checkBeforeClose(Fn<void()> close);
	void restoreHistoryStack(
		std::vector<std::shared_ptr<ContentMemento>> stack);
	bool hasStackHistory() const {
		return !_historyStack.empty();
	}
	void showNewContent(not_null<ContentMemento*> memento);
	void showNewContent(
		not_null<ContentMemento*> memento,
		const Window::SectionShow &params);
	bool returnToFirstStackFrame(
		not_null<ContentMemento*> memento,
		const Window::SectionShow &params);
	void setupTop();
	void setupTopBarMenuToggle();
	void createTopBar();
	void highlightTopBar();
	void setupShortcuts();

	[[nodiscard]] bool hasBackButton() const;
	[[nodiscard]] bool willHaveBackButton(
		const Window::SectionShow &params) const;

	not_null<RpWidget*> topWidget() const;

	QRect contentGeometry() const;
	rpl::producer<int> desiredHeightForContent() const;
	void finishShowContent();
	rpl::producer<bool> topShadowToggledValue() const;
	void updateContentGeometry();

	void showContent(object_ptr<ContentWidget> content);
	object_ptr<ContentWidget> createContent(
		not_null<ContentMemento*> memento,
		not_null<Controller*> controller);
	std::unique_ptr<Controller> createController(
		not_null<Window::SessionController*> window,
		not_null<ContentMemento*> memento);

	rpl::producer<SelectedItems> selectedListValue() const;
	bool requireTopBarSearch() const;

	void addTopBarMenuButton();
	void addProfileCallsButton();
	void showTopBarMenu(bool check);

	rpl::variable<Wrap> _wrap;
	std::unique_ptr<Controller> _controller;
	object_ptr<ContentWidget> _content = { nullptr };
	int _additionalScroll = 0;
	int _maxVisibleHeight = 0;
	bool _expanding = false;
	rpl::variable<bool> _grabbingForExpanding = false;
	object_ptr<TopBar> _topBar = { nullptr };
	object_ptr<Ui::RpWidget> _topBarSurrogate = { nullptr };
	Ui::Animations::Simple _topBarOverrideAnimation;
	bool _topBarOverrideShown = false;

	object_ptr<Ui::FadeShadow> _topShadow;
	object_ptr<Ui::FadeShadow> _bottomShadow;
	base::unique_qptr<Ui::IconButton> _topBarMenuToggle;
	base::unique_qptr<Ui::PopupMenu> _topBarMenu;

	std::vector<StackItem> _historyStack;
	rpl::event_stream<> _removeRequests;

	rpl::event_stream<rpl::producer<int>> _desiredHeights;
	rpl::event_stream<rpl::producer<bool>> _desiredShadowVisibilities;
	rpl::event_stream<rpl::producer<bool>> _desiredBottomShadowVisibilities;
	rpl::event_stream<rpl::producer<SelectedItems>> _selectedLists;
	rpl::event_stream<rpl::producer<int>> _scrollTillBottomChanges;
	rpl::event_stream<> _contentChanges;

};

} // namespace Info
