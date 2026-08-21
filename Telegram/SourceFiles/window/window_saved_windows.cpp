/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_saved_windows.h"

#include "apiwrap.h"
#include "base/call_delayed.h"
#include "core/application.h"
#include "data/data_channel.h"
#include "data/data_community.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_peer.h"
#include "data/data_saved_messages.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "data/data_thread.h"
#include "data/data_user.h"
#include "dialogs/ui/restore_windows_offer.h"
#include "history/history.h"
#include "history/admin_log/history_admin_log_section.h"
#include "history/view/history_view_chat_section.h"
#include "history/view/history_view_pinned_section.h"
#include "history/view/history_view_scheduled_section.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "settings.h"
#include "storage/storage_account.h"
#include "storage/storage_shared_media.h"
#include "ui/layers/layer_widget.h"
#include "ui/ui_utility.h"
#include "ui/wrap/fade_wrap.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_basic.h"
#include "styles/style_dialogs.h"

namespace Window {
namespace {

constexpr auto kSaveDelay = crl::time(1000);
constexpr auto kResolveTimeout = 20 * crl::time(1000);
constexpr auto kBatchResolveFallback = 10 * crl::time(1000);
constexpr auto kMaxSavedWindows = 64;
constexpr auto kMaxSavedChats = 64;
constexpr auto kMaxClosedWindows = 16;
constexpr auto kVersion = 3;
constexpr auto kPrefKey = std::string_view("windows_state");
constexpr auto kRestoreKey = std::string_view("windows_state.restore");
constexpr auto kAskedKey = std::string_view("windows_state.asked");

[[nodiscard]] bool ValidType(int type) {
	switch (SeparateType(type)) {
	case SeparateType::Primary:
	case SeparateType::Archive:
	case SeparateType::Chat:
	case SeparateType::Forum:
	case SeparateType::Community:
	case SeparateType::SavedSublist:
	case SeparateType::SharedMedia:
		return true;
	}
	return false;
}

[[nodiscard]] bool ReplayableType(SeparateType type) {
	switch (type) {
	case SeparateType::Primary:
	case SeparateType::Archive:
	case SeparateType::Chat:
	case SeparateType::Forum:
	case SeparateType::SavedSublist:
		return true;
	case SeparateType::Community:
	case SeparateType::SharedMedia:
		return false;
	}
	Unexpected("Type in Window::ReplayableType.");
}

[[nodiscard]] bool NeedsThread(SeparateType type) {
	return (type != SeparateType::Primary)
		&& (type != SeparateType::Archive);
}

[[nodiscard]] bool SameWindow(const SavedWindow &a, const SavedWindow &b) {
	return (a.accountIndex == b.accountIndex)
		&& (a.userPeer == b.userPeer)
		&& (a.type == b.type)
		&& (a.sharedMediaType == b.sharedMediaType)
		&& SameChat(a.thread, b.thread);
}

[[nodiscard]] QByteArray Serialize(const std::vector<SavedWindow> &list) {
	auto result = QByteArray();
	{
		QDataStream stream(&result, QIODevice::WriteOnly);
		stream.setVersion(QDataStream::Qt_5_1);
		stream << qint32(kVersion) << qint32(list.size());
		for (const auto &window : list) {
			stream << qint32(window.accountIndex)
				<< quint64(window.userPeer)
				<< qint32(window.type)
				<< qint32(window.sharedMediaType)
				<< quint64(window.thread.peer.value)
				<< quint64(window.thread.accessHash)
				<< qint64(window.thread.topicRootId.bare)
				<< quint64(window.thread.monoforumPeer.value)
				<< quint64(window.thread.monoforumAccessHash)
				<< qint32(window.position.x)
				<< qint32(window.position.y)
				<< qint32(window.position.w)
				<< qint32(window.position.h)
				<< qint32(window.position.moncrc)
				<< qint32(window.position.maximized)
				<< qint32(window.position.scale)
				<< qint32(window.chats.size());
			for (const auto &chat : window.chats) {
				stream << quint64(chat.peer.value)
					<< quint64(chat.accessHash)
					<< qint64(chat.topicRootId.bare)
					<< quint64(chat.monoforumPeer.value)
					<< quint64(chat.monoforumAccessHash)
					<< qint64(chat.msgId.bare)
					<< qint32(chat.section);
			}
		}
	}
	return result;
}

[[nodiscard]] std::vector<SavedWindow> Deserialize(
		const QByteArray &serialized) {
	if (serialized.isEmpty()) {
		return {};
	}
	QDataStream stream(serialized);
	stream.setVersion(QDataStream::Qt_5_1);
	auto version = qint32();
	auto count = qint32();
	stream >> version >> count;
	if (stream.status() != QDataStream::Ok
		|| version < 1
		|| version > kVersion
		|| count <= 0
		|| count > kMaxSavedWindows) {
		return {};
	}
	auto result = std::vector<SavedWindow>();
	result.reserve(count);
	for (auto i = 0; i != count; ++i) {
		auto accountIndex = qint32();
		auto userPeer = quint64();
		auto type = qint32();
		auto sharedMediaType = qint32();
		auto threadPeer = quint64();
		auto threadHash = quint64();
		auto threadRoot = qint64();
		auto threadMono = quint64();
		auto threadMonoHash = quint64();
		auto x = qint32();
		auto y = qint32();
		auto w = qint32();
		auto h = qint32();
		auto moncrc = qint32();
		auto maximized = qint32();
		auto scale = qint32();
		auto chatsCount = qint32();
		stream >> accountIndex
			>> userPeer
			>> type
			>> sharedMediaType
			>> threadPeer;
		if (version > 1) {
			stream >> threadHash;
		}
		stream >> threadRoot >> threadMono;
		if (version > 1) {
			stream >> threadMonoHash;
		}
		stream >> x
			>> y
			>> w
			>> h
			>> moncrc
			>> maximized
			>> scale
			>> chatsCount;
		if (stream.status() != QDataStream::Ok
			|| !ValidType(type)
			|| chatsCount < 0
			|| chatsCount > kMaxSavedChats) {
			return {};
		}
		auto window = SavedWindow();
		window.accountIndex = accountIndex;
		window.userPeer = userPeer;
		window.type = SeparateType(type);
		window.sharedMediaType = sharedMediaType;
		window.thread = SavedChat{
			.peer = PeerId(BareId(threadPeer)),
			.accessHash = threadHash,
			.topicRootId = MsgId(threadRoot),
			.monoforumPeer = PeerId(BareId(threadMono)),
			.monoforumAccessHash = threadMonoHash,
		};
		window.position = Core::WindowPosition{
			.moncrc = moncrc,
			.maximized = maximized,
			.scale = scale,
			.x = x,
			.y = y,
			.w = w,
			.h = h,
		};
		window.chats.reserve(chatsCount);
		for (auto j = 0; j != chatsCount; ++j) {
			auto peer = quint64();
			auto hash = quint64();
			auto root = qint64();
			auto mono = quint64();
			auto monoHash = quint64();
			auto msg = qint64();
			auto section = qint32();
			stream >> peer;
			if (version > 1) {
				stream >> hash;
			}
			stream >> root >> mono;
			if (version > 1) {
				stream >> monoHash;
			}
			stream >> msg;
			if (version > 2) {
				stream >> section;
			}
			if (section < int(SavedChatSection::Chat)
				|| section > int(SavedChatSection::AdminLog)) {
				section = int(SavedChatSection::Chat);
			}
			window.chats.push_back(SavedChat{
				.peer = PeerId(BareId(peer)),
				.accessHash = hash,
				.topicRootId = MsgId(root),
				.monoforumPeer = PeerId(BareId(mono)),
				.monoforumAccessHash = monoHash,
				.msgId = MsgId(msg),
				.section = SavedChatSection(section),
			});
		}
		if (stream.status() != QDataStream::Ok) {
			return {};
		}
		if (NeedsThread(window.type) && !window.thread.valid()) {
			continue;
		}
		result.push_back(std::move(window));
	}
	return result;
}

} // namespace

bool SameChat(const SavedChat &a, const SavedChat &b) {
	return (a.peer == b.peer)
		&& (a.topicRootId == b.topicRootId)
		&& (a.monoforumPeer == b.monoforumPeer)
		&& (a.section == b.section);
}

SavedChat SavedChatFromThread(not_null<Data::Thread*> thread, MsgId msgId) {
	const auto peer = thread->owningHistory()->peer;
	const auto monoforumPeerId = thread->monoforumPeerId();
	const auto monoforumPeer = monoforumPeerId
		? peer->owner().peerLoaded(monoforumPeerId)
		: nullptr;
	return {
		.peer = peer->id,
		.accessHash = SavedAccessHash(peer),
		.topicRootId = thread->topicRootId(),
		.monoforumPeer = monoforumPeerId,
		.monoforumAccessHash = (monoforumPeer
			? SavedAccessHash(monoforumPeer)
			: 0),
		.msgId = IsServerMsgId(msgId) ? msgId : MsgId(),
	};
}

uint64 SavedAccessHash(not_null<PeerData*> peer) {
	if (const auto user = peer->asUser()) {
		return user->accessHash();
	} else if (const auto channel = peer->asChannel()) {
		return channel->accessHash();
	}
	return 0;
}

struct SavedWindows::Step {
	SavedWindow data;
	Main::Session *session = nullptr;
	std::vector<Data::Thread*> slots;
	int pending = 0;
	bool dispatching = false;
	bool dead = false;
	base::Timer timeout;
	rpl::lifetime lifetime;
};

struct SavedWindows::BatchResolve {
	bool sent = false;
	bool done = false;
	bool sliceSeen = false;
	QVector<MTPInputUser> users;
	QVector<MTPInputChannel> channels;
	QVector<MTPlong> chats;
	base::Timer fallback;
	rpl::event_stream<> doneEvents;
	rpl::lifetime lifetime;
};

SavedWindows::SavedWindows(not_null<Core::Application*> app)
: _app(app)
, _saveTimer([=] { save(); }) {
	const auto asked = app->settings().readPref<bool>(kAskedKey, false);
	if (!asked || app->settings().readPref<bool>(kRestoreKey, false)) {
		_toRestore = Deserialize(
			app->settings().readPref<QByteArray>(kPrefKey));
	}
}

SavedWindows::~SavedWindows() = default;

bool SavedWindows::restoreOnLaunch() const {
	return _app->settings().readPref<bool>(kRestoreKey, false);
}

void SavedWindows::setRestoreOnLaunch(bool restore) {
	const auto asked = _app->settings().readPref<bool>(kAskedKey, false);
	if (asked && restoreOnLaunch() == restore) {
		return;
	}
	markAsked(restore);
	if (restore) {
		maybeBeginRestore();
	} else {
		discardRestore();
	}
}

void SavedWindows::markAsked(bool restore) {
	_app->settings().writePref<bool>(kAskedKey, true);
	_app->settings().writePref<bool>(kRestoreKey, restore);
}

void SavedWindows::attachToWindow(not_null<Controller*> window) {
	if (!_restoring && _hideOffer) {
		hideOffer();
		stashUndecided();
	}
	scheduleSave();
	window->sessionControllerValue(
	) | rpl::on_next([=](SessionController *controller) {
		scheduleSave();
		if (controller) {
			controller->activeChatEntryValue(
			) | rpl::on_next([=](const Dialogs::RowDescriptor &) {
				scheduleSave();
			}, controller->lifetime());
		}
	}, window->lifetime());
}

void SavedWindows::scheduleSave() {
	if (Core::Quitting()) {
		return;
	}
	_saveTimer.callOnce(kSaveDelay);
}

void SavedWindows::writeNow() {
	_saveTimer.cancel();
	if (!_restoring && !restoreOnLaunch()) {
		// Not restored windows are not offered on the next launch.
		_toRestore.clear();
		_undecided.clear();
	}
	save();
}

void SavedWindows::save() {
	_app->settings().writePref<QByteArray>(kPrefKey, collect());
}

QByteArray SavedWindows::collect() const {
	auto list = std::vector<SavedWindow>();
	auto used = std::vector<not_null<Controller*>>();
	const auto push = [&](not_null<Controller*> window) {
		if (ranges::contains(used, window)) {
			return;
		}
		used.push_back(window);
		if (auto serialized = serializeWindow(window)) {
			list.push_back(std::move(*serialized));
		}
	};
	for (const auto &window : _app->windowStack()) {
		push(window);
	}
	_app->enumerateWindows(push);
	const auto accountExists = [&](int index) {
		if (!_app->domain().started()) {
			return true;
		}
		for (const auto &entry : _app->domain().accounts()) {
			if (entry.index == index) {
				return true;
			}
		}
		return false;
	};
	const auto appendPending = [&](const SavedWindow &window) {
		const auto same = [&](const SavedWindow &existing) {
			return SameWindow(existing, window);
		};
		if (!ranges::any_of(list, same)
			&& accountExists(window.accountIndex)) {
			list.push_back(window);
		}
	};
	if (_step) {
		appendPending(_step->data);
	}
	for (const auto &window : _toRestore) {
		appendPending(window);
	}
	for (const auto &window : _undecided) {
		appendPending(window);
	}
	if (list.size() > kMaxSavedWindows) {
		list.resize(kMaxSavedWindows);
	}
	return Serialize(list);
}

std::optional<SavedWindow> SavedWindows::serializeWindow(
		not_null<Controller*> window) const {
	const auto id = window->id();
	if (!id) {
		return {};
	}
	const auto controller = window->sessionController();
	if (!controller) {
		return {};
	}
	const auto session = &controller->session();
	auto result = SavedWindow();
	for (const auto &entry : _app->domain().accounts()) {
		if (entry.account.get() == id.account) {
			result.accountIndex = entry.index;
			break;
		}
	}
	if (result.accountIndex < 0) {
		return {};
	}
	result.userPeer = session->userPeerId().value;
	result.type = id.type;
	result.sharedMediaType = static_cast<int>(id.sharedMediaType);
	if (id.thread) {
		result.thread = SavedChatFromThread(id.thread);
	}
	result.position = window->widget()->countPositionForSave();
	if (ReplayableType(id.type)) {
		result.chats = controller->content()->chatStackForSave();
		if (result.chats.size() > kMaxSavedChats) {
			result.chats.erase(
				begin(result.chats),
				end(result.chats) - kMaxSavedChats);
		}
	}
	return result;
}

namespace {

using OfferChoice = Dialogs::RestoreWindowsChoice;
using OfferCard = Dialogs::RestoreWindowsOffer;

void RaiseAboveSiblings(not_null<QWidget*> card) {
	card->raise();
	const auto parent = card->parentWidget();
	if (!parent) {
		return;
	}
	for (const auto child : parent->children()) {
		if (const auto layer = dynamic_cast<Ui::LayerStackWidget*>(child)) {
			layer->raise();
			return;
		}
	}
}

[[nodiscard]] Fn<void()> ShowRestoreOffer(
		not_null<Controller*> window,
		Fn<void(OfferChoice)> chosen) {
	const auto controller = window->sessionController();
	Expects(controller != nullptr);

	const auto body = window->widget()->bodyWidget();
	const auto notified = std::make_shared<bool>(false);
	const auto notify = [=, chosen = std::move(chosen)](OfferChoice choice) {
		if (!std::exchange(*notified, true)) {
			chosen(choice);
		}
	};
	const auto wrap = Ui::CreateChild<Ui::FadeWrap<OfferCard>>(
		body,
		object_ptr<OfferCard>(body));
	const auto card = wrap->entity();
	const auto close = [=] {
		wrap->hide(anim::type::normal);
		base::call_delayed(st::fadeWrapDuration, wrap, [=] {
			wrap->deleteLater();
		});
	};
	card->chosen(
	) | rpl::on_next([=](OfferChoice choice) {
		close();
		notify(choice);
	}, wrap->lifetime());
	rpl::combine(
		body->sizeValue(),
		card->sizeValue()
	) | rpl::on_next([=](QSize outer, QSize size) {
		const auto &position = st::restoreWindowsOfferPosition;
		const auto &margins = st::dialogsTopBarSuggestionMargins;
		card->setAvailableWidth(outer.width() - 2 * position.x());
		wrap->moveToRight(
			position.x() - margins.right(),
			position.y() - margins.top(),
			outer.width());
	}, wrap->lifetime());
	wrap->hide(anim::type::instant);
	RaiseAboveSiblings(wrap);
	controller->lifetime().add(crl::guard(wrap, [=] {
		wrap->hide(anim::type::instant);
		wrap->deleteLater();
		notify(OfferChoice::Detached);
	}));
	const auto desired = std::make_shared<bool>(false);
	auto loaded = rpl::single(
		controller->session().data().chatsListLoaded()
	) | rpl::then(controller->session().data().chatsListLoadedEvents(
	) | rpl::map([](Data::Folder *folder) {
		return !folder;
	}) | rpl::filter([](bool mainList) {
		return mainList;
	}));
	rpl::combine(
		controller->activeChatValue(),
		rpl::single(
			controller->mainSectionShown()
		) | rpl::then(controller->mainSectionShownChanges()),
		std::move(loaded)
	) | rpl::map([](Dialogs::Key key, bool section, bool loaded) {
		return loaded && !key && !section;
	}) | rpl::distinct_until_changed(
	) | rpl::on_next([=](bool show) {
		*desired = show;
		if (!show) {
			wrap->hide(anim::type::normal);
		} else {
			base::call_delayed(st::slideDuration, wrap, [=] {
				if (*desired) {
					wrap->show(anim::type::normal);
				}
			});
		}
	}, wrap->lifetime());
	return crl::guard(wrap, [=] {
		*notified = true;
		close();
	});
}

} // namespace

void SavedWindows::startRestore() {
	if (_toRestore.empty()) {
		_restoreFinished = true;
		return;
	}
	_deferUntilActivated = cStartInTray()
		|| (cLaunchMode() == LaunchModeAutoStart && cStartMinimized());
	rpl::combine(
		_app->domain().activeValue(),
		_app->passcodeLockValue()
	) | rpl::filter([=](Main::Account *account, bool locked) {
		return (account != nullptr) && !locked;
	}) | rpl::take(1) | rpl::to_empty | rpl::on_next([=] {
		_domainReady = true;
		maybeBeginRestore();
	}, _lifetime);

	_app->domain().activeSessionChanges(
	) | rpl::on_next([=](Main::Session *session) {
		if (session) {
			crl::on_main(this, [=] {
				maybeBeginRestore();
			});
		}
	}, _lifetime);
}

void SavedWindows::windowActivated() {
	scheduleSave();
	_activatedOnce = true;
	maybeBeginRestore();
}

void SavedWindows::windowClosed(not_null<Controller*> window) {
	if (Core::Quitting()) {
		return;
	}
	const auto id = window->id();
	if (!id || id.type == SeparateType::Primary) {
		return;
	}
	if (auto serialized = serializeWindow(window)) {
		while (_closed.size() >= kMaxClosedWindows) {
			_closed.erase(begin(_closed));
		}
		_closed.push_back(std::move(*serialized));
	}
}

bool SavedWindows::reopenLastClosed() {
	if (_closed.empty() || Core::Quitting()) {
		return false;
	}
	if (!_restoring) {
		stashUndecided();
	}
	_toRestore.push_back(std::move(_closed.back()));
	_closed.pop_back();
	if (!_restoring) {
		_restoring = true;
		processNext();
	}
	return true;
}

void SavedWindows::maybeBeginRestore() {
	if (_restoring
		|| _restoreFinished
		|| !_domainReady
		|| (_deferUntilActivated && !_activatedOnce)) {
		return;
	}
	if (!_app->settings().readPref<bool>(kAskedKey, false)) {
		maybeOfferRestore();
	} else if (_app->settings().readPref<bool>(kRestoreKey, false)) {
		beginRestore();
	} else {
		discardRestore();
	}
}

void SavedWindows::maybeOfferRestore() {
	if (_offered) {
		return;
	} else if (!worthOffering()) {
		discardRestore();
		return;
	}
	const auto window = _app->activePrimaryWindow();
	if (!window || !window->sessionController()) {
		return;
	}
	_offered = true;
	_hideOffer = ShowRestoreOffer(window, crl::guard(this, [=](
			OfferChoice choice) {
		_hideOffer = nullptr;
		switch (choice) {
		case OfferChoice::Always:
			markAsked(true);
			_announceOnFinish = true;
			beginRestore();
			break;
		case OfferChoice::Once:
			beginRestore();
			break;
		case OfferChoice::Never:
			markAsked(false);
			discardRestore();
			if (const auto window = _app->activeWindow()) {
				window->showToast(
					tr::lng_restore_windows_disabled_toast(tr::now));
			}
			break;
		case OfferChoice::Dismiss:
			stashUndecided();
			break;
		case OfferChoice::Detached:
			_offered = false;
			break;
		}
	}));
}

void SavedWindows::hideOffer() {
	if (const auto hide = base::take(_hideOffer)) {
		hide();
	}
}

bool SavedWindows::worthOffering() const {
	return (_toRestore.size() > 1);
}

void SavedWindows::beginRestore() {
	if (_restoring || Core::Quitting()) {
		return;
	}
	hideOffer();
	_restoring = true;
	if (!_undecided.empty()) {
		_toRestore.insert(
			begin(_toRestore),
			std::make_move_iterator(begin(_undecided)),
			std::make_move_iterator(end(_undecided)));
		_undecided.clear();
	}
	processNext();
}

void SavedWindows::discardRestore() {
	hideOffer();
	_toRestore.clear();
	_undecided.clear();
	_restoreFinished = true;
}

void SavedWindows::stashUndecided() {
	if (_toRestore.empty()) {
		return;
	}
	_undecided.insert(
		end(_undecided),
		std::make_move_iterator(begin(_toRestore)),
		std::make_move_iterator(end(_toRestore)));
	_toRestore.clear();
}

void SavedWindows::processNext() {
	Expects(_step == nullptr);

	while (!_toRestore.empty()) {
		auto data = std::move(_toRestore.front());
		_toRestore.erase(begin(_toRestore));
		if (startStep(std::move(data))) {
			return;
		}
	}
	finishRestore();
}

Main::Session *SavedWindows::sessionFor(const SavedWindow &data) const {
	auto account = (Main::Account*)nullptr;
	for (const auto &entry : _app->domain().accounts()) {
		if (entry.index == data.accountIndex) {
			account = entry.account.get();
			break;
		}
	}
	const auto session = account ? account->maybeSession() : nullptr;
	if (!session
		|| (data.userPeer != 0
			&& session->userPeerId().value != data.userPeer)) {
		return nullptr;
	}
	return session;
}

bool SavedWindows::startStep(SavedWindow &&data) {
	const auto session = sessionFor(data);
	if (!session) {
		return false;
	}
	_step = std::make_unique<Step>();
	const auto step = _step.get();
	step->data = std::move(data);
	step->session = session;
	step->slots.resize(1 + step->data.chats.size(), nullptr);
	// step->timeout.setCallback([=] { queueFinishStep(); });
	// step->timeout.callOnce(kResolveTimeout);
	session->account().sessionChanges(
	) | rpl::on_next([=](Main::Session *) {
		step->dead = true;
		queueFinishStep();
	}, step->lifetime);

	const auto generation = _generation;
	step->dispatching = true;
	step->pending = (step->data.thread.valid() ? 1 : 0)
		+ int(step->data.chats.size());
	if (!step->pending) {
		step->dispatching = false;
		queueFinishStep();
		return true;
	}
	if (step->data.thread.valid()) {
		resolveSlot(0);
	}
	const auto count = int(step->data.chats.size());
	for (auto i = 0; i != count; ++i) {
		if (!_step || _generation != generation) {
			return true;
		}
		resolveSlot(1 + i);
	}
	if (_step && _generation == generation) {
		_step->dispatching = false;
		if (!_step->pending) {
			queueFinishStep();
		}
	}
	return true;
}

void SavedWindows::queueFinishStep() {
	const auto generation = _generation;
	crl::on_main(crl::guard(this, [=] {
		if (_generation == generation && _step) {
			finishStep();
		}
	}));
}

void SavedWindows::finishStep() {
	Expects(_step != nullptr);

	++_generation;
	const auto step = std::move(_step);
	step->timeout.cancel();
	step->lifetime.destroy();
	if (!step->dead) {
		createWindow(*step);
	}
	processNext();
}

void SavedWindows::finishRestore() {
	_restoring = false;
	_restoreFinished = true;
	scheduleSave();
	if (_announceOnFinish) {
		_announceOnFinish = false;
		if (const auto window = _app->activeWindow()) {
			window->showToast(
				tr::lng_restore_windows_enabled_toast(tr::now));
		}
	}
}

void SavedWindows::resolveSlot(int index) {
	Expects(_step != nullptr);

	const auto step = _step.get();
	const auto generation = _generation;
	const auto key = index
		? step->data.chats[index - 1]
		: step->data.thread;
	const auto session = step->session;
	const auto apply = crl::guard(this, [=](Data::Thread *thread) {
		if (_generation != generation || !_step) {
			return;
		}
		_step->slots[index] = thread;
		if (!--_step->pending && !_step->dispatching) {
			queueFinishStep();
		}
	});
	waitPeer(key.peer, [=](PeerData *peer) {
		if (!peer) {
			apply(nullptr);
			return;
		}
		const auto history = session->data().history(peer).get();
		if (key.topicRootId) {
			const auto forum = peer->forum();
			if (!forum) {
				apply(history);
			} else if (const auto topic = forum->topicFor(key.topicRootId)) {
				apply(topic);
			} else {
				forum->requestTopic(key.topicRootId, crl::guard(this, [=] {
					if (_generation != generation || !_step) {
						return;
					}
					const auto forum = peer->forum();
					apply(forum ? forum->topicFor(key.topicRootId) : nullptr);
				}));
			}
		} else if (key.monoforumPeer) {
			waitPeer(key.monoforumPeer, [=](PeerData *sublistPeer) {
				if (!sublistPeer) {
					apply(nullptr);
				} else if (peer->isSelf()) {
					apply(session->data().savedMessages().sublist(
						sublistPeer).get());
				} else if (const auto monoforum = peer->monoforum()) {
					const auto sublist = monoforum->sublist(sublistPeer);
					monoforum->requestSublist(sublistPeer);
					apply(sublist.get());
				} else {
					apply(history);
				}
			});
		} else {
			apply(history);
		}
	});
}

void SavedWindows::waitPeer(PeerId peerId, Fn<void(PeerData*)> done) {
	Expects(_step != nullptr);

	const auto session = _step->session;
	const auto owner = &session->data();
	if (const auto peer = owner->peerLoaded(peerId)) {
		done(peer);
		return;
	}
	session->local().readSearchSuggestions();
	if (const auto peer = owner->peerLoaded(peerId)) {
		done(peer);
		return;
	}
	const auto batch = ensureBatchResolve(session);
	const auto check = [=]() -> std::optional<PeerData*> {
		if (const auto peer = owner->peerLoaded(peerId)) {
			return peer;
		} else if (batchResolveDone(session)
			&& owner->chatsListLoaded(nullptr)) {
			return static_cast<PeerData*>(nullptr);
		}
		return std::nullopt;
	};
	if (const auto result = check()) {
		done(*result);
		return;
	}
	const auto generation = _generation;
	const auto finished = std::make_shared<bool>(false);
	rpl::merge(
		owner->chatsListChanges() | rpl::to_empty,
		owner->chatsListLoadedEvents() | rpl::to_empty,
		batch->doneEvents.events()
	) | rpl::on_next(crl::guard(this, [=] {
		if (*finished || _generation != generation || !_step) {
			return;
		}
		if (const auto result = check()) {
			*finished = true;
			done(*result);
		}
	}), _step->lifetime);
}

not_null<SavedWindows::BatchResolve*> SavedWindows::ensureBatchResolve(
		not_null<Main::Session*> session) {
	auto &batch = _batches[session];
	if (batch) {
		return batch.get();
	}
	batch = std::make_unique<BatchResolve>();
	const auto raw = batch.get();
	const auto owner = &session->data();
	const auto maybeSend = crl::guard(this, [=] {
		if (!raw->sent
			&& raw->sliceSeen
			&& session->data().contactsLoaded().current()) {
			sendBatchResolve(session);
		}
	});
	// raw->fallback.setCallback(crl::guard(this, [=] {
	// 	sendBatchResolve(session);
	// }));
	// raw->fallback.callOnce(kBatchResolveFallback);
	session->account().sessionChanges(
	) | rpl::take(1) | rpl::on_next([=](Main::Session *) {
		_batches.remove(session);
	}, _lifetime);
	if (owner->chatsListLoaded(nullptr)) {
		raw->sliceSeen = true;
	} else {
		rpl::merge(
			owner->chatsListChanges() | rpl::to_empty,
			owner->chatsListLoadedEvents() | rpl::to_empty
		) | rpl::on_next([=] {
			raw->sliceSeen = true;
			maybeSend();
		}, raw->lifetime);
	}
	owner->contactsLoaded().value(
	) | rpl::filter(
		rpl::mappers::_1
	) | rpl::take(1) | rpl::to_empty | rpl::on_next([=] {
		maybeSend();
	}, raw->lifetime);
	maybeSend();
	return raw;
}

bool SavedWindows::batchResolveDone(
		not_null<Main::Session*> session) const {
	const auto i = _batches.find(session.get());
	return (i == end(_batches)) || i->second->done;
}

void SavedWindows::sendBatchResolve(not_null<Main::Session*> session) {
	const auto i = _batches.find(session.get());
	if (i == end(_batches) || i->second->sent) {
		return;
	}
	const auto batch = i->second.get();
	batch->sent = true;
	batch->fallback.cancel();

	auto hashes = base::flat_map<PeerId, uint64>();
	const auto add = [&](const SavedChat &chat) {
		const auto push = [&](PeerId id, uint64 hash) {
			if (!id || session->data().peerLoaded(id)) {
				return;
			}
			auto &entry = hashes[id];
			if (hash != 0) {
				entry = hash;
			}
		};
		push(chat.peer, chat.accessHash);
		push(chat.monoforumPeer, chat.monoforumAccessHash);
	};
	const auto addWindow = [&](const SavedWindow &window) {
		if (sessionFor(window) != session) {
			return;
		}
		add(window.thread);
		for (const auto &chat : window.chats) {
			add(chat);
		}
	};
	if (_step) {
		addWindow(_step->data);
	}
	for (const auto &window : _toRestore) {
		addWindow(window);
	}
	for (const auto &[id, hash] : hashes) {
		if (peerIsUser(id)) {
			if (hash != 0) {
				batch->users.push_back(MTP_inputUser(
					MTP_long(peerToUser(id).bare),
					MTP_long(hash)));
			}
		} else if (peerIsChannel(id)) {
			if (hash != 0) {
				batch->channels.push_back(MTP_inputChannel(
					MTP_long(peerToChannel(id).bare),
					MTP_long(hash)));
			}
		} else if (peerIsChat(id)) {
			batch->chats.push_back(MTP_long(peerToChat(id).bare));
		}
	}
	sendNextBatchRequest(session);
}

void SavedWindows::sendNextBatchRequest(not_null<Main::Session*> session) {
	const auto i = _batches.find(session.get());
	if (i == end(_batches) || i->second->done) {
		return;
	}
	const auto batch = i->second.get();
	const auto next = crl::guard(this, [=] {
		sendNextBatchRequest(session);
	});
	const auto chatsDone = [=](const MTPmessages_Chats &result) {
		result.match([&](const auto &data) {
			session->data().processChats(data.vchats());
		});
		next();
	};
	const auto api = &session->api();
	if (!batch->users.isEmpty()) {
		api->request(MTPusers_GetUsers(
			MTP_vector<MTPInputUser>(base::take(batch->users))
		)).done([=](const MTPVector<MTPUser> &result) {
			session->data().processUsers(result);
			next();
		}).fail([=] {
			next();
		}).send();
	} else if (!batch->channels.isEmpty()) {
		api->request(MTPchannels_GetChannels(
			MTP_vector<MTPInputChannel>(base::take(batch->channels))
		)).done(chatsDone).fail([=] {
			next();
		}).send();
	} else if (!batch->chats.isEmpty()) {
		api->request(MTPmessages_GetChats(
			MTP_vector<MTPlong>(base::take(batch->chats))
		)).done(chatsDone).fail([=] {
			next();
		}).send();
	} else {
		batch->done = true;
		batch->doneEvents.fire({});
	}
}

void SavedWindows::createWindow(const Step &step) {
	const auto &data = step.data;
	const auto session = step.session;
	const auto windowThread = step.slots[0];
	auto id = SeparateId(nullptr);
	switch (data.type) {
	case SeparateType::Primary:
		id = SeparateId(not_null(&session->account()));
		break;
	case SeparateType::Archive:
		id = SeparateId(SeparateType::Archive, session);
		break;
	case SeparateType::Chat:
		if (windowThread) {
			id = SeparateId(SeparateType::Chat, windowThread);
		}
		break;
	case SeparateType::Forum:
		if (windowThread && windowThread->asForum()) {
			id = SeparateId(SeparateType::Forum, windowThread);
		}
		break;
	case SeparateType::Community:
		if (windowThread) {
			const auto channel = windowThread->peer()->asChannel();
			if (channel && channel->communityInfo()) {
				id = SeparateId(SeparateType::Community, windowThread);
			}
		}
		break;
	case SeparateType::SavedSublist:
		if (windowThread && windowThread->asSublist()) {
			id = SeparateId(SeparateType::SavedSublist, windowThread);
		}
		break;
	case SeparateType::SharedMedia:
		if (windowThread
			&& data.sharedMediaType >= 0
			&& data.sharedMediaType < Storage::kSharedMediaTypeCount) {
			id = SeparateId(
				windowThread,
				Storage::SharedMediaType(data.sharedMediaType));
		}
		break;
	}
	if (!id) {
		return;
	}
	const auto existed = (_app->separateWindowFor(id) != nullptr);
	auto showAtMsgId = MsgId(0);
	if (!data.chats.empty() && SameChat(data.chats.back(), data.thread)) {
		showAtMsgId = data.chats.back().msgId;
	}
	const auto validPosition = (data.position.w > 0)
		&& (data.position.h > 0);
	if (!existed && validPosition) {
		_restorePosition = data.position;
	}
	const auto window = _app->ensureSeparateWindowFor(id, showAtMsgId);
	_restorePosition = std::nullopt;
	if (!window) {
		return;
	}
	if (existed && validPosition) {
		window->widget()->applySavedPosition(data.position);
	} else if (!existed && data.position.maximized) {
		window->widget()->setWindowState(Qt::WindowMaximized);
	}
	if (ReplayableType(data.type)) {
		if (const auto controller = window->sessionController()) {
			replayChats(window, controller, step, windowThread);
		}
	}
}

void SavedWindows::replayChats(
		not_null<Controller*> window,
		not_null<SessionController*> controller,
		const Step &step,
		Data::Thread *windowThread) {
	struct Entry {
		not_null<Data::Thread*> thread;
		MsgId rootId;
		MsgId msgId;
		SavedChatSection section = SavedChatSection::Chat;
	};
	auto entries = std::vector<Entry>();
	const auto count = int(step.data.chats.size());
	for (auto i = 0; i != count; ++i) {
		const auto thread = step.slots[1 + i];
		if (!thread) {
			continue;
		}
		const auto &saved = step.data.chats[i];
		const auto section = saved.section;
		const auto rootId = thread->asHistory() ? saved.topicRootId : MsgId();
		const auto plain = (section == SavedChatSection::Chat) && !rootId;
		if (!entries.empty()
			&& entries.back().thread == thread
			&& entries.back().rootId == rootId
			&& entries.back().section == section) {
			continue;
		}
		if (plain) {
			if (const auto history = thread->asHistory()) {
				const auto other = _app->windowForShowingHistory(
					history->peer);
				if (other && other != window.get()) {
					continue;
				}
			}
		}
		entries.push_back({ thread, rootId, saved.msgId, section });
	}
	const auto alreadyShown = (step.data.type == SeparateType::Chat)
		|| (step.data.type == SeparateType::SavedSublist);
	if (entries.empty()
		|| (alreadyShown
			&& entries.size() == 1
			&& !entries.front().rootId
			&& entries.front().section == SavedChatSection::Chat
			&& entries.front().thread == windowThread)) {
		return;
	}
	const auto makeParams = [](SectionShow::Way way) {
		auto params = SectionShow(
			way,
			anim::type::instant,
			anim::activation::background);
		params.allowDuplicateInStack = true;
		return params;
	};
	if (window->id().hasChatsList()
		&& controller->activeChatCurrent()
		&& (!entries.front().thread->asHistory()
			|| entries.front().rootId)) {
		controller->clearSectionStack(
			makeParams(SectionShow::Way::ClearStack));
	}
	for (auto i = 0, size = int(entries.size()); i != size; ++i) {
		const auto params = makeParams(i
			? SectionShow::Way::Forward
			: SectionShow::Way::ClearStack);
		const auto thread = entries[i].thread;
		const auto msgId = entries[i].msgId
			? entries[i].msgId
			: ShowAtUnreadMsgId;
		if (entries[i].section == SavedChatSection::Pinned) {
			controller->showSection(
				std::make_shared<HistoryView::PinnedMemento>(
					thread,
					entries[i].msgId),
				params);
		} else if (entries[i].section == SavedChatSection::Scheduled) {
			if (const auto topic = thread->asTopic()) {
				controller->showSection(
					std::make_shared<HistoryView::ScheduledMemento>(topic),
					params);
			} else if (const auto history = thread->asHistory()) {
				controller->showSection(
					std::make_shared<HistoryView::ScheduledMemento>(history),
					params);
			}
		} else if (entries[i].section == SavedChatSection::AdminLog) {
			if (const auto channel = thread->peer()->asChannel()) {
				controller->showSection(
					std::make_shared<AdminLog::SectionMemento>(channel),
					params);
			}
		} else if (const auto topic = thread->asTopic()) {
			auto memento = std::make_shared<HistoryView::ChatMemento>(
				HistoryView::ChatViewId{
					.history = topic->history(),
					.repliesRootId = topic->rootId(),
				},
				msgId);
			memento->setFromTopic(topic);
			controller->showSection(std::move(memento), params);
		} else if (const auto rootId = entries[i].rootId) {
			auto memento = std::make_shared<HistoryView::ChatMemento>(
				HistoryView::ChatViewId{
					.history = thread->owningHistory(),
					.repliesRootId = rootId,
				},
				msgId);
			controller->showSection(std::move(memento), params);
		} else {
			controller->showThread(thread, msgId, params);
		}
	}
}

auto SavedWindows::restorePositionFor(SeparateId id)
-> std::optional<Core::WindowPosition> {
	if (auto position = base::take(_restorePosition)) {
		return position;
	} else if (_restoreFinished || !id) {
		return std::nullopt;
	}
	auto accountIndex = -1;
	for (const auto &entry : _app->domain().accounts()) {
		if (entry.account.get() == id.account) {
			accountIndex = entry.index;
			break;
		}
	}
	if (accountIndex < 0) {
		return std::nullopt;
	}
	const auto session = id.account->maybeSession();
	const auto userPeer = session ? session->userPeerId().value : 0;
	const auto thread = id.thread
		? SavedChatFromThread(id.thread)
		: SavedChat();
	const auto find = [&](const std::vector<SavedWindow> &list)
	-> std::optional<Core::WindowPosition> {
		for (const auto &window : list) {
			if (window.accountIndex == accountIndex
				&& (!window.userPeer
					|| !userPeer
					|| window.userPeer == userPeer)
				&& window.type == id.type
				&& window.sharedMediaType == int(id.sharedMediaType)
				&& SameChat(window.thread, thread)) {
				return (window.position.w > 0 && window.position.h > 0)
					? std::make_optional(window.position)
					: std::nullopt;
			}
		}
		return std::nullopt;
	};
	if (auto position = find(_toRestore)) {
		return position;
	}
	return find(_undecided);
}

} // namespace Window
