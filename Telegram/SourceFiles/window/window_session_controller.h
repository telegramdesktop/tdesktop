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
#include "dialogs/dialogs_key.h"

class MainWidget;
class MainWindow;
class HistoryMessage;
class HistoryService;

namespace ChatHelpers {
class TabbedSelector;
} // namespace ChatHelpers

namespace Main {
class Session;
} // namespace Main

namespace Settings {
enum class Type;
} // namespace Settings

namespace Media {
namespace Player {
class FloatController;
class FloatDelegate;
} // namespace Player
} // namespace Media

namespace Passport {
struct FormRequest;
class FormController;
} // namespace Passport

namespace Window {

class LayerWidget;
class MainWindow;
class SectionMemento;

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

};

class SessionController;

class SessionNavigation {
public:
	explicit SessionNavigation(not_null<Main::Session*> session);

	Main::Session &session() const;

	virtual void showSection(
		SectionMemento &&memento,
		const SectionShow &params = SectionShow()) = 0;
	virtual void showBackFromStack(
		const SectionShow &params = SectionShow()) = 0;
	virtual not_null<SessionController*> parentController() = 0;

	void showPeerInfo(
		PeerId peerId,
		const SectionShow &params = SectionShow());
	void showPeerInfo(
		not_null<PeerData*> peer,
		const SectionShow &params = SectionShow());
	void showPeerInfo(
		not_null<History*> history,
		const SectionShow &params = SectionShow());

	void showSettings(
		Settings::Type type,
		const SectionShow &params = SectionShow());
	void showSettings(const SectionShow &params = SectionShow());

	virtual ~SessionNavigation() = default;

private:
	const not_null<Main::Session*> _session;

};

class SessionController
	: public SessionNavigation
	, private base::Subscriber {
public:
	SessionController(
		not_null<Main::Session*> session,
		not_null<::MainWindow*> window);

	[[nodiscard]] not_null<::MainWindow*> window() const {
		return _window;
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
	base::Observable<void> &floatPlayerAreaUpdated() {
		return _floatPlayerAreaUpdated;
	}

	struct ColumnLayout {
		int bodyWidth;
		int dialogsWidth;
		int chatWidth;
		int thirdWidth;
		Adaptive::WindowLayout windowLayout;
	};
	ColumnLayout computeColumnLayout() const;
	int dialogsSmallColumnWidth() const;
	bool forceWideDialogs() const;
	void updateColumnLayout();
	bool canShowThirdSection() const;
	bool canShowThirdSectionWithoutResize() const;
	bool takeThirdSectionFromLayer();
	void resizeForThirdSection();
	void closeThirdSection();

	void showSection(
		SectionMemento &&memento,
		const SectionShow &params = SectionShow()) override;
	void showBackFromStack(
		const SectionShow &params = SectionShow()) override;

	void showPeerHistory(
		PeerId peerId,
		const SectionShow &params = SectionShow::Way::ClearStack,
		MsgId msgId = ShowAtUnreadMsgId);
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

	void showSpecialLayer(
		object_ptr<LayerWidget> &&layer,
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

	void setDefaultFloatPlayerDelegate(
		not_null<Media::Player::FloatDelegate*> delegate);
	void replaceFloatPlayerDelegate(
		not_null<Media::Player::FloatDelegate*> replacement);
	void restoreFloatPlayerDelegate(
		not_null<Media::Player::FloatDelegate*> replacement);
	rpl::producer<FullMsgId> floatPlayerClosed() const;

	rpl::lifetime &lifetime() {
		return _lifetime;
	}

	~SessionController();

private:
	void init();
	void initSupportMode();

	int minimalThreeColumnWidth() const;
	not_null<MainWidget*> chats() const;
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

	const not_null<::MainWindow*> _window;

	std::unique_ptr<Passport::FormController> _passportForm;

	GifPauseReasons _gifPauseReasons = 0;
	base::Observable<void> _gifPauseLevelChanged;
	base::Observable<void> _floatPlayerAreaUpdated;

	// Depends on _gifPause*.
	const std::unique_ptr<ChatHelpers::TabbedSelector> _tabbedSelector;

	rpl::variable<Dialogs::RowDescriptor> _activeChatEntry;
	base::Variable<bool> _dialogsListFocused = { false };
	base::Variable<bool> _dialogsListDisplayForced = { false };
	std::deque<Dialogs::RowDescriptor> _chatEntryHistory;
	int _chatEntryHistoryPosition = -1;

	std::unique_ptr<Media::Player::FloatController> _floatPlayers;
	Media::Player::FloatDelegate *_defaultFloatPlayerDelegate = nullptr;
	Media::Player::FloatDelegate *_replacementFloatPlayerDelegate = nullptr;

	PeerData *_showEditPeer = nullptr;
	rpl::variable<Data::Folder*> _openedFolder;

	rpl::lifetime _lifetime;

};

} // namespace Window
