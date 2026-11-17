/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_resources.h"

#include "twidget/resources.h"

#include <QtCore/QSaveFile>

namespace Tgw {
namespace {

enum class Status : uchar {
	Idle,
	Preparing,
	Ready,
	Failed,
};

struct Script {
	QString name;
	QByteArray source;
};

struct Package {
	Script sandbox;
	Script validator;
};

struct State {
	Status status = Status::Idle;
	std::shared_ptr<twidget::SandboxResources> resources;
	std::vector<Fn<void(std::shared_ptr<twidget::SandboxResources>)>> waiters;
	Fn<void()> stopped;
};

[[nodiscard]] State &Current() {
	static auto result = State();
	return result;
}

[[nodiscard]] QByteArray ReadAsset(const QString &name) {
	auto file = QFile(u":/tgw/"_q + name);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

[[nodiscard]] QByteArray ReadSource(const QString &name) {
	auto result = ReadAsset(name);
	result.replace("\r\n", "\n");
	return result;
}

[[nodiscard]] std::optional<Script> ReadScript(const QString &name) {
	auto result = Script{
		.name = name,
		.source = ReadSource(name + u".js"_q),
	};
	if (result.source.isEmpty()) {
		LOG(("Tgw Error: Missing packaged '%1.js'.").arg(name));
		return std::nullopt;
	}
	return result;
}

[[nodiscard]] std::optional<Package> ReadPackage() {
	auto sandbox = ReadScript(u"sandbox-bundle"_q);
	auto validator = ReadScript(u"validator-bundle"_q);
	if (!sandbox || !validator) {
		return std::nullopt;
	}
	return Package{
		.sandbox = std::move(*sandbox),
		.validator = std::move(*validator),
	};
}

[[nodiscard]] bool WriteAsset(const QString &path, const QByteArray &bytes) {
	auto file = QSaveFile(path);
	if (file.open(QIODevice::WriteOnly)
		&& (file.write(bytes) == bytes.size())
		&& file.commit()) {
		return true;
	}
	LOG(("Tgw Error: Could not write '%1'.").arg(path));
	return false;
}

[[nodiscard]] bool WriteScript(const QString &folder, const Script &script) {
	return WriteAsset(folder + '/' + script.name + u".js"_q, script.source);
}

[[nodiscard]] bool WritePackage(
		const QString &folder,
		const Package &package) {
	if (!QDir().mkpath(folder)) {
		LOG(("Tgw Error: Could not create '%1'.").arg(folder));
		return false;
	}
	return WriteScript(folder, package.sandbox)
		&& WriteScript(folder, package.validator);
}

[[nodiscard]] std::shared_ptr<twidget::SandboxResources> Load(
		const QString &folder) {
	try {
		return twidget::SandboxResources::fromDirectory(
			QFile::encodeName(folder).toStdString());
	} catch (const std::exception &e) {
		LOG(("Tgw Error: Could not load '%1': %2."
			).arg(folder, QString::fromUtf8(e.what())));
	}
	return nullptr;
}

[[nodiscard]] bool SameScript(
		const Script &script,
		const std::string &loaded) {
	return (size_t(script.source.size()) == loaded.size())
		&& !memcmp(script.source.constData(), loaded.data(), loaded.size());
}

[[nodiscard]] bool CheckLoaded(
		const twidget::SandboxResources &resources,
		const Package &package) {
	return SameScript(package.sandbox, resources.sandboxBundle)
		&& SameScript(package.validator, resources.validatorBundle);
}

[[nodiscard]] std::shared_ptr<twidget::SandboxResources> Prepare(
		const QString &folder) {
	const auto package = ReadPackage();
	if (!package || !WritePackage(folder, *package)) {
		return nullptr;
	}
	auto result = Load(folder);
	if (!result) {
		return nullptr;
	} else if (!CheckLoaded(*result, *package)) {
		LOG(("Tgw Error: Resources in '%1' differ from the package."
			).arg(folder));
		return nullptr;
	}
	return result;
}

void Finish(std::shared_ptr<twidget::SandboxResources> resources) {
	auto &state = Current();
	state.status = resources ? Status::Ready : Status::Failed;
	state.resources = resources;
	for (const auto &done : base::take(state.waiters)) {
		done(resources);
	}
	if (const auto stopped = base::take(state.stopped)) {
		stopped();
	}
}

} // namespace

void PrepareResources(
		Fn<void(std::shared_ptr<twidget::SandboxResources>)> done) {
	auto &state = Current();
	if (state.status == Status::Ready || state.status == Status::Failed) {
		const auto resources = state.resources;
		crl::on_main([done = std::move(done), resources] {
			done(resources);
		});
		return;
	}
	state.waiters.push_back(std::move(done));
	if (state.status == Status::Preparing) {
		return;
	}
	state.status = Status::Preparing;
	const auto folder = cWorkingDir() + u"tdata/tgw"_q;
	crl::async([=] {
		auto resources = Prepare(folder);
		crl::on_main([resources = std::move(resources)] {
			Finish(resources);
		});
	});
}

bool StopResources(Fn<void()> stopped) {
	auto &state = Current();
	state.waiters.clear();
	if (state.status != Status::Preparing) {
		return false;
	}
	state.stopped = std::move(stopped);
	return true;
}

bool ResourcesFailed() {
	return (Current().status == Status::Failed);
}

} // namespace Tgw
