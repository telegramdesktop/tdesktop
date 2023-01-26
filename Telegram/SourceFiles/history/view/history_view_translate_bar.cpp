/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_translate_bar.h"

#include "boxes/translate_box.h" // Ui::LanguageName.
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_changes.h"
#include "history/history.h"
#include "main/main_session.h"
#include "spellcheck/spellcheck_types.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/shadow.h"
#include "styles/style_chat.h"

#include <QtGui/QtEvents>

namespace HistoryView {

TranslateBar::TranslateBar(
	not_null<QWidget*> parent,
	not_null<History*> history)
: _wrap(parent, object_ptr<Ui::FlatButton>(
	parent,
	QString(),
	st::historyComposeButton))
, _shadow(std::make_unique<Ui::PlainShadow>(_wrap.parentWidget())) {
	_wrap.hide(anim::type::instant);
	_shadow->hide();

	setup(history);
}

TranslateBar::~TranslateBar() = default;

void TranslateBar::updateControlsGeometry(QRect wrapGeometry) {
	const auto hidden = _wrap.isHidden() || !wrapGeometry.height();
	if (_shadow->isHidden() != hidden) {
		_shadow->setVisible(!hidden);
	}
}

void TranslateBar::setShadowGeometryPostprocess(
		Fn<QRect(QRect)> postprocess) {
	_shadowGeometryPostprocess = std::move(postprocess);
	updateShadowGeometry(_wrap.geometry());
}

void TranslateBar::updateShadowGeometry(QRect wrapGeometry) {
	const auto regular = QRect(
		wrapGeometry.x(),
		wrapGeometry.y() + wrapGeometry.height(),
		wrapGeometry.width(),
		st::lineWidth);
	_shadow->setGeometry(_shadowGeometryPostprocess
		? _shadowGeometryPostprocess(regular)
		: regular);
}

void TranslateBar::setup(not_null<History*> history) {
	_wrap.geometryValue(
	) | rpl::start_with_next([=](QRect rect) {
		updateShadowGeometry(rect);
	updateControlsGeometry(rect);
	}, _wrap.lifetime());

	const auto button = static_cast<Ui::FlatButton*>(_wrap.entity());
	button->setClickedCallback([=] {
		const auto to = history->translatedTo()
			? LanguageId()
			: Core::App().settings().translateTo();
		history->translateTo(to);
		if (const auto migrated = history->migrateFrom()) {
			migrated->translateTo(to);
		}
	});

	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		button,
		st::historyTranslateLabel);
	const auto icon = Ui::CreateChild<Ui::RpWidget>(button);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	icon->setAttribute(Qt::WA_TransparentForMouseEvents);
	icon->resize(st::historyTranslateIcon.size());
	icon->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(icon);
		st::historyTranslateIcon.paint(p, 0, 0, icon->width());
	}, icon->lifetime());
	const auto settings = Ui::CreateChild<Ui::IconButton>(
		button,
		st::historyTranslateSettings);
	const auto updateLabelGeometry = [=] {
		const auto full = _wrap.width() - icon->width();
		const auto skip = st::semiboldFont->spacew * 2;
		const auto natural = label->naturalWidth();
		const auto top = [&] {
			return (_wrap.height() - label->height()) / 2;
		};
		if (natural <= full - 2 * (settings->width() + skip)) {
			label->resizeToWidth(natural);
			label->moveToRight((full - label->width()) / 2, top());
		} else {
			const auto available = full - settings->width() - 2 * skip;
			label->resizeToWidth(std::min(natural, available));
			label->moveToRight(settings->width() + skip, top());
		}
		icon->move(
			label->x() - icon->width(),
			(_wrap.height() - icon->height()) / 2);
	};

	_wrap.sizeValue() | rpl::start_with_next([=](QSize size) {
		settings->moveToRight(0, 0, size.width());
		updateLabelGeometry();
	}, lifetime());

	rpl::combine(
		Core::App().settings().translateToValue(),
		history->session().changes().historyFlagsValue(
			history,
			(Data::HistoryUpdate::Flag::TranslatedTo
				| Data::HistoryUpdate::Flag::TranslateFrom))
	) | rpl::map([=](LanguageId to, const auto&) {
		return history->translatedTo()
			? u"Show Original"_q
			: history->translateOfferedFrom()
			? u"Translate to "_q + Ui::LanguageName(to.locale())
			: QString();
	}) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](QString phrase) {
		_shouldBeShown = !phrase.isEmpty();
		if (_shouldBeShown) {
			label->setText(phrase);
			updateLabelGeometry();
		}
		if (!_forceHidden) {
			_wrap.toggle(_shouldBeShown, anim::type::normal);
		}
	}, lifetime());
}

void TranslateBar::show() {
	if (!_forceHidden) {
		return;
	}
	_forceHidden = false;
	if (_shouldBeShown) {
		_wrap.show(anim::type::instant);
		_shadow->show();
	}
}

void TranslateBar::hide() {
	if (_forceHidden) {
		return;
	}
	_forceHidden = true;
	_wrap.hide(anim::type::instant);
	_shadow->hide();
}

void TranslateBar::raise() {
	_wrap.raise();
	_shadow->raise();
}

void TranslateBar::finishAnimating() {
	_wrap.finishAnimating();
}

void TranslateBar::move(int x, int y) {
	_wrap.move(x, y);
}

void TranslateBar::resizeToWidth(int width) {
	_wrap.entity()->resizeToWidth(width);
}

int TranslateBar::height() const {
	return !_forceHidden
		? _wrap.height()
		: _shouldBeShown
		? st::historyReplyHeight
		: 0;
}

rpl::producer<int> TranslateBar::heightValue() const {
	return _wrap.heightValue();
}

} // namespace Ui
