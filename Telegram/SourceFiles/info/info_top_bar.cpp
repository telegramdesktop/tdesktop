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
#include "info/info_top_bar.h"

#include "styles/style_info.h"
#include "lang/lang_keys.h"
#include "info/info_wrap_widget.h"
#include "storage/storage_shared_media.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/widgets/shadow.h"

namespace Info {

TopBar::TopBar(QWidget *parent, const style::InfoTopBar &st)
: RpWidget(parent)
, _st(st) {
	setAttribute(Qt::WA_OpaquePaintEvent);
}

void TopBar::setTitle(rpl::producer<QString> &&title) {
	_title.create(this, std::move(title), _st.title);
	if (_back) {
		_title->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
	updateControlsGeometry(width());
}

void TopBar::enableBackButton(bool enable) {
	if (enable) {
		_back.create(this, _st.back);
		_back->clicks()
			| rpl::start_to_stream(_backClicks, lifetime());
	} else {
		_back.destroy();
	}
	if (_title) {
		_title->setAttribute(Qt::WA_TransparentForMouseEvents, enable);
	}
	updateControlsGeometry(width());
}

void TopBar::pushButton(object_ptr<Ui::RpWidget> button) {
	auto weak = Ui::AttachParentChild(this, button);
	_buttons.push_back(std::move(button));
	weak->widthValue()
		| rpl::start_with_next([this] {
			this->updateControlsGeometry(this->width());
		}, lifetime());
}

int TopBar::resizeGetHeight(int newWidth) {
	updateControlsGeometry(newWidth);
	return _st.height;
}

void TopBar::updateControlsGeometry(int newWidth) {
	auto right = 0;
	for (auto &button : _buttons) {
		button->moveToRight(right, 0, newWidth);
		right += button->width();
	}
	if (_back) {
		_back->setGeometryToLeft(
			0,
			0,
			newWidth - right,
			_back->height(),
			newWidth);
	}
	if (_title) {
		_title->moveToLeft(
			_back ? _st.back.width : _st.titlePosition.x(),
			_st.titlePosition.y(),
			newWidth);
	}
}

void TopBar::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.fillRect(e->rect(), _st.bg);
}

rpl::producer<QString> TitleValue(
		const Section &section,
		PeerId peerId) {
	return Lang::Viewer([&] {
		switch (section.type()) {
		case Section::Type::Profile:
			if (peerIsUser(peerId)) {
				return App::user(peerId)->botInfo
					? lng_info_bot_title
					: lng_info_user_title;
			} else if (peerIsChannel(peerId)) {
				return App::channel(peerId)->isMegagroup()
					? lng_info_group_title
					: lng_info_channel_title;
			} else if (peerIsChat(peerId)) {
				return lng_info_group_title;
			}
			Unexpected("Bad peer type in Info::TitleValue()");

		case Section::Type::Media:
			switch (section.mediaType()) {
			case Section::MediaType::Photo:
				return lng_media_type_photos;
			case Section::MediaType::Video:
				return lng_media_type_videos;
			case Section::MediaType::MusicFile:
				return lng_media_type_songs;
			case Section::MediaType::File:
				return lng_media_type_files;
			case Section::MediaType::VoiceFile:
				return lng_media_type_audios;
			case Section::MediaType::Link:
				return lng_media_type_links;
			case Section::MediaType::RoundFile:
				return lng_media_type_rounds;
			}
			Unexpected("Bad media type in Info::TitleValue()");

		case Section::Type::CommonGroups:
			return lng_profile_common_groups_section;

		}
		Unexpected("Bad section type in Info::TitleValue()");
	}());
}

} // namespace Info
