/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"
#include "inline_bots/inline_bot_layout_item.h"
#include "media/clip/media_clip_reader.h"
#include "ui/effects/animations.h"
#include "ui/effects/radial_animation.h"
#include "ui/text/text.h"

namespace Lottie {
class SinglePlayer;
} // namespace Lottie

namespace Data {
class PhotoMedia;
class DocumentMedia;
} // namespace Data

namespace InlineBots {
namespace Layout {
namespace internal {

class FileBase : public ItemBase {
public:
	FileBase(not_null<Context*> context, not_null<Result*> result);

	// For saved gif layouts.
	FileBase(not_null<Context*> context, not_null<DocumentData*> document);

protected:
	DocumentData *getShownDocument() const;

	int content_width() const;
	int content_height() const;
	int content_duration() const;

};

class DeleteSavedGifClickHandler : public LeftButtonClickHandler {
public:
	DeleteSavedGifClickHandler(not_null<DocumentData*> data) : _data(data) {
	}

protected:
	void onClickImpl() const override;

private:
	const not_null<DocumentData*> _data;

};

class Gif final : public FileBase {
public:
	Gif(not_null<Context*> context, not_null<Result*> result);
	Gif(
		not_null<Context*> context,
		not_null<DocumentData*> document,
		bool hasDeleteButton);

	void setPosition(int32 position) override;
	void initDimensions() override;

	bool isFullLine() const override {
		return false;
	}
	bool hasRightSkip() const override {
		return true;
	}

	void paint(Painter &p, const QRect &clip, const PaintContext *context) const override;
	TextState getState(
		QPoint point,
		StateRequest request) const override;

	// ClickHandlerHost interface
	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override;

	int resizeGetHeight(int width) override;

	void unloadHeavyPart() override;

private:
	enum class StateFlag {
		Over = (1 << 0),
		DeleteOver = (1 << 1),
	};
	using StateFlags = base::flags<StateFlag>;
	friend inline constexpr auto is_flag_type(StateFlag) { return true; };

	struct AnimationData {
		template <typename Callback>
		AnimationData(Callback &&callback)
			: radial(std::forward<Callback>(callback)) {
		}
		bool over = false;
		Ui::Animations::Simple _a_over;
		Ui::RadialAnimation radial;
	};

	void ensureDataMediaCreated(not_null<DocumentData*> document) const;
	QSize countFrameSize() const;

	void validateThumbnail(
		Image *image,
		QSize size,
		QSize frame,
		bool good) const;
	void prepareThumbnail(QSize size, QSize frame) const;

	void ensureAnimation() const;
	bool isRadialAnimation() const;
	void radialAnimationCallback(crl::time now) const;

	void clipCallback(Media::Clip::Notification notification);

	StateFlags _state;

	Media::Clip::ReaderPointer _gif;
	ClickHandlerPtr _delete;
	mutable QPixmap _thumb;
	mutable bool _thumbGood = false;

	mutable std::shared_ptr<Data::DocumentMedia> _dataMedia;

	mutable std::unique_ptr<AnimationData> _animation;
	mutable Ui::Animations::Simple _a_deleteOver;

};

class Photo : public ItemBase {
public:
	Photo(not_null<Context*> context, not_null<Result*> result);
	// Not used anywhere currently.
	//Photo(not_null<Context*> context, not_null<PhotoData*> photo);

	void initDimensions() override;

	bool isFullLine() const override {
		return false;
	}
	bool hasRightSkip() const override {
		return true;
	}

	void paint(Painter &p, const QRect &clip, const PaintContext *context) const override;
	TextState getState(
		QPoint point,
		StateRequest request) const override;

	void unloadHeavyPart() override;

private:
	PhotoData *getShownPhoto() const;

	QSize countFrameSize() const;

	mutable QPixmap _thumb;
	mutable bool _thumbGood = false;
	void prepareThumbnail(QSize size, QSize frame) const;
	void validateThumbnail(
		Image *image,
		QSize size,
		QSize frame,
		bool good) const;

	mutable std::shared_ptr<Data::PhotoMedia> _photoMedia;

};

class Sticker : public FileBase {
public:
	Sticker(not_null<Context*> context, not_null<Result*> result);
	~Sticker();
	// Not used anywhere currently.
	//Sticker(not_null<Context*> context, not_null<DocumentData*> document);

	void initDimensions() override;

	bool isFullLine() const override {
		return false;
	}
	bool hasRightSkip() const override {
		return false;
	}
	void preload() const override;

	void paint(Painter &p, const QRect &clip, const PaintContext *context) const override;
	TextState getState(
		QPoint point,
		StateRequest request) const override;

	// ClickHandlerHost interface
	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override;

	void unloadHeavyPart() override;

private:
	void ensureDataMediaCreated(not_null<DocumentData*> document) const;
	void setupLottie() const;
	QSize getThumbSize() const;
	void prepareThumbnail() const;

	mutable Ui::Animations::Simple _a_over;
	mutable bool _active = false;

	mutable QPixmap _thumb;
	mutable bool _thumbLoaded = false;

	mutable std::unique_ptr<Lottie::SinglePlayer> _lottie;
	mutable std::shared_ptr<Data::DocumentMedia> _dataMedia;
	mutable rpl::lifetime _lifetime;

};

class Video : public FileBase {
public:
	Video(not_null<Context*> context, not_null<Result*> result);

