/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_common.h"
#include "base/required.h"
#include "base/unique_qptr.h"
#include "base/timer.h"
#include "chat_helpers/compose/compose_features.h"
#include "dialogs/dialogs_key.h"
#include "history/view/controls/compose_controls_common.h"
#include "ui/round_rect.h"
#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
#include "ui/widgets/fields/input_field.h"

class History;
class DocumentData;
class Image;

namespace style {
struct ComposeControls;
} // namespace style

namespace SendMenu {
struct Details;
} // namespace SendMenu

namespace ChatHelpers {
class TabbedPanel;
class TabbedSelector;
struct FileChosen;
struct PhotoChosen;
class Show;
enum class PauseReason;
class FieldAutocomplete;
} // namespace ChatHelpers

namespace Data {
struct MessagePosition;
struct Draft;
class DraftKey;
class PhotoMedia;
struct WebPageDraft;
} // namespace Data

namespace InlineBots {
namespace Layout {
class ItemBase;
class Widget;
} // namespace Layout
class Result;
struct ResultSelected;
} // namespace InlineBots

namespace Ui {
class SendButton;
class IconButton;
class EmojiButton;
class SendAsButton;
class SilentToggle;
class DropdownMenu;
struct PreparedList;
} // namespace Ui

namespace Ui::Emoji {
class SuggestionsController;
} // namespace Ui::Emoji

namespace Main {
class Session;
} // namespace Main

namespace Window {
struct SectionShow;
class SessionController;
} // namespace Window

namespace Api {
enum class SendProgressType;
} // namespace Api

namespace HistoryView::Controls {
class VoiceRecordBar;
class TTLButton;
class WebpageProcessor;
class CharactersLimitLabel;
} // namespace HistoryView::Controls

namespace HistoryView {

class FieldHeader;

enum class ComposeControlsMode {
	Normal,
	Scheduled,
};

extern const ChatHelpers::PauseReason kDefaultPanelsLevel;

struct ComposeControlsDescriptor {
	const style::ComposeControls *stOverride = nullptr;
	std::shared_ptr<ChatHelpers::Show> show;
	Fn<void(not_null<DocumentData*>)> unavailableEmojiPasted;
	ComposeControlsMode mode = ComposeControlsMode::Normal;
	Fn<SendMenu::Details()> sendMenuDetails = nullptr;
	Window::SessionController *regularWindow = nullptr;
	rpl::producer<ChatHelpers::FileChosen> stickerOrEmojiChosen;
	rpl::producer<QString> customPlaceholder;
	QWidget *panelsParent = nullptr;
	ChatHelpers::PauseReason panelsLevel = kDefaultPanelsLevel;
	QString voiceCustomCancelText;
	bool voiceLockFromBottom = false;
	ChatHelpers::ComposeFeatures features;
	rpl::producer<bool> scheduledToggleValue;
};

class ComposeControls final {
public:
	using FileChosen = ChatHelpers::FileChosen;
	using PhotoChosen = ChatHelpers::PhotoChosen;
	using InlineChosen = InlineBots::ResultSelected;

	using MessageToEdit = Controls::MessageToEdit;
	using VoiceToSend = Controls::VoiceToSend;
	using SendActionUpdate = Controls::SendActionUpdate;
	using SetHistoryArgs = Controls::SetHistoryArgs;
	using ReplyNextRequest = Controls::ReplyNextRequest;
	using FieldHistoryAction = Ui::InputField::HistoryAction;
	using Mode = ComposeControlsMode;

	ComposeControls(
		not_null<Ui::RpWidget*> parent,
		ComposeControlsDescriptor descriptor);
	~ComposeControls();

	[[nodiscard]] Main::Session &session() const;
	void setHistory(SetHistoryArgs &&args);
	void updateTopicRootId(MsgId topicRootId);
	void updateShortcutId(BusinessShortcutId shortcutId);
	void setCurrentDialogsEntryState(Dialogs::EntryState state);
	[[nodiscard]] PeerData *sendAsPeer() const;

	void finishAnimating();

	void move(int x, int y);
	void resizeToWidth(int width);
	void setAutocompleteBoundingRect(QRect rect);
	[[nodiscard]] rpl::producer<int> height() const;
	[[nodiscard]] int heightCurrent() const;

