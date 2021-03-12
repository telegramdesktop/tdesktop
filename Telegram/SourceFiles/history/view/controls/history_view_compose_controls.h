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
#include "base/timer.h"
#include "dialogs/dialogs_key.h"
#include "history/view/controls/compose_controls_common.h"
#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
#include "ui/widgets/input_fields.h"
#include "chat_helpers/tabbed_selector.h"

class History;
class FieldAutocomplete;

namespace SendMenu {
enum class Type;
} // namespace SendMenu

namespace ChatHelpers {
class TabbedPanel;
class TabbedSelector;
} // namespace ChatHelpers

namespace Data {
struct MessagePosition;
struct Draft;
class DraftKey;
enum class PreviewState : char;
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
class SilentToggle;
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

namespace Controls {
class VoiceRecordBar;
class TTLButton;
} // namespace Controls

class FieldHeader;

class ComposeControls final {
public:
	using FileChosen = ChatHelpers::TabbedSelector::FileChosen;
	using PhotoChosen = ChatHelpers::TabbedSelector::PhotoChosen;
	using InlineChosen = ChatHelpers::TabbedSelector::InlineChosen;

	using MessageToEdit = Controls::MessageToEdit;
	using VoiceToSend = Controls::VoiceToSend;
	using SendActionUpdate = Controls::SendActionUpdate;
	using SetHistoryArgs = Controls::SetHistoryArgs;
	using ReplyNextRequest = Controls::ReplyNextRequest;
	using FieldHistoryAction = Ui::InputField::HistoryAction;

	enum class Mode {
		Normal,
		Scheduled,
	};

	ComposeControls(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> window,
		Mode mode,
		SendMenu::Type sendMenuType);
	~ComposeControls();

	[[nodiscard]] Main::Session &session() const;
	void setHistory(SetHistoryArgs &&args);
	void setCurrentDialogsEntryState(Dialogs::EntryState state);

	void finishAnimating();

	void move(int x, int y);
	void resizeToWidth(int width);
	void setAutocompleteBoundingRect(QRect rect);
	[[nodiscard]] rpl::producer<int> height() const;
	[[nodiscard]] int heightCurrent() const;

	bool focus();
	[[nodiscard]] rpl::producer<> cancelRequests() const;
	[[nodiscard]] rpl::producer<Api::SendOptions> sendRequests() const;
	[[nodiscard]] rpl::producer<VoiceToSend> sendVoiceRequests() const;
	[[nodiscard]] rpl::producer<QString> sendCommandRequests() const;
	[[nodiscard]] rpl::producer<MessageToEdit> editRequests() const;
	[[nodiscard]] rpl::producer<> attachRequests() const;
	[[nodiscard]] rpl::producer<FileChosen> fileChosen() const;
	[[nodiscard]] rpl::producer<PhotoChosen> photoChosen() const;
	[[nodiscard]] rpl::producer<Data::MessagePosition> scrollRequests() const;
	[[nodiscard]] rpl::producer<InlineChosen> inlineResultChosen() const;
	[[nodiscard]] rpl::producer<SendActionUpdate> sendActionUpdates() const;
	[[nodiscard]] rpl::producer<not_null<QEvent*>> viewportEvents() const;
	[[nodiscard]] auto scrollKeyEvents() const
	-> rpl::producer<not_null<QKeyEvent*>>;
	[[nodiscard]] auto editLastMessageRequests() const
	-> rpl::producer<not_null<QKeyEvent*>>;
	[[nodiscard]] auto replyNextRequests() const
	-> rpl::producer<ReplyNextRequest>;

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

	[[nodiscard]] bool preventsClose(Fn<void()> &&continueCallback) const;

	void showForGrab();
	void showStarted();
	void showFinished();
	void raisePanels();

	void editMessage(FullMsgId id);
	void cancelEditMessage();

	void replyToMessage(FullMsgId id);
	void cancelReplyMessage();

	bool handleCancelRequest();

	[[nodiscard]] TextWithTags getTextWithAppliedMarkdown() const;
	[[nodiscard]] WebPageId webPageId() const;
	void setText(const TextWithTags &text);
	void clear();
	void hidePanelsAnimated();
	void clearListenState();

	[[nodiscard]] rpl::producer<bool> lockShowStarts() const;
	[[nodiscard]] bool isLockPresent() const;
	[[nodiscard]] bool isRecording() const;

	void applyDraft(
		FieldHistoryAction fieldHistoryAction = FieldHistoryAction::Clear);

	Fn<void()> restoreTextCallback(const QString &insertTextOnCancel) const;

private:
	enum class TextUpdateEvent {
		SaveDraft = (1 << 0),
		SendTyping = (1 << 1),
	};
	enum class DraftType {
		Normal,
		Edit,
	};
	enum class SendRequestType {
		Text,
		Voice,
	};
	using TextUpdateEvents = base::flags<TextUpdateEvent>;
	friend inline constexpr bool is_flag_type(TextUpdateEvent) { return true; };

