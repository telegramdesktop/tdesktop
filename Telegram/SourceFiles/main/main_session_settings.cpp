/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "main/main_session_settings.h"

#include "chat_helpers/tabbed_selector.h"
#include "ui/widgets/input_fields.h"
#include "ui/chat/attach/attach_send_files_way.h"
#include "window/section_widget.h"
#include "support/support_common.h"
#include "storage/serialize_common.h"
#include "boxes/send_files_box.h"
#include "core/application.h"
#include "core/core_settings.h"

namespace Main {
namespace {

constexpr auto kLegacyCallsPeerToPeerNobody = 4;
constexpr auto kVersionTag = -1;
constexpr auto kVersion = 2;
constexpr auto kMaxSavedPlaybackPositions = 16;

} // namespace

SessionSettings::SessionSettings()
: _selectorTab(ChatHelpers::SelectorTab::Emoji)
, _supportSwitch(Support::SwitchSettings::Next) {
}

QByteArray SessionSettings::serialize() const {
	const auto autoDownload = _autoDownload.serialize();
	auto size = sizeof(qint32) * 38;
	size += _groupStickersSectionHidden.size() * sizeof(quint64);
	size += _mediaLastPlaybackPosition.size() * 2 * sizeof(quint64);
	size += Serialize::bytearraySize(autoDownload);
	size += sizeof(qint32) + _hiddenPinnedMessages.size() * (sizeof(quint64) + sizeof(qint32));

	auto result = QByteArray();
	result.reserve(size);
	{
		QDataStream stream(&result, QIODevice::WriteOnly);
		stream.setVersion(QDataStream::Qt_5_1);
		stream << qint32(kVersionTag) << qint32(kVersion);
		stream << static_cast<qint32>(_selectorTab);
		stream << qint32(_groupStickersSectionHidden.size());
		for (auto peerId : _groupStickersSectionHidden) {
			stream << quint64(peerId);
		}
		stream << qint32(_supportSwitch);
		stream << qint32(_supportFixChatsOrder ? 1 : 0);
		stream << qint32(_supportTemplatesAutocomplete ? 1 : 0);
		stream << qint32(_supportChatsTimeSlice.current());
		stream << autoDownload;
		stream << qint32(_supportAllSearchResults.current() ? 1 : 0);
		stream << qint32(_archiveCollapsed.current() ? 1 : 0);
		stream << qint32(_archiveInMainMenu.current() ? 1 : 0);
		stream << qint32(_skipArchiveInSearch.current() ? 1 : 0);
		stream << qint32(_mediaLastPlaybackPosition.size());
		for (const auto &[id, time] : _mediaLastPlaybackPosition) {
			stream << quint64(id) << qint64(time);
		}
		stream << qint32(_hiddenPinnedMessages.size());
		for (const auto &[key, value] : _hiddenPinnedMessages) {
			stream << quint64(key) << qint32(value);
		}
		stream << qint32(_dialogsFiltersEnabled ? 1 : 0);
		stream << qint32(_supportAllSilent ? 1 : 0);
	}
	return result;
}

void SessionSettings::addFromSerialized(const QByteArray &serialized) {
	if (serialized.isEmpty()) {
		return;
	}

	auto &app = Core::App().settings();

	QDataStream stream(serialized);
	stream.setVersion(QDataStream::Qt_5_1);
	qint32 versionTag = 0;
	qint32 version = 0;
	qint32 selectorTab = static_cast<qint32>(ChatHelpers::SelectorTab::Emoji);
	qint32 appLastSeenWarningSeen = app.lastSeenWarningSeen() ? 1 : 0;
	qint32 appTabbedSelectorSectionEnabled = 1;
	qint32 legacyTabbedSelectorSectionTooltipShown = 0;
	qint32 appFloatPlayerColumn = static_cast<qint32>(Window::Column::Second);
	qint32 appFloatPlayerCorner = static_cast<qint32>(RectPart::TopRight);
	base::flat_map<QString, QString> appSoundOverrides;
	base::flat_set<PeerId> groupStickersSectionHidden;
	qint32 appThirdSectionInfoEnabled = 0;
	qint32 legacySmallDialogsList = 0;
	float64 appDialogsWidthRatio = app.dialogsWidthRatio();
	int appThirdColumnWidth = app.thirdColumnWidth();
	int appThirdSectionExtendedBy = app.thirdSectionExtendedBy();
	qint32 appSendFilesWay = app.sendFilesWay().serialize();
	qint32 legacyCallsPeerToPeer = qint32(0);
	qint32 appSendSubmitWay = static_cast<qint32>(app.sendSubmitWay());
	qint32 supportSwitch = static_cast<qint32>(_supportSwitch);
	qint32 supportFixChatsOrder = _supportFixChatsOrder ? 1 : 0;
	qint32 supportTemplatesAutocomplete = _supportTemplatesAutocomplete ? 1 : 0;
	qint32 supportChatsTimeSlice = _supportChatsTimeSlice.current();
	qint32 appIncludeMutedCounter = app.includeMutedCounter() ? 1 : 0;
	qint32 appCountUnreadMessages = app.countUnreadMessages() ? 1 : 0;
	qint32 appExeLaunchWarning = app.exeLaunchWarning() ? 1 : 0;
	QByteArray autoDownload;
	qint32 supportAllSearchResults = _supportAllSearchResults.current() ? 1 : 0;
	qint32 archiveCollapsed = _archiveCollapsed.current() ? 1 : 0;
	qint32 appNotifyAboutPinned = app.notifyAboutPinned() ? 1 : 0;
	qint32 archiveInMainMenu = _archiveInMainMenu.current() ? 1 : 0;
	qint32 skipArchiveInSearch = _skipArchiveInSearch.current() ? 1 : 0;
	qint32 legacyAutoplayGifs = 1;
	qint32 appLoopAnimatedStickers = app.loopAnimatedStickers() ? 1 : 0;
	qint32 appLargeEmoji = app.largeEmoji() ? 1 : 0;
	qint32 appReplaceEmoji = app.replaceEmoji() ? 1 : 0;
	qint32 appSuggestEmoji = app.suggestEmoji() ? 1 : 0;
	qint32 appSuggestStickersByEmoji = app.suggestStickersByEmoji() ? 1 : 0;
	qint32 appSpellcheckerEnabled = app.spellcheckerEnabled() ? 1 : 0;
	std::vector<std::pair<DocumentId, crl::time>> mediaLastPlaybackPosition;
	qint32 appVideoPlaybackSpeed = Core::Settings::SerializePlaybackSpeed(app.videoPlaybackSpeed());
	QByteArray appVideoPipGeometry = app.videoPipGeometry();
	std::vector<int> appDictionariesEnabled;
	qint32 appAutoDownloadDictionaries = app.autoDownloadDictionaries() ? 1 : 0;
	base::flat_map<PeerId, MsgId> hiddenPinnedMessages;
	qint32 dialogsFiltersEnabled = _dialogsFiltersEnabled ? 1 : 0;
	qint32 supportAllSilent = _supportAllSilent ? 1 : 0;

	stream >> versionTag;
	if (versionTag == kVersionTag) {
		stream >> version;
		stream >> selectorTab;
	} else {
		selectorTab = versionTag;
	}
	if (version < 2) {
		stream >> appLastSeenWarningSeen;
		if (!stream.atEnd()) {
			stream >> appTabbedSelectorSectionEnabled;
		}
		if (!stream.atEnd()) {
			auto count = qint32(0);
			stream >> count;
			if (stream.status() == QDataStream::Ok) {
				for (auto i = 0; i != count; ++i) {
					QString key, value;
					stream >> key >> value;
					if (stream.status() != QDataStream::Ok) {
						LOG(("App Error: "
							"Bad data for SessionSettings::addFromSerialized()"));
						return;
					}
					appSoundOverrides.emplace(key, value);
				}
			}
		}
		if (!stream.atEnd()) {
			stream >> legacyTabbedSelectorSectionTooltipShown;
		}
		if (!stream.atEnd()) {
			stream >> appFloatPlayerColumn >> appFloatPlayerCorner;
		}
	}
	if (!stream.atEnd()) {
		auto count = qint32(0);
		stream >> count;
		if (stream.status() == QDataStream::Ok) {
			for (auto i = 0; i != count; ++i) {
				quint64 peerId;
				stream >> peerId;
				if (stream.status() != QDataStream::Ok) {
					LOG(("App Error: "
						"Bad data for SessionSettings::addFromSerialized()"));
					return;
				}
				groupStickersSectionHidden.insert(peerId);
			}
		}
	}
	if (version < 2) {
		if (!stream.atEnd()) {
			stream >> appThirdSectionInfoEnabled;
			stream >> legacySmallDialogsList;
		}
		if (!stream.atEnd()) {
			qint32 value = 0;
			stream >> value;
			appDialogsWidthRatio = snap(value / 1000000., 0., 1.);

			stream >> value;
			appThirdColumnWidth = value;

			stream >> value;
			appThirdSectionExtendedBy = value;
		}
		if (!stream.atEnd()) {
			stream >> appSendFilesWay;
		}
		if (!stream.atEnd()) {
			stream >> legacyCallsPeerToPeer;
		}
	}
	if (!stream.atEnd()) {
		if (version < 2) {
			stream >> appSendSubmitWay;
		}
		stream >> supportSwitch;
		stream >> supportFixChatsOrder;
	}
	if (!stream.atEnd()) {
		stream >> supportTemplatesAutocomplete;
	}
	if (!stream.atEnd()) {
		stream >> supportChatsTimeSlice;
	}
	if (version < 2) {
		if (!stream.atEnd()) {
			stream >> appIncludeMutedCounter;
			stream >> appCountUnreadMessages;
		}
		if (!stream.atEnd()) {
			stream >> appExeLaunchWarning;
		}
	}
	if (!stream.atEnd()) {
		stream >> autoDownload;
	}
	if (!stream.atEnd()) {
		stream >> supportAllSearchResults;
	}
	if (!stream.atEnd()) {
		stream >> archiveCollapsed;
	}
	if (version < 2) {
		if (!stream.atEnd()) {
			stream >> appNotifyAboutPinned;
		}
	}
	if (!stream.atEnd()) {
		stream >> archiveInMainMenu;
	}
	if (!stream.atEnd()) {
		stream >> skipArchiveInSearch;
	}
	if (version < 2) {
		if (!stream.atEnd()) {
			stream >> legacyAutoplayGifs;
			stream >> appLoopAnimatedStickers;
			stream >> appLargeEmoji;
			stream >> appReplaceEmoji;
			stream >> appSuggestEmoji;
			stream >> appSuggestStickersByEmoji;
		}
		if (!stream.atEnd()) {
			stream >> appSpellcheckerEnabled;
		}
	}
	if (!stream.atEnd()) {
		auto count = qint32(0);
		stream >> count;
		if (stream.status() == QDataStream::Ok) {
			for (auto i = 0; i != count; ++i) {
				quint64 documentId;
				qint64 time;
				stream >> documentId >> time;
				if (stream.status() != QDataStream::Ok) {
					LOG(("App Error: "
						"Bad data for SessionSettings::addFromSerialized()"));
					return;
				}
				mediaLastPlaybackPosition.emplace_back(documentId, time);
			}
		}
	}
	if (version < 2) {
		if (!stream.atEnd()) {
			stream >> appVideoPlaybackSpeed;
		}
		if (!stream.atEnd()) {
			stream >> appVideoPipGeometry;
		}
		if (!stream.atEnd()) {
			auto count = qint32(0);
			stream >> count;
			if (stream.status() == QDataStream::Ok) {
				for (auto i = 0; i != count; ++i) {
					qint64 langId;
					stream >> langId;
					if (stream.status() != QDataStream::Ok) {
						LOG(("App Error: "
							"Bad data for SessionSettings::addFromSerialized()"));
						return;
					}
					appDictionariesEnabled.emplace_back(langId);
				}
			}
		}
		if (!stream.atEnd()) {
			stream >> appAutoDownloadDictionaries;
		}
	}
	if (!stream.atEnd()) {
		auto count = qint32(0);
		stream >> count;
		if (stream.status() == QDataStream::Ok) {
			for (auto i = 0; i != count; ++i) {
				auto key = quint64();
				auto value = qint32();
				stream >> key >> value;
				if (stream.status() != QDataStream::Ok) {
					LOG(("App Error: "
						"Bad data for SessionSettings::addFromSerialized()"));
					return;
				}
				hiddenPinnedMessages.emplace(key, value);
			}
		}
	}
	if (!stream.atEnd()) {
		stream >> dialogsFiltersEnabled;
	}
	if (!stream.atEnd()) {
		stream >> supportAllSilent;
	}
	if (stream.status() != QDataStream::Ok) {
		LOG(("App Error: "
			"Bad data for SessionSettings::addFromSerialized()"));
		return;
	}
	if (!autoDownload.isEmpty()
		&& !_autoDownload.setFromSerialized(autoDownload)) {
		return;
	}
	if (!version) {
		if (!legacyAutoplayGifs) {
			using namespace Data::AutoDownload;
			_autoDownload = WithDisabledAutoPlay(_autoDownload);
		}
	}

	auto uncheckedTab = static_cast<ChatHelpers::SelectorTab>(selectorTab);
	switch (uncheckedTab) {
	case ChatHelpers::SelectorTab::Emoji:
	case ChatHelpers::SelectorTab::Stickers:
	case ChatHelpers::SelectorTab::Gifs: _selectorTab = uncheckedTab; break;
	}
	_groupStickersSectionHidden = std::move(groupStickersSectionHidden);
	auto uncheckedSupportSwitch = static_cast<Support::SwitchSettings>(
		supportSwitch);
	switch (uncheckedSupportSwitch) {
	case Support::SwitchSettings::None:
	case Support::SwitchSettings::Next:
	case Support::SwitchSettings::Previous: _supportSwitch = uncheckedSupportSwitch; break;
	}
	_supportFixChatsOrder = (supportFixChatsOrder == 1);
	_supportTemplatesAutocomplete = (supportTemplatesAutocomplete == 1);
	_supportChatsTimeSlice = supportChatsTimeSlice;
	_hadLegacyCallsPeerToPeerNobody = (legacyCallsPeerToPeer == kLegacyCallsPeerToPeerNobody);
	_supportAllSearchResults = (supportAllSearchResults == 1);
	_archiveCollapsed = (archiveCollapsed == 1);
	_archiveInMainMenu = (archiveInMainMenu == 1);
	_skipArchiveInSearch = (skipArchiveInSearch == 1);
	_mediaLastPlaybackPosition = std::move(mediaLastPlaybackPosition);
	_hiddenPinnedMessages = std::move(hiddenPinnedMessages);
	_dialogsFiltersEnabled = (dialogsFiltersEnabled == 1);
	_supportAllSilent = (supportAllSilent == 1);

	if (version < 2) {
		app.setLastSeenWarningSeen(appLastSeenWarningSeen == 1);
		for (const auto &[key, value] : appSoundOverrides) {
			app.setSoundOverride(key, value);
		}
		if (const auto sendFilesWay = Ui::SendFilesWay::FromSerialized(appSendFilesWay)) {
			app.setSendFilesWay(*sendFilesWay);
		}
		auto uncheckedSendSubmitWay = static_cast<Ui::InputSubmitSettings>(
			appSendSubmitWay);
		switch (uncheckedSendSubmitWay) {
		case Ui::InputSubmitSettings::Enter:
		case Ui::InputSubmitSettings::CtrlEnter: app.setSendSubmitWay(uncheckedSendSubmitWay); break;
		}
		app.setIncludeMutedCounter(appIncludeMutedCounter == 1);
		app.setCountUnreadMessages(appCountUnreadMessages == 1);
		app.setExeLaunchWarning(appExeLaunchWarning == 1);
		app.setNotifyAboutPinned(appNotifyAboutPinned == 1);
		app.setLoopAnimatedStickers(appLoopAnimatedStickers == 1);
		app.setLargeEmoji(appLargeEmoji == 1);
		app.setReplaceEmoji(appReplaceEmoji == 1);
		app.setSuggestEmoji(appSuggestEmoji == 1);
		app.setSuggestStickersByEmoji(appSuggestStickersByEmoji == 1);
		app.setSpellcheckerEnabled(appSpellcheckerEnabled == 1);
		app.setVideoPlaybackSpeed(Core::Settings::DeserializePlaybackSpeed(appVideoPlaybackSpeed));
		app.setVideoPipGeometry(appVideoPipGeometry);
		app.setDictionariesEnabled(std::move(appDictionariesEnabled));
		app.setAutoDownloadDictionaries(appAutoDownloadDictionaries == 1);
		app.setTabbedSelectorSectionEnabled(appTabbedSelectorSectionEnabled == 1);
		auto uncheckedColumn = static_cast<Window::Column>(appFloatPlayerColumn);
		switch (uncheckedColumn) {
		case Window::Column::First:
		case Window::Column::Second:
		case Window::Column::Third: app.setFloatPlayerColumn(uncheckedColumn); break;
		}
		auto uncheckedCorner = static_cast<RectPart>(appFloatPlayerCorner);
		switch (uncheckedCorner) {
		case RectPart::TopLeft:
		case RectPart::TopRight:
		case RectPart::BottomLeft:
		case RectPart::BottomRight: app.setFloatPlayerCorner(uncheckedCorner); break;
		}
		app.setThirdSectionInfoEnabled(appThirdSectionInfoEnabled);
		app.setDialogsWidthRatio(appDialogsWidthRatio);
		app.setThirdColumnWidth(appThirdColumnWidth);
		app.setThirdSectionExtendedBy(appThirdSectionExtendedBy);
	}
}

void SessionSettings::setSupportChatsTimeSlice(int slice) {
	_supportChatsTimeSlice = slice;
}

int SessionSettings::supportChatsTimeSlice() const {
	return _supportChatsTimeSlice.current();
}

rpl::producer<int> SessionSettings::supportChatsTimeSliceValue() const {
	return _supportChatsTimeSlice.value();
}

void SessionSettings::setSupportAllSearchResults(bool all) {
	_supportAllSearchResults = all;
}

bool SessionSettings::supportAllSearchResults() const {
	return _supportAllSearchResults.current();
}

rpl::producer<bool> SessionSettings::supportAllSearchResultsValue() const {
	return _supportAllSearchResults.value();
}

void SessionSettings::setMediaLastPlaybackPosition(DocumentId id, crl::time time) {
	auto &map = _mediaLastPlaybackPosition;
	const auto i = ranges::find(
		map,
		id,
		&std::pair<DocumentId, crl::time>::first);
	if (i != map.end()) {
		if (time > 0) {
			i->second = time;
		} else {
			map.erase(i);
		}
	} else if (time > 0) {
		if (map.size() >= kMaxSavedPlaybackPositions) {
			map.erase(map.begin());
		}
		map.emplace_back(id, time);
	}
}

crl::time SessionSettings::mediaLastPlaybackPosition(DocumentId id) const {
	const auto i = ranges::find(
		_mediaLastPlaybackPosition,
		id,
		&std::pair<DocumentId, crl::time>::first);
	return (i != _mediaLastPlaybackPosition.end()) ? i->second : 0;
}

void SessionSettings::setArchiveCollapsed(bool collapsed) {
	_archiveCollapsed = collapsed;
}

bool SessionSettings::archiveCollapsed() const {
	return _archiveCollapsed.current();
}

rpl::producer<bool> SessionSettings::archiveCollapsedChanges() const {
	return _archiveCollapsed.changes();
}

void SessionSettings::setArchiveInMainMenu(bool inMainMenu) {
	_archiveInMainMenu = inMainMenu;
}

bool SessionSettings::archiveInMainMenu() const {
	return _archiveInMainMenu.current();
}

rpl::producer<bool> SessionSettings::archiveInMainMenuChanges() const {
	return _archiveInMainMenu.changes();
}

void SessionSettings::setSkipArchiveInSearch(bool skip) {
	_skipArchiveInSearch = skip;
}

bool SessionSettings::skipArchiveInSearch() const {
	return _skipArchiveInSearch.current();
}

rpl::producer<bool> SessionSettings::skipArchiveInSearchChanges() const {
	return _skipArchiveInSearch.changes();
}

} // namespace Main
