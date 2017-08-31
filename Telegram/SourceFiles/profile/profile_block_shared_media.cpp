/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "profile/profile_block_shared_media.h"

#include "profile/profile_common_groups_section.h"
#include "profile/profile_section_memento.h"
#include "styles/style_profile.h"
#include "observer_peer.h"
#include "ui/widgets/buttons.h"
#include "mainwidget.h"
#include "lang/lang_keys.h"

namespace Profile {
namespace {

QString getButtonText(MediaOverviewType type, int count) {
	if (count <= 0) {
		return QString();
	}

	switch (type) {
	case OverviewPhotos: return lng_profile_photos(lt_count, count);
	case OverviewVideos: return lng_profile_videos(lt_count, count);
	case OverviewMusicFiles: return lng_profile_songs(lt_count, count);
	case OverviewFiles: return lng_profile_files(lt_count, count);
	case OverviewVoiceFiles: return lng_profile_audios(lt_count, count);
	case OverviewLinks: return lng_profile_shared_links(lt_count, count);
	}
	return QString();
}

} // namespace

SharedMediaWidget::SharedMediaWidget(QWidget *parent, PeerData *peer) : BlockWidget(parent, peer, lang(lng_profile_shared_media))
, _history(App::history(peer))
, _migrated(_history->migrateFrom()) {
	auto observeEvents = Notify::PeerUpdate::Flag::SharedMediaChanged
		| Notify::PeerUpdate::Flag::UserCommonChatsChanged;
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(observeEvents, [this](const Notify::PeerUpdate &update) {
		notifyPeerUpdated(update);
	}));

	for (auto i = 0; i != OverviewCount; ++i) {
		auto type = static_cast<MediaOverviewType>(i);
		if (!getButtonText(type, 1).isEmpty()) {
			App::main()->preloadOverview(peer, type);
			if (_migrated) {
				App::main()->preloadOverview(_migrated->peer, type);
			}
		}
	}

	refreshButtons();
	refreshVisibility();
}

void SharedMediaWidget::notifyPeerUpdated(const Notify::PeerUpdate &update) {
	if (update.peer != peer() && (!_migrated || update.peer != _migrated->peer)) {
		return;
	}

	auto updated = false;
	for (auto i = 0; i != OverviewCount; ++i) {
		if (update.mediaTypesMask & (1 << i)) {
			refreshButton(static_cast<MediaOverviewType>(i));
			updated = true;
		}
	}
	if (update.flags & Notify::PeerUpdate::Flag::UserCommonChatsChanged) {
		refreshCommonGroups();
		updated = true;
	}
	if (updated) {
		refreshVisibility();

		contentSizeUpdated();
	}
}

int SharedMediaWidget::resizeGetHeight(int newWidth) {
	int newHeight = contentTop();

	resizeButtons(newWidth, &newHeight);

	return newHeight;
}

void SharedMediaWidget::refreshButtons() {
	for (int typeIndex = 0; typeIndex < OverviewCount; ++typeIndex) {
		refreshButton(static_cast<MediaOverviewType>(typeIndex));
	}
	refreshCommonGroups();
}

void SharedMediaWidget::refreshButton(MediaOverviewType type) {
	int count = _history->overviewCount(type), migrated = _migrated ? _migrated->overviewCount(type) : 0;
	int finalCount = (count >= 0 && migrated >= 0) ? (count + migrated) : -1;
	auto text = getButtonText(type, finalCount);
	if (text.isEmpty()) {
		if (_mediaButtons[type]) {
			delete _mediaButtons[type];
			_mediaButtons[type] = nullptr;
		}
	} else {
		if (_mediaButtons[type]) {
			_mediaButtons[type]->setText(text);
		} else {
			_mediaButtons[type] = new Ui::LeftOutlineButton(this, text, st::defaultLeftOutlineButton);
			_mediaButtons[type]->show();
			connect(_mediaButtons[type], SIGNAL(clicked()), this, SLOT(onMediaChosen()));
		}
	}
}

void SharedMediaWidget::refreshVisibility() {
	for_const (auto button, _mediaButtons) {
		if (button) {
			show();
			return;
		}
	}
	setVisible(_commonGroups != nullptr);
}

void SharedMediaWidget::onMediaChosen() {
	for (int i = 0; i < OverviewCount; ++i) {
		auto button = _mediaButtons[i];
		if (button && button == sender()) {
			App::main()->showMediaOverview(peer(), static_cast<MediaOverviewType>(i));
			return;
		}
	}
}

void SharedMediaWidget::resizeButtons(int newWidth, int *top) {
	Assert(top != nullptr);

	int left = defaultOutlineButtonLeft();
	int availableWidth = newWidth - left - st::profileBlockMarginRight;
	accumulate_min(availableWidth, st::profileBlockOneLineWidthMax);
	for_const (auto button, _mediaButtons) {
		if (!button) continue;

		button->resizeToWidth(availableWidth);
		button->moveToLeft(left, *top);
		*top += button->height();
	}

	if (_commonGroups) {
		_commonGroups->resizeToWidth(availableWidth);
		_commonGroups->moveToLeft(left, *top);
		*top += _commonGroups->height();
	}

}

int SharedMediaWidget::getCommonGroupsCount() const {
	if (auto user = peer()->asUser()) {
		return user->commonChatsCount();
	}
	return 0;
}

void SharedMediaWidget::refreshCommonGroups() {
	if (auto count = getCommonGroupsCount()) {
		auto text = lng_profile_common_groups(lt_count, count);
		if (_commonGroups) {
			_commonGroups->setText(text);
		} else {
			_commonGroups.create(this, text, st::defaultLeftOutlineButton);
			_commonGroups->setClickedCallback([this] { onShowCommonGroups(); });
			_commonGroups->show();
		}
	} else if (_commonGroups) {
		_commonGroups.destroyDelayed();
	}
}

void SharedMediaWidget::onShowCommonGroups() {
	auto count = getCommonGroupsCount();
	if (count <= 0) {
		refreshCommonGroups();
		return;
	}
	if (auto main = App::main()) {
		main->showWideSection(Profile::CommonGroups::SectionMemento(peer()->asUser()));
	}
}

} // namespace Profile
