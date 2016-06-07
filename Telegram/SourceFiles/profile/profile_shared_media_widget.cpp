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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "profile/profile_shared_media_widget.h"

#include "styles/style_profile.h"
#include "observer_peer.h"
#include "ui/buttons/left_outline_button.h"
#include "mainwidget.h"
#include "lang.h"

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
, _migrated(peer->migrateFrom() ? App::history(peer->migrateFrom()) : nullptr) {
	Notify::registerPeerObserver(Notify::PeerUpdate::Flag::SharedMediaChanged, this, &SharedMediaWidget::notifyPeerUpdated);

	App::main()->preloadOverviews(peer);
	if (_migrated) {
		App::main()->preloadOverviews(_migrated->peer);
	}

	refreshButtons();
	refreshVisibility();
}

void SharedMediaWidget::notifyPeerUpdated(const Notify::PeerUpdate &update) {
	if (update.peer != peer() && (!_migrated || update.peer != _migrated->peer)) {
		return;
	}

	bool updated = false;
	for (int i = 0; i < OverviewCount; ++i) {
		if (update.mediaTypesMask & (1 << i)) {
			refreshButton(static_cast<MediaOverviewType>(i));
			updated = true;
		}
	}
	if (updated) {
		refreshVisibility();

		contentSizeUpdated();
	}
}

int SharedMediaWidget::resizeGetHeight(int newWidth) {
	int newHeight = contentTop();

	resizeButtons(&newHeight);

	return newHeight;
}

void SharedMediaWidget::refreshButtons() {
	for (int typeIndex = 0; typeIndex < OverviewCount; ++typeIndex) {
		refreshButton(static_cast<MediaOverviewType>(typeIndex));
	}
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
	hide();
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

void SharedMediaWidget::resizeButtons(int *top) {
	t_assert(top != nullptr);

	int left = defaultOutlineButtonLeft();
	int availableWidth = width() - left - st::profileBlockMarginRight;
	accumulate_min(availableWidth, st::profileBlockOneLineWidthMax);
	for_const (auto button, _mediaButtons) {
		if (!button) continue;

		button->resizeToWidth(availableWidth);
		button->moveToLeft(left, *top);
		*top += button->height();
	}
}

} // namespace Profile
