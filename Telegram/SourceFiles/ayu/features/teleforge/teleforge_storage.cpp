#include "ayu/features/teleforge/teleforge_storage.h"

#include "ayu/libs/sqlite/sqlite_orm.h"

namespace TeleForge::Storage {
namespace {

using namespace sqlite_orm;

[[nodiscard]] auto MakeStorage() {
	return make_storage(
		"./tdata/teleforge.db",
		make_table<PersonalityCoreRecord>(
			"PersonalityCore",
			make_column("singletonId", &PersonalityCoreRecord::singletonId, primary_key()),
			make_column("aggression", &PersonalityCoreRecord::aggression),
			make_column("brevity", &PersonalityCoreRecord::brevity),
			make_column("emojis", &PersonalityCoreRecord::emojis),
			make_column("toxicity", &PersonalityCoreRecord::toxicity),
			make_column("creativity", &PersonalityCoreRecord::creativity),
			make_column("systemPrompt", &PersonalityCoreRecord::systemPrompt),
			make_column("sourceDevice", &PersonalityCoreRecord::sourceDevice),
			make_column("updatedAt", &PersonalityCoreRecord::updatedAt)),
		make_table<PerChatSettingsRecord>(
			"PerChatSettings",
			make_column("peerId", &PerChatSettingsRecord::peerId, primary_key()),
			make_column("aiAnswer", &PerChatSettingsRecord::aiAnswer),
			make_column("webAccess", &PerChatSettingsRecord::webAccess),
			make_column("calendarAccess", &PerChatSettingsRecord::calendarAccess),
			make_column("pcAgent", &PerChatSettingsRecord::pcAgent),
			make_column("directoryWhitelistJson", &PerChatSettingsRecord::directoryWhitelistJson),
			make_column("updatedAt", &PerChatSettingsRecord::updatedAt)),
		make_table<SyncArtifactRecord>(
			"SyncArtifact",
			make_column("id", &SyncArtifactRecord::id, primary_key().autoincrement()),
			make_column("artifactType", &SyncArtifactRecord::artifactType),
			make_column("artifactName", &SyncArtifactRecord::artifactName),
			make_column("payload", &SyncArtifactRecord::payload),
			make_column("deviceId", &SyncArtifactRecord::deviceId),
			make_column("updatedAt", &SyncArtifactRecord::updatedAt)),
		make_index(
			"idx_sync_artifact_type_updated",
			column<SyncArtifactRecord>(&SyncArtifactRecord::artifactType),
			column<SyncArtifactRecord>(&SyncArtifactRecord::updatedAt)));
}

auto storage = MakeStorage();

} // namespace

void initialize() {
	try {
		storage.sync_schema(true);
	} catch (const std::exception &ex) {
		LOG(("TeleForge storage initialization failed: %1").arg(ex.what()));
	}
}

std::optional<PersonalityCoreRecord> loadPersonalityCore() {
	try {
		if (const auto record = storage.get_pointer<PersonalityCoreRecord>(1)) {
			return *record;
		}
		return std::nullopt;
	} catch (const std::exception &ex) {
		LOG(("TeleForge loadPersonalityCore failed: %1").arg(ex.what()));
		return std::nullopt;
	}
}

void upsertPersonalityCore(const PersonalityCoreRecord &record) {
	try {
		storage.replace(record);
	} catch (const std::exception &ex) {
		LOG(("TeleForge upsertPersonalityCore failed: %1").arg(ex.what()));
	}
}

std::optional<PerChatSettingsRecord> loadPerChatSettings(long long peerId) {
	try {
		if (const auto record = storage.get_pointer<PerChatSettingsRecord>(peerId)) {
			return *record;
		}
		return std::nullopt;
	} catch (const std::exception &ex) {
		LOG(("TeleForge loadPerChatSettings failed: %1").arg(ex.what()));
		return std::nullopt;
	}
}

void upsertPerChatSettings(const PerChatSettingsRecord &record) {
	try {
		storage.replace(record);
	} catch (const std::exception &ex) {
		LOG(("TeleForge upsertPerChatSettings failed: %1").arg(ex.what()));
	}
}

std::vector<SyncArtifactRecord> loadSyncArtifacts(const std::string &artifactType) {
	try {
		return storage.get_all<SyncArtifactRecord>(
			where(column<SyncArtifactRecord>(&SyncArtifactRecord::artifactType) == artifactType),
			order_by(column<SyncArtifactRecord>(&SyncArtifactRecord::updatedAt)).desc());
	} catch (const std::exception &ex) {
		LOG(("TeleForge loadSyncArtifacts failed: %1").arg(ex.what()));
		return {};
	}
}

void storeSyncArtifact(const SyncArtifactRecord &record) {
	try {
		auto stored = record;
		const auto latest = storage.select(
			max(&SyncArtifactRecord::id));
		stored.id = latest.empty() || !latest.front()
			? 1
			: (*latest.front() + 1);
		storage.replace(stored);
	} catch (const std::exception &ex) {
		LOG(("TeleForge storeSyncArtifact failed: %1").arg(ex.what()));
	}
}

} // namespace TeleForge::Storage
