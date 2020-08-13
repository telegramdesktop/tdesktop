/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"

class PeerData;
class Image;

namespace Data {
class CloudImageView;
class PhotoMedia;
} // namespace Data

namespace Calls {

class Userpic final {
public:
	Userpic(
		not_null<QWidget*> parent,
		not_null<PeerData*> peer,
		rpl::producer<bool> muted);
	~Userpic();

	void setVisible(bool visible);
	void setGeometry(int x, int y, int size);
	void setMuteLayout(QPoint position, int size, int stroke);

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _content.lifetime();
	}

private:
	void setup(rpl::producer<bool> muted);

	void paint();
	void setMuted(bool muted);
	[[nodiscard]] int size() const;

	void processPhoto();
	void refreshPhoto();
	[[nodiscard]] bool isGoodPhoto(PhotoData *photo) const;
	void createCache(Image *image);

	Ui::RpWidget _content;

	not_null<PeerData*> _peer;
	std::shared_ptr<Data::CloudImageView> _userpic;
	std::shared_ptr<Data::PhotoMedia> _photo;
	Ui::Animations::Simple _mutedAnimation;
	QPixmap _userPhoto;
	PhotoId _userPhotoId = 0;
	QPoint _mutePosition;
	int _muteSize = 0;
	int _muteStroke = 0;
	bool _userPhotoFull = false;
	bool _muted = false;

};

} // namespace Calls
