/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"
#include "base/object_ptr.h"
#include "base/observer.h"
#include "base/weak_ptr.h"
#include "base/timer.h"
#include "data/data_chat_participant_status.h"
#include "dialogs/dialogs_key.h"
#include "ui/layers/layer_widget.h"
#include "ui/layers/show.h"
#include "settings/settings_type.h"
#include "window/window_adaptive.h"
#include "mtproto/sender.h"

class PhotoData;
class MainWidget;
class MainWindow;

namespace Adaptive {
enum class WindowLayout;
} // namespace Adaptive

namespace ChatHelpers {
class TabbedSelector;
class EmojiInteractions;
} // namespace ChatHelpers

namespace Main {
class Session;
} // namespace Main

namespace InlineBots {
class AttachWebView;
enum class PeerType : uint8;
using PeerTypes = base::flags<PeerType>;
} // namespace InlineBots

namespace Calls {
struct StartGroupCallArgs;
} // namespace Calls

namespace Passport {
struct FormRequest;
class FormController;
} // namespace Passport

namespace Ui {
class LayerWidget;
enum class ReportReason;
class ChatStyle;
class ChatTheme;
struct ChatThemeKey;
struct ChatPaintContext;
struct ChatThemeBackground;
struct ChatThemeBackgroundData;
class MessageSendingAnimationController;
} // namespace Ui

namespace Data {
struct CloudTheme;
enum class CloudThemeType;
} // namespace Data

namespace HistoryView::Reactions {
class CachedIconFactory;
} // namespace HistoryView::Reactions

namespace Window {

class MainWindow;
class SectionMemento;
class Controller;
class FiltersMenu;

enum class GifPauseReason {
	Any           = 0,
	InlineResults = (1 << 0),
	SavedGifs     = (1 << 1),
	Layer         = (1 << 2),
	RoundPlaying  = (1 << 3),
	MediaPreview  = (1 << 4),
};
using GifPauseReasons = base::flags<GifPauseReason>;
inline constexpr bool is_flag_type(GifPauseReason) { return true; };

enum class ResolveType {
	Default,
	BotStart,
	AddToGroup,
	AddToChannel,
	ShareGame,
	Mention,
};

struct PeerThemeOverride {
	PeerData *peer = nullptr;
	std::shared_ptr<Ui::ChatTheme> theme;
};
bool operator==(const PeerThemeOverride &a, const PeerThemeOverride &b);
bool operator!=(const PeerThemeOverride &a, const PeerThemeOverride &b);

class DateClickHandler : public ClickHandler {
public:
	DateClickHandler(Dialogs::Key chat, QDate date);

	void setDate(QDate date);
	void onClick(ClickContext context) const override;

private:
	Dialogs::Key _chat;
	QDate _date;

};

struct SectionShow {
	enum class Way {
		Forward,
		Backward,
		ClearStack,
	};

	struct OriginMessage {
		FullMsgId id;
	};
	using Origin = std::variant<v::null_t, OriginMessage>;

	SectionShow(
		Way way = Way::Forward,
		anim::type animated = anim::type::normal,
		anim::activation activation = anim::activation::normal)
	: way(way)
	, animated(animated)
	, activation(activation) {
	}
	SectionShow(
		anim::type animated,
		anim::activation activation = anim::activation::normal)
	: animated(animated)
	, activation(activation) {
	}

	SectionShow withWay(Way newWay) const {
		return SectionShow(newWay, animated, activation);
	}
	SectionShow withThirdColumn() const {
		auto copy = *this;
		copy.thirdColumn = true;
		return copy;
	}

	Way way = Way::Forward;
	anim::type animated = anim::type::normal;
	anim::activation activation = anim::activation::normal;
	bool thirdColumn = false;
	Origin origin;

};

class SessionController;

class SessionNavigation : public base::has_weak_ptr {
public:
	explicit SessionNavigation(not_null<Main::Session*> session);
	virtual ~SessionNavigation();

	Main::Session &session() const;

	virtual void showSection(
		std::shared_ptr<SectionMemento> memento,
		const SectionShow &params = SectionShow()) = 0;
	virtual void showBackFromStack(
		const SectionShow &params = SectionShow()) = 0;
	virtual not_null<SessionController*> parentController() = 0;

