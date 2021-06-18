/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_media.h"
#include "ui/effects/animations.h"
#include "ui/effects/radial_animation.h"

class FileClickHandler;

namespace HistoryView {

class File : public Media, public base::has_weak_ptr {
public:
	File(
		not_null<Element*> parent,
		not_null<HistoryItem*> realParent)
	: Media(parent)
	, _realParent(realParent) {
	}

	[[nodiscard]] bool toggleSelectionByHandlerClick(
		const ClickHandlerPtr &p) const override;
	[[nodiscard]] bool dragItemByHandler(
		const ClickHandlerPtr &p) const override;

	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override;
	void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) override;

	void refreshParentId(not_null<HistoryItem*> realParent) override;

	[[nodiscard]] bool allowsFastShare() const override {
		return true;
	}

	~File();

protected:
	using FileClickHandlerPtr = std::shared_ptr<FileClickHandler>;

	not_null<HistoryItem*> _realParent;
	FileClickHandlerPtr _openl, _savel, _cancell;

	void setLinks(
		FileClickHandlerPtr &&openl,
		FileClickHandlerPtr &&savel,
		FileClickHandlerPtr &&cancell);
	void setDocumentLinks(
		not_null<DocumentData*> document,
		not_null<HistoryItem*> realParent);

	// >= 0 will contain download / upload string, _statusSize = loaded bytes
	// < 0 will contain played string, _statusSize = -(seconds + 1) played
	// 0x7FFFFFF0 will contain status for not yet downloaded file
	// 0x7FFFFFF1 will contain status for already downloaded file
	// 0x7FFFFFF2 will contain status for failed to download / upload file
	mutable int _statusSize;
	mutable QString _statusText;

	// duration = -1 - no duration, duration = -2 - "GIF" duration
	void setStatusSize(int newSize, int fullSize, int duration, qint64 realDuration) const;

	void radialAnimationCallback(crl::time now) const;
	void thumbAnimationCallback();

	void ensureAnimation() const;
	void checkAnimationFinished() const;

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

	virtual float64 dataProgress() const = 0;
	virtual bool dataFinished() const = 0;
	virtual bool dataLoaded() const = 0;

	struct AnimationData {
		template <typename Callback>
		AnimationData(Callback &&radialCallback)
		: radial(std::forward<Callback>(radialCallback)) {
		}

		Ui::Animations::Simple a_thumbOver;
		Ui::RadialAnimation radial;
	};
	mutable std::unique_ptr<AnimationData> _animation;

};

} // namespace HistoryView
