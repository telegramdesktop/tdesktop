/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/required.h"
#include "api/api_common.h"
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

namespace Api {
enum class SendProgressType;
} // namespace Api

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
	struct VoiceToSend {
		QByteArray bytes;
		VoiceWaveform waveform;
		int duration = 0;
	};
	struct SendActionUpdate {
		Api::SendProgressType type = Api::SendProgressType();
		int progress = 0;
	};

	ComposeControls(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> window,
		Mode mode);
	~ComposeControls();

	[[nodiscard]] Main::Session &session() const;

	struct SetHistoryArgs {
		required<History*> history;
		Fn<bool()> showSlowmodeError;
		rpl::producer<int> slowmodeSecondsLeft;
		rpl::producer<bool> sendDisabledBySlowmode;
		rpl::producer<std::optional<QString>> writeRestriction;
	};
	void setHistory(SetHistoryArgs &&args);
	void finishAnimating();

	void move(int x, int y);
	void resizeToWidth(int width);
	[[nodiscard]] rpl::producer<int> height() const;
	[[nodiscard]] int heightCurrent() const;

	bool focus();
	[[nodiscard]] rpl::producer<> cancelRequests() const;
	[[nodiscard]] rpl::producer<> sendRequests() const;
	[[nodiscard]] rpl::producer<VoiceToSend> sendVoiceRequests() const;
	[[nodiscard]] rpl::producer<MessageToEdit> editRequests() const;
	[[nodiscard]] rpl::producer<> attachRequests() const;
	[[nodiscard]] rpl::producer<FileChosen> fileChosen() const;
	[[nodiscard]] rpl::producer<PhotoChosen> photoChosen() const;
	[[nodiscard]] rpl::producer<Data::MessagePosition> scrollRequests() const;
	[[nodiscard]] rpl::producer<not_null<QKeyEvent*>> keyEvents() const;
	[[nodiscard]] auto inlineResultChosen() const
		-> rpl::producer<ChatHelpers::TabbedSelector::InlineChosen>;
	[[nodiscard]] rpl::producer<SendActionUpdate> sendActionUpdates() const;

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
	void initWriteRestriction();
	void updateSendButtonType();
	void updateHeight();
	void updateControlsVisibility();
	void updateControlsGeometry(QSize size);
	void updateOuterGeometry(QRect rect);
	void paintBackground(QRect clip);

	void escape();
	void fieldChanged();
	void toggleTabbedSelectorMode();
	void createTabbedPanel();
	void setTabbedPanel(std::unique_ptr<ChatHelpers::TabbedPanel> panel);

	void setTextFromEditingMessage(not_null<HistoryItem*> item);

	void recordError();
	void recordUpdated(quint16 level, int samples);
	void recordDone(QByteArray result, VoiceWaveform waveform, int samples);

	bool recordingAnimationCallback(crl::time now);
	void stopRecording(bool send);

	void recordStartCallback();
	void recordStopCallback(bool active);
	void recordUpdateCallback(QPoint globalPos);

	bool showRecordButton() const;
	void drawRecording(Painter &p, float64 recordActive);
	void drawRestrictedWrite(Painter &p, const QString &error);
	void updateOverStates(QPoint pos);

	const not_null<QWidget*> _parent;
	const not_null<Window::SessionController*> _window;
	History *_history = nullptr;
	Fn<bool()> _showSlowmodeError;
	rpl::variable<int> _slowmodeSecondsLeft;
	rpl::variable<bool> _sendDisabledBySlowmode;
	rpl::variable<std::optional<QString>> _writeRestriction;
	Mode _mode = Mode::Normal;

	const std::unique_ptr<Ui::RpWidget> _wrap;
	const std::unique_ptr<Ui::RpWidget> _writeRestricted;

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
	rpl::event_stream<SendActionUpdate> _sendActionUpdates;
	rpl::event_stream<VoiceToSend> _sendVoiceRequests;

	TextWithTags _localSavedText;
	TextUpdateEvents _textUpdateEvents;

	bool _recording = false;
	bool _inField = false;
	//bool _inReplyEditForward = false;
	//bool _inClickable = false;
	int _recordingSamples = 0;
	int _recordCancelWidth;
	rpl::lifetime _recordingLifetime;

	rpl::lifetime _uploaderSubscriptions;

	// This can animate for a very long time (like in music playing),
	// so it should be a Basic, not a Simple animation.
	Ui::Animations::Basic _recordingAnimation;
	anim::value _recordingLevel;

	Fn<void()> _raiseEmojiSuggestions;

};

} // namespace HistoryView