	struct CommentId {
		MsgId id = 0;
	};
	struct ThreadId {
		MsgId id = 0;
	};
	using RepliesByLinkInfo = std::variant<v::null_t, CommentId, ThreadId>;
	struct PeerByLinkInfo {
		std::variant<QString, ChannelId> usernameOrId;
		QString phone;
		MsgId messageId = ShowAtUnreadMsgId;
		RepliesByLinkInfo repliesInfo;
		ResolveType resolveType = ResolveType::Default;
		QString startToken;
		ChatAdminRights startAdminRights;
		bool startAutoSubmit = false;
		QString attachBotUsername;
		std::optional<QString> attachBotToggleCommand;
		InlineBots::PeerTypes attachBotChooseTypes;
		std::optional<QString> voicechatHash;
		FullMsgId clickFromMessageId;
	};
	void showPeerByLink(const PeerByLinkInfo &info);

	void showRepliesForMessage(
		not_null<History*> history,
		MsgId rootId,
		MsgId commentId = 0,
		const SectionShow &params = SectionShow());

	void showPeerInfo(
		PeerId peerId,
		const SectionShow &params = SectionShow());
	void showPeerInfo(
		not_null<PeerData*> peer,
		const SectionShow &params = SectionShow());
	void showPeerInfo(
		not_null<History*> history,
		const SectionShow &params = SectionShow());

	virtual void showPeerHistory(
		PeerId peerId,
		const SectionShow &params = SectionShow::Way::ClearStack,
		MsgId msgId = ShowAtUnreadMsgId) = 0;
	void showPeerHistory(
		not_null<PeerData*> peer,
		const SectionShow &params = SectionShow::Way::ClearStack,
		MsgId msgId = ShowAtUnreadMsgId);
	void showPeerHistory(
		not_null<History*> history,
		const SectionShow &params = SectionShow::Way::ClearStack,
		MsgId msgId = ShowAtUnreadMsgId);

	void clearSectionStack(
			const SectionShow &params = SectionShow::Way::ClearStack) {
		showPeerHistory(
			PeerId(0),
			params,
			ShowAtUnreadMsgId);
	}

	void showSettings(
		Settings::Type type,
		const SectionShow &params = SectionShow());
	void showSettings(const SectionShow &params = SectionShow());

	void showPollResults(
		not_null<PollData*> poll,
		FullMsgId contextId,
		const SectionShow &params = SectionShow());

private:
	void resolvePhone(
		const QString &phone,
		Fn<void(not_null<PeerData*>)> done);
	void resolveUsername(
		const QString &username,
		Fn<void(not_null<PeerData*>)> done);
	void resolveChannelById(
		ChannelId channelId,
		Fn<void(not_null<ChannelData*>)> done);

	void resolveDone(
		const MTPcontacts_ResolvedPeer &result,
		Fn<void(not_null<PeerData*>)> done);

	void showPeerByLinkResolved(
		not_null<PeerData*> peer,
		const PeerByLinkInfo &info);
	void joinVoiceChatFromLink(
		not_null<PeerData*> peer,
		const PeerByLinkInfo &info);

	const not_null<Main::Session*> _session;

	MTP::Sender _api;

	mtpRequestId _resolveRequestId = 0;

	History *_showingRepliesHistory = nullptr;
	MsgId _showingRepliesRootId = 0;
	mtpRequestId _showingRepliesRequestId = 0;

};

class SessionController : public SessionNavigation {
public:
	SessionController(
		not_null<Main::Session*> session,
		not_null<Controller*> window);
	~SessionController();

	[[nodiscard]] Controller &window() const {
		return *_window;
	}
	[[nodiscard]] PeerData *singlePeer() const;
	[[nodiscard]] bool isPrimary() const;
	[[nodiscard]] not_null<::MainWindow*> widget() const;
	[[nodiscard]] not_null<MainWidget*> content() const;
	[[nodiscard]] Adaptive &adaptive() const;
	[[nodiscard]] ChatHelpers::EmojiInteractions &emojiInteractions() const {
		return *_emojiInteractions;
	}

