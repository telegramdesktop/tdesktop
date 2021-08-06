/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_auto_download.h"
#include "ui/rect_part.h"

namespace Support {
enum class SwitchSettings;
} // namespace Support

namespace ChatHelpers {
enum class SelectorTab;
} // namespace ChatHelpers

namespace Main {

class SessionSettings final {
public:
	SessionSettings();

	[[nodiscard]] QByteArray serialize() const;
	void addFromSerialized(const QByteArray &serialized);

	void setSupportSwitch(Support::SwitchSettings value) {
		_supportSwitch = value;
	}
	[[nodiscard]] Support::SwitchSettings supportSwitch() const {
		return _supportSwitch;
	}
	void setSupportFixChatsOrder(bool fix) {
		_supportFixChatsOrder = fix;
	}
	[[nodiscard]] bool supportFixChatsOrder() const {
		return _supportFixChatsOrder;
	}
	void setSupportTemplatesAutocomplete(bool enabled) {
		_supportTemplatesAutocomplete = enabled;
	}
	[[nodiscard]] bool supportTemplatesAutocomplete() const {
		return _supportTemplatesAutocomplete;
	}
	void setSupportChatsTimeSlice(int slice);
	[[nodiscard]] int supportChatsTimeSlice() const;
	[[nodiscard]] rpl::producer<int> supportChatsTimeSliceValue() const;
	void setSupportAllSearchResults(bool all);
	[[nodiscard]] bool supportAllSearchResults() const;
	[[nodiscard]] rpl::producer<bool> supportAllSearchResultsValue() const;
	void setSupportAllSilent(bool enabled) {
		_supportAllSilent = enabled;
	}
	[[nodiscard]] bool supportAllSilent() const {
		return _supportAllSilent;
	}

	[[nodiscard]] ChatHelpers::SelectorTab selectorTab() const {
		return _selectorTab;
	}
	void setSelectorTab(ChatHelpers::SelectorTab tab) {
		_selectorTab = tab;
	}

	void setGroupStickersSectionHidden(PeerId peerId) {
		_groupStickersSectionHidden.insert(peerId);
	}
	[[nodiscard]] bool isGroupStickersSectionHidden(PeerId peerId) const {
		return _groupStickersSectionHidden.contains(peerId);
	}
	void removeGroupStickersSectionHidden(PeerId peerId) {
		_groupStickersSectionHidden.remove(peerId);
	}

	void setMediaLastPlaybackPosition(DocumentId id, crl::time time);
	[[nodiscard]] crl::time mediaLastPlaybackPosition(DocumentId id) const;

	[[nodiscard]] Data::AutoDownload::Full &autoDownload() {
		return _autoDownload;
	}
	[[nodiscard]] const Data::AutoDownload::Full &autoDownload() const {
		return _autoDownload;
	}

	void setArchiveCollapsed(bool collapsed);
	[[nodiscard]] bool archiveCollapsed() const;
	[[nodiscard]] rpl::producer<bool> archiveCollapsedChanges() const;

	void setArchiveInMainMenu(bool inMainMenu);
	[[nodiscard]] bool archiveInMainMenu() const;
	[[nodiscard]] rpl::producer<bool> archiveInMainMenuChanges() const;

	void setSkipArchiveInSearch(bool skip);
	[[nodiscard]] bool skipArchiveInSearch() const;
	[[nodiscard]] rpl::producer<bool> skipArchiveInSearchChanges() const;

	[[nodiscard]] bool hadLegacyCallsPeerToPeerNobody() const {
		return _hadLegacyCallsPeerToPeerNobody;
	}

	[[nodiscard]] MsgId hiddenPinnedMessageId(PeerId peerId) const {
		const auto i = _hiddenPinnedMessages.find(peerId);
		return (i != end(_hiddenPinnedMessages)) ? i->second : 0;
	}
	void setHiddenPinnedMessageId(PeerId peerId, MsgId msgId) {
		if (msgId) {
			_hiddenPinnedMessages[peerId] = msgId;
		} else {
			_hiddenPinnedMessages.remove(peerId);
		}
	}

	[[nodiscard]] bool dialogsFiltersEnabled() const {
		return _dialogsFiltersEnabled;
	}
	void setDialogsFiltersEnabled(bool value) {
		_dialogsFiltersEnabled = value;
	}

	[[nodiscard]] bool photoEditorHintShown() const;
	void incrementPhotoEditorHintShown();

private:
	static constexpr auto kDefaultSupportChatsLimitSlice = 7 * 24 * 60 * 60;
	static constexpr auto kPhotoEditorHintMaxShowsCount = 10;

	ChatHelpers::SelectorTab _selectorTab; // per-window
	base::flat_set<PeerId> _groupStickersSectionHidden;
	bool _hadLegacyCallsPeerToPeerNobody = false;
	Data::AutoDownload::Full _autoDownload;
	rpl::variable<bool> _archiveCollapsed = false;
	rpl::variable<bool> _archiveInMainMenu = false;
	rpl::variable<bool> _skipArchiveInSearch = false;
	std::vector<std::pair<DocumentId, crl::time>> _mediaLastPlaybackPosition;
	base::flat_map<PeerId, MsgId> _hiddenPinnedMessages;
	bool _dialogsFiltersEnabled = false;
	int _photoEditorHintShowsCount = 0;

	Support::SwitchSettings _supportSwitch;
	bool _supportFixChatsOrder = true;
	bool _supportTemplatesAutocomplete = true;
	bool _supportAllSilent = false;
	rpl::variable<int> _supportChatsTimeSlice
		= kDefaultSupportChatsLimitSlice;
	rpl::variable<bool> _supportAllSearchResults = false;

};

} // namespace Main
