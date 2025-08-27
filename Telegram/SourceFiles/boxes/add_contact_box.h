/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/box_content.h"
#include "base/timer.h"
#include "mtproto/sender.h"

class PeerListBox;
struct RequestPeerQuery;

namespace Window {
class SessionNavigation;
} // namespace Window

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class FlatLabel;
class InputField;
class PhoneInput;
class UsernameInput;
class Checkbox;
template <typename Enum>
class RadioenumGroup;
template <typename Enum>
class Radioenum;
class LinkButton;
class UserpicButton;
class Show;
} // namespace Ui

enum class PeerFloodType {
	Send,
	InviteGroup,
	InviteChannel,
};

struct ForbiddenInvites;

[[nodiscard]] TextWithEntities PeerFloodErrorText(
	not_null<Main::Session*> session,
	PeerFloodType type);
void ShowAddParticipantsError(
	std::shared_ptr<Ui::Show> show,
	const QString &error,
	not_null<PeerData*> chat,
	const ForbiddenInvites &forbidden);
void ShowAddParticipantsError(
	std::shared_ptr<Ui::Show> show,
	const QString &error,
	not_null<PeerData*> chat,
	not_null<UserData*> user);

class AddContactBox : public Ui::BoxContent {
public:
	AddContactBox(QWidget*, not_null<Main::Session*> session);
	AddContactBox(
		QWidget*,
		not_null<Main::Session*> session,
		QString fname,
		QString lname,
		QString phone);

protected:
	void prepare() override;

	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void setInnerFocus() override;

private:
	void submit();
	void retry();
	void save();
	void updateButtons();

	const not_null<Main::Session*> _session;

	object_ptr<Ui::InputField> _first;
	object_ptr<Ui::InputField> _last;
	object_ptr<Ui::PhoneInput> _phone;

	bool _retrying = false;
	bool _invertOrder = false;

	uint64 _contactId = 0;

	mtpRequestId _addRequest = 0;
	QString _sentName;

};

class GroupInfoBox : public Ui::BoxContent {
public:
	enum class Type {
		Group,
		Channel,
		Megagroup,
		Forum,
	};
	GroupInfoBox(
		QWidget*,
		not_null<Window::SessionNavigation*> navigation,
		Type type,
		const QString &title = QString(),
		Fn<void(not_null<ChannelData*>)> channelDone = nullptr);
	GroupInfoBox(
		QWidget*,
		not_null<Window::SessionNavigation*> navigation,
		not_null<UserData*> bot,
		RequestPeerQuery query,
		Fn<void(not_null<PeerData*>)> done);

protected:
	void prepare() override;
	void setInnerFocus() override;

	void resizeEvent(QResizeEvent *e) override;

private:
	void createChannel(const QString &title, const QString &description);
	void createGroup(
		base::weak_qptr<Ui::BoxContent> selectUsersBox,
		const QString &title,
		const std::vector<not_null<PeerData*>> &users);
	void submitName();
	void submit();
	void checkInviteLink();
	void channelReady();

	void descriptionResized();
	void updateMaxHeight();

	[[nodiscard]] TimeId ttlPeriod() const;

	const not_null<Window::SessionNavigation*> _navigation;
	MTP::Sender _api;

	Type _type = Type::Group;
	QString _initialTitle;
	bool _mustBePublic = false;
	UserData *_canAddBot = nullptr;
	Fn<void(not_null<PeerData*>)> _done;

	object_ptr<Ui::UserpicButton> _photo = { nullptr };
	object_ptr<Ui::InputField> _title = { nullptr };
	object_ptr<Ui::InputField> _description = { nullptr };

	// group / channel creation
	mtpRequestId _creationRequestId = 0;
	bool _creatingInviteLink = false;
	ChannelData *_createdChannel = nullptr;
	TimeId _ttlPeriod = 0;
	bool _ttlPeriodOverridden = false;

};

class SetupChannelBox final : public Ui::BoxContent {
public:
	SetupChannelBox(
		QWidget*,
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChannelData*> channel,
		bool mustBePublic,
		Fn<void(not_null<PeerData*>)> done);

	void setInnerFocus() override;

protected:
	void prepare() override;

	void keyPressEvent(QKeyEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	enum class Privacy {
		Public,
		Private,
	};
	enum class UsernameResult {
		Ok,
		Invalid,
		Occupied,
		ChatsTooMuch,
		NA,
		Unknown,
	};
	[[nodiscard]] UsernameResult parseError(const QString &error);

	void privacyChanged(Privacy value);
	void updateSelected(const QPoint &cursorGlobalPosition);
	void handleChange();
	void check();
	void save();

	void updateFail(UsernameResult result);

	void mustBePublicFailed();
	void checkFail(UsernameResult result);
	void firstCheckFail(UsernameResult result);

	void updateMaxHeight();

	void showRevokePublicLinkBoxForEdit();

	const not_null<Window::SessionNavigation*> _navigation;
	const not_null<ChannelData*> _channel;
	MTP::Sender _api;

	bool _creatingInviteLink = false;
	bool _mustBePublic = false;
	Fn<void(not_null<PeerData*>)> _done;

	std::shared_ptr<Ui::RadioenumGroup<Privacy>> _privacyGroup;
	object_ptr<Ui::Radioenum<Privacy>> _public;
	object_ptr<Ui::Radioenum<Privacy>> _private;
	int32 _aboutPublicWidth, _aboutPublicHeight;
	Ui::Text::String _aboutPublic, _aboutPrivate;

	object_ptr<Ui::UsernameInput> _link;

	QRect _invitationLink;
	bool _linkOver = false;
	bool _tooMuchUsernames = false;

	mtpRequestId _saveRequestId = 0;
	mtpRequestId _checkRequestId = 0;
	QString _sentUsername, _checkUsername, _errorText, _goodText;

	base::Timer _checkTimer;

};

class EditNameBox : public Ui::BoxContent {
public:
	EditNameBox(QWidget*, not_null<UserData*> user);

protected:
	void setInnerFocus() override;
	void prepare() override;

	void resizeEvent(QResizeEvent *e) override;

private:
	void submit();
	void save();
	void saveSelfFail(const QString &error);

	const not_null<UserData*> _user;
	MTP::Sender _api;

	object_ptr<Ui::InputField> _first;
	object_ptr<Ui::InputField> _last;

	bool _invertOrder = false;

	mtpRequestId _requestId = 0;
	QString _sentName;

};
