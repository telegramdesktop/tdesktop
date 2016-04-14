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

#include "inline_bots/inline_bot_layout_item.h"
#include "ui/text/text.h"

namespace InlineBots {
namespace Layout {
namespace internal {

class FileBase : public ItemBase {
public:
	FileBase(Result *result);
	// for saved gif layouts
	FileBase(DocumentData *doc);

protected:
	DocumentData *getShownDocument() const;

	int content_width() const;
	int content_height() const;
	int content_duration() const;
	ImagePtr content_thumb() const;
};

class DeleteSavedGifClickHandler : public LeftButtonClickHandler {
public:
	DeleteSavedGifClickHandler(DocumentData *data) : _data(data) {
	}

protected:
	void onClickImpl() const override;

private:
	DocumentData  *_data;

};

class Gif : public FileBase {
public:
	Gif(Result *result);
	Gif(DocumentData *doc, bool hasDeleteButton);

	void setPosition(int32 position) override;
	void initDimensions() override;

	bool isFullLine() const override {
		return false;
	}
	bool hasRightSkip() const override {
		return true;
	}

	void paint(Painter &p, const QRect &clip, const PaintContext *context) const override;
	void getState(ClickHandlerPtr &link, HistoryCursorState &cursor, int x, int y) const override;

	// ClickHandlerHost interface
	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override;

	~Gif();

private:

	QSize countFrameSize() const;

	enum class StateFlag {
		Over = 0x01,
		DeleteOver = 0x02,
	};
	Q_DECLARE_FLAGS(StateFlags, StateFlag);
	StateFlags _state;
	friend inline StateFlags operator~(StateFlag flag) {
		return ~StateFlags(flag);
	}

	ClipReader *_gif = nullptr;
	ClickHandlerPtr _delete;
	bool gif() const {
		return (!_gif || _gif == BadClipReader) ? false : true;
	}
	mutable QPixmap _thumb;
	void prepareThumb(int32 width, int32 height, const QSize &frame) const;

	void ensureAnimation() const;
	bool isRadialAnimation(uint64 ms) const;
	void step_radial(uint64 ms, bool timer);

	void clipCallback(ClipReaderNotification notification);

	struct AnimationData {
		AnimationData(AnimationCreator creator)
			: over(false)
			, radial(creator) {
		}
		bool over;
		FloatAnimation _a_over;
		RadialAnimation radial;
	};
	mutable AnimationData *_animation = nullptr;
	mutable FloatAnimation _a_deleteOver;

};

class Photo : public ItemBase {
public:
	Photo(Result *result);
	// Not used anywhere currently.
	//LayoutInlinePhoto(PhotoData *photo);

	void initDimensions() override;

	bool isFullLine() const override {
		return false;
	}
	bool hasRightSkip() const override {
		return true;
	}

	void paint(Painter &p, const QRect &clip, const PaintContext *context) const override;
	void getState(ClickHandlerPtr &link, HistoryCursorState &cursor, int x, int y) const override;

private:
	PhotoData *getShownPhoto() const;

	QSize countFrameSize() const;

	mutable QPixmap _thumb;
	mutable bool _thumbLoaded = false;
	void prepareThumb(int32 width, int32 height, const QSize &frame) const;

};

class Sticker : public FileBase {
public:
	Sticker(Result *result);
	// Not used anywhere currently.
	//LayoutInlineSticker(DocumentData *document);

	void initDimensions() override;

	bool isFullLine() const override {
		return false;
	}
	bool hasRightSkip() const override {
		return false;
	}
	void preload() const override;

	void paint(Painter &p, const QRect &clip, const PaintContext *context) const override;
	void getState(ClickHandlerPtr &link, HistoryCursorState &cursor, int x, int y) const override;

	// ClickHandlerHost interface
	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override;

private:

	QSize getThumbSize() const;

	mutable FloatAnimation _a_over;
	mutable bool _active = false;

	mutable QPixmap _thumb;
	mutable bool _thumbLoaded = false;
	void prepareThumb() const;

};

class Video : public FileBase {
public:
	Video(Result *result);