	// We need access to this from MainWidget::MainWidget, where
	// we can't call content() yet.
	void setSelectingPeer(bool selecting) {
		_selectingPeer = selecting;
	}
	[[nodiscard]] bool selectingPeer() const {
		return _selectingPeer;
	}

	void setConnectingBottomSkip(int skip);
	rpl::producer<int> connectingBottomSkipValue() const;

	QPointer<Ui::BoxContent> show(
		object_ptr<Ui::BoxContent> content,
		Ui::LayerOptions options = Ui::LayerOption::KeepOther,
		anim::type animated = anim::type::normal);

	void hideLayer(anim::type animated = anim::type::normal);

	[[nodiscard]] auto sendingAnimation() const
	-> Ui::MessageSendingAnimationController &;
	[[nodiscard]] auto tabbedSelector() const
	-> not_null<ChatHelpers::TabbedSelector*>;
	void takeTabbedSelectorOwnershipFrom(not_null<QWidget*> parent);
	[[nodiscard]] bool hasTabbedSelectorOwnership() const;

	// This is needed for History TopBar updating when searchInChat
	// is changed in the Dialogs::Widget of the current window.
	rpl::variable<Dialogs::Key> searchInChat;
	bool uniqueChatsInSearchResults() const;
	void openFolder(not_null<Data::Folder*> folder);
	void closeFolder();
	const rpl::variable<Data::Folder*> &openedFolder() const;

	void setActiveChatEntry(Dialogs::RowDescriptor row);
	void setActiveChatEntry(Dialogs::Key key);
	Dialogs::RowDescriptor activeChatEntryCurrent() const;
	Dialogs::Key activeChatCurrent() const;
	rpl::producer<Dialogs::RowDescriptor> activeChatEntryChanges() const;
	rpl::producer<Dialogs::Key> activeChatChanges() const;
	rpl::producer<Dialogs::RowDescriptor> activeChatEntryValue() const;
	rpl::producer<Dialogs::Key> activeChatValue() const;
	bool jumpToChatListEntry(Dialogs::RowDescriptor row);

	[[nodiscard]] Dialogs::RowDescriptor resolveChatNext(
		Dialogs::RowDescriptor from = {}) const;
	[[nodiscard]] Dialogs::RowDescriptor resolveChatPrevious(
		Dialogs::RowDescriptor from = {}) const;

	void showEditPeerBox(PeerData *peer);

	void enableGifPauseReason(GifPauseReason reason);
	void disableGifPauseReason(GifPauseReason reason);
	rpl::producer<> gifPauseLevelChanged() const {
		return _gifPauseLevelChanged.events();
	}
	bool isGifPausedAtLeastFor(GifPauseReason reason) const;
	void floatPlayerAreaUpdated();

	struct ColumnLayout {
		int bodyWidth = 0;
		int dialogsWidth = 0;
		int chatWidth = 0;
		int thirdWidth = 0;
		Adaptive::WindowLayout windowLayout = Adaptive::WindowLayout();
	};
	[[nodiscard]] ColumnLayout computeColumnLayout() const;
	int dialogsSmallColumnWidth() const;
	bool forceWideDialogs() const;
	void updateColumnLayout();
	bool canShowThirdSection() const;
	bool canShowThirdSectionWithoutResize() const;
	bool takeThirdSectionFromLayer();
	void resizeForThirdSection();
	void closeThirdSection();

	[[nodiscard]] bool canShowSeparateWindow(not_null<PeerData*> peer) const;
	void showPeer(not_null<PeerData*> peer, MsgId msgId = ShowAtUnreadMsgId);

	void startOrJoinGroupCall(
		not_null<PeerData*> peer,
		const Calls::StartGroupCallArgs &args);

	void showSection(
		std::shared_ptr<SectionMemento> memento,
		const SectionShow &params = SectionShow()) override;
	void showBackFromStack(
		const SectionShow &params = SectionShow()) override;

	using SessionNavigation::showPeerHistory;
	void showPeerHistory(
		PeerId peerId,
		const SectionShow &params = SectionShow::Way::ClearStack,
		MsgId msgId = ShowAtUnreadMsgId) override;