	bool focus();
	[[nodiscard]] bool focused() const;
	[[nodiscard]] rpl::producer<bool> focusedValue() const;
	[[nodiscard]] rpl::producer<bool> tabbedPanelShownValue() const;
	[[nodiscard]] rpl::producer<> cancelRequests() const;
	[[nodiscard]] rpl::producer<Api::SendOptions> sendRequests() const;
	[[nodiscard]] rpl::producer<VoiceToSend> sendVoiceRequests() const;
	[[nodiscard]] rpl::producer<QString> sendCommandRequests() const;
	[[nodiscard]] rpl::producer<MessageToEdit> editRequests() const;
	[[nodiscard]] rpl::producer<std::optional<bool>> attachRequests() const;
	[[nodiscard]] rpl::producer<FileChosen> fileChosen() const;
	[[nodiscard]] rpl::producer<PhotoChosen> photoChosen() const;
	[[nodiscard]] rpl::producer<FullReplyTo> jumpToItemRequests() const;
	[[nodiscard]] rpl::producer<InlineChosen> inlineResultChosen() const;
	[[nodiscard]] rpl::producer<SendActionUpdate> sendActionUpdates() const;
	[[nodiscard]] rpl::producer<not_null<QEvent*>> viewportEvents() const;
	[[nodiscard]] rpl::producer<> likeToggled() const;
	[[nodiscard]] auto scrollKeyEvents() const
	-> rpl::producer<not_null<QKeyEvent*>>;
	[[nodiscard]] auto editLastMessageRequests() const
	-> rpl::producer<not_null<QKeyEvent*>>;
	[[nodiscard]] auto replyNextRequests() const
	-> rpl::producer<ReplyNextRequest>;
	[[nodiscard]] rpl::producer<> focusRequests() const;
	[[nodiscard]] rpl::producer<> showScheduledRequests() const;

	using MimeDataHook = Fn<bool(
		not_null<const QMimeData*> data,
		Ui::InputField::MimeAction action)>;
	void setMimeDataHook(MimeDataHook hook);
	bool confirmMediaEdit(Ui::PreparedList &list);

	bool pushTabbedSelectorToThirdSection(
		not_null<Data::Thread*> thread,
		const Window::SectionShow &params);
	bool returnTabbedSelector();

	[[nodiscard]] bool isEditingMessage() const;
	[[nodiscard]] bool readyToForward() const;
	[[nodiscard]] const HistoryItemsList &forwardItems() const;
	[[nodiscard]] FullReplyTo replyingToMessage() const;

	[[nodiscard]] bool preventsClose(Fn<void()> &&continueCallback) const;

	void showForGrab();
	void showStarted();
	void showFinished();
	void raisePanels();

	void editMessage(FullMsgId id, const TextSelection &selection);
	void cancelEditMessage();
	void maybeCancelEditMessage(); // Confirm if changed and cancel.

	void replyToMessage(FullReplyTo id);
	void cancelReplyMessage();

	void updateForwarding();
	void cancelForward();

	bool handleCancelRequest();
	void tryProcessKeyInput(not_null<QKeyEvent*> e);

	[[nodiscard]] TextWithTags getTextWithAppliedMarkdown() const;
	[[nodiscard]] Data::WebPageDraft webPageDraft() const;
	void setText(const TextWithTags &text);
	void clear();
	void hidePanelsAnimated();
	void clearListenState();

	void hide();
	void show();

	[[nodiscard]] rpl::producer<bool> lockShowStarts() const;
	[[nodiscard]] bool isLockPresent() const;
	[[nodiscard]] bool isTTLButtonShown() const;
	[[nodiscard]] bool isRecording() const;
	[[nodiscard]] bool isRecordingPressed() const;
	[[nodiscard]] rpl::producer<bool> recordingActiveValue() const;
	[[nodiscard]] rpl::producer<bool> hasSendTextValue() const;
	[[nodiscard]] rpl::producer<bool> fieldMenuShownValue() const;
	[[nodiscard]] not_null<Ui::RpWidget*> likeAnimationTarget() const;
	[[nodiscard]] int fieldCharacterCount() const;

