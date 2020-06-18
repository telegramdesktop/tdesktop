/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "main/main_session_settings.h"

#include "chat_helpers/tabbed_selector.h"
#include "window/section_widget.h"
#include "ui/widgets/input_fields.h"
#include "support/support_common.h"
#include "storage/serialize_common.h"
#include "boxes/send_files_box.h"
#include "base/platform/base_platform_info.h"

namespace Main {
namespace {

constexpr auto kLegacyCallsPeerToPeerNobody = 4;
constexpr auto kVersionTag = -1;
constexpr auto kVersion = 1;
constexpr auto kMaxSavedPlaybackPositions = 16;

[[nodiscard]] qint32 SerializePlaybackSpeed(float64 speed) {
	return int(std::round(std::clamp(speed * 4., 2., 8.))) - 2;
}

float64 DeserializePlaybackSpeed(qint32 speed) {
	return (std::clamp(speed, 0, 6) + 2) / 4.;
}

} // namespace

SessionSettings::Variables::Variables()
: sendFilesWay(SendFilesWay::Album)
, selectorTab(ChatHelpers::SelectorTab::Emoji)
, floatPlayerColumn(Window::Column::Second)
, floatPlayerCorner(RectPart::TopRight)
, dialogsWidthRatio(ThirdColumnByDefault()
	? kDefaultBigDialogsWidthRatio
	: kDefaultDialogsWidthRatio)
, sendSubmitWay(Ui::InputSubmitSettings::Enter)
, supportSwitch(Support::SwitchSettings::Next) {
}

bool SessionSettings::ThirdColumnByDefault() {
	return Platform::IsMacStoreBuild();
}

QByteArray SessionSettings::serialize() const {
	const auto autoDownload = _variables.autoDownload.serialize();
	auto size = sizeof(qint32) * 38;
	for (const auto &[key, value] : _variables.soundOverrides) {
		size += Serialize::stringSize(key) + Serialize::stringSize(value);
	}
	size += _variables.groupStickersSectionHidden.size() * sizeof(quint64);
	size += _variables.mediaLastPlaybackPosition.size() * 2 * sizeof(quint64);
	size += Serialize::bytearraySize(autoDownload);
	size += Serialize::bytearraySize(_variables.videoPipGeometry);
	size += sizeof(qint32) + _variables.hiddenPinnedMessages.size() * (sizeof(quint64) + sizeof(qint32));

	auto result = QByteArray();
	result.reserve(size);
	{
		QDataStream stream(&result, QIODevice::WriteOnly);
		stream.setVersion(QDataStream::Qt_5_1);
		stream << qint32(kVersionTag) << qint32(kVersion);
		stream << static_cast<qint32>(_variables.selectorTab);
		stream << qint32(_variables.lastSeenWarningSeen ? 1 : 0);
		stream << qint32(_variables.tabbedSelectorSectionEnabled ? 1 : 0);
		stream << qint32(_variables.soundOverrides.size());
		for (const auto &[key, value] : _variables.soundOverrides) {
			stream << key << value;
		}
		stream << qint32(_variables.tabbedSelectorSectionTooltipShown);
		stream << qint32(_variables.floatPlayerColumn);
		stream << qint32(_variables.floatPlayerCorner);
		stream << qint32(_variables.groupStickersSectionHidden.size());
		for (auto peerId : _variables.groupStickersSectionHidden) {
			stream << quint64(peerId);
		}
		stream << qint32(_variables.thirdSectionInfoEnabled ? 1 : 0);
		stream << qint32(_variables.smallDialogsList ? 1 : 0);
		stream << qint32(snap(
			qRound(_variables.dialogsWidthRatio.current() * 1000000),
			0,
			1000000));
		stream << qint32(_variables.thirdColumnWidth.current());
		stream << qint32(_variables.thirdSectionExtendedBy);
		stream << qint32(_variables.sendFilesWay);
		stream << qint32(0);// LEGACY _variables.callsPeerToPeer.current());
		stream << qint32(_variables.sendSubmitWay);
		stream << qint32(_variables.supportSwitch);
		stream << qint32(_variables.supportFixChatsOrder ? 1 : 0);
		stream << qint32(_variables.supportTemplatesAutocomplete ? 1 : 0);
		stream << qint32(_variables.supportChatsTimeSlice.current());
		stream << qint32(_variables.includeMutedCounter ? 1 : 0);
		stream << qint32(_variables.countUnreadMessages ? 1 : 0);
		stream << qint32(_variables.exeLaunchWarning ? 1 : 0);
		stream << autoDownload;
		stream << qint32(_variables.supportAllSearchResults.current() ? 1 : 0);
		stream << qint32(_variables.archiveCollapsed.current() ? 1 : 0);
		stream << qint32(_variables.notifyAboutPinned.current() ? 1 : 0);
		stream << qint32(_variables.archiveInMainMenu.current() ? 1 : 0);
		stream << qint32(_variables.skipArchiveInSearch.current() ? 1 : 0);
		stream << qint32(0);// LEGACY _variables.autoplayGifs ? 1 : 0);
		stream << qint32(_variables.loopAnimatedStickers ? 1 : 0);
		stream << qint32(_variables.largeEmoji.current() ? 1 : 0);
		stream << qint32(_variables.replaceEmoji.current() ? 1 : 0);
		stream << qint32(_variables.suggestEmoji ? 1 : 0);
		stream << qint32(_variables.suggestStickersByEmoji ? 1 : 0);
		stream << qint32(_variables.spellcheckerEnabled.current() ? 1 : 0);
		stream << qint32(_variables.mediaLastPlaybackPosition.size());
		for (const auto &[id, time] : _variables.mediaLastPlaybackPosition) {
			stream << quint64(id) << qint64(time);
		}
		stream << qint32(SerializePlaybackSpeed(_variables.videoPlaybackSpeed.current()));
		stream << _variables.videoPipGeometry;
		stream << qint32(_variables.dictionariesEnabled.current().size());
		for (const auto i : _variables.dictionariesEnabled.current()) {
			stream << quint64(i);
		}
		stream << qint32(_variables.autoDownloadDictionaries.current() ? 1 : 0);
		stream << qint32(_variables.hiddenPinnedMessages.size());
		for (const auto &[key, value] : _variables.hiddenPinnedMessages) {
			stream << quint64(key) << qint32(value);
		}
	}
	return result;
}

std::unique_ptr<SessionSettings> SessionSettings::FromSerialized(
		const QByteArray &serialized) {
	if (serialized.isEmpty()) {
		return nullptr;
	}

	auto result = std::make_unique<SessionSettings>();
	const auto variables = &result->_variables;

	QDataStream stream(serialized);
	stream.setVersion(QDataStream::Qt_5_1);
	qint32 versionTag = 0;
	qint32 version = 0;
	qint32 selectorTab = static_cast<qint32>(ChatHelpers::SelectorTab::Emoji);
	qint32 lastSeenWarningSeen = 0;
	qint32 tabbedSelectorSectionEnabled = 1;
	qint32 tabbedSelectorSectionTooltipShown = 0;
	qint32 floatPlayerColumn = static_cast<qint32>(Window::Column::Second);
	qint32 floatPlayerCorner = static_cast<qint32>(RectPart::TopRight);
	base::flat_map<QString, QString> soundOverrides;
	base::flat_set<PeerId> groupStickersSectionHidden;
	qint32 thirdSectionInfoEnabled = 0;
	qint32 smallDialogsList = 0;
	float64 dialogsWidthRatio = variables->dialogsWidthRatio.current();
	int thirdColumnWidth = variables->thirdColumnWidth.current();
	int thirdSectionExtendedBy = variables->thirdSectionExtendedBy;
	qint32 sendFilesWay = static_cast<qint32>(variables->sendFilesWay);
	qint32 legacyCallsPeerToPeer = qint32(0);
	qint32 sendSubmitWay = static_cast<qint32>(variables->sendSubmitWay);
	qint32 supportSwitch = static_cast<qint32>(variables->supportSwitch);
	qint32 supportFixChatsOrder = variables->supportFixChatsOrder ? 1 : 0;
	qint32 supportTemplatesAutocomplete = variables->supportTemplatesAutocomplete ? 1 : 0;
	qint32 supportChatsTimeSlice = variables->supportChatsTimeSlice.current();
	qint32 includeMutedCounter = variables->includeMutedCounter ? 1 : 0;
	qint32 countUnreadMessages = variables->countUnreadMessages ? 1 : 0;
	qint32 exeLaunchWarning = variables->exeLaunchWarning ? 1 : 0;
	QByteArray autoDownload;
	qint32 supportAllSearchResults = variables->supportAllSearchResults.current() ? 1 : 0;
	qint32 archiveCollapsed = variables->archiveCollapsed.current() ? 1 : 0;
	qint32 notifyAboutPinned = variables->notifyAboutPinned.current() ? 1 : 0;
	qint32 archiveInMainMenu = variables->archiveInMainMenu.current() ? 1 : 0;
	qint32 skipArchiveInSearch = variables->skipArchiveInSearch.current() ? 1 : 0;
	qint32 autoplayGifs = 1;
	qint32 loopAnimatedStickers = variables->loopAnimatedStickers ? 1 : 0;
	qint32 largeEmoji = variables->largeEmoji.current() ? 1 : 0;
	qint32 replaceEmoji = variables->replaceEmoji.current() ? 1 : 0;
	qint32 suggestEmoji = variables->suggestEmoji ? 1 : 0;
	qint32 suggestStickersByEmoji = variables->suggestStickersByEmoji ? 1 : 0;
	qint32 spellcheckerEnabled = variables->spellcheckerEnabled.current() ? 1 : 0;
	std::vector<std::pair<DocumentId, crl::time>> mediaLastPlaybackPosition;
	qint32 videoPlaybackSpeed = SerializePlaybackSpeed(variables->videoPlaybackSpeed.current());
	QByteArray videoPipGeometry = variables->videoPipGeometry;
	std::vector<int> dictionariesEnabled;
	qint32 autoDownloadDictionaries = variables->autoDownloadDictionaries.current() ? 1 : 0;
	base::flat_map<PeerId, MsgId> hiddenPinnedMessages;

	stream >> versionTag;
	if (versionTag == kVersionTag) {
		stream >> version;
		stream >> selectorTab;
	} else {
		selectorTab = versionTag;
	}
	stream >> lastSeenWarningSeen;
	if (!stream.atEnd()) {
		stream >> tabbedSelectorSectionEnabled;
	}
	if (!stream.atEnd()) {
		auto count = qint32(0);
		stream >> count;
		if (stream.status() == QDataStream::Ok) {
			for (auto i = 0; i != count; ++i) {
				QString key, value;
				stream >> key >> value;
				soundOverrides.emplace(key, value);
			}
		}
	}
	if (!stream.atEnd()) {
		stream >> tabbedSelectorSectionTooltipShown;
	}
	if (!stream.atEnd()) {
		stream >> floatPlayerColumn >> floatPlayerCorner;
	}
	if (!stream.atEnd()) {
		auto count = qint32(0);
		stream >> count;
		if (stream.status() == QDataStream::Ok) {
			for (auto i = 0; i != count; ++i) {
				quint64 peerId;
				stream >> peerId;
				groupStickersSectionHidden.insert(peerId);
			}
		}
	}
	if (!stream.atEnd()) {
		stream >> thirdSectionInfoEnabled;
		stream >> smallDialogsList;
	}
	if (!stream.atEnd()) {
		qint32 value = 0;
		stream >> value;
		dialogsWidthRatio = snap(value / 1000000., 0., 1.);

		stream >> value;
		thirdColumnWidth = value;

		stream >> value;
		thirdSectionExtendedBy = value;
	}
	if (!stream.atEnd()) {
		stream >> sendFilesWay;
	}
	if (!stream.atEnd()) {
		stream >> legacyCallsPeerToPeer;
	}
	if (!stream.atEnd()) {
		stream >> sendSubmitWay;
		stream >> supportSwitch;
		stream >> supportFixChatsOrder;
	}
	if (!stream.atEnd()) {
		stream >> supportTemplatesAutocomplete;
	}
	if (!stream.atEnd()) {
		stream >> supportChatsTimeSlice;
	}
	if (!stream.atEnd()) {
		stream >> includeMutedCounter;
		stream >> countUnreadMessages;
	}
	if (!stream.atEnd()) {
		stream >> exeLaunchWarning;
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
	if (!stream.atEnd()) {
		stream >> notifyAboutPinned;
	}
	if (!stream.atEnd()) {
		stream >> archiveInMainMenu;
	}
	if (!stream.atEnd()) {
		stream >> skipArchiveInSearch;
	}
	if (!stream.atEnd()) {
		stream >> autoplayGifs;
		stream >> loopAnimatedStickers;
		stream >> largeEmoji;
		stream >> replaceEmoji;
		stream >> suggestEmoji;
		stream >> suggestStickersByEmoji;
	}
	if (!stream.atEnd()) {
		stream >> spellcheckerEnabled;
	}
	if (!stream.atEnd()) {
		auto count = qint32(0);
		stream >> count;
		if (stream.status() == QDataStream::Ok) {
			for (auto i = 0; i != count; ++i) {
				quint64 documentId;
				qint64 time;
				stream >> documentId >> time;
				mediaLastPlaybackPosition.emplace_back(documentId, time);
			}
		}
	}
	if (!stream.atEnd()) {
		stream >> videoPlaybackSpeed;
	}
	if (!stream.atEnd()) {
		stream >> videoPipGeometry;
	}
	if (!stream.atEnd()) {
		auto count = qint32(0);
		stream >> count;
		if (stream.status() == QDataStream::Ok) {
			for (auto i = 0; i != count; ++i) {
				qint64 langId;
				stream >> langId;
				dictionariesEnabled.emplace_back(langId);
			}
		}
	}
	if (!stream.atEnd()) {
		stream >> autoDownloadDictionaries;
	}
	if (!stream.atEnd()) {
		auto count = qint32(0);
		stream >> count;
		if (stream.status() == QDataStream::Ok) {
			for (auto i = 0; i != count; ++i) {
				auto key = quint64();
				auto value = qint32();
				stream >> key >> value;
				hiddenPinnedMessages.emplace(key, value);
			}
		}
	}
	if (stream.status() != QDataStream::Ok) {
		LOG(("App Error: "
			"Bad data for Main::SessionSettings::FromSerialized()"));
		return nullptr;
	}
	if (!autoDownload.isEmpty()
		&& !variables->autoDownload.setFromSerialized(autoDownload)) {
		return nullptr;
	}
	if (!version) {
		if (!autoplayGifs) {
			using namespace Data::AutoDownload;
			variables->autoDownload = WithDisabledAutoPlay(
				variables->autoDownload);
		}
	}

	auto uncheckedTab = static_cast<ChatHelpers::SelectorTab>(selectorTab);
	switch (uncheckedTab) {
	case ChatHelpers::SelectorTab::Emoji:
	case ChatHelpers::SelectorTab::Stickers:
	case ChatHelpers::SelectorTab::Gifs: variables->selectorTab = uncheckedTab; break;
	}
	variables->lastSeenWarningSeen = (lastSeenWarningSeen == 1);
	variables->tabbedSelectorSectionEnabled = (tabbedSelectorSectionEnabled == 1);
	variables->soundOverrides = std::move(soundOverrides);
	variables->tabbedSelectorSectionTooltipShown = tabbedSelectorSectionTooltipShown;
	auto uncheckedColumn = static_cast<Window::Column>(floatPlayerColumn);
	switch (uncheckedColumn) {
	case Window::Column::First:
	case Window::Column::Second:
	case Window::Column::Third: variables->floatPlayerColumn = uncheckedColumn; break;
	}
	auto uncheckedCorner = static_cast<RectPart>(floatPlayerCorner);
	switch (uncheckedCorner) {
	case RectPart::TopLeft:
	case RectPart::TopRight:
	case RectPart::BottomLeft:
	case RectPart::BottomRight: variables->floatPlayerCorner = uncheckedCorner; break;
	}
	variables->groupStickersSectionHidden = std::move(groupStickersSectionHidden);
	variables->thirdSectionInfoEnabled = thirdSectionInfoEnabled;
	variables->smallDialogsList = smallDialogsList;
	variables->dialogsWidthRatio = dialogsWidthRatio;
	variables->thirdColumnWidth = thirdColumnWidth;
	variables->thirdSectionExtendedBy = thirdSectionExtendedBy;
	if (variables->thirdSectionInfoEnabled) {
		variables->tabbedSelectorSectionEnabled = false;
	}
	auto uncheckedSendFilesWay = static_cast<SendFilesWay>(sendFilesWay);
	switch (uncheckedSendFilesWay) {
	case SendFilesWay::Album:
	case SendFilesWay::Photos:
	case SendFilesWay::Files: variables->sendFilesWay = uncheckedSendFilesWay; break;
	}
	auto uncheckedSendSubmitWay = static_cast<Ui::InputSubmitSettings>(
		sendSubmitWay);
	switch (uncheckedSendSubmitWay) {
	case Ui::InputSubmitSettings::Enter:
	case Ui::InputSubmitSettings::CtrlEnter: variables->sendSubmitWay = uncheckedSendSubmitWay; break;
	}
	auto uncheckedSupportSwitch = static_cast<Support::SwitchSettings>(
		supportSwitch);
	switch (uncheckedSupportSwitch) {
	case Support::SwitchSettings::None:
	case Support::SwitchSettings::Next:
	case Support::SwitchSettings::Previous: variables->supportSwitch = uncheckedSupportSwitch; break;
	}
	variables->supportFixChatsOrder = (supportFixChatsOrder == 1);
	variables->supportTemplatesAutocomplete = (supportTemplatesAutocomplete == 1);
	variables->supportChatsTimeSlice = supportChatsTimeSlice;
	variables->hadLegacyCallsPeerToPeerNobody = (legacyCallsPeerToPeer == kLegacyCallsPeerToPeerNobody);
	variables->includeMutedCounter = (includeMutedCounter == 1);
	variables->countUnreadMessages = (countUnreadMessages == 1);
	variables->exeLaunchWarning = (exeLaunchWarning == 1);
	variables->supportAllSearchResults = (supportAllSearchResults == 1);
	variables->archiveCollapsed = (archiveCollapsed == 1);
	variables->notifyAboutPinned = (notifyAboutPinned == 1);
	variables->archiveInMainMenu = (archiveInMainMenu == 1);
	variables->skipArchiveInSearch = (skipArchiveInSearch == 1);
	variables->loopAnimatedStickers = (loopAnimatedStickers == 1);
	variables->largeEmoji = (largeEmoji == 1);
	variables->replaceEmoji = (replaceEmoji == 1);
	variables->suggestEmoji = (suggestEmoji == 1);
	variables->suggestStickersByEmoji = (suggestStickersByEmoji == 1);
	variables->spellcheckerEnabled = (spellcheckerEnabled == 1);
	variables->mediaLastPlaybackPosition = std::move(mediaLastPlaybackPosition);
	variables->videoPlaybackSpeed = DeserializePlaybackSpeed(videoPlaybackSpeed);
	variables->videoPipGeometry = videoPipGeometry;
	variables->dictionariesEnabled = std::move(dictionariesEnabled);
	variables->autoDownloadDictionaries = (autoDownloadDictionaries == 1);
	variables->hiddenPinnedMessages = std::move(hiddenPinnedMessages);
	return result;
}

void SessionSettings::setSupportChatsTimeSlice(int slice) {
	_variables.supportChatsTimeSlice = slice;
}

int SessionSettings::supportChatsTimeSlice() const {
	return _variables.supportChatsTimeSlice.current();
}

rpl::producer<int> SessionSettings::supportChatsTimeSliceValue() const {
	return _variables.supportChatsTimeSlice.value();
}

void SessionSettings::setSupportAllSearchResults(bool all) {
	_variables.supportAllSearchResults = all;
}

bool SessionSettings::supportAllSearchResults() const {
	return _variables.supportAllSearchResults.current();
}

rpl::producer<bool> SessionSettings::supportAllSearchResultsValue() const {
	return _variables.supportAllSearchResults.value();
}

void SessionSettings::setTabbedSelectorSectionEnabled(bool enabled) {
	_variables.tabbedSelectorSectionEnabled = enabled;
	if (enabled) {
		setThirdSectionInfoEnabled(false);
	}
	setTabbedReplacedWithInfo(false);
}

rpl::producer<bool> SessionSettings::tabbedReplacedWithInfoValue() const {
	return _tabbedReplacedWithInfoValue.events_starting_with(
		tabbedReplacedWithInfo());
}

void SessionSettings::setThirdSectionInfoEnabled(bool enabled) {
	if (_variables.thirdSectionInfoEnabled != enabled) {
		_variables.thirdSectionInfoEnabled = enabled;
		if (enabled) {
			setTabbedSelectorSectionEnabled(false);
		}
		setTabbedReplacedWithInfo(false);
		_thirdSectionInfoEnabledValue.fire_copy(enabled);
	}
}

rpl::producer<bool> SessionSettings::thirdSectionInfoEnabledValue() const {
	return _thirdSectionInfoEnabledValue.events_starting_with(
		thirdSectionInfoEnabled());
}

void SessionSettings::setTabbedReplacedWithInfo(bool enabled) {
	if (_tabbedReplacedWithInfo != enabled) {
		_tabbedReplacedWithInfo = enabled;
		_tabbedReplacedWithInfoValue.fire_copy(enabled);
	}
}

QString SessionSettings::getSoundPath(const QString &key) const {
	auto it = _variables.soundOverrides.find(key);
	if (it != _variables.soundOverrides.end()) {
		return it->second;
	}
	return qsl(":/sounds/") + key + qsl(".mp3");
}

void SessionSettings::setDialogsWidthRatio(float64 ratio) {
	_variables.dialogsWidthRatio = ratio;
}

float64 SessionSettings::dialogsWidthRatio() const {
	return _variables.dialogsWidthRatio.current();
}

rpl::producer<float64> SessionSettings::dialogsWidthRatioChanges() const {
	return _variables.dialogsWidthRatio.changes();
}

void SessionSettings::setThirdColumnWidth(int width) {
	_variables.thirdColumnWidth = width;
}

int SessionSettings::thirdColumnWidth() const {
	return _variables.thirdColumnWidth.current();
}

rpl::producer<int> SessionSettings::thirdColumnWidthChanges() const {
	return _variables.thirdColumnWidth.changes();
}

void SessionSettings::setMediaLastPlaybackPosition(DocumentId id, crl::time time) {
	auto &map = _variables.mediaLastPlaybackPosition;
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
		_variables.mediaLastPlaybackPosition,
		id,
		&std::pair<DocumentId, crl::time>::first);
	return (i != _variables.mediaLastPlaybackPosition.end()) ? i->second : 0;
}

void SessionSettings::setArchiveCollapsed(bool collapsed) {
	_variables.archiveCollapsed = collapsed;
}

bool SessionSettings::archiveCollapsed() const {
	return _variables.archiveCollapsed.current();
}

rpl::producer<bool> SessionSettings::archiveCollapsedChanges() const {
	return _variables.archiveCollapsed.changes();
}

void SessionSettings::setArchiveInMainMenu(bool inMainMenu) {
	_variables.archiveInMainMenu = inMainMenu;
}

bool SessionSettings::archiveInMainMenu() const {
	return _variables.archiveInMainMenu.current();
}

rpl::producer<bool> SessionSettings::archiveInMainMenuChanges() const {
	return _variables.archiveInMainMenu.changes();
}

void SessionSettings::setNotifyAboutPinned(bool notify) {
	_variables.notifyAboutPinned = notify;
}

bool SessionSettings::notifyAboutPinned() const {
	return _variables.notifyAboutPinned.current();
}

rpl::producer<bool> SessionSettings::notifyAboutPinnedChanges() const {
	return _variables.notifyAboutPinned.changes();
}

void SessionSettings::setSkipArchiveInSearch(bool skip) {
	_variables.skipArchiveInSearch = skip;
}

bool SessionSettings::skipArchiveInSearch() const {
	return _variables.skipArchiveInSearch.current();
}

rpl::producer<bool> SessionSettings::skipArchiveInSearchChanges() const {
	return _variables.skipArchiveInSearch.changes();
}

void SessionSettings::setLargeEmoji(bool value) {
	_variables.largeEmoji = value;
}

bool SessionSettings::largeEmoji() const {
	return _variables.largeEmoji.current();
}

rpl::producer<bool> SessionSettings::largeEmojiValue() const {
	return _variables.largeEmoji.value();
}

rpl::producer<bool> SessionSettings::largeEmojiChanges() const {
	return _variables.largeEmoji.changes();
}

void SessionSettings::setReplaceEmoji(bool value) {
	_variables.replaceEmoji = value;
}

bool SessionSettings::replaceEmoji() const {
	return _variables.replaceEmoji.current();
}

rpl::producer<bool> SessionSettings::replaceEmojiValue() const {
	return _variables.replaceEmoji.value();
}

rpl::producer<bool> SessionSettings::replaceEmojiChanges() const {
	return _variables.replaceEmoji.changes();
}

} // namespace Main