	void showPeerHistoryAtItem(not_null<const HistoryItem*> item);
	void cancelUploadLayer(not_null<HistoryItem*> item);

	void showLayer(
		std::unique_ptr<Ui::LayerWidget> &&layer,
		Ui::LayerOptions options,
		anim::type animated = anim::type::normal);

	void showSpecialLayer(
		object_ptr<Ui::LayerWidget> &&layer,
		anim::type animated = anim::type::normal);
	void hideSpecialLayer(
			anim::type animated = anim::type::normal) {
		showSpecialLayer(nullptr, animated);
	}
	void removeLayerBlackout();

	void showCalendar(
		Dialogs::Key chat,
		QDate requestedDate);

	void showAddContact();
	void showNewGroup();
	void showNewChannel();

	void showPassportForm(const Passport::FormRequest &request);
	void clearPassportForm();

	void openPhoto(not_null<PhotoData*> photo, FullMsgId contextId);
	void openPhoto(not_null<PhotoData*> photo, not_null<PeerData*> peer);
	void openDocument(
		not_null<DocumentData*> document,
		FullMsgId contextId,
		bool showInMediaView = false);

	void showChooseReportMessages(
		not_null<PeerData*> peer,
		Ui::ReportReason reason,
		Fn<void(MessageIdsList)> done);
	void clearChooseReportMessages();

	void toggleChooseChatTheme(not_null<PeerData*> peer);

	base::Variable<bool> &dialogsListFocused() {
		return _dialogsListFocused;
	}
	const base::Variable<bool> &dialogsListFocused() const {
		return _dialogsListFocused;
	}
	base::Variable<bool> &dialogsListDisplayForced() {
		return _dialogsListDisplayForced;
	}
	const base::Variable<bool> &dialogsListDisplayForced() const {
		return _dialogsListDisplayForced;
	}

	not_null<SessionController*> parentController() override {
		return this;
	}

	[[nodiscard]] int filtersWidth() const;
	[[nodiscard]] rpl::producer<FilterId> activeChatsFilter() const;
	[[nodiscard]] FilterId activeChatsFilterCurrent() const;
	void setActiveChatsFilter(FilterId id);

	void toggleFiltersMenu(bool enabled);
	[[nodiscard]] rpl::producer<> filtersMenuChanged() const;

	[[nodiscard]] auto defaultChatTheme() const
	-> const std::shared_ptr<Ui::ChatTheme> & {
		return _defaultChatTheme;
	}
	[[nodiscard]] auto cachedChatThemeValue(
		const Data::CloudTheme &data,
		Data::CloudThemeType type)
	-> rpl::producer<std::shared_ptr<Ui::ChatTheme>>;
	void setChatStyleTheme(const std::shared_ptr<Ui::ChatTheme> &theme);
	void clearCachedChatThemes();
	void pushLastUsedChatTheme(const std::shared_ptr<Ui::ChatTheme> &theme);
	[[nodiscard]] not_null<Ui::ChatTheme*> currentChatTheme() const;

	void overridePeerTheme(
		not_null<PeerData*> peer,
		std::shared_ptr<Ui::ChatTheme> theme);
	void clearPeerThemeOverride(not_null<PeerData*> peer);
	[[nodiscard]] auto peerThemeOverrideValue() const
		-> rpl::producer<PeerThemeOverride> {
		return _peerThemeOverride.value();
	}

	struct PaintContextArgs {
		not_null<Ui::ChatTheme*> theme;
		int visibleAreaTop = 0;
		int visibleAreaTopGlobal = 0;
		int visibleAreaWidth = 0;
		QRect clip;
	};
	[[nodiscard]] Ui::ChatPaintContext preparePaintContext(
		PaintContextArgs &&args);
	[[nodiscard]] not_null<const Ui::ChatStyle*> chatStyle() const {
		return _chatStyle.get();
	}

	[[nodiscard]] auto cachedReactionIconFactory() const
	-> HistoryView::Reactions::CachedIconFactory & {
		return *_cachedReactionIconFactory;
	}

	void setPremiumRef(const QString &ref);
	[[nodiscard]] QString premiumRef() const;

	rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	struct CachedTheme;

