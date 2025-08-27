/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "ui/chat/chat_style.h"

class Painter;

namespace Data {
class Story;
} // namespace Data

namespace Ui {
class RippleAnimation;
} // namespace Ui

namespace Media::Stories {

class Controller;
struct RepostClickHandler;

class RepostView final
	: public base::has_weak_ptr
	, public ClickHandlerHost {
public:
	RepostView(
		not_null<Controller*> controller,
		not_null<Data::Story*> story);
	~RepostView();

	[[nodiscard]] int height() const;
	void draw(Painter &p, int x, int y, int availableWidth);
	[[nodiscard]] RepostClickHandler lookupHandler(QPoint position);

	[[nodiscard]] PeerData *fromPeer() const;
	[[nodiscard]] QString fromName() const;

private:
	void recountDimensions();

	void clickHandlerPressedChanged(
		const ClickHandlerPtr &action,
		bool pressed);

	const not_null<Controller*> _controller;
	const not_null<Data::Story*> _story;
	PeerData *_sourcePeer = nullptr;
	ClickHandlerPtr _link;
	std::unique_ptr<Ui::RippleAnimation> _ripple;

	Ui::Text::String _name;
	Ui::Text::String _text;
	Ui::Text::QuotePaintCache _quoteCache;
	Ui::BackgroundEmojiData _backgroundEmojiData;
	Ui::ColorIndicesCompressed _colorIndices;
	QPoint _lastPosition;
	mutable int _lastWidth = 0;
	uint32 _maxWidth : 31 = 0;
	uint32 _loading : 1 = 0;

	rpl::lifetime _lifetime;

};

} // namespace Media::Stories
