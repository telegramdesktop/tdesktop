/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/generic_box.h"

class PeerData;

namespace Api {
struct InviteLink;
} // namespace Api

namespace Ui {
class VerticalLayout;
} // namespace Ui

[[nodiscard]] bool IsExpiredLink(const Api::InviteLink &data, TimeId now);

void AddSinglePeerRow(
	not_null<Ui::VerticalLayout*> container,
	not_null<PeerData*> peer,
	rpl::producer<QString> status);

void AddPermanentLinkBlock(
	not_null<Ui::VerticalLayout*> container,
	not_null<PeerData*> peer,
	not_null<UserData*> admin,
	rpl::producer<Api::InviteLink> fromList);

void CopyInviteLink(const QString &link);
void ShareInviteLinkBox(not_null<PeerData*> peer, const QString &link);
void InviteLinkQrBox(const QString &link);
void RevokeLink(
	not_null<PeerData*> peer,
	not_null<UserData*> admin,
	const QString &link);
void EditLink(
	not_null<PeerData*> peer,
	const Api::InviteLink &data);
void DeleteLink(
	not_null<PeerData*> peer,
	not_null<UserData*> admin,
	const QString &link);

void ShowInviteLinkBox(
	not_null<PeerData*> peer,
	const Api::InviteLink &link);
