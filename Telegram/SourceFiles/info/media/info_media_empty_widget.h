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
#pragma once

#include "ui/rp_widget.h"
#include "info/media/info_media_widget.h"

namespace Ui {
class FlatLabel;
} // namespace Ui

namespace Info {
namespace Media {

class EmptyWidget : public Ui::RpWidget {
public:
	EmptyWidget(QWidget *parent);

	void setFullHeight(rpl::producer<int> fullHeightValue);
	void setType(Type type);
	void setSearchQuery(const QString &query);

protected:
	void paintEvent(QPaintEvent *e) override;

	int resizeGetHeight(int newWidth) override;

private:
	object_ptr<Ui::FlatLabel> _text;
	Type _type = Type::kCount;
	const style::icon *_icon = nullptr;
	int _height = 0;

};

} // namespace Media
} // namespace Info
