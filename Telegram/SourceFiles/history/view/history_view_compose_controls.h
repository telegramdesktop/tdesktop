/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
#include "chat_helpers/tabbed_selector.h"

namespace ChatHelpers {
class TabbedPanel;
class TabbedSelector;
} // namespace ChatHelpers

namespace InlineBots {
namespace Layout {
class ItemBase;
class Widget;
} // namespace Layout
class Result;
} // namespace InlineBots

namespace Ui {
class SendButton;
class IconButton;
class EmojiButton;
class InputField;
} // namespace Ui

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
struct SectionShow;
} // namespace Window

namespace HistoryView {

class ComposeControls final {
public:
	enum class Mode {
		Normal,
		Scheduled,
	};

	ComposeControls(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> window,
		Mode mode);
	~ComposeControls();

	[[nodiscard]] Main::Session &session() const;

	void setHistory(History *history);

	void move(int x, int y);
	void resizeToWidth(int width);
	[[nodiscard]] rpl::producer<int> height() const;
	[[nodiscard]] int heightCurrent() const;

	void focus();
	[[nodiscard]] rpl::producer<> cancelRequests() const;
	[[nodiscard]] rpl::producer<> sendRequests() const;
	[[nodiscard]] rpl::producer<> attachRequests() const;
	[[nodiscard]] rpl::producer<not_null<DocumentData*>> fileChosen() const;
	[[nodiscard]] rpl::producer<not_null<PhotoData*>> photoChosen() const;
	[[nodiscard]] auto inlineResultChosen() const
		-> rpl::producer<ChatHelpers::TabbedSelector::InlineChosen>;

	void pushTabbedSelectorToThirdSection(const Window::SectionShow &params);
	bool returnTabbedSelector();

	void showForGrab();
	void showStarted();
	void showFinished();

	[[nodiscard]] TextWithTags getTextWithAppliedMarkdown() const;
	void clear();
	void hidePanelsAnimated();

private:
	void init();
	void initField();
	void initTabbedSelector();
	void initSendButton();
	void updateSendButtonType();
	void updateHeight();
	void updateControlsGeometry(QSize size);
	void updateOuterGeometry(QRect rect);
	void paintBackground(QRect clip);

	void escape();
	void toggleTabbedSelectorMode();
	void createTabbedPanel();
	void setTabbedPanel(std::unique_ptr<ChatHelpers::TabbedPanel> panel);
	void setText(const TextWithTags &text);

	const not_null<QWidget*> _parent;
	const not_null<Window::SessionController*> _window;
	History *_history = nullptr;
	//Mode _mode = Mode::Normal;

	const std::unique_ptr<Ui::RpWidget> _wrap;

	const not_null<Ui::SendButton*> _send;
	const not_null<Ui::IconButton*> _attachToggle;
	const not_null<Ui::EmojiButton*> _tabbedSelectorToggle;
	const not_null<Ui::InputField*> _field;
	std::unique_ptr<InlineBots::Layout::Widget> _inlineResults;
	std::unique_ptr<ChatHelpers::TabbedPanel> _tabbedPanel;

	rpl::event_stream<> _cancelRequests;
	rpl::event_stream<not_null<DocumentData*>> _fileChosen;
	rpl::event_stream<not_null<PhotoData*>> _photoChosen;
	rpl::event_stream<ChatHelpers::TabbedSelector::InlineChosen> _inlineResultChosen;

	//bool _recording = false;
	//bool _inField = false;
	//bool _inReplyEditForward = false;
	//bool _inClickable = false;
	//int _recordingSamples = 0;
	//int _recordCancelWidth;

	rpl::lifetime _uploaderSubscriptions;

	// This can animate for a very long time (like in music playing),
	// so it should be a Basic, not a Simple animation.
	Ui::Animations::Basic _recordingAnimation;
	anim::value _recordingLevel;

	Fn<void()> _raiseEmojiSuggestions;

};

} // namespace HistoryView