	void init();
	void initField();
	void initTabbedSelector();
	void initSendButton();
	void initWebpageProcess();
	void initWriteRestriction();
	void initVoiceRecordBar();
	void initAutocomplete();
	void initKeyHandler();
	void updateSubmitSettings();
	void updateSendButtonType();
	void updateMessagesTTLShown();
	void updateHeight();
	void updateWrappingVisibility();
	void updateControlsVisibility();
	void updateControlsGeometry(QSize size);
	void updateOuterGeometry(QRect rect);
	void paintBackground(QRect clip);

	[[nodiscard]] auto computeSendButtonType() const;
	[[nodiscard]] SendMenu::Type sendMenuType() const;
	[[nodiscard]] SendMenu::Type sendButtonMenuType() const;

	void sendSilent();
	void sendScheduled();
	[[nodiscard]] auto sendContentRequests(
		SendRequestType requestType = SendRequestType::Text) const;

	void orderControls();
	void checkAutocomplete();
	void updateStickersByEmoji();
	void updateFieldPlaceholder();
	void updateSilentBroadcast();
	void editMessage(not_null<HistoryItem*> item);

	void escape();
	void fieldChanged();
	void fieldTabbed();
	void toggleTabbedSelectorMode();
	void createTabbedPanel();
	void setTabbedPanel(std::unique_ptr<ChatHelpers::TabbedPanel> panel);

	bool showRecordButton() const;
	void drawRestrictedWrite(Painter &p, const QString &error);
	bool updateBotCommandShown();

	void cancelInlineBot();
	void clearInlineBot();
	void inlineBotChanged();

	bool hasSilentBroadcastToggle() const;

	// Look in the _field for the inline bot and query string.
	void updateInlineBotQuery();

	// Request to show results in the emoji panel.
	void applyInlineBotQuery(UserData *bot, const QString &query);

	void inlineBotResolveDone(const MTPcontacts_ResolvedPeer &result);
	void inlineBotResolveFail(const MTP::Error &error, const QString &username);

	[[nodiscard]] Data::DraftKey draftKey(
		DraftType type = DraftType::Normal) const;
	[[nodiscard]] Data::DraftKey draftKeyCurrent() const;
	void saveDraft(bool delayed = false);
	void saveDraftDelayed();

	void writeDrafts();
	void writeDraftTexts();
	void writeDraftCursors();
	void setFieldText(
		const TextWithTags &textWithTags,
		TextUpdateEvents events = 0,
		FieldHistoryAction fieldHistoryAction = FieldHistoryAction::Clear);
	void clearFieldText(
		TextUpdateEvents events = 0,
		FieldHistoryAction fieldHistoryAction = FieldHistoryAction::Clear);
	void saveFieldToHistoryLocalDraft();

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

	const std::shared_ptr<Ui::SendButton> _send;
	const not_null<Ui::IconButton*> _attachToggle;
	const not_null<Ui::EmojiButton*> _tabbedSelectorToggle;
	const not_null<Ui::InputField*> _field;
	const not_null<Ui::IconButton*> _botCommandStart;
	std::unique_ptr<Ui::SilentToggle> _silent;
	std::unique_ptr<Controls::TTLButton> _ttlInfo;

	std::unique_ptr<InlineBots::Layout::Widget> _inlineResults;
	std::unique_ptr<ChatHelpers::TabbedPanel> _tabbedPanel;
	std::unique_ptr<FieldAutocomplete> _autocomplete;

	friend class FieldHeader;
	const std::unique_ptr<FieldHeader> _header;
	const std::unique_ptr<Controls::VoiceRecordBar> _voiceRecordBar;

	const SendMenu::Type _sendMenuType;

	rpl::event_stream<Api::SendOptions> _sendCustomRequests;
	rpl::event_stream<> _cancelRequests;
	rpl::event_stream<FileChosen> _fileChosen;
	rpl::event_stream<PhotoChosen> _photoChosen;
	rpl::event_stream<InlineChosen> _inlineResultChosen;
	rpl::event_stream<SendActionUpdate> _sendActionUpdates;
	rpl::event_stream<QString> _sendCommandRequests;
	rpl::event_stream<not_null<QKeyEvent*>> _scrollKeyEvents;
	rpl::event_stream<not_null<QKeyEvent*>> _editLastMessageRequests;
	rpl::event_stream<> _attachRequests;
	rpl::event_stream<ReplyNextRequest> _replyNextRequests;

	TextUpdateEvents _textUpdateEvents = TextUpdateEvents()
		| TextUpdateEvent::SaveDraft
		| TextUpdateEvent::SendTyping;
	Dialogs::EntryState _currentDialogsEntryState;

	crl::time _saveDraftStart = 0;
	bool _saveDraftText = false;
	base::Timer _saveDraftTimer;

	UserData *_inlineBot = nullptr;
	QString _inlineBotUsername;
	bool _inlineLookingUpBot = false;
	mtpRequestId _inlineBotResolveRequestId = 0;
	bool _isInlineBot = false;
	bool _botCommandShown = false;

	Fn<void()> _previewCancel;
	Fn<void(Data::PreviewState)> _previewSetState;
	Data::PreviewState _previewState = Data::PreviewState();

	rpl::lifetime _uploaderSubscriptions;

	Fn<void()> _raiseEmojiSuggestions;

};

} // namespace HistoryView
