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
class SettingsSlider;
class FadeShadow;
class PlainShadow;
class DropdownMenu;
class IconButton;
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
	explicit SelectedItem(FullMsgId msgId) : msgId(msgId) {
	}

	FullMsgId msgId;
	bool canDelete = false;
	bool canForward = false;
};

struct SelectedItems {
	explicit SelectedItems(Storage::SharedMediaType type)
	: type(type) {
	}

	Storage::SharedMediaType type;
	std::vector<SelectedItem> list;

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
	std::shared_ptr<Window::SectionMemento> createMemento() override;

	rpl::producer<int> desiredHeightValue() const override;

	void updateInternalState(not_null<Memento*> memento);

	// Float player interface.
	bool floatPlayerHandleWheelEvent(QEvent *e) override;
	QRect floatPlayerAvailableRect() override;

	object_ptr<Ui::RpWidget> createTopBarSurrogate(QWidget *parent);

	bool closeByOutsideClick() const;

	void updateGeometry(QRect newGeometry, int additionalScroll);
	int scrollTillBottom(int forHeight) const;
	rpl::producer<int>  scrollTillBottomChanges() const;

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
	//enum class Tab {
	//	Profile,
	//	Media,
	//	None,
	//};
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
	//void setupTabbedTop();
	//void setupTabs(Tab tab);
	//void createTabs();
	void createTopBar();
	void highlightTopBar();
	void setupShortcuts();

	not_null<RpWidget*> topWidget() const;

	QRect contentGeometry() const;
	rpl::producer<int> desiredHeightForContent() const;
	void finishShowContent();
	rpl::producer<bool> topShadowToggledValue() const;
	void updateContentGeometry();

	//void showTab(Tab tab);
	void showContent(object_ptr<ContentWidget> content);
	//std::shared_ptr<ContentMemento> createTabMemento(Tab tab);
	object_ptr<ContentWidget> createContent(
		not_null<ContentMemento*> memento,
		not_null<Controller*> controller);
	std::unique_ptr<Controller> createController(
		not_null<Window::SessionController*> window,
		not_null<ContentMemento*> memento);
	//void convertProfileFromStackToTab();

	rpl::producer<SelectedItems> selectedListValue() const;
	bool requireTopBarSearch() const;

	void addTopBarMenuButton();
	void addContentSaveButton();
	void addProfileCallsButton();
	void addProfileNotificationsButton();
	void showTopBarMenu();

	rpl::variable<Wrap> _wrap;
	std::unique_ptr<Controller> _controller;
	object_ptr<ContentWidget> _content = { nullptr };
	int _additionalScroll = 0;
	//object_ptr<Ui::PlainShadow> _topTabsBackground = { nullptr };
	//object_ptr<Ui::SettingsSlider> _topTabs = { nullptr };
	object_ptr<TopBar> _topBar = { nullptr };
	object_ptr<Ui::RpWidget> _topBarSurrogate = { nullptr };
	Ui::Animations::Simple _topBarOverrideAnimation;
	bool _topBarOverrideShown = false;

	object_ptr<Ui::FadeShadow> _topShadow;
	base::unique_qptr<Ui::IconButton> _topBarMenuToggle;
	base::unique_qptr<Ui::DropdownMenu> _topBarMenu;

//	Tab _tab = Tab::Profile;
//	std::shared_ptr<ContentMemento> _anotherTabMemento;
	std::vector<StackItem> _historyStack;

	rpl::event_stream<rpl::producer<int>> _desiredHeights;
	rpl::event_stream<rpl::producer<bool>> _desiredShadowVisibilities;
	rpl::event_stream<rpl::producer<SelectedItems>> _selectedLists;
	rpl::event_stream<rpl::producer<int>> _scrollTillBottomChanges;
	rpl::event_stream<> _contentChanges;

};

} // namespace Info
