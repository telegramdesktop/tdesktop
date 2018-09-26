/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace MTP {
class Instance;
} // namespace MTP

namespace Core {

bool UpdaterDisabled();
void SetUpdaterDisabledAtStartup();

class Updater;

class UpdateChecker {
public:
	enum class State {
		None,
		Download,
		Ready,
	};
	struct Progress {
		int64 already;
		int64 size;
	};

	UpdateChecker();

	rpl::producer<> checking() const;
	rpl::producer<> isLatest() const;
	rpl::producer<Progress> progress() const;
	rpl::producer<> failed() const;
	rpl::producer<> ready() const;

	void start(bool forceWait = false);
	void stop();
	void test();

	void setMtproto(const QPointer<MTP::Instance> &mtproto);

	State state() const;
	int already() const;
	int size() const;

private:
	const std::shared_ptr<Updater> _updater;

};

bool checkReadyUpdate();
void UpdateApplication();
QString countAlphaVersionSignature(uint64 version);

} // namespace Core
