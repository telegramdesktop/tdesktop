/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_changes.h"
#include "ui/rp_widget.h"

class History;
class PeerData;

namespace Data {
class ForumTopic;
class SavedSublist;
struct SendError;
} // namespace Data

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
class FlatButton;
class IconButton;
} // namespace Ui

namespace HistoryView {

enum class BottomControlsMode {
	History,
	Replies,
	Sublist,
};

enum class BottomControlsAction {
	Unblock,
	BotStart,
	JoinChannel,
	JoinGroup,
	MuteUnmute,
	Report,
};

struct BottomControlsDescriptor {
	Window::SessionController *controller = nullptr;
	History *history = nullptr;
	MsgId repliesRootId = 0;
	Data::ForumTopic *topic = nullptr;
	Data::SavedSublist *sublist = nullptr;
	BottomControlsMode mode = BottomControlsMode::History;
};

class BottomControls final : public Ui::RpWidget {
public:
	BottomControls(
		QWidget *parent,
		BottomControlsDescriptor descriptor);
	~BottomControls();

	void setCanSendMessages(bool value);
	void setTopic(Data::ForumTopic *topic);
	void applyPeerUpdate(Data::PeerUpdate::Flags flags);
	void updateControlsVisibility();
	void setInReportMode(bool value);
	void updateReportMessagesText(int selectedCount);

	[[nodiscard]] bool isButtonActive() const;
	[[nodiscard]] bool botStartShown() const;
	[[nodiscard]] bool canSendTexts() const;
	[[nodiscard]] bool hasOpenChatButton() const;
	[[nodiscard]] bool hasAboutHiddenAuthor() const;
	[[nodiscard]] int contentHeight() const;

	[[nodiscard]] rpl::producer<bool> isButtonActiveValue() const;
	[[nodiscard]] rpl::producer<int> contentHeightValue() const;

	[[nodiscard]] rpl::producer<BottomControlsAction> actionRequests() const;

private:
	void setupButtons();
	void setupGiftToChannelButton();
	void setupDirectMessageButton();
	void setupOverlayIconButton(
		not_null<Ui::IconButton*> button,
		bool alignRight,
		Fn<void()> refresh);
	void setupOpenChatButton();
	void setupAboutHiddenAuthor();
	void setupPeerUpdates();

	void refreshJoinChannelText();
	void refreshJoinGroupText();
	void refreshUnblockText();
	void refreshMuteUnmuteText();
	void refreshGiftToChannelShown();
	void refreshDirectMessageShown();

	void recomputeContentHeight();

	[[nodiscard]] bool isBotStart() const;
	[[nodiscard]] bool isBlocked() const;
	[[nodiscard]] bool isJoinChannel() const;
	[[nodiscard]] bool isJoinGroup() const;
	[[nodiscard]] bool isMuteUnmute() const;
	[[nodiscard]] bool isReportMessages() const;
	[[nodiscard]] bool isChoosingTheme() const;

	void resizeEvent(QResizeEvent *e) override;

	Window::SessionController * const _controller = nullptr;
	History * const _history = nullptr;
	PeerData * const _peer = nullptr;
	MsgId _repliesRootId = 0;
	Data::ForumTopic *_topic = nullptr;
	Data::SavedSublist * const _sublist = nullptr;
	const BottomControlsMode _mode = BottomControlsMode::History;

	std::unique_ptr<Ui::FlatButton> _unblock;
	std::unique_ptr<Ui::FlatButton> _botStart;
	std::unique_ptr<Ui::FlatButton> _joinChannel;
	std::unique_ptr<Ui::FlatButton> _joinGroup;
	std::unique_ptr<Ui::FlatButton> _muteUnmute;
	std::unique_ptr<Ui::FlatButton> _reportMessages;
	QPointer<Ui::IconButton> _giftToChannel;
	QPointer<Ui::IconButton> _directMessage;
	rpl::lifetime _directMessageLifetime;

	std::unique_ptr<Ui::FlatButton> _openChatButton;
	std::unique_ptr<Ui::RpWidget> _aboutHiddenAuthor;

	rpl::event_stream<BottomControlsAction> _actionRequests;

	rpl::variable<bool> _isButtonActive = false;
	rpl::variable<int> _contentHeight = 0;

	bool _canSendMessages = false;
	bool _canSendTexts = false;
	bool _inReportMode = false;

};

} // namespace HistoryView
