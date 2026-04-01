#pragma once

#include <optional>
#include <vector>

#include <QtCore/QDateTime>
#include <QtCore/QString>

namespace TeleForge {

struct PersonalityWeights {
	double aggression = 5.;
	double brevity = 5.;
	double emojis = 5.;
	double toxicity = 5.;
	double creativity = 5.;
};

struct PersonalityCore {
	PersonalityWeights weights;
	QString systemPrompt;
	QString sourceDevice;
	QDateTime updatedAt;
};

PersonalityCore DefaultPersonalityCore();
PersonalityCore BuildPersonalityCoreFromMessages(const std::vector<QString> &messages);

std::optional<PersonalityCore> LoadPersonalityCore();
void PersistPersonalityCore(const PersonalityCore &core);

QString SerializePersonalityCore(const PersonalityCore &core);
std::optional<PersonalityCore> ParsePersonalityCore(const QString &serialized);
bool ExportPersonalityCoreSnapshot(
	const PersonalityCore &core,
	const QString &path = QString());
QString DefaultSnapshotPath();

} // namespace TeleForge
