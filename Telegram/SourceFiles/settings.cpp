/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings.h"

#include "ui/emoji_config.h"

namespace {

constexpr auto kRecentEmojiLimit = 42;

auto UpdatesRecentEmoji = rpl::event_stream<>();

} // namespace

Qt::LayoutDirection gLangDir = Qt::LeftToRight;

bool gInstallBetaVersion = AppBetaVersion;
uint64 gAlphaVersion = AppAlphaVersion;
uint64 gRealAlphaVersion = AppAlphaVersion;
QByteArray gAlphaPrivateKey;

bool gManyInstance = false;
QString gKeyFile;
QString gWorkingDir, gExeDir, gExeName;

QStringList gSendPaths;
QString gStartUrl;

QString gDialogLastPath, gDialogHelperPath; // optimize QFileDialog

bool gStartMinimized = false;
bool gStartInTray = false;
bool gAutoStart = false;
bool gSendToMenu = false;
bool gUseExternalVideoPlayer = false;
bool gUseFreeType = false;
bool gAutoUpdate = true;
LaunchMode gLaunchMode = LaunchModeNormal;
bool gSeenTrayTooltip = false;
bool gRestartingUpdate = false, gRestarting = false, gRestartingToSettings = false, gWriteProtected = false;
int32 gLastUpdateCheck = 0;
bool gNoStartUpdate = false;
bool gStartToSettings = false;
bool gDebugMode = false;

uint32 gConnectionsInSession = 1;

QByteArray gLocalSalt;
int gScreenScale = style::kScaleAuto;
int gConfigScale = style::kScaleAuto;

QString gTimeFormat = qsl("hh:mm");

RecentEmojiPack gRecentEmoji;
RecentEmojiPreload gRecentEmojiPreload;
EmojiColorVariants gEmojiVariants;

RecentStickerPreload gRecentStickersPreload;
RecentStickerPack gRecentStickers;

RecentHashtagPack gRecentWriteHashtags, gRecentSearchHashtags;

RecentInlineBots gRecentInlineBots;

bool gPasswordRecovered = false;
int32 gPasscodeBadTries = 0;
crl::time gPasscodeLastTry = 0;

float64 gRetinaFactor = 1.;
int32 gIntRetinaFactor = 1;

int gOtherOnline = 0;

int32 gAutoDownloadPhoto = 0; // all auto download
int32 gAutoDownloadAudio = 0;
int32 gAutoDownloadGif = 0;

RecentEmojiPack &GetRecentEmoji() {
	if (cRecentEmoji().isEmpty()) {
		RecentEmojiPack result;
		auto haveAlready = [&result](EmojiPtr emoji) {
			for (auto &row : result) {
				if (row.first->id() == emoji->id()) {
					return true;
				}
			}
			return false;
		};
		if (!cRecentEmojiPreload().isEmpty()) {
			auto preload = cRecentEmojiPreload();
			cSetRecentEmojiPreload(RecentEmojiPreload());
			result.reserve(preload.size());
			for (auto i = preload.cbegin(), e = preload.cend(); i != e; ++i) {
				if (auto emoji = Ui::Emoji::Find(i->first)) {
					if (!haveAlready(emoji)) {
						result.push_back(qMakePair(emoji, i->second));
					}
				}
			}
		}
		for (const auto emoji : Ui::Emoji::GetDefaultRecent()) {
			if (result.size() >= kRecentEmojiLimit) break;

			if (!haveAlready(emoji)) {
				result.push_back(qMakePair(emoji, 1));
			}
		}
		cSetRecentEmoji(result);
	}
	return cRefRecentEmoji();
}

EmojiPack GetRecentEmojiSection() {
	const auto &recent = GetRecentEmoji();

	auto result = EmojiPack();
	result.reserve(recent.size());
	for (const auto &item : recent) {
		result.push_back(item.first);
	}
	return result;
}

void AddRecentEmoji(EmojiPtr emoji) {
	auto &recent = GetRecentEmoji();
	auto i = recent.begin(), e = recent.end();
	for (; i != e; ++i) {
		if (i->first == emoji) {
			++i->second;
			if (i->second > 0x8000) {
				for (auto j = recent.begin(); j != e; ++j) {
					if (j->second > 1) {
						j->second /= 2;
					} else {
						j->second = 1;
					}
				}
			}
			for (; i != recent.begin(); --i) {
				if ((i - 1)->second > i->second) {
					break;
				}
				std::swap(*i, *(i - 1));
			}
			break;
		}
	}
	if (i == e) {
		while (recent.size() >= kRecentEmojiLimit) {
			recent.pop_back();
		}
		recent.push_back(qMakePair(emoji, 1));
		for (i = recent.end() - 1; i != recent.begin(); --i) {
			if ((i - 1)->second > i->second) {
				break;
			}
			std::swap(*i, *(i - 1));
		}
	}
	UpdatesRecentEmoji.fire({});
}

rpl::producer<> UpdatedRecentEmoji() {
	return UpdatesRecentEmoji.events();
}
