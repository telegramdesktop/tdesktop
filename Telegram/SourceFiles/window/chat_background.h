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
#pragma once

namespace Window {

struct ChatBackgroundUpdate {
	enum class Type {
		New,
		Changed,
		Start,
	};

	ChatBackgroundUpdate(Type type, bool tiled) : type(type), tiled(tiled) {
	}
	Type type;
	bool tiled;
};

class ChatBackground : public base::Observable<ChatBackgroundUpdate> {
public:
	bool empty() const;
	void initIfEmpty();
	void init(int32 id, QPixmap &&image, QPixmap &&dog);
	void reset();

	int32 id() const;
	const QPixmap &image() const;
	const QPixmap &dog() const;
	bool tile() const;
	void setTile(bool tile);

private:
	int32 _id = 0;
	QPixmap _image;
	QPixmap _dog;
	bool _tile = false;

};

ChatBackground *chatBackground();

} // namespace Window
