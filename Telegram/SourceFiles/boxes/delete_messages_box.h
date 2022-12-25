/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/box_content.h"

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class Checkbox;
class FlatLabel;
class LinkButton;
} // namespace Ui

class DeleteMessagesBox final : public Ui::BoxContent {
public:
	DeleteMessagesBox(
		QWidget*,
		not_null<HistoryItem*> item,
		bool suggestModerateActions);
	DeleteMessagesBox(
		QWidget*,
		not_null<Main::Session*> session,
		MessageIdsList &&selected);
	DeleteMessagesBox(
		QWidget*,
		not_null<PeerData*> peer,
		QDate firstDayToDelete,
		QDate lastDayToDelete);
	DeleteMessagesBox(QWidget*, not_null<PeerData*> peer, bool justClear);

	void setDeleteConfirmedCallback(Fn<void()> callback) {
		_deleteConfirmedCallback = std::move(callback);
	}

protected:
	void prepare() override;

	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	struct RevokeConfig {
		TextWithEntities checkbox;
		TextWithEntities description;
	};
	void deleteAndClear();
	[[nodiscard]] PeerData *checkFromSinglePeer() const;
	[[nodiscard]] bool hasScheduledMessages() const;
	[[nodiscard]] std::optional<RevokeConfig> revokeText(
		not_null<PeerData*> peer) const;

	const not_null<Main::Session*> _session;

	PeerData * const _wipeHistoryPeer = nullptr;
	const bool _wipeHistoryJustClear = false;
	const QDate _wipeHistoryFirstToDelete;
	const QDate _wipeHistoryLastToDelete;
	const MessageIdsList _ids;
	PeerData *_moderateFrom = nullptr;
	ChannelData *_moderateInChannel = nullptr;
	bool _moderateBan = false;
	bool _moderateDeleteAll = false;

	bool _revokeForBot = false;

	object_ptr<Ui::FlatLabel> _text = { nullptr };
	object_ptr<Ui::Checkbox> _revoke = { nullptr };
	object_ptr<Ui::Checkbox> _banUser = { nullptr };
	object_ptr<Ui::Checkbox> _reportSpam = { nullptr };
	object_ptr<Ui::Checkbox> _deleteAll = { nullptr };
	object_ptr<Ui::LinkButton> _autoDeleteSettings = { nullptr };

	Fn<void()> _deleteConfirmedCallback;

};
