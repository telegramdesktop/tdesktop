/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_file.h"

#include "lang/lang_keys.h"
#include "ui/text/format_values.h"
#include "history/history_item.h"
#include "history/history.h"
#include "history/view/history_view_element.h"
#include "data/data_document.h"
#include "data/data_file_click_handler.h"
#include "data/data_session.h"
#include "styles/style_chat.h"

namespace HistoryView {

bool File::toggleSelectionByHandlerClick(const ClickHandlerPtr &p) const {
	return p == _openl || p == _savel || p == _cancell;
}
bool File::dragItemByHandler(const ClickHandlerPtr &p) const {
	return p == _openl || p == _savel || p == _cancell;
}

void File::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	if (p == _savel || p == _cancell) {
		if (active && !dataLoaded()) {
			ensureAnimation();
			_animation->a_thumbOver.start([this] { thumbAnimationCallback(); }, 0., 1., st::msgFileOverDuration);
		} else if (!active && _animation && !dataLoaded()) {
			_animation->a_thumbOver.start([this] { thumbAnimationCallback(); }, 1., 0., st::msgFileOverDuration);
		}
	}
}

void File::thumbAnimationCallback() {
	history()->owner().requestViewRepaint(_parent);
}

void File::clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) {
	history()->owner().requestViewRepaint(_parent);
}

void File::setLinks(
		FileClickHandlerPtr &&openl,
		FileClickHandlerPtr &&savel,
		FileClickHandlerPtr &&cancell) {
	_openl = std::move(openl);
	_savel = std::move(savel);
	_cancell = std::move(cancell);
}

void File::refreshParentId(not_null<HistoryItem*> realParent) {
	const auto contextId = realParent->fullId();
	_openl->setMessageId(contextId);
	_savel->setMessageId(contextId);
	_cancell->setMessageId(contextId);
}

void File::setStatusSize(int newSize, int fullSize, int duration, qint64 realDuration) const {
	_statusSize = newSize;
	if (_statusSize == Ui::FileStatusSizeReady) {
		_statusText = (duration >= 0) ? Ui::FormatDurationAndSizeText(duration, fullSize) : (duration < -1 ? Ui::FormatGifAndSizeText(fullSize) : Ui::FormatSizeText(fullSize));
	} else if (_statusSize == Ui::FileStatusSizeLoaded) {
		_statusText = (duration >= 0) ? Ui::FormatDurationText(duration) : (duration < -1 ? qsl("GIF") : Ui::FormatSizeText(fullSize));
	} else if (_statusSize == Ui::FileStatusSizeFailed) {
		_statusText = tr::lng_attach_failed(tr::now);
	} else if (_statusSize >= 0) {
		_statusText = Ui::FormatDownloadText(_statusSize, fullSize);
	} else {
		_statusText = Ui::FormatPlayedText(-_statusSize - 1, realDuration);
	}
}

void File::radialAnimationCallback(crl::time now) const {
	const auto updated = [&] {
		return _animation->radial.update(
			dataProgress(),
			dataFinished(),
			now);
	}();
	if (!anim::Disabled() || updated) {
		history()->owner().requestViewRepaint(_parent);
	}
	if (!_animation->radial.animating()) {
		checkAnimationFinished();
	}
}

void File::ensureAnimation() const {
	if (!_animation) {
		_animation = std::make_unique<AnimationData>([=](crl::time now) {
			radialAnimationCallback(now);
		});
	}
}

void File::checkAnimationFinished() const {
	if (_animation && !_animation->a_thumbOver.animating() && !_animation->radial.animating()) {
		if (dataLoaded()) {
			_animation.reset();
		}
	}
}
void File::setDocumentLinks(
		not_null<DocumentData*> document,
		not_null<HistoryItem*> realParent) {
	const auto context = realParent->fullId();
	setLinks(
		std::make_shared<DocumentOpenClickHandler>(
			document,
			crl::guard(this, [=](FullMsgId id) {
				_parent->delegate()->elementOpenDocument(document, id);
			}),
			context),
		std::make_shared<DocumentSaveClickHandler>(document, context),
		std::make_shared<DocumentCancelClickHandler>(
			document,
			crl::guard(this, [=](FullMsgId id) {
				_parent->delegate()->elementCancelUpload(id);
			}),
			context));
}

File::~File() = default;

} // namespace HistoryView
