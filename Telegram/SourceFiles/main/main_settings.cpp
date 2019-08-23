/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "main/main_settings.h"

#include "chat_helpers/tabbed_selector.h"
#include "window/section_widget.h"
#include "ui/widgets/input_fields.h"
#include "support/support_common.h"
#include "storage/serialize_common.h"
#include "boxes/send_files_box.h"

namespace Main {
namespace {

constexpr auto kAutoLockTimeoutLateMs = crl::time(3000);
constexpr auto kLegacyCallsPeerToPeerNobody = 4;

} // namespace

Settings::Variables::Variables()
: sendFilesWay(SendFilesWay::Album)
, selectorTab(ChatHelpers::SelectorTab::Emoji)
, floatPlayerColumn(Window::Column::Second)
, floatPlayerCorner(RectPart::TopRight)
, sendSubmitWay(Ui::InputSubmitSettings::Enter)
, supportSwitch(Support::SwitchSettings::Next) {
}

QByteArray Settings::serialize() const {
	const auto autoDownload = _variables.autoDownload.serialize();
	auto size = sizeof(qint32) * 30;
	for (auto i = _variables.soundOverrides.cbegin(), e = _variables.soundOverrides.cend(); i != e; ++i) {
		size += Serialize::stringSize(i.key()) + Serialize::stringSize(i.value());
	}
	size += _variables.groupStickersSectionHidden.size() * sizeof(quint64);
	size += Serialize::bytearraySize(autoDownload);

	auto result = QByteArray();
	result.reserve(size);
	{
		QDataStream stream(&result, QIODevice::WriteOnly);
		stream.setVersion(QDataStream::Qt_5_1);
		stream << static_cast<qint32>(_variables.selectorTab);
		stream << qint32(_variables.lastSeenWarningSeen ? 1 : 0);
		stream << qint32(_variables.tabbedSelectorSectionEnabled ? 1 : 0);
		stream << qint32(_variables.soundOverrides.size());
		for (auto i = _variables.soundOverrides.cbegin(), e = _variables.soundOverrides.cend(); i != e; ++i) {
			stream << i.key() << i.value();
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
		stream << qint32(_variables.autoplayGifs ? 1 : 0);
		stream << qint32(_variables.loopAnimatedStickers ? 1 : 0);
		stream << qint32(_variables.largeEmoji.current() ? 1 : 0);
		stream << qint32(_variables.replaceEmoji.current() ? 1 : 0);
		stream << qint32(_variables.suggestEmoji ? 1 : 0);
		stream << qint32(_variables.suggestStickersByEmoji ? 1 : 0);
	}
	return result;
}

void Settings::constructFromSerialized(const QByteArray &serialized) {
	if (serialized.isEmpty()) {
		return;
	}

	QDataStream stream(serialized);
	stream.setVersion(QDataStream::Qt_5_1);
	qint32 selectorTab = static_cast<qint32>(ChatHelpers::SelectorTab::Emoji);
	qint32 lastSeenWarningSeen = 0;
	qint32 tabbedSelectorSectionEnabled = 1;
	qint32 tabbedSelectorSectionTooltipShown = 0;
	qint32 floatPlayerColumn = static_cast<qint32>(Window::Column::Second);
	qint32 floatPlayerCorner = static_cast<qint32>(RectPart::TopRight);
	QMap<QString, QString> soundOverrides;
	base::flat_set<PeerId> groupStickersSectionHidden;
	qint32 thirdSectionInfoEnabled = 0;
	qint32 smallDialogsList = 0;
	float64 dialogsWidthRatio = _variables.dialogsWidthRatio.current();
	int thirdColumnWidth = _variables.thirdColumnWidth.current();
	int thirdSectionExtendedBy = _variables.thirdSectionExtendedBy;
	qint32 sendFilesWay = static_cast<qint32>(_variables.sendFilesWay);
	qint32 legacyCallsPeerToPeer = qint32(0);
	qint32 sendSubmitWay = static_cast<qint32>(_variables.sendSubmitWay);
	qint32 supportSwitch = static_cast<qint32>(_variables.supportSwitch);
	qint32 supportFixChatsOrder = _variables.supportFixChatsOrder ? 1 : 0;
	qint32 supportTemplatesAutocomplete = _variables.supportTemplatesAutocomplete ? 1 : 0;
	qint32 supportChatsTimeSlice = _variables.supportChatsTimeSlice.current();
	qint32 includeMutedCounter = _variables.includeMutedCounter ? 1 : 0;
	qint32 countUnreadMessages = _variables.countUnreadMessages ? 1 : 0;
	qint32 exeLaunchWarning = _variables.exeLaunchWarning ? 1 : 0;
	QByteArray autoDownload;
	qint32 supportAllSearchResults = _variables.supportAllSearchResults.current() ? 1 : 0;
	qint32 archiveCollapsed = _variables.archiveCollapsed.current() ? 1 : 0;
	qint32 notifyAboutPinned = _variables.notifyAboutPinned.current() ? 1 : 0;
	qint32 archiveInMainMenu = _variables.archiveInMainMenu.current() ? 1 : 0;
	qint32 skipArchiveInSearch = _variables.skipArchiveInSearch.current() ? 1 : 0;
	qint32 autoplayGifs = _variables.autoplayGifs ? 1 : 0;
	qint32 loopAnimatedStickers = _variables.loopAnimatedStickers ? 1 : 0;
	qint32 largeEmoji = _variables.largeEmoji.current() ? 1 : 0;
	qint32 replaceEmoji = _variables.replaceEmoji.current() ? 1 : 0;
	qint32 suggestEmoji = _variables.suggestEmoji ? 1 : 0;
	qint32 suggestStickersByEmoji = _variables.suggestStickersByEmoji ? 1 : 0;

	stream >> selectorTab;
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
				soundOverrides[key] = value;
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
	if (stream.status() != QDataStream::Ok) {
		LOG(("App Error: "
			"Bad data for Main::Settings::constructFromSerialized()"));
		return;
	}
	if (!autoDownload.isEmpty()
		&& !_variables.autoDownload.setFromSerialized(autoDownload)) {
		return;
	}

	auto uncheckedTab = static_cast<ChatHelpers::SelectorTab>(selectorTab);
	switch (uncheckedTab) {
	case ChatHelpers::SelectorTab::Emoji:
	case ChatHelpers::SelectorTab::Stickers:
	case ChatHelpers::SelectorTab::Gifs: _variables.selectorTab = uncheckedTab; break;
	}
	_variables.lastSeenWarningSeen = (lastSeenWarningSeen == 1);
	_variables.tabbedSelectorSectionEnabled = (tabbedSelectorSectionEnabled == 1);
	_variables.soundOverrides = std::move(soundOverrides);
	_variables.tabbedSelectorSectionTooltipShown = tabbedSelectorSectionTooltipShown;
	auto uncheckedColumn = static_cast<Window::Column>(floatPlayerColumn);
	switch (uncheckedColumn) {
	case Window::Column::First:
	case Window::Column::Second:
	case Window::Column::Third: _variables.floatPlayerColumn = uncheckedColumn; break;
	}
	auto uncheckedCorner = static_cast<RectPart>(floatPlayerCorner);
	switch (uncheckedCorner) {
	case RectPart::TopLeft:
	case RectPart::TopRight:
	case RectPart::BottomLeft:
	case RectPart::BottomRight: _variables.floatPlayerCorner = uncheckedCorner; break;
	}
	_variables.groupStickersSectionHidden = std::move(groupStickersSectionHidden);
	_variables.thirdSectionInfoEnabled = thirdSectionInfoEnabled;
	_variables.smallDialogsList = smallDialogsList;
	_variables.dialogsWidthRatio = dialogsWidthRatio;
	_variables.thirdColumnWidth = thirdColumnWidth;
	_variables.thirdSectionExtendedBy = thirdSectionExtendedBy;
	if (_variables.thirdSectionInfoEnabled) {
		_variables.tabbedSelectorSectionEnabled = false;
	}
	auto uncheckedSendFilesWay = static_cast<SendFilesWay>(sendFilesWay);
	switch (uncheckedSendFilesWay) {
	case SendFilesWay::Album:
	case SendFilesWay::Photos:
	case SendFilesWay::Files: _variables.sendFilesWay = uncheckedSendFilesWay; break;
	}
	auto uncheckedSendSubmitWay = static_cast<Ui::InputSubmitSettings>(
		sendSubmitWay);
	switch (uncheckedSendSubmitWay) {
	case Ui::InputSubmitSettings::Enter:
	case Ui::InputSubmitSettings::CtrlEnter: _variables.sendSubmitWay = uncheckedSendSubmitWay; break;
	}
	auto uncheckedSupportSwitch = static_cast<Support::SwitchSettings>(
		supportSwitch);
	switch (uncheckedSupportSwitch) {
	case Support::SwitchSettings::None:
	case Support::SwitchSettings::Next:
	case Support::SwitchSettings::Previous: _variables.supportSwitch = uncheckedSupportSwitch; break;
	}
	_variables.supportFixChatsOrder = (supportFixChatsOrder == 1);
	_variables.supportTemplatesAutocomplete = (supportTemplatesAutocomplete == 1);
	_variables.supportChatsTimeSlice = supportChatsTimeSlice;
	_variables.hadLegacyCallsPeerToPeerNobody = (legacyCallsPeerToPeer == kLegacyCallsPeerToPeerNobody);
	_variables.includeMutedCounter = (includeMutedCounter == 1);
	_variables.countUnreadMessages = (countUnreadMessages == 1);
	_variables.exeLaunchWarning = (exeLaunchWarning == 1);
	_variables.supportAllSearchResults = (supportAllSearchResults == 1);
	_variables.archiveCollapsed = (archiveCollapsed == 1);
	_variables.notifyAboutPinned = (notifyAboutPinned == 1);
	_variables.archiveInMainMenu = (archiveInMainMenu == 1);
	_variables.skipArchiveInSearch = (skipArchiveInSearch == 1);
	_variables.autoplayGifs = (autoplayGifs == 1);
	_variables.loopAnimatedStickers = (loopAnimatedStickers == 1);
	_variables.largeEmoji = (largeEmoji == 1);
	_variables.replaceEmoji = (replaceEmoji == 1);
	_variables.suggestEmoji = (suggestEmoji == 1);
	_variables.suggestStickersByEmoji = (suggestStickersByEmoji == 1);
}

void Settings::setSupportChatsTimeSlice(int slice) {
	_variables.supportChatsTimeSlice = slice;
}

int Settings::supportChatsTimeSlice() const {
	return _variables.supportChatsTimeSlice.current();
}

rpl::producer<int> Settings::supportChatsTimeSliceValue() const {
	return _variables.supportChatsTimeSlice.value();
}

void Settings::setSupportAllSearchResults(bool all) {
	_variables.supportAllSearchResults = all;
}

bool Settings::supportAllSearchResults() const {
	return _variables.supportAllSearchResults.current();
}

rpl::producer<bool> Settings::supportAllSearchResultsValue() const {
	return _variables.supportAllSearchResults.value();
}

void Settings::setTabbedSelectorSectionEnabled(bool enabled) {
	_variables.tabbedSelectorSectionEnabled = enabled;
	if (enabled) {
		setThirdSectionInfoEnabled(false);
	}
	setTabbedReplacedWithInfo(false);
}

rpl::producer<bool> Settings::tabbedReplacedWithInfoValue() const {
	return _tabbedReplacedWithInfoValue.events_starting_with(
		tabbedReplacedWithInfo());
}

void Settings::setThirdSectionInfoEnabled(bool enabled) {
	if (_variables.thirdSectionInfoEnabled != enabled) {
		_variables.thirdSectionInfoEnabled = enabled;
		if (enabled) {
			setTabbedSelectorSectionEnabled(false);
		}
		setTabbedReplacedWithInfo(false);
		_thirdSectionInfoEnabledValue.fire_copy(enabled);
	}
}

rpl::producer<bool> Settings::thirdSectionInfoEnabledValue() const {
	return _thirdSectionInfoEnabledValue.events_starting_with(
		thirdSectionInfoEnabled());
}

void Settings::setTabbedReplacedWithInfo(bool enabled) {
	if (_tabbedReplacedWithInfo != enabled) {
		_tabbedReplacedWithInfo = enabled;
		_tabbedReplacedWithInfoValue.fire_copy(enabled);
	}
}

QString Settings::getSoundPath(const QString &key) const {
	auto it = _variables.soundOverrides.constFind(key);
	if (it != _variables.soundOverrides.end()) {
		return it.value();
	}
	return qsl(":/sounds/") + key + qsl(".mp3");
}

void Settings::setDialogsWidthRatio(float64 ratio) {
	_variables.dialogsWidthRatio = ratio;
}

float64 Settings::dialogsWidthRatio() const {
	return _variables.dialogsWidthRatio.current();
}

rpl::producer<float64> Settings::dialogsWidthRatioChanges() const {
	return _variables.dialogsWidthRatio.changes();
}

void Settings::setThirdColumnWidth(int width) {
	_variables.thirdColumnWidth = width;
}

int Settings::thirdColumnWidth() const {
	return _variables.thirdColumnWidth.current();
}

rpl::producer<int> Settings::thirdColumnWidthChanges() const {
	return _variables.thirdColumnWidth.changes();
}

void Settings::setArchiveCollapsed(bool collapsed) {
	_variables.archiveCollapsed = collapsed;
}

bool Settings::archiveCollapsed() const {
	return _variables.archiveCollapsed.current();
}

rpl::producer<bool> Settings::archiveCollapsedChanges() const {
	return _variables.archiveCollapsed.changes();
}

void Settings::setArchiveInMainMenu(bool inMainMenu) {
	_variables.archiveInMainMenu = inMainMenu;
}

bool Settings::archiveInMainMenu() const {
	return _variables.archiveInMainMenu.current();
}

rpl::producer<bool> Settings::archiveInMainMenuChanges() const {
	return _variables.archiveInMainMenu.changes();
}

void Settings::setNotifyAboutPinned(bool notify) {
	_variables.notifyAboutPinned = notify;
}

bool Settings::notifyAboutPinned() const {
	return _variables.notifyAboutPinned.current();
}

rpl::producer<bool> Settings::notifyAboutPinnedChanges() const {
	return _variables.notifyAboutPinned.changes();
}

void Settings::setSkipArchiveInSearch(bool skip) {
	_variables.skipArchiveInSearch = skip;
}

bool Settings::skipArchiveInSearch() const {
	return _variables.skipArchiveInSearch.current();
}

rpl::producer<bool> Settings::skipArchiveInSearchChanges() const {
	return _variables.skipArchiveInSearch.changes();
}

void Settings::setLargeEmoji(bool value) {
	_variables.largeEmoji = value;
}

bool Settings::largeEmoji() const {
	return _variables.largeEmoji.current();
}

rpl::producer<bool> Settings::largeEmojiValue() const {
	return _variables.largeEmoji.value();
}

rpl::producer<bool> Settings::largeEmojiChanges() const {
	return _variables.largeEmoji.changes();
}

void Settings::setReplaceEmoji(bool value) {
	_variables.replaceEmoji = value;
}

bool Settings::replaceEmoji() const {
	return _variables.replaceEmoji.current();
}

rpl::producer<bool> Settings::replaceEmojiValue() const {
	return _variables.replaceEmoji.value();
}

rpl::producer<bool> Settings::replaceEmojiChanges() const {
	return _variables.replaceEmoji.changes();
}

} // namespace Main
