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
#include "data/stickers/data_custom_emoji.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "lang/lang_keys.h"
#include "ui/text/text_utilities.h"
#include "boxes/sticker_set_box.h"
#include "boxes/premium_preview_box.h"
#include "lottie/lottie_single_player.h"
#include "window/window_session_controller.h"
#include "settings/settings_premium.h"
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
	_hiding.push_back(_weak);
	for (const auto &weak : base::take(_hiding)) {
		if (const auto strong = weak.get()) {
			delete strong->widget();
		}
	}
}

void StickerToast::showFor(
		not_null<DocumentData*> document,
		Section section) {
	const auto sticker = document->sticker();
	if (!sticker || !document->session().premiumPossible()) {
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
	_section = section;

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

	static auto counter = 0;
	const auto setType = _for->sticker()->setType;
	const auto isEmoji = (_section == Section::TopicIcon)
		|| (setType == Data::StickersType::Emoji);
	const auto toSaved = isEmoji
		&& (_section == Section::Message)
		&& !(++counter % 2);
	const auto text = Ui::Text::Bold(
		title
	).append('\n').append(
		(toSaved
			? tr::lng_animated_emoji_saved(tr::now, Ui::Text::RichLangValue)
			: isEmoji
			? tr::lng_animated_emoji_text(tr::now, Ui::Text::RichLangValue)
			: tr::lng_sticker_premium_text(tr::now, Ui::Text::RichLangValue))
	);
	_st = st::historyPremiumToast;
	const auto skip = _st.padding.top();
	const auto size = _st.style.font->height * 2;
	const auto view = toSaved
		? tr::lng_animated_emoji_saved_open(tr::now)
		: tr::lng_sticker_premium_view(tr::now);
	_st.padding.setLeft(skip + size + skip);
	_st.padding.setRight(st::historyPremiumViewSet.font->width(view)
		- st::historyPremiumViewSet.width);

	clearHiddenHiding();
	if (_weak.get()) {
		_hiding.push_back(_weak);
	}

	_weak = Ui::Toast::Show(_parent, Ui::Toast::Config{
		.text = text,
		.st = &_st,
		.duration = kPremiumToastDuration,
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
	const auto hideToast = [weak = _weak] {
		if (const auto strong = weak.get()) {
			strong->hideAnimated();
		}
	};

	const auto clickableBackground = Ui::CreateChild<Ui::AbstractButton>(
		widget.get());
	clickableBackground->setPointerCursor(false);
	clickableBackground->setAcceptBoth();
	clickableBackground->show();
	clickableBackground->addClickHandler([=](Qt::MouseButton button) {
		if (button == Qt::RightButton) {
			hideToast();
		}
	});

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
		clickableBackground->resize(outer);
	}, widget->lifetime());
	const auto preview = Ui::CreateChild<Ui::RpWidget>(widget.get());
	preview->moveToLeft(skip, skip);
	preview->resize(size, size);
	preview->show();

	if (isEmoji) {
		setupEmojiPreview(preview, size);
	} else {
		setupLottiePreview(preview, size);
	}
	button->setClickedCallback([=] {
		if (toSaved) {
			_controller->showPeerHistory(
				_controller->session().userPeerId(),
				Window::SectionShow::Way::Forward);
			hideToast();
			return;
		} else if (_section == Section::TopicIcon) {
			Settings::ShowPremium(_controller, u"forum_topic_icon"_q);
			return;
		}
		const auto id = _for->sticker()->set.id;
		const auto &sets = _for->owner().stickers().sets();
		const auto i = sets.find(id);
		if (isEmoji
			&& (i != end(sets))
			&& (i->second->flags & Data::StickersSetFlag::Installed)) {
			ShowPremiumPreviewBox(
				_controller,
				PremiumFeature::AnimatedEmoji);
		} else {
			_controller->show(Box<StickerSetBox>(
				_controller->uiShow(),
				_for->sticker()->set,
				setType));
		}
		hideToast();
	});
}

void StickerToast::clearHiddenHiding() {
	_hiding.erase(
		ranges::remove(
			_hiding,
			nullptr,
			&base::weak_ptr<Ui::Toast::Instance>::get),
		end(_hiding));
}

void StickerToast::setupEmojiPreview(
		not_null<Ui::RpWidget*> widget,
		int size) {
	Expects(_for != nullptr);

	struct Instance {
		Instance(
			std::unique_ptr<Ui::CustomEmoji::Loader> loader,
			Fn<void(
				not_null<Ui::CustomEmoji::Instance*>,
				Ui::CustomEmoji::RepaintRequest)> repaintLater,
			Fn<void()> repaint)
		: emoji(
			Ui::CustomEmoji::Loading(
				std::move(loader),
				Ui::CustomEmoji::Preview()),
			std::move(repaintLater))
		, object(&emoji, repaint)
		, timer(repaint) {
		}

		Ui::CustomEmoji::Instance emoji;
		Ui::CustomEmoji::Object object;
		base::Timer timer;
	};

	const auto repaintDelayed = [=](
			not_null<Ui::CustomEmoji::Instance*> instance,
			Ui::CustomEmoji::RepaintRequest request) {
		if (!request.when) {
			return;
		}
		const auto now = crl::now();
		if (now > request.when) {
			reinterpret_cast<Instance*>(instance.get())->timer.callOnce(
				now - request.when);
		} else {
			widget->update();
		}
	};
	const auto instance = widget->lifetime().make_state<Instance>(
		_for->owner().customEmojiManager().createLoader(
			_for,
			Data::CustomEmojiManager::SizeTag::Large),
		std::move(repaintDelayed),
		[=] { widget->update(); });

	widget->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(widget);
		const auto size = Ui::Emoji::GetSizeLarge()
			/ style::DevicePixelRatio();
		instance->object.paint(p, Ui::Text::CustomEmoji::Context{
			.textColor = st::toastFg->c,
			.now = crl::now(),
			.position = QPoint(
				(widget->width() - size) / 2,
				(widget->height() - size) / 2),
		});
	}, widget->lifetime());
}

void StickerToast::setupLottiePreview(not_null<Ui::RpWidget*> widget, int size) {
	Expects(_for != nullptr);

	const auto bytes = _for->createMediaView()->bytes();
	const auto filepath = _for->filepath();
	const auto player = widget->lifetime().make_state<Lottie::SinglePlayer>(
		Lottie::ReadContent(bytes, filepath),
		Lottie::FrameRequest{ QSize(size, size) },
		Lottie::Quality::Default);

	widget->paintRequest(
	) | rpl::start_with_next([=] {
		if (!player->ready()) {
			return;
		}
		const auto image = player->frame();
		QPainter(widget).drawImage(
			QRect(QPoint(), image.size() / image.devicePixelRatio()),
			image);
		player->markFrameShown();
	}, widget->lifetime());

	player->updates(
	) | rpl::start_with_next([=] {
		widget->update();
	}, widget->lifetime());
}

} // namespace HistoryView