	[[nodiscard]] TextWithEntities prepareTextForEditMsg() const;

	void applyCloudDraft();
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
	void initFieldAutocomplete();
	void initTabbedSelector();
	void initSendButton();
	void initSendAsButton(not_null<PeerData*> peer);
	void initWebpageProcess();
	void initForwardProcess();
	void initWriteRestriction();
	void initVoiceRecordBar();
	void initKeyHandler();
	void updateSubmitSettings();
	void updateSendButtonType();
	void updateMessagesTTLShown();
	bool updateSendAsButton();
	void updateAttachBotsMenu();
	void updateHeight();
	void updateWrappingVisibility();
	void updateControlsVisibility();
	void updateControlsGeometry(QSize size);
	bool updateReplaceMediaButton();
	void updateOuterGeometry(QRect rect);
	void paintBackground(QPainter &p, QRect full, QRect clip);

	[[nodiscard]] auto computeSendButtonType() const;
	[[nodiscard]] SendMenu::Details sendMenuDetails() const;
	[[nodiscard]] SendMenu::Details saveMenuDetails() const;
	[[nodiscard]] SendMenu::Details sendButtonMenuDetails() const;

	[[nodiscard]] auto sendContentRequests(
		SendRequestType requestType = SendRequestType::Text) const;

	void orderControls();
	void updateFieldPlaceholder();
	void updateSilentBroadcast();
	void editMessage(not_null<HistoryItem*> item);

	void escape();
	void fieldChanged();
	void toggleTabbedSelectorMode();
	void createTabbedPanel();
	void setTabbedPanel(std::unique_ptr<ChatHelpers::TabbedPanel> panel);

	bool showRecordButton() const;
	bool updateBotCommandShown();
	bool updateLikeShown();

	void cancelInlineBot();
	void clearInlineBot();
	void inlineBotChanged();

	bool hasSilentBroadcastToggle() const;

	// Look in the _field for the inline bot and query string.
	void updateInlineBotQuery();

	// Request to show results in the emoji panel.
	void applyInlineBotQuery(UserData *bot, const QString &query);

	[[nodiscard]] Data::DraftKey draftKey(
		DraftType type = DraftType::Normal) const;
	[[nodiscard]] Data::DraftKey draftKeyCurrent() const;
	void saveDraft(bool delayed = false);
	void saveDraftDelayed();
	void saveCloudDraft();

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

	void unregisterDraftSources();
	void registerDraftSource();
	void changeFocusedControl();

	void checkCharsLimitation();

	const style::ComposeControls &_st;
	const ChatHelpers::ComposeFeatures _features;
	const not_null<QWidget*> _parent;
	const not_null<QWidget*> _panelsParent;
	const std::shared_ptr<ChatHelpers::Show> _show;
	const not_null<Main::Session*> _session;

	Window::SessionController * const _regularWindow = nullptr;
	std::unique_ptr<ChatHelpers::TabbedSelector> _ownedSelector;
	const not_null<ChatHelpers::TabbedSelector*> _selector;
	rpl::event_stream<ChatHelpers::FileChosen> _stickerOrEmojiChosen;

	History *_history = nullptr;
	MsgId _topicRootId = 0;
	BusinessShortcutId _shortcutId = 0;
	Fn<bool()> _showSlowmodeError;
	Fn<Api::SendAction()> _sendActionFactory;
	rpl::variable<int> _slowmodeSecondsLeft;
	rpl::variable<bool> _sendDisabledBySlowmode;
	rpl::variable<bool> _liked;
	rpl::variable<Controls::WriteRestriction> _writeRestriction;
	rpl::variable<bool> _hidden;
	Mode _mode = Mode::Normal;

	const std::unique_ptr<Ui::RpWidget> _wrap;
	std::unique_ptr<Ui::RpWidget> _writeRestricted;
	rpl::event_stream<FullReplyTo> _jumpToItemRequests;

	std::optional<Ui::RoundRect> _backgroundRect;

