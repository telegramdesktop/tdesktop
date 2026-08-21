/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "core/core_settings.h"
#include "data/data_msg_id.h"
#include "data/data_peer_id.h"
#include "window/window_separate_id.h"

class PeerData;

namespace Core {
class Application;
} // namespace Core

namespace Data {
class Thread;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Window {

class Controller;
class SessionController;

enum class SavedChatSection {
	Chat = 0,
	Pinned = 1,
	Scheduled = 2,
	AdminLog = 3,
};

struct SavedChat {
	PeerId peer = 0;
	uint64 accessHash = 0;
	MsgId topicRootId = 0;
	PeerId monoforumPeer = 0;
	uint64 monoforumAccessHash = 0;
	MsgId msgId = 0;
	SavedChatSection section = SavedChatSection::Chat;

	[[nodiscard]] bool valid() const {
		return (peer != 0);
	}
};

[[nodiscard]] bool SameChat(const SavedChat &a, const SavedChat &b);
[[nodiscard]] SavedChat SavedChatFromThread(
	not_null<Data::Thread*> thread,
	MsgId msgId = 0);
[[nodiscard]] uint64 SavedAccessHash(not_null<PeerData*> peer);

struct SavedWindow {
	int accountIndex = -1;
	uint64 userPeer = 0;
	SeparateType type = SeparateType::Primary;
	int sharedMediaType = 0;
	SavedChat thread;
	Core::WindowPosition position;
	std::vector<SavedChat> chats;
};

class SavedWindows final : public base::has_weak_ptr {
public:
	explicit SavedWindows(not_null<Core::Application*> app);
	~SavedWindows();

	[[nodiscard]] bool restoreOnLaunch() const;
	void setRestoreOnLaunch(bool restore);

	void attachToWindow(not_null<Controller*> window);
	void scheduleSave();
	void writeNow();

	void startRestore();
	void windowActivated();
	void windowClosed(not_null<Controller*> window);
	bool reopenLastClosed();

	[[nodiscard]] auto restorePositionFor(SeparateId id)
	-> std::optional<Core::WindowPosition>;

private:
	struct Step;
	struct BatchResolve;

	void save();
	[[nodiscard]] QByteArray collect() const;
	[[nodiscard]] std::optional<SavedWindow> serializeWindow(
		not_null<Controller*> window) const;

	void maybeBeginRestore();
	void maybeOfferRestore();
	void hideOffer();
	[[nodiscard]] bool worthOffering() const;
	void markAsked(bool restore);
	void beginRestore();
	void discardRestore();
	void stashUndecided();
	void processNext();
	bool startStep(SavedWindow &&data);
	void queueFinishStep();
	void finishStep();
	void finishRestore();
	void createWindow(const Step &step);
	void replayChats(
		not_null<Controller*> window,
		not_null<SessionController*> controller,
		const Step &step,
		Data::Thread *windowThread);
	void resolveSlot(int index);
	void waitPeer(PeerId peerId, Fn<void(PeerData*)> done);

	[[nodiscard]] Main::Session *sessionFor(const SavedWindow &data) const;
	[[nodiscard]] not_null<BatchResolve*> ensureBatchResolve(
		not_null<Main::Session*> session);
	[[nodiscard]] bool batchResolveDone(
		not_null<Main::Session*> session) const;
	void sendBatchResolve(not_null<Main::Session*> session);
	void sendNextBatchRequest(not_null<Main::Session*> session);

	const not_null<Core::Application*> _app;
	base::Timer _saveTimer;

	std::vector<SavedWindow> _toRestore;
	std::vector<SavedWindow> _undecided;
	std::vector<SavedWindow> _closed;
	std::unique_ptr<Step> _step;
	base::flat_map<
		Main::Session*,
		std::unique_ptr<BatchResolve>> _batches;
	std::optional<Core::WindowPosition> _restorePosition;
	Fn<void()> _hideOffer;
	int _generation = 0;
	bool _domainReady = false;
	bool _offered = false;
	bool _announceOnFinish = false;
	bool _activatedOnce = false;
	bool _deferUntilActivated = false;
	bool _restoring = false;
	bool _restoreFinished = false;

	rpl::lifetime _lifetime;

};

} // namespace Window
