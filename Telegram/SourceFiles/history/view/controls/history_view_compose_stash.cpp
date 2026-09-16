/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_compose_stash.h"

#include "api/api_common.h"
#include "apiwrap.h"
#include "boxes/send_files_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_changes.h"
#include "data/data_chat_participant_status.h"
#include "data/data_peer.h"
#include "history/view/history_view_corner_buttons.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "menu/menu_send.h"
#include "ui/widgets/popup_menu.h"

#include "styles/style_chat_helpers.h"
#include "styles/style_menu_icons.h"

namespace HistoryView::Controls {
namespace {

void FillStashMenu(
		not_null<Ui::PopupMenu*> menu,
		std::shared_ptr<ChatHelpers::Show> show,
		const SendMenu::Details &details,
		Fn<void()> remove,
		Fn<void(Api::SendOptions)> send) {
	using Type = SendMenu::Type;
	const auto type = details.type;
	menu->addAction(
		tr::lng_context_delete_msg(tr::now),
		std::move(remove),
		&st::menuIconDelete);
	if (type != Type::Disabled && type != Type::Reminder) {
		menu->addAction(
			tr::lng_send_silent_message(tr::now),
			[=] { send({ .silent = true }); },
			&st::defaultComposeIcons.menuMute);
	}
	if (type == Type::Scheduled
		|| type == Type::ScheduledToUser
		|| type == Type::Reminder) {
		const auto schedule = SendMenu::DefaultCallback(show, send);
		menu->addAction(
			((type == Type::Reminder)
				? tr::lng_reminder_message(tr::now)
				: tr::lng_schedule_message(tr::now)),
			[=] {
				schedule({ .type = SendMenu::ActionType::Schedule }, details);
			},
			&st::defaultComposeIcons.menuSchedule);
	}
	menu->addAction(
		tr::lng_context_send_message(tr::now),
		[=] { send({}); },
		&st::menuIconSend);
}

} // namespace

StashManager::StashManager(StashManagerDescriptor &&descriptor)
: _data(std::move(descriptor)) {
	_data.buttons->stashClicks(
	) | rpl::on_next([=] {
		exchange();
	}, _lifetime);

	_data.buttons->setStashMenuFiller([=](not_null<Ui::PopupMenu*> menu) {
		if (!current()) {
			return;
		}
		FillStashMenu(
			menu,
			_data.show,
			_data.menuDetails(),
			[=] { remove(); },
			[=](Api::SendOptions options) { sendStashed(options); });
	});

	_data.session->changes().historyUpdates(
		Data::HistoryUpdate::Flag::ComposeStash
	) | rpl::filter([=](const Data::HistoryUpdate &update) {
		return (update.history.get() == _data.history());
	}) | rpl::on_next([=] {
		updateButton();
	}, _lifetime);
}

StashManager::~StashManager() = default;

Data::ComposeStash *StashManager::current() const {
	const auto history = _data.history();
	return history ? history->composeStash(_data.key()) : nullptr;
}

bool StashManager::applicable(const Data::ComposeStash &stash) const {
	const auto history = _data.history();
	return history
		&& _data.allowed()
		&& (_data.canSendTexts() || !stash.hasText())
		&& (!stash.hasFiles()
			|| !Data::FileRestrictionError(
				history->peer,
				stash.files,
				std::nullopt));
}

bool StashManager::checkApplicable(const Data::ComposeStash &stash) const {
	if (applicable(stash)) {
		return true;
	} else if (stash.hasFiles()) {
		_data.filesError(stash.files);
	}
	return false;
}

bool StashManager::canExchange() const {
	return _data.allowed() && (_data.hasContent() || current());
}

bool StashManager::exchange() {
	const auto history = _data.history();
	const auto key = history ? _data.key() : Data::DraftKey::None();
	if (!key || !_data.allowed()) {
		return false;
	} else if (const auto stashed = history->composeStash(key)) {
		if (!checkApplicable(*stashed)) {
			return false;
		}
	} else if (!_data.hasContent()) {
		return false;
	}
	auto taken = history->takeComposeStash(key);
	if (auto now = _data.take()) {
		history->setComposeStash(key, std::move(now));
	}
	if (taken) {
		applyStash(std::move(*taken));
	}
	updateButton();
	return true;
}

void StashManager::applyStash(Data::ComposeStash &&stash) {
	auto files = std::move(stash.files);
	_data.apply(std::move(stash));
	if (!files.files.empty() || !files.filesToProcess.empty()) {
		_openedBox = _data.openFiles(files);
		if (!_openedBox) {
			keepFiles(std::move(files));
		}
	}
}

void StashManager::keepFiles(Ui::PreparedList &&files) {
	const auto history = _data.history();
	const auto key = history ? _data.key() : Data::DraftKey::None();
	if (!key) {
		return;
	} else if (const auto stash = history->composeStash(key)) {
		stash->files = std::move(files);
	} else {
		auto created = std::make_unique<Data::ComposeStash>();
		created->files = std::move(files);
		history->setComposeStash(key, std::move(created));
	}
}

void StashManager::remove() {
	if (const auto history = _data.history()) {
		history->setComposeStash(_data.key(), nullptr);
	}
	updateButton();
}

void StashManager::sendStashed(Api::SendOptions options) {
	const auto history = _data.history();
	const auto key = history ? _data.key() : Data::DraftKey::None();
	const auto stashed = key ? history->composeStash(key) : nullptr;
	if (!stashed || !_data.allowed() || !checkApplicable(*stashed)) {
		return;
	}
	auto kept = _data.take();
	auto taken = history->takeComposeStash(key);
	_openedBox = nullptr;
	applyStash(std::move(*taken));
	if (const auto box = _openedBox.data()) {
		box->sendWithOptions(options);
	} else {
		_data.send(options);
	}
	if (kept) {
		if (auto unsent = _data.take()) {
			if (const auto files = history->composeStash(key)) {
				unsent->files = std::move(files->files);
			}
			history->setComposeStash(key, std::move(unsent));
		}
		_data.apply(std::move(*kept));
	}
	updateButton();
}

bool StashManager::canTakeFromBox() const {
	if (!_data.allowed()) {
		return false;
	}
	const auto previous = current();
	return !previous || applicable(*previous);
}

void StashManager::takeFromBox(SendFilesStashed &&stashed) {
	const auto history = _data.history();
	const auto key = history ? _data.key() : Data::DraftKey::None();
	if (!key) {
		return;
	}
	auto previous = history->takeComposeStash(key);
	auto result = std::make_unique<Data::ComposeStash>();
	const auto length = int(stashed.caption.text.size());
	result->draft = Data::Draft(
		stashed.caption,
		stashed.replyTo,
		_data.suggest(),
		MessageCursor(length, length, Ui::kQFixedMax),
		Data::WebPageDraft());
	result->files = std::move(stashed.list);

	const auto topicRootId = key.topicRootId();
	const auto monoforumPeerId = key.monoforumPeerId();
	result->forward = history->forwardDraft(topicRootId, monoforumPeerId);
	if (!result->forward.ids.empty()) {
		history->setForwardDraft(topicRootId, monoforumPeerId, {});
	}
	_data.clearComposer();
	_data.session->api().saveCurrentDraftToCloud();
	history->setComposeStash(key, std::move(result));

	if (previous) {
		const auto shared = std::shared_ptr<Data::ComposeStash>(
			std::move(previous));
		crl::on_main(this, [=] {
			if (_data.allowed()) {
				applyStash(std::move(*shared));
			}
		});
	}
	updateButton();
}

void StashManager::updateButton() {
	if (_data.buttons->ignoresVisibility()) {
		return;
	}
	const auto stash = current();
	_data.buttons->updateVisibility(
		CornerButtonType::Stash,
		stash && applicable(*stash));
}

} // namespace HistoryView::Controls