	void init();
	void initSupportMode();
	void refreshFiltersMenu();
	void checkOpenedFilter();
	void suggestArchiveAndMute();
	void activateFirstChatsFilter();

	int minimalThreeColumnWidth() const;
	int countDialogsWidthFromRatio(int bodyWidth) const;
	int countThirdColumnWidthFromRatio(int bodyWidth) const;
	struct ShrinkResult {
		int dialogsWidth;
		int thirdWidth;
	};
	ShrinkResult shrinkDialogsAndThirdColumns(
		int dialogsWidth,
		int thirdWidth,
		int bodyWidth) const;

	void pushToChatEntryHistory(Dialogs::RowDescriptor row);
	bool chatEntryHistoryMove(int steps);
	void resetFakeUnreadWhileOpened();

	void checkInvitePeek();
	void setupPremiumToast();

	void pushDefaultChatBackground();
	void cacheChatTheme(
		const Data::CloudTheme &data,
		Data::CloudThemeType type);
	void cacheChatThemeDone(std::shared_ptr<Ui::ChatTheme> result);
	void updateCustomThemeBackground(CachedTheme &theme);
	[[nodiscard]] Ui::ChatThemeBackgroundData backgroundData(
		CachedTheme &theme,
		bool generateGradient = true) const;

	const not_null<Controller*> _window;
	const std::unique_ptr<ChatHelpers::EmojiInteractions> _emojiInteractions;
	const bool _isPrimary = false;

	using SendingAnimation = Ui::MessageSendingAnimationController;
	const std::unique_ptr<SendingAnimation> _sendingAnimation;

	std::unique_ptr<Passport::FormController> _passportForm;
	std::unique_ptr<FiltersMenu> _filters;

	GifPauseReasons _gifPauseReasons = 0;
	rpl::event_stream<> _gifPauseLevelChanged;

	// Depends on _gifPause*.
	const std::unique_ptr<ChatHelpers::TabbedSelector> _tabbedSelector;

	rpl::variable<Dialogs::RowDescriptor> _activeChatEntry;
	base::Variable<bool> _dialogsListFocused = { false };
	base::Variable<bool> _dialogsListDisplayForced = { false };
	std::deque<Dialogs::RowDescriptor> _chatEntryHistory;
	int _chatEntryHistoryPosition = -1;
	bool _filtersActivated = false;
	bool _selectingPeer = false;

	base::Timer _invitePeekTimer;

	rpl::variable<FilterId> _activeChatsFilter;

	rpl::variable<int> _connectingBottomSkip;

	PeerData *_showEditPeer = nullptr;
	rpl::variable<Data::Folder*> _openedFolder;

	rpl::event_stream<> _filtersMenuChanged;

	std::shared_ptr<Ui::ChatTheme> _defaultChatTheme;
	base::flat_map<Ui::ChatThemeKey, CachedTheme> _customChatThemes;
	rpl::event_stream<std::shared_ptr<Ui::ChatTheme>> _cachedThemesStream;
	const std::unique_ptr<Ui::ChatStyle> _chatStyle;
	std::weak_ptr<Ui::ChatTheme> _chatStyleTheme;
	std::deque<std::shared_ptr<Ui::ChatTheme>> _lastUsedCustomChatThemes;
	rpl::variable<PeerThemeOverride> _peerThemeOverride;

	using ReactionIconFactory = HistoryView::Reactions::CachedIconFactory;
	std::unique_ptr<ReactionIconFactory> _cachedReactionIconFactory;

	QString _premiumRef;

	rpl::lifetime _lifetime;

};

void ActivateWindow(not_null<SessionController*> controller);

class Show : public Ui::Show {
public:
	explicit Show(not_null<SessionNavigation*> navigation);
	explicit Show(Controller *window);
	~Show();
	void showBox(
		object_ptr<Ui::BoxContent> content,
		Ui::LayerOptions options = Ui::LayerOption::KeepOther) const override;
	void hideLayer() const override;
	[[nodiscard]] not_null<QWidget*> toastParent() const override;
	[[nodiscard]] bool valid() const override;
	operator bool() const override;

private:
	const base::weak_ptr<Controller> _window;

};

} // namespace Window
