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
#include "info/media/info_media_empty_widget.h"

#include "ui/widgets/labels.h"
#include "styles/style_info.h"
#include "lang/lang_keys.h"

namespace Info {
namespace Media {

EmptyWidget::EmptyWidget(QWidget *parent)
: RpWidget(parent)
, _text(this, st::infoEmptyLabel) {
}

void EmptyWidget::setFullHeight(rpl::producer<int> fullHeightValue) {
	std::move(
		fullHeightValue
	) | rpl::start_with_next([this](int fullHeight) {
		// Make icon center be on 1/3 height.
		auto iconCenter = fullHeight / 3;
		auto iconHeight = st::infoEmptyFile.height();
		auto iconTop = iconCenter - iconHeight / 2;
		_height = iconTop + st::infoEmptyIconTop;
		resizeToWidth(width());
	}, lifetime());
}

void EmptyWidget::setType(Type type) {
	_type = type;
	_icon = [&] {
		switch (_type) {
		case Type::Photo: return &st::infoEmptyPhoto;
		case Type::Video: return &st::infoEmptyVideo;
		case Type::MusicFile: return &st::infoEmptyAudio;
		case Type::File: return &st::infoEmptyFile;
		case Type::Link: return &st::infoEmptyLink;
		case Type::VoiceFile: return &st::infoEmptyVoice;
		}
		Unexpected("Bad type in EmptyWidget::setType()");
	}();
	update();
}

void EmptyWidget::setSearchQuery(const QString &query) {
	auto key = [&] {
		switch (_type) {
		case Type::Photo:
			return lng_media_photo_empty;
		case Type::Video:
			return lng_media_video_empty;
		case Type::MusicFile:
			return query.isEmpty()
				? lng_media_song_empty
				: lng_media_song_empty_search;
		case Type::File:
			return query.isEmpty()
				? lng_media_file_empty
				: lng_media_file_empty_search;
		case Type::Link:
			return query.isEmpty()
				? lng_media_link_empty
				: lng_media_link_empty_search;
		case Type::VoiceFile:
			return lng_media_audio_empty;
		}
		Unexpected("Bad type in EmptyWidget::setSearchQuery()");
	}();
	_text->setText(lang(key));
	resizeToWidth(width());
}

void EmptyWidget::paintEvent(QPaintEvent *e) {
	if (!_icon) {
		return;
	}

	Painter p(this);

	auto iconLeft = (width() - _icon->width()) / 2;
	auto iconTop = height() - st::infoEmptyIconTop;
	_icon->paint(p, iconLeft, iconTop, width());
}

int EmptyWidget::resizeGetHeight(int newWidth) {
	auto labelTop = _height - st::infoEmptyLabelTop;
	auto labelWidth = newWidth - 2 * st::infoEmptyLabelSkip;
	_text->resizeToNaturalWidth(labelWidth);

	auto labelLeft = (newWidth - _text->width()) / 2;
	_text->moveToLeft(labelLeft, labelTop, newWidth);

	update();
	return _height;
}

} // namespace Media
} // namespace Info