	const std::shared_ptr<Ui::SendButton> _send;
	Ui::IconButton * const _like = nullptr;
	const not_null<Ui::IconButton*> _attachToggle;
	std::unique_ptr<Ui::IconButton> _replaceMedia;
	const not_null<Ui::EmojiButton*> _tabbedSelectorToggle;
	rpl::producer<QString> _fieldCustomPlaceholder;
	const not_null<Ui::InputField*> _field;
	Ui::IconButton * const _botCommandStart = nullptr;
	std::unique_ptr<Ui::SendAsButton> _sendAs;
	std::unique_ptr<Ui::SilentToggle> _silent;
	std::unique_ptr<Controls::TTLButton> _ttlInfo;
	base::unique_qptr<Controls::CharactersLimitLabel> _charsLimitation;
	base::unique_qptr<Ui::IconButton> _scheduled;

	std::unique_ptr<InlineBots::Layout::Widget> _inlineResults;
	std::unique_ptr<ChatHelpers::TabbedPanel> _tabbedPanel;
	std::unique_ptr<Ui::DropdownMenu> _attachBotsMenu;
	std::unique_ptr<ChatHelpers::FieldAutocomplete> _autocomplete;
	std::unique_ptr<Ui::Emoji::SuggestionsController> _emojiSuggestions;

	friend class FieldHeader;
	const std::unique_ptr<FieldHeader> _header;
	const std::unique_ptr<Controls::VoiceRecordBar> _voiceRecordBar;

	const Fn<SendMenu::Details()> _sendMenuDetails;
	const Fn<void(not_null<DocumentData*>)> _unavailableEmojiPasted;

	rpl::event_stream<Api::SendOptions> _sendCustomRequests;
	rpl::event_stream<> _cancelRequests;
	rpl::event_stream<FileChosen> _fileChosen;
	rpl::event_stream<PhotoChosen> _photoChosen;
	rpl::event_stream<InlineChosen> _inlineResultChosen;
	rpl::event_stream<SendActionUpdate> _sendActionUpdates;
	rpl::event_stream<QString> _sendCommandRequests;
	rpl::event_stream<not_null<QKeyEvent*>> _scrollKeyEvents;
	rpl::event_stream<not_null<QKeyEvent*>> _editLastMessageRequests;
	rpl::event_stream<std::optional<bool>> _attachRequests;
	rpl::event_stream<> _likeToggled;
	rpl::event_stream<ReplyNextRequest> _replyNextRequests;
	rpl::event_stream<> _focusRequests;
	rpl::event_stream<> _showScheduledRequests;
	rpl::variable<bool> _recording;
	rpl::variable<bool> _hasSendText;

	TextUpdateEvents _textUpdateEvents = TextUpdateEvents()
		| TextUpdateEvent::SaveDraft
		| TextUpdateEvent::SendTyping;
	Dialogs::EntryState _currentDialogsEntryState;

	crl::time _saveDraftStart = 0;
	bool _saveDraftText = false;
	base::Timer _saveDraftTimer;
	base::Timer _saveCloudDraftTimer;

	UserData *_inlineBot = nullptr;
	QString _inlineBotUsername;
	bool _inlineLookingUpBot = false;
	mtpRequestId _inlineBotResolveRequestId = 0;
	bool _isInlineBot = false;
	bool _botCommandShown = false;
	bool _likeShown = false;

	FullMsgId _editingId;
	std::shared_ptr<Data::PhotoMedia> _photoEditMedia;
	bool _canReplaceMedia = false;

	std::unique_ptr<Controls::WebpageProcessor> _preview;

	rpl::lifetime _historyLifetime;
	rpl::lifetime _uploaderSubscriptions;

};

[[nodiscard]] rpl::producer<int> SlowmodeSecondsLeft(
	not_null<PeerData*> peer);
[[nodiscard]] rpl::producer<bool> SendDisabledBySlowmode(
	not_null<PeerData*> peer);

void ShowPhotoEditSpoilerMenu(
	not_null<Ui::RpWidget*> parent,
	not_null<HistoryItem*> item,
	const std::optional<bool> &override,
	Fn<void(bool)> callback);

[[nodiscard]] Image *MediaPreviewWithOverriddenSpoiler(
	not_null<HistoryItem*> item,
	bool spoiler);

} // namespace HistoryView
