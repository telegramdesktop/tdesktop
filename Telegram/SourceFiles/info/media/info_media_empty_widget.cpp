/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
		case Type::Photo:
		case Type::GIF: return &st::infoEmptyPhoto;
		case Type::Video: return &st::infoEmptyVideo;
		case Type::MusicFile: return &st::infoEmptyAudio;
		case Type::File: return &st::infoEmptyFile;
		case Type::Link: return &st::infoEmptyLink;
		case Type::RoundVoiceFile: return &st::infoEmptyVoice;
		}
		Unexpected("Bad type in EmptyWidget::setType()");
	}();
	update();
}

void EmptyWidget::setSearchQuery(const QString &query) {
	_text->setText([&] {
		switch (_type) {
		case Type::Photo:
			return tr::lng_media_photo_empty(tr::now);
		case Type::GIF:
			return tr::lng_media_gif_empty(tr::now);
		case Type::Video:
			return tr::lng_media_video_empty(tr::now);
		case Type::MusicFile:
			return query.isEmpty()
				? tr::lng_media_song_empty(tr::now)
				: tr::lng_media_song_empty_search(tr::now);
		case Type::File:
			return query.isEmpty()
				? tr::lng_media_file_empty(tr::now)
				: tr::lng_media_file_empty_search(tr::now);
		case Type::Link:
			return query.isEmpty()
				? tr::lng_media_link_empty(tr::now)
				: tr::lng_media_link_empty_search(tr::now);
		case Type::RoundVoiceFile:
			return tr::lng_media_audio_empty(tr::now);
		}
		Unexpected("Bad type in EmptyWidget::setSearchQuery()");
	}());
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
