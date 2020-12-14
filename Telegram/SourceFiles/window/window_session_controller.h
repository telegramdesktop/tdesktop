/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/variable.h>
#include "base/flags.h"
#include "base/observer.h"
#include "base/object_ptr.h"
#include "base/weak_ptr.h"
#include "base/timer.h"
#include "dialogs/dialogs_key.h"
#include "ui/effects/animation_value.h"

class PhotoData;
class MainWidget;
class MainWindow;
class HistoryMessage;
class HistoryService;

namespace Adaptive {
enum class WindowLayout;
} // namespace Adaptive

namespace ChatHelpers {
class TabbedSelector;
} // namespace ChatHelpers

namespace Main {
class Session;
} // namespace Main

namespace Settings {
enum class Type;
} // namespace Settings

namespace Passport {
struct FormRequest;
class FormController;
} // namespace Passport

namespace Ui {
class LayerWidget;
} // namespace Ui

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
		MsgId messageId = ShowAtUnreadMsgId;
		RepliesByLinkInfo repliesInfo;
		QString startToken;
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
	void resolveUsername(
		const QString &username,
		Fn<void(not_null<PeerData*>)> done);
	void resolveChannelById(
		ChannelId channelId,
		Fn<void(not_null<ChannelData*>)> done);

	void showPeerByLinkResolved(
		not_null<PeerData*> peer,
		const PeerByLinkInfo &info);

	const not_null<Main::Session*> _session;

	mtpRequestId _resolveRequestId = 0;

	History *_showingRepliesHistory = nullptr;
	MsgId _showingRepliesRootId = 0;
	mtpRequestId _showingRepliesRequestId = 0;


};

class SessionController : public SessionNavigation, private base::Subscriber {
public:
	SessionController(
		not_null<Main::Session*> session,
		not_null<Controller*> window);
	~SessionController();

	[[nodiscard]] Controller &window() const {
		return *_window;
	}
	[[nodiscard]] not_null<::MainWindow*> widget() const;
	[[nodiscard]] not_null<MainWidget*> content() const;

	// We need access to this from MainWidget::MainWidget, where
	// we can't call content() yet.
	void setSelectingPeer(bool selecting) {
		_selectingPeer = selecting;
	}
	[[nodiscard]] bool selectingPeer() const {
		return _selectingPeer;
	}

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
	void showEditPeerBox(PeerData *peer);

	void enableGifPauseReason(GifPauseReason reason);
	void disableGifPauseReason(GifPauseReason reason);
	base::Observable<void> &gifPauseLevelChanged() {
		return _gifPauseLevelChanged;
	}
	bool isGifPausedAtLeastFor(GifPauseReason reason) const;
	void floatPlayerAreaUpdated();

	struct ColumnLayout {
		int bodyWidth;
		int dialogsWidth;
		int chatWidth;
		int thirdWidth;
		Adaptive::WindowLayout windowLayout;
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

	void startOrJoinGroupCall(
		not_null<PeerData*> peer,
		bool confirmedLeaveOther = false);

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

	void showSpecialLayer(
		object_ptr<Ui::LayerWidget> &&layer,
		anim::type animated = anim::type::normal);
	void hideSpecialLayer(
			anim::type animated = anim::type::normal) {
		showSpecialLayer(nullptr, animated);
	}
	void removeLayerBlackout();

	void showJumpToDate(
		Dialogs::Key chat,
		QDate requestedDate);

	void showPassportForm(const Passport::FormRequest &request);
	void clearPassportForm();

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

	rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	void init();
	void initSupportMode();
	void refreshFiltersMenu();
	void checkOpenedFilter();
	void suggestArchiveAndMute();

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

	const not_null<Controller*> _window;

	std::unique_ptr<Passport::FormController> _passportForm;
	std::unique_ptr<FiltersMenu> _filters;

	GifPauseReasons _gifPauseReasons = 0;
	base::Observable<void> _gifPauseLevelChanged;

	// Depends on _gifPause*.
	const std::unique_ptr<ChatHelpers::TabbedSelector> _tabbedSelector;

	rpl::variable<Dialogs::RowDescriptor> _activeChatEntry;
	base::Variable<bool> _dialogsListFocused = { false };
	base::Variable<bool> _dialogsListDisplayForced = { false };
	std::deque<Dialogs::RowDescriptor> _chatEntryHistory;
	int _chatEntryHistoryPosition = -1;
	bool _selectingPeer = false;

	base::Timer _invitePeekTimer;

	rpl::variable<FilterId> _activeChatsFilter;

	PeerData *_showEditPeer = nullptr;
	rpl::variable<Data::Folder*> _openedFolder;

	rpl::event_stream<> _filtersMenuChanged;

	rpl::lifetime _lifetime;

};

void ActivateWindow(not_null<SessionController*> controller);

} // namespace Window
