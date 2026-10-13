/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/text_copied_toast.h"

#include "base/timer.h"
#include "lang/lang_keys.h"
#include "ui/effects/animations.h"
#include "ui/effects/radial_animation.h"
#include "ui/layers/show.h"
#include "ui/toast/toast.h"
#include "ui/widgets/labels.h"
#include "ui/painter.h"

#include "styles/style_chat_helpers.h"
#include "styles/style_widgets.h"

namespace Ui {
namespace {

constexpr auto kExpandDuration = crl::time(300);
constexpr auto kExpandWidthShare = 0.6;

[[nodiscard]] object_ptr<FlatLabel> MakeTextLabel(const style::Toast &st) {
	auto lifetime = rpl::lifetime();
	const auto labelSt = lifetime.make_state<style::FlatLabel>(
		st::defaultFlatLabel);
	labelSt->style = st.style;
	labelSt->textFg = st::toastFg;
	labelSt->palette = st.palette;
	labelSt->minWidth = st.minWidth - st.padding.left() - st.padding.right();
	labelSt->maxHeight = st.style.font->height * Toast::Config().maxlines;

	auto result = object_ptr<FlatLabel>((QWidget*)nullptr, *labelSt);
	result->lifetime().add(std::move(lifetime));
	result->setMarkedText(tr::lng_text_copied(tr::now, tr::marked));
	return result;
}

[[nodiscard]] object_ptr<RpWidget> MakeIconSlot() {
	auto result = object_ptr<RpWidget>((QWidget*)nullptr);
	result->resize(st::toastLottieIconSize);
	result->setAttribute(Qt::WA_TransparentForMouseEvents);
	return result;
}

void PaintSpinner(
		not_null<RpWidget*> slot,
		not_null<InfiniteRadialAnimation*> spinner) {
	auto p = QPainter(slot);
	const auto &st = st::textCopiedToastLoading;
	const auto shift = st.thickness - (st.thickness / 2);
	spinner->draw(
		p,
		{ shift, shift },
		st.size - QSize(2 * shift, 2 * shift),
		slot->width());
}

void AddCopyIcon(not_null<RpWidget*> slot) {
	auto icon = Toast::internal::MakeIconByFactory(slot, {
		.iconLottie = u"toast/copy"_q,
		.iconLottieSize = st::toastLottieIconSize,
	});
	if (const auto raw = icon.release()) {
		raw->move(0, 0);
		raw->show();
	}
}

} // namespace

struct TextCopiedToastPending::State : base::has_weak_ptr {
	void expandTo(float64 progress);

	rpl::variable<QMargins> padding;
	QPointer<FlatLabel> label;
	std::unique_ptr<InfiniteRadialAnimation> spinner;
	Animations::Simple expand;
	base::Timer hide;
	base::weak_ptr<Toast::Instance> toast;
	int collapse = 0;
};

void TextCopiedToastPending::State::expandTo(float64 progress) {
	const auto width = anim::easeOutCubic(
		1.,
		std::min(1., progress / kExpandWidthShare));
	padding = QMargins(
		0,
		0,
		-int(base::SafeRound((1. - width) * collapse)),
		0);
	if (progress > kExpandWidthShare) {
		if (const auto shown = label.data()) {
			shown->setOpacity((progress - kExpandWidthShare)
				/ (1. - kExpandWidthShare));
			shown->show();
		}
	}
	if (progress >= 1.) {
		hide.callOnce(Toast::kDefaultDuration, Qt::PreciseTimer);
	}
}

void ShowTextCopiedToast(const std::shared_ptr<Show> &show) {
	auto icon = MakeIconSlot();
	AddCopyIcon(icon.data());
	show->showToast({
		.content = MakeTextLabel(st::defaultMultilineToast),
		.iconContent = std::move(icon),
	});
}

TextCopiedToastPending::TextCopiedToastPending(std::shared_ptr<Show> show)
: _show(std::move(show)) {
	auto label = MakeTextLabel(st::defaultMultilineToast);
	auto slot = MakeIconSlot();
	const auto raw = slot.data();
	const auto state = raw->lifetime().make_state<State>();
	_slot = raw;
	_state = state;
	state->label = label.data();
	state->collapse = label->naturalWidth()
		+ st::defaultMultilineToast.padding.right();
	state->padding = QMargins(0, 0, -state->collapse, 0);
	state->spinner = std::make_unique<InfiniteRadialAnimation>(
		[=] { raw->update(); },
		st::textCopiedToastLoading);
	state->spinner->start(st::textCopiedToastLoading.sineDuration);
	state->hide.setCallback([=] {
		if (const auto toast = state->toast.get()) {
			toast->hideAnimated();
		}
	});
	raw->paintRequest() | rpl::on_next([=] {
		if (const auto spinner = state->spinner.get()) {
			PaintSpinner(raw, spinner);
		}
	}, raw->lifetime());

	_toast = _show->showToast({
		.content = std::move(label),
		.iconContent = std::move(slot),
		.padding = state->padding.value(),
		.infinite = true,
	});
	if (const auto strong = _state.get()) {
		strong->toast = _toast;
		if (const auto shown = strong->label.data()) {
			shown->hide();
		}
	}
}

TextCopiedToastPending::~TextCopiedToastPending() {
	if (!_finished) {
		if (const auto toast = _toast.get()) {
			toast->hide();
		}
	}
}

void TextCopiedToastPending::finished() {
	_finished = true;
	const auto slot = _slot.data();
	const auto state = _state.get();
	if (!slot || !state) {
		ShowTextCopiedToast(_show);
		return;
	}
	state->spinner = nullptr;
	slot->update();
	AddCopyIcon(slot);
	state->expand.start([=](float64 progress) {
		state->expandTo(progress);
	}, 0., 1., kExpandDuration);
}

} // namespace Ui
