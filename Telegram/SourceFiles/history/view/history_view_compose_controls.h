/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_common.h"
#include "api/api_send_progress.h"
#include "base/unique_qptr.h"
#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
#include "ui/widgets/input_fields.h"
#include "chat_helpers/tabbed_selector.h"

class History;

namespace ChatHelpers {
class TabbedPanel;
class TabbedSelector;
} // namespace ChatHelpers

namespace Data {
struct MessagePosition;
} // namespace Data

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
} // namespace Ui

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
struct SectionShow;
} // namespace Window

namespace HistoryView {

class FieldHeader;

class ComposeControls final {
public:
	using FileChosen = ChatHelpers::TabbedSelector::FileChosen;
	using PhotoChosen = ChatHelpers::TabbedSelector::PhotoChosen;
	enum class Mode {
		Normal,
		Scheduled,
	};

	struct MessageToEdit {
		FullMsgId fullId;
		Api::SendOptions options;
		TextWithTags textWithTags;
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
	[[nodiscard]] rpl::producer<MessageToEdit> editRequests() const;
	[[nodiscard]] rpl::producer<> attachRequests() const;
	[[nodiscard]] rpl::producer<FileChosen> fileChosen() const;
	[[nodiscard]] rpl::producer<PhotoChosen> photoChosen() const;
	[[nodiscard]] rpl::producer<Data::MessagePosition> scrollRequests() const;
	[[nodiscard]] rpl::producer<not_null<QKeyEvent*>> keyEvents() const;
	[[nodiscard]] auto inlineResultChosen() const
		-> rpl::producer<ChatHelpers::TabbedSelector::InlineChosen>;
	[[nodiscard]] rpl::producer<Api::SendProgress> sendActionUpdates() const;

	using MimeDataHook = Fn<bool(
		not_null<const QMimeData*> data,
		Ui::InputField::MimeAction action)>;
	void setMimeDataHook(MimeDataHook hook);

	bool pushTabbedSelectorToThirdSection(
		not_null<PeerData*> peer,
		const Window::SectionShow &params);
	bool returnTabbedSelector();

	[[nodiscard]] bool isEditingMessage() const;
	[[nodiscard]] FullMsgId replyingToMessage() const;

	void showForGrab();
	void showStarted();
	void showFinished();

	void editMessage(FullMsgId id);
	void cancelEditMessage();

	void replyToMessage(FullMsgId id);
	void cancelReplyMessage();

	[[nodiscard]] TextWithTags getTextWithAppliedMarkdown() const;
	[[nodiscard]] WebPageId webPageId() const;
	void setText(const TextWithTags &text);
	void clear();
	void hidePanelsAnimated();

private:
	enum class TextUpdateEvent {
		//SaveDraft = (1 << 0),
		SendTyping = (1 << 1),
	};
	using TextUpdateEvents = base::flags<TextUpdateEvent>;
	friend inline constexpr bool is_flag_type(TextUpdateEvent) { return true; };

	void init();
	void initField();
	void initTabbedSelector();
	void initSendButton();
	void initWebpageProcess();
	void updateSendButtonType();
	void updateHeight();
	void updateControlsGeometry(QSize size);
	void updateOuterGeometry(QRect rect);
	void paintBackground(QRect clip);

	void escape();
	void fieldChanged();
	void toggleTabbedSelectorMode();
	void createTabbedPanel();
	void setTabbedPanel(std::unique_ptr<ChatHelpers::TabbedPanel> panel);

	void setTextFromEditingMessage(not_null<HistoryItem*> item);

	const not_null<QWidget*> _parent;
	const not_null<Window::SessionController*> _window;
	History *_history = nullptr;
	Mode _mode = Mode::Normal;

	const std::unique_ptr<Ui::RpWidget> _wrap;

	const not_null<Ui::SendButton*> _send;
	const not_null<Ui::IconButton*> _attachToggle;
	const not_null<Ui::EmojiButton*> _tabbedSelectorToggle;
	const not_null<Ui::InputField*> _field;
	std::unique_ptr<InlineBots::Layout::Widget> _inlineResults;
	std::unique_ptr<ChatHelpers::TabbedPanel> _tabbedPanel;

	friend class FieldHeader;
	const std::unique_ptr<FieldHeader> _header;

	rpl::event_stream<> _cancelRequests;
	rpl::event_stream<FileChosen> _fileChosen;
	rpl::event_stream<PhotoChosen> _photoChosen;
	rpl::event_stream<ChatHelpers::TabbedSelector::InlineChosen> _inlineResultChosen;
	rpl::event_stream<Api::SendProgress> _sendActionUpdates;

	TextWithTags _localSavedText;
	TextUpdateEvents _textUpdateEvents;

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