	void initDimensions() override;

	void paint(Painter &p, const QRect &clip, const PaintContext *context) const override;
	TextState getState(
		QPoint point,
		StateRequest request) const override;

	void unloadHeavyPart() override;

private:
	ClickHandlerPtr _link;

	mutable QPixmap _thumb;
	mutable std::shared_ptr<Data::DocumentMedia> _documentMedia;
	Ui::Text::String _title, _description;
	QString _duration;
	int _durationWidth = 0;

	[[nodiscard]] bool withThumbnail() const;
	void prepareThumbnail(QSize size) const;

};

class CancelFileClickHandler : public LeftButtonClickHandler {
public:
	CancelFileClickHandler(not_null<Result*> result) : _result(result) {
	}

protected:
	void onClickImpl() const override;

private:
	not_null<Result*> _result;

};

class File : public FileBase {
public:
	File(not_null<Context*> context, not_null<Result*> result);
	~File();

	void initDimensions() override;

	void paint(Painter &p, const QRect &clip, const PaintContext *context) const override;
	TextState getState(
		QPoint point,
		StateRequest request) const override;

	// ClickHandlerHost interface
	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override;

	void unloadHeavyPart() override;

private:
	void thumbAnimationCallback();
	void radialAnimationCallback(crl::time now) const;

	void ensureAnimation() const;
	void ensureDataMediaCreated() const;
	void checkAnimationFinished() const;
	bool updateStatusText() const;

	bool isRadialAnimation() const {
		if (_animation) {
			if (_animation->radial.animating()) {
				return true;
			}
			checkAnimationFinished();
		}
		return false;
	}
	bool isThumbAnimation() const {
		if (_animation) {
			if (_animation->a_thumbOver.animating()) {
				return true;
			}
			checkAnimationFinished();
		}
		return false;
	}

	struct AnimationData {
		template <typename Callback>
		AnimationData(Callback &&radialCallback)
		: radial(std::forward<Callback>(radialCallback)) {
		}
		Ui::Animations::Simple a_thumbOver;
		Ui::RadialAnimation radial;
	};
	mutable std::unique_ptr<AnimationData> _animation;

	Ui::Text::String _title, _description;
	ClickHandlerPtr _cancel;

	// >= 0 will contain download / upload string, _statusSize = loaded bytes
	// < 0 will contain played string, _statusSize = -(seconds + 1) played
	// 0x7FFFFFF0 will contain status for not yet downloaded file
	// 0x7FFFFFF1 will contain status for already downloaded file
	// 0x7FFFFFF2 will contain status for failed to download / upload file
	mutable int32 _statusSize;
	mutable QString _statusText;

	// duration = -1 - no duration, duration = -2 - "GIF" duration
	void setStatusSize(int32 newSize, int32 fullSize, int32 duration, qint64 realDuration) const;

	not_null<DocumentData*> _document;
	mutable std::shared_ptr<Data::DocumentMedia> _documentMedia;

};

class Contact : public ItemBase {
public:
	Contact(not_null<Context*> context, not_null<Result*> result);

	void initDimensions() override;

	void paint(Painter &p, const QRect &clip, const PaintContext *context) const override;
	TextState getState(
		QPoint point,
		StateRequest request) const override;

private:
	mutable QPixmap _thumb;
	Ui::Text::String _title, _description;

	void prepareThumbnail(int width, int height) const;

};

class Article : public ItemBase {
public:
	Article(not_null<Context*> context, not_null<Result*> result, bool withThumb);

	void initDimensions() override;
	int resizeGetHeight(int width) override;

	void paint(Painter &p, const QRect &clip, const PaintContext *context) const override;
	TextState getState(
		QPoint point,
		StateRequest request) const override;

private:
	ClickHandlerPtr _url, _link;

	bool _withThumb;
	mutable QPixmap _thumb;
	Ui::Text::String _title, _description;
	QString _thumbLetter, _urlText;
	int32 _urlWidth;

	void prepareThumbnail(int width, int height) const;

};

class Game : public ItemBase {
public:
	Game(not_null<Context*> context, not_null<Result*> result);

	void setPosition(int32 position) override;
	void initDimensions() override;

	void paint(Painter &p, const QRect &clip, const PaintContext *context) const override;
	TextState getState(
		QPoint point,
		StateRequest request) const override;

	void unloadHeavyPart() override;

private:
	void ensureDataMediaCreated(not_null<PhotoData*> photo) const;
	void ensureDataMediaCreated(not_null<DocumentData*> document) const;
	void countFrameSize();

	void prepareThumbnail(QSize size) const;
	void validateThumbnail(Image *image, QSize size, bool good) const;

	bool isRadialAnimation() const;
	void radialAnimationCallback(crl::time now) const;

	void clipCallback(Media::Clip::Notification notification);

	Media::Clip::ReaderPointer _gif;
	mutable std::shared_ptr<Data::PhotoMedia> _photoMedia;
	mutable std::shared_ptr<Data::DocumentMedia> _documentMedia;
	mutable QPixmap _thumb;
	mutable bool _thumbGood = false;
	mutable std::unique_ptr<Ui::RadialAnimation> _radial;
	Ui::Text::String _title, _description;

	QSize _frameSize;

};

} // namespace internal
} // namespace Layout
} // namespace InlineBots