	void initDimensions() override;

	void paint(Painter &p, const QRect &clip, const PaintContext *context) const override;
	void getState(ClickHandlerPtr &link, HistoryCursorState &cursor, int x, int y) const override;

private:

	ClickHandlerPtr _link;

	mutable QPixmap _thumb;
	Text _title, _description;
	QString _duration;
	int _durationWidth = 0;

	void prepareThumb(int32 width, int32 height) const;

};

class OpenFileClickHandler : public LeftButtonClickHandler {
public:
	OpenFileClickHandler(Result *result) : _result(result) {
	}

protected:
	void onClickImpl() const override;

private:
	Result *_result;

};

class CancelFileClickHandler : public LeftButtonClickHandler {
public:
	CancelFileClickHandler(Result *result) : _result(result) {
	}

protected:
	void onClickImpl() const override;

private:
	Result *_result;

};

class File : public FileBase {
public:
	File(Result *result);

	void initDimensions() override;

	void paint(Painter &p, const QRect &clip, const PaintContext *context) const override;
	void getState(ClickHandlerPtr &link, HistoryCursorState &cursor, int x, int y) const override;

	// ClickHandlerHost interface
	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override;

	~File();

private:
	void step_thumbOver(float64 ms, bool timer);
	void step_radial(uint64 ms, bool timer);

	void ensureAnimation() const;
	void checkAnimationFinished();
	bool updateStatusText() const;

	bool isRadialAnimation(uint64 ms) const {
		if (!_animation || !_animation->radial.animating()) return false;

		_animation->radial.step(ms);
		return _animation && _animation->radial.animating();
	}
	bool isThumbAnimation(uint64 ms) const {
		if (!_animation || !_animation->_a_thumbOver.animating()) return false;

		_animation->_a_thumbOver.step(ms);
		return _animation && _animation->_a_thumbOver.animating();
	}

	struct AnimationData {
		AnimationData(AnimationCreator thumbOverCallbacks, AnimationCreator radialCallbacks) : a_thumbOver(0, 0)
			, _a_thumbOver(thumbOverCallbacks)
			, radial(radialCallbacks) {
		}
		anim::fvalue a_thumbOver;
		Animation _a_thumbOver;

		RadialAnimation radial;
	};
	mutable std_::unique_ptr<AnimationData> _animation;

	Text _title, _description;
	ClickHandlerPtr _open, _cancel;

	// >= 0 will contain download / upload string, _statusSize = loaded bytes
	// < 0 will contain played string, _statusSize = -(seconds + 1) played
	// 0x7FFFFFF0 will contain status for not yet downloaded file
	// 0x7FFFFFF1 will contain status for already downloaded file
	// 0x7FFFFFF2 will contain status for failed to download / upload file
	mutable int32 _statusSize;
	mutable QString _statusText;

	// duration = -1 - no duration, duration = -2 - "GIF" duration
	void setStatusSize(int32 newSize, int32 fullSize, int32 duration, qint64 realDuration) const;

};

class Contact : public ItemBase {
public:
	Contact(Result *result);

	void initDimensions() override;
	int resizeGetHeight(int width) override;

	void paint(Painter &p, const QRect &clip, const PaintContext *context) const override;
	void getState(ClickHandlerPtr &link, HistoryCursorState &cursor, int x, int y) const override;

private:

	mutable QPixmap _thumb;
	Text _title, _description;

	void prepareThumb(int width, int height) const;

};

class Article : public ItemBase {
public:
	Article(Result *result, bool withThumb);

	void initDimensions() override;
	int resizeGetHeight(int width) override;

	void paint(Painter &p, const QRect &clip, const PaintContext *context) const override;
	void getState(ClickHandlerPtr &link, HistoryCursorState &cursor, int x, int y) const override;

private:

	ClickHandlerPtr _url, _link;

	bool _withThumb;
	mutable QPixmap _thumb;
	Text _title, _description;
	QString _thumbLetter, _urlText;
	int32 _urlWidth;

	void prepareThumb(int width, int height) const;

};

} // namespace internal
} // namespace Layout
} // namespace InlineBots
