/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

template <typename Object>
class object_ptr;

class PeerData;

namespace Api {
struct InviteLink;
} // namespace Api

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class VerticalLayout;
class Show;
class BoxContent;
} // namespace Ui

[[nodiscard]] bool IsExpiredLink(const Api::InviteLink &data, TimeId now);

void AddSinglePeerRow(
	not_null<Ui::VerticalLayout*> container,
	not_null<PeerData*> peer,
	rpl::producer<QString> status);

void AddPermanentLinkBlock(
	std::shared_ptr<Ui::Show> show,
	not_null<Ui::VerticalLayout*> container,
	not_null<PeerData*> peer,
	not_null<UserData*> admin,
	rpl::producer<Api::InviteLink> fromList);

void CopyInviteLink(std::shared_ptr<Ui::Show> show, const QString &link);
[[nodiscard]] object_ptr<Ui::BoxContent> ShareInviteLinkBox(
	not_null<PeerData*> peer,
	const QString &link,
	const QString &copied = {});
[[nodiscard]] object_ptr<Ui::BoxContent> ShareInviteLinkBox(
	not_null<Main::Session*> session,
	const QString &link,
	const QString &copied = {});
[[nodiscard]] object_ptr<Ui::BoxContent> InviteLinkQrBox(
	PeerData *peer,
	const QString &link,
	rpl::producer<QString> title,
	rpl::producer<QString> about);
[[nodiscard]] object_ptr<Ui::BoxContent> RevokeLinkBox(
	not_null<PeerData*> peer,
	not_null<UserData*> admin,
	const QString &link,
	bool permanent = false);
[[nodiscard]] object_ptr<Ui::BoxContent> EditLinkBox(
	not_null<PeerData*> peer,
	const Api::InviteLink &data);
[[nodiscard]] object_ptr<Ui::BoxContent> DeleteLinkBox(
	not_null<PeerData*> peer,
	not_null<UserData*> admin,
	const QString &link);

[[nodiscard]] object_ptr<Ui::BoxContent> ShowInviteLinkBox(
	not_null<PeerData*> peer,
	const Api::InviteLink &link);

[[nodiscard]] QString PrepareRequestedRowStatus(TimeId date);
