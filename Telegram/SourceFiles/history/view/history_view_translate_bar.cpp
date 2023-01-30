/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_translate_bar.h"

#include "boxes/translate_box.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_changes.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "spellcheck/spellcheck_types.h"
#include "ui/boxes/choose_language_box.h" // EditSkipTranslationLanguages.
#include "ui/layers/box_content.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/shadow.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_menu_icons.h"

#include <QtGui/QtEvents>

namespace HistoryView {
namespace {

constexpr auto kToastDuration = 4 * crl::time(1000);

} // namespace

TranslateBar::TranslateBar(
	not_null<QWidget*> parent,
	not_null<Window::SessionController*> controller,
	not_null<History*> history)
: _controller(controller)
, _history(history)
, _wrap(parent, object_ptr<Ui::FlatButton>(
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
	const auto translateTo = [=](LanguageId id) {
		history->translateTo(id);
		if (const auto migrated = history->migrateFrom()) {
			migrated->translateTo(id);
		}
	};
	button->setClickedCallback([=] {
		translateTo(history->translatedTo()
			? LanguageId()
			: Core::App().settings().translateTo());
	});

	Core::App().settings().translateToValue(
	) | rpl::filter([=](LanguageId should) {
		const auto now = history->translatedTo();
		return now && (now != should);
	}) | rpl::start_with_next([=](LanguageId should) {
		translateTo(should);
	}, _wrap.lifetime());

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
	settings->setClickedCallback([=] { showMenu(createMenu(settings)); });

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
				| Data::HistoryUpdate::Flag::TranslateFrom)),
		history->session().changes().peerFlagsValue(
			history->peer,
			Data::PeerUpdate::Flag::TranslationDisabled)
	) | rpl::map([=](LanguageId to, const auto&, const auto&) {
		using Flag = PeerData::TranslationFlag;
		return (history->peer->translationFlag() != Flag::Enabled)
			? rpl::single(QString())
			: history->translatedTo()
			? tr::lng_translate_show_original()
			: history->translateOfferedFrom()
			? tr::lng_translate_bar_to(
				lt_name,
				rpl::single(Ui::LanguageName(to)))
			: rpl::single(QString());
	}) | rpl::flatten_latest(
	) | rpl::distinct_until_changed(
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

base::unique_qptr<Ui::PopupMenu> TranslateBar::createMenu(
		not_null<Ui::IconButton*> button) {
	if (_menu) {
		return nullptr;
	}
	auto result = base::make_unique_q<Ui::PopupMenu>(
		&_wrap,
		st::popupMenuExpandedSeparator);
	result->setDestroyedCallback([
		this,
		weak = Ui::MakeWeak(&_wrap),
		weakButton = Ui::MakeWeak(button),
		menu = result.get()
	] {
		if (weak && _menu == menu) {
			if (weakButton) {
				weakButton->setForceRippled(false);
			}
		}
	});
	button->setForceRippled(true);
	return result;
}

void TranslateBar::showMenu(base::unique_qptr<Ui::PopupMenu> menu) {
	if (!menu) {
		return;
	}
	_menu = std::move(menu);
	_menu->setForcedOrigin(Ui::PanelAnimation::Origin::TopRight);

	const auto weak = base::make_weak(_controller);
	_menu->addAction(tr::lng_translate_menu_to(tr::now), [=] {
		if (const auto strong = weak.get()) {
			strong->show(Ui::ChooseTranslateToBox());
		}
	}, &st::menuIconTranslate);
	_menu->addSeparator();
	const auto history = _history;
	if (const auto translateOfferedFrom = _history->translateOfferedFrom()) {
		const auto name = Ui::LanguageName(translateOfferedFrom);
		const auto addToIgnoreList = [=] {
			showSettingsToast(history->peer, translateOfferedFrom);

			history->peer->saveTranslationDisabled(true);

			auto &settings = Core::App().settings();
			auto skip = settings.skipTranslationLanguages();
			if (!ranges::contains(skip, translateOfferedFrom)) {
				skip.push_back(translateOfferedFrom);
			}
			settings.setSkipTranslationLanguages(std::move(skip));
			Core::App().saveSettingsDelayed();
		};
		_menu->addAction(
			tr::lng_translate_menu_dont(tr::now, lt_name, name),
			addToIgnoreList,
			&st::menuIconBlock);
	}
	const auto hideBar = [=] {
		showHiddenToast(history->peer);

		history->peer->saveTranslationDisabled(true);
	};
	_menu->addAction(
		tr::lng_translate_menu_hide(tr::now),
		hideBar,
		&st::menuIconCancel);
	_menu->popup(_wrap.mapToGlobal(
		QPoint(_wrap.width(), 0) + st::historyTranslateMenuPosition));
}

void TranslateBar::showSettingsToast(
		not_null<PeerData*> peer,
		LanguageId ignored) {
	const auto weak = base::make_weak(_controller);
	const auto text = tr::lng_translate_dont_added(
		tr::now,
		lt_name,
		Ui::Text::Bold(Ui::LanguageName(ignored)),
		Ui::Text::WithEntities);
	showToast(text, tr::lng_translate_settings(tr::now), [=] {
		if (const auto strong = weak.get()) {
			const auto box = strong->show(
				Ui::EditSkipTranslationLanguages());
			if (box) {
				box->boxClosing() | rpl::start_with_next([=] {
					const auto in = ranges::contains(
						Core::App().settings().skipTranslationLanguages(),
						ignored);
					if (!in && weak) {
						peer->saveTranslationDisabled(false);
					}
				}, box->lifetime());
			}
		}
	});
}

void TranslateBar::showHiddenToast(not_null<PeerData*> peer) {
	const auto &phrase = peer->isUser()
		? tr::lng_translate_hidden_user
		: peer->isBroadcast()
		? tr::lng_translate_hidden_channel
		: tr::lng_translate_hidden_group;
	const auto proj = Ui::Text::WithEntities;
	showToast(phrase(tr::now, proj), tr::lng_translate_undo(tr::now), [=] {
		peer->saveTranslationDisabled(false);
	});
}

void TranslateBar::showToast(
		TextWithEntities text,
		const QString &buttonText,
		Fn<void()> buttonCallback) {
	const auto st = std::make_shared<style::Toast>(st::historyPremiumToast);
	const auto skip = st->padding.top();
	st->padding.setRight(st::historyPremiumViewSet.font->width(buttonText)
		- st::historyPremiumViewSet.width);

	const auto weak = Ui::Toast::Show(_wrap.window(), Ui::Toast::Config{
		.text = std::move(text),
		.st = st.get(),
		.durationMs = kToastDuration,
		.multiline = true,
		.dark = true,
		.slideSide = RectPart::Bottom,
	});
	const auto strong = weak.get();
	if (!strong) {
		return;
	}
	strong->setInputUsed(true);
	const auto widget = strong->widget();
	widget->lifetime().add([st] {});
	const auto hideToast = [weak] {
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
		rpl::single(buttonText),
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

	button->setClickedCallback([=] {
		buttonCallback();
		hideToast();
	});
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
