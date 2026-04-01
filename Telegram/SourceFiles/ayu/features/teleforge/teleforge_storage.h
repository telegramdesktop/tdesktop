#pragma once

#include <optional>
#include <string>
#include <vector>

namespace TeleForge::Storage {

struct PersonalityCoreRecord {
	int singletonId = 1;
	double aggression = 5.;
	double brevity = 5.;
	double emojis = 5.;
	double toxicity = 5.;
	double creativity = 5.;
	std::string systemPrompt;
	std::string sourceDevice;
	int updatedAt = 0;
};

struct PerChatSettingsRecord {
	long long peerId = 0;
	bool aiAnswer = true;
	bool webAccess = false;
	bool calendarAccess = false;
	bool pcAgent = false;
	std::string directoryWhitelistJson = "[]";
	int updatedAt = 0;
};

struct SyncArtifactRecord {
	int id = 0;
	std::string artifactType;
	std::string artifactName;
	std::string payload;
	std::string deviceId;
	int updatedAt = 0;
};

void initialize();

std::optional<PersonalityCoreRecord> loadPersonalityCore();
void upsertPersonalityCore(const PersonalityCoreRecord &record);

std::optional<PerChatSettingsRecord> loadPerChatSettings(long long peerId);
void upsertPerChatSettings(const PerChatSettingsRecord &record);

std::vector<SyncArtifactRecord> loadSyncArtifacts(const std::string &artifactType);
void storeSyncArtifact(const SyncArtifactRecord &record);

} // namespace TeleForge::Storage
