/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "storage/file_download.h"
#include "storage/localimageloader.h"

#include <QtCore/QTimer>

class History;

namespace Data {
class WallPaper;
class DocumentMedia;
} // namespace Data

namespace Lang {
struct Language;
} // namespace Lang

namespace Storage {
namespace details {
struct ReadSettingsContext;
} // namespace details
class EncryptionKey;
} // namespace Storage

namespace Window {
namespace Theme {
struct Object;
struct Saved;
} // namespace Theme
} // namespace Window

namespace Export {
struct Settings;
} // namespace Export

namespace MTP {
class AuthKey;
using AuthKeyPtr = std::shared_ptr<AuthKey>;
} // namespace MTP

namespace Local {

void start();
void sync();
void finish();

void writeSettings();
void rewriteSettingsIfNeeded();

void writeAutoupdatePrefix(const QString &prefix);
QString readAutoupdatePrefix();

void writeBackground(const Data::WallPaper &paper, const QImage &image);
bool readBackground();
void moveLegacyBackground(
	const QString &fromBasePath,
	const MTP::AuthKeyPtr &fromLocalKey,
	uint64 legacyBackgroundKeyDay,
	uint64 legacyBackgroundKeyNight);

void reset();

int32 oldSettingsVersion();

void countVoiceWaveform(not_null<Data::DocumentMedia*> media);

void cancelTask(TaskId id);

void writeTheme(const Window::Theme::Saved &saved);
void clearTheme();
[[nodiscard]] Window::Theme::Saved readThemeAfterSwitch();

[[nodiscard]] Window::Theme::Object ReadThemeContent();

void writeLangPack();
void pushRecentLanguage(const Lang::Language &language);
std::vector<Lang::Language> readRecentLanguages();
void saveRecentLanguages(const std::vector<Lang::Language> &list);
void removeRecentLanguage(const QString &id);
void incrementRecentHashtag(RecentHashtagPack &recent, const QString &tag);

bool readOldMtpData(
	bool remove,
	Storage::details::ReadSettingsContext &context);
bool readOldUserSettings(
	bool remove,
	Storage::details::ReadSettingsContext &context);

} // namespace Local
