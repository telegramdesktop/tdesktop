/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_sticker_toast.h"

#include "ui/toast/toast.h"
#include "ui/toast/toast_widget.h"
#include "ui/widgets/buttons.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "lang/lang_keys.h"
#include "ui/text/text_utilities.h"
#include "boxes/sticker_set_box.h"
#include "lottie/lottie_single_player.h"
#include "window/window_session_controller.h"
#include "apiwrap.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

constexpr auto kPremiumToastDuration = 5 * crl::time(1000);

} // namespace

StickerToast::StickerToast(
	not_null<Window::SessionController*> controller,
	not_null<QWidget*> parent,
	Fn<void()> destroy)
: _controller(controller)
, _parent(parent)
, _destroy(std::move(destroy)) {
}

StickerToast::~StickerToast() {
	cancelRequest();
}

void StickerToast::showFor(not_null<DocumentData*> document) {
	const auto sticker = document->sticker();
	if (!sticker
		|| sticker->type != StickerType::Tgs
		|| !document->session().premiumPossible()) {
		return;
	} else if (const auto strong = _weak.get()) {
		if (_for == document) {
			return;
		}
		strong->hideAnimated();
	} else if (_setRequestId) {
		if (_for == document) {
			return;
		}
		cancelRequest();
	}
	_for = document;

	const auto title = lookupTitle();
	if (!title.isEmpty()) {
		showWithTitle(title);
	} else {
		requestSet();
	}
}

QString StickerToast::lookupTitle() const {
	Expects(_for != nullptr);

	const auto sticker = _for->sticker();
	if (!sticker) {
		return {};
	}

	const auto id = sticker->set.id;
	if (!id) {
		return {};
	}

	const auto &sets = _for->owner().stickers().sets();
	const auto i = sets.find(id);
	if (i == end(sets)) {
		return {};
	}
	return i->second->title;
}

void StickerToast::requestSet() {
	Expects(_for != nullptr);

	if (const auto sticker = _for->sticker()) {
		const auto api = &_controller->session().api();
		_setRequestId = api->request(MTPmessages_GetStickerSet(
			Data::InputStickerSet(sticker->set),
			MTP_int(0) // hash
		)).done([=](const MTPmessages_StickerSet &result) {
			_setRequestId = 0;
			result.match([&](const MTPDmessages_stickerSet &data) {
				data.vset().match([&](const MTPDstickerSet &data) {
					const auto owner = &_controller->session().data();
					showWithTitle(owner->stickers().getSetTitle(data));
				});
			}, [&](const MTPDmessages_stickerSetNotModified &) {
				LOG(("API Error: Got messages.stickerSetNotModified."));
			});
		}).fail([=] {
			_setRequestId = 0;
		}).send();
	}
}

void StickerToast::cancelRequest() {
	_controller->session().api().request(base::take(_setRequestId)).cancel();
}

void StickerToast::showWithTitle(const QString &title) {
	Expects(_for != nullptr);

	const auto text = Ui::Text::Bold(
		title
	).append('\n').append(
		tr::lng_sticker_premium_text(tr::now)
	);
	_st = st::historyPremiumToast;
	const auto skip = _st.padding.top();
	const auto size = _st.style.font->height * 2;
	const auto view = tr::lng_sticker_premium_view(tr::now);
	_st.padding.setLeft(skip + size + skip);
	_st.padding.setRight(st::historyPremiumViewSet.font->width(view)
		- st::historyPremiumViewSet.width);
	_weak = Ui::Toast::Show(_parent, Ui::Toast::Config{
		.text = text,
		.st = &_st,
		.durationMs = kPremiumToastDuration,
		.multiline = true,
		.dark = true,
		.slideSide = RectPart::Bottom,
	});
	const auto strong = _weak.get();
	if (!strong) {
		return;
	}
	strong->setInputUsed(true);
	const auto widget = strong->widget();
	const auto button = Ui::CreateChild<Ui::RoundButton>(
		widget.get(),
		rpl::single(view),
		st::historyPremiumViewSet);
	button->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
	button->show();
	rpl::combine(
		widget->sizeValue(),
		button->sizeValue()
	) | rpl::start_with_next([=](QSize outer, QSize inner) {
		button->moveToRight(
			0,
			(outer.height() - inner.height()) / 2,
			outer.width());
	}, widget->lifetime());
	const auto preview = Ui::CreateChild<Ui::RpWidget>(widget.get());
	preview->moveToLeft(skip, skip);
	preview->resize(size, size);
	preview->show();

	const auto bytes = _for->createMediaView()->bytes();
	const auto filepath = _for->filepath();
	const auto player = preview->lifetime().make_state<Lottie::SinglePlayer>(
		Lottie::ReadContent(bytes, filepath),
		Lottie::FrameRequest{ QSize(size, size) },
		Lottie::Quality::Default);
	preview->paintRequest(
	) | rpl::start_with_next([=] {
		if (!player->ready()) {
			return;
		}
		const auto image = player->frame();
		QPainter(preview).drawImage(
			QRect(QPoint(), image.size() / image.devicePixelRatio()),
			image);
		player->markFrameShown();
	}, preview->lifetime());
	player->updates(
	) | rpl::start_with_next([=] {
		preview->update();
	}, preview->lifetime());

	button->setClickedCallback([=, weak = _weak] {
		_controller->show(
			Box<StickerSetBox>(_controller, _for->sticker()->set),
			Ui::LayerOption::KeepOther);
		if (const auto strong = weak.get()) {
			strong->hideAnimated();
		}
	});
}

} // namespace HistoryView
