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

#include "base/flags.h"
#include "inline_bots/inline_bot_layout_item.h"
#include "ui/effects/radial_animation.h"
#include "ui/text/text.h"

namespace InlineBots {
namespace Layout {
namespace internal {

class FileBase : public ItemBase {
public:
	FileBase(not_null<Context*> context, Result *result);
	// for saved gif layouts
	FileBase(not_null<Context*> context, DocumentData *doc);

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
	Gif(not_null<Context*> context, Result *result);
	Gif(not_null<Context*> context, DocumentData *doc, bool hasDeleteButton);

	void setPosition(int32 position) override;
	void initDimensions() override;

	bool isFullLine() const override {
		return false;
	}
	bool hasRightSkip() const override {
		return true;
	}

	void paint(Painter &p, const QRect &clip, const PaintContext *context) const override;
	void getState(ClickHandlerPtr &link, HistoryCursorState &cursor, QPoint point) const override;

	// ClickHandlerHost interface
	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override;

private:
	QSize countFrameSize() const;

	enum class StateFlag {
		Over       = (1 << 0),
		DeleteOver = (1 << 1),
	};
	using StateFlags = base::flags<StateFlag>;
	friend inline constexpr auto is_flag_type(StateFlag) { return true; };
	StateFlags _state;

	Media::Clip::ReaderPointer _gif;
	ClickHandlerPtr _delete;
	mutable QPixmap _thumb;
	void prepareThumb(int32 width, int32 height, const QSize &frame) const;

	void ensureAnimation() const;
	bool isRadialAnimation(TimeMs ms) const;
	void step_radial(TimeMs ms, bool timer);

	void clipCallback(Media::Clip::Notification notification);

	struct AnimationData {
		AnimationData(AnimationCallbacks &&callbacks)
			: over(false)
			, radial(std::move(callbacks)) {
		}
		bool over;
		Animation _a_over;
		Ui::RadialAnimation radial;
	};
	mutable std::unique_ptr<AnimationData> _animation;
	mutable Animation _a_deleteOver;

};

class Photo : public ItemBase {
public:
	Photo(not_null<Context*> context, Result *result);
	// Not used anywhere currently.
	//Photo(not_null<Context*> context, PhotoData *photo);

	void initDimensions() override;

	bool isFullLine() const override {
		return false;
	}
	bool hasRightSkip() const override {
		return true;
	}

	void paint(Painter &p, const QRect &clip, const PaintContext *context) const override;
	void getState(ClickHandlerPtr &link, HistoryCursorState &cursor, QPoint point) const override;

private:
	PhotoData *getShownPhoto() const;

	QSize countFrameSize() const;

	mutable QPixmap _thumb;
	mutable bool _thumbLoaded = false;
	void prepareThumb(int32 width, int32 height, const QSize &frame) const;

};

class Sticker : public FileBase {
public:
	Sticker(not_null<Context*> context, Result *result);
	// Not used anywhere currently.
	//Sticker(not_null<Context*> context, DocumentData *document);

	void initDimensions() override;

	bool isFullLine() const override {
		return false;
	}
	bool hasRightSkip() const override {
		return false;
	}
	void preload() const override;

	void paint(Painter &p, const QRect &clip, const PaintContext *context) const override;
	void getState(ClickHandlerPtr &link, HistoryCursorState &cursor, QPoint point) const override;

	// ClickHandlerHost interface
	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override;

private:
	QSize getThumbSize() const;

	mutable Animation _a_over;
	mutable bool _active = false;

	mutable QPixmap _thumb;
	mutable bool _thumbLoaded = false;
	void prepareThumb() const;

};

class Video : public FileBase {
public:
	Video(not_null<Context*> context, Result *result);

	void initDimensions() override;

	void paint(Painter &p, const QRect &clip, const PaintContext *context) const override;
	void getState(ClickHandlerPtr &link, HistoryCursorState &cursor, QPoint point) const override;

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
	File(not_null<Context*> context, Result *result);

	void initDimensions() override;

	void paint(Painter &p, const QRect &clip, const PaintContext *context) const override;
	void getState(ClickHandlerPtr &link, HistoryCursorState &cursor, QPoint point) const override;

	// ClickHandlerHost interface
	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override;

	~File();

private:
	void thumbAnimationCallback();
	void step_radial(TimeMs ms, bool timer);

	void ensureAnimation() const;
	void checkAnimationFinished() const;
	bool updateStatusText() const;

	bool isRadialAnimation(TimeMs ms) const {
		if (!_animation || !_animation->radial.animating()) return false;

		_animation->radial.step(ms);
		return _animation && _animation->radial.animating();
	}
	bool isThumbAnimation(TimeMs ms) const {
		if (_animation) {
			if (_animation->a_thumbOver.animating(ms)) {
				return true;
			}
			checkAnimationFinished();
		}
		return false;
	}

	struct AnimationData {
		AnimationData(AnimationCallbacks &&radialCallbacks) : radial(std::move(radialCallbacks)) {
		}
		Animation a_thumbOver;
		Ui::RadialAnimation radial;
	};
	mutable std::unique_ptr<AnimationData> _animation;

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
	Contact(not_null<Context*> context, Result *result);

	void initDimensions() override;
	int resizeGetHeight(int width) override;

	void paint(Painter &p, const QRect &clip, const PaintContext *context) const override;
	void getState(ClickHandlerPtr &link, HistoryCursorState &cursor, QPoint point) const override;

private:
	mutable QPixmap _thumb;
	Text _title, _description;

	void prepareThumb(int width, int height) const;

};

class Article : public ItemBase {
public:
	Article(not_null<Context*> context, Result *result, bool withThumb);

	void initDimensions() override;
	int resizeGetHeight(int width) override;

	void paint(Painter &p, const QRect &clip, const PaintContext *context) const override;
	void getState(ClickHandlerPtr &link, HistoryCursorState &cursor, QPoint point) const override;

private:
	ClickHandlerPtr _url, _link;

	bool _withThumb;
	mutable QPixmap _thumb;
	Text _title, _description;
	QString _thumbLetter, _urlText;
	int32 _urlWidth;

	void prepareThumb(int width, int height) const;

};

class Game : public ItemBase {
public:
	Game(not_null<Context*> context, Result *result);

	void setPosition(int32 position) override;
	void initDimensions() override;

	void paint(Painter &p, const QRect &clip, const PaintContext *context) const override;
	void getState(ClickHandlerPtr &link, HistoryCursorState &cursor, QPoint point) const override;

private:
	void countFrameSize();

	void prepareThumb(int32 width, int32 height) const;

	bool isRadialAnimation(TimeMs ms) const;
	void step_radial(TimeMs ms, bool timer);

	void clipCallback(Media::Clip::Notification notification);

	Media::Clip::ReaderPointer _gif;
	mutable QPixmap _thumb;
	mutable std::unique_ptr<Ui::RadialAnimation> _radial;
	Text _title, _description;

	QSize _frameSize;

};

} // namespace internal
} // namespace Layout
} // namespace InlineBots
