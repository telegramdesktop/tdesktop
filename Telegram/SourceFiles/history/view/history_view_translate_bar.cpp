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
#include "ui/effects/ripple_animation.h"
#include "ui/boxes/choose_language_box.h" // EditSkipTranslationLanguages.
#include "ui/layers/box_content.h"
#include "ui/widgets/menu/menu_item_base.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/shadow.h"
#include "ui/painter.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_menu_icons.h"

#include <QtGui/QtEvents>

namespace HistoryView {
namespace {

constexpr auto kToastDuration = 4 * crl::time(1000);

class TwoTextAction final : public Ui::Menu::ItemBase {
public:
	TwoTextAction(
		not_null<Ui::RpWidget*> parent,
		const style::Menu &st,
		const QString &text1,
		const QString &text2,
		Fn<void()> callback,
		const style::icon *icon,
		const style::icon *iconOver);

	bool isEnabled() const override;
	not_null<QAction*> action() const override;

	void handleKeyPress(not_null<QKeyEvent*> e) override;

private:
	QPoint prepareRippleStartPosition() const override;
	QImage prepareRippleMask() const override;

	int contentHeight() const override;

	void paint(Painter &p);
	void prepare(const QString &text1);

	const not_null<QAction*> _dummyAction;
	const style::Menu &_st;
	const style::icon *_icon;
	const style::icon *_iconOver;
	Ui::Text::String _text1;
	QString _text2;
	int _textWidth1 = 0;
	int _textWidth2 = 0;
	const int _height;

};

TextParseOptions MenuTextOptions = {
	TextParseLinks, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

TwoTextAction::TwoTextAction(
	not_null<Ui::RpWidget*> parent,
	const style::Menu &st,
	const QString &text1,
	const QString &text2,
	Fn<void()> callback,
	const style::icon *icon,
	const style::icon *iconOver)
: ItemBase(parent, st)
, _dummyAction(new QAction(parent))
, _st(st)
, _icon(icon)
, _iconOver(iconOver)
, _text2(text2)
, _height(st::ttlItemPadding.top()
	+ _st.itemStyle.font->height
	+ st::ttlItemTimerFont->height
	+ st::ttlItemPadding.bottom()) {
	initResizeHook(parent->sizeValue());
	setClickedCallback(std::move(callback));

	paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(this);
		paint(p);
	}, lifetime());

	enableMouseSelecting();
	prepare(text1);
}

void TwoTextAction::paint(Painter &p) {
	const auto selected = isSelected();
	if (selected && _st.itemBgOver->c.alpha() < 255) {
		p.fillRect(0, 0, width(), _height, _st.itemBg);
	}
	p.fillRect(0, 0, width(), _height, selected ? _st.itemBgOver : _st.itemBg);
	if (isEnabled()) {
		paintRipple(p, 0, 0);
	}

	const auto normalHeight = _st.itemPadding.top()
		+ _st.itemStyle.font->height
		+ _st.itemPadding.bottom();
	const auto deltaHeight = _height - normalHeight;
	if (const auto icon = selected ? _iconOver : _icon) {
		icon->paint(
			p,
			_st.itemIconPosition + QPoint(0, deltaHeight / 2),
			width());
	}

	p.setPen(selected ? _st.itemFgOver : _st.itemFg);
	_text1.drawLeftElided(
		p,
		_st.itemPadding.left(),
		st::ttlItemPadding.top(),
		_textWidth1,
		width());

	p.setFont(st::ttlItemTimerFont);
	p.setPen(selected ? _st.itemFgShortcutOver : _st.itemFgShortcut);
	p.drawTextLeft(
		_st.itemPadding.left(),
		st::ttlItemPadding.top() + _st.itemStyle.font->height,
		width(),
		_text2);
}

void TwoTextAction::prepare(const QString &text1) {
	_text1.setMarkedText(_st.itemStyle, { text1 }, MenuTextOptions);
	const auto textWidth1 = _text1.maxWidth();
	const auto textWidth2 = st::ttlItemTimerFont->width(_text2);
	const auto &padding = _st.itemPadding;

	const auto goodWidth = padding.left()
		+ std::max(textWidth1, textWidth2)
		+ padding.right();
	const auto ttlMaxWidth = [&](const QString &duration) {
		return padding.left()
			+ st::ttlItemTimerFont->width(tr::lng_context_auto_delete_in(
				tr::now,
				lt_duration,
				duration))
			+ padding.right();
	};
	const auto maxWidth1 = ttlMaxWidth("23:59:59");
	const auto maxWidth2 = ttlMaxWidth(tr::lng_days(tr::now, lt_count, 7));

	const auto w = std::clamp(
		std::max({ goodWidth, maxWidth1, maxWidth2 }),
		_st.widthMin,
		_st.widthMax);
	_textWidth1 = w - (goodWidth - textWidth1);
	_textWidth2 = w - (goodWidth - textWidth2);
	setMinWidth(w);
	update();
}

bool TwoTextAction::isEnabled() const {
	return true;
}

not_null<QAction*> TwoTextAction::action() const {
	return _dummyAction;
}

QPoint TwoTextAction::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos());
}

QImage TwoTextAction::prepareRippleMask() const {
	return Ui::RippleAnimation::RectMask(size());
}

int TwoTextAction::contentHeight() const {
	return _height;
}

void TwoTextAction::handleKeyPress(not_null<QKeyEvent*> e) {
	if (!isSelected()) {
		return;
	}
	const auto key = e->key();
	if (key == Qt::Key_Enter || key == Qt::Key_Return) {
		setClicked(Ui::Menu::TriggeredSource::Keyboard);
	}
}

[[nodiscard]] base::unique_qptr<Ui::Menu::ItemBase> MakeTranslateToItem(
		not_null<Ui::Menu::Menu*> menu,
		const QString &language,
		Fn<void()> callback) {
	return base::make_unique_q<TwoTextAction>(
		menu,
		menu->st(),
		tr::lng_translate_menu_to(tr::now),
		language,
		std::move(callback),
		&st::menuIconTranslate,
		&st::menuIconTranslate);
}

} // namespace

TranslateBar::TranslateBar(
	not_null<QWidget*> parent,
	not_null<Window::SessionController*> controller,
	not_null<History*> history)
: _controller(controller)
, _history(history)
, _wrap(parent, object_ptr<Ui::AbstractButton>(parent))
, _shadow(std::make_unique<Ui::PlainShadow>(_wrap.parentWidget())) {
	_wrap.hide(anim::type::instant);
	_shadow->hide();

	_shadow->showOn(rpl::combine(
		_wrap.shownValue(),
		_wrap.heightValue(),
		rpl::mappers::_1 && rpl::mappers::_2 > 0
	) | rpl::filter([=](bool shown) {
		return (shown == _shadow->isHidden());
	}));

	setup(history);
}

TranslateBar::~TranslateBar() = default;

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
	}, _wrap.lifetime());

	const auto translateTo = [=](LanguageId id) {
		history->translateTo(id);
		if (const auto migrated = history->migrateFrom()) {
			migrated->translateTo(id);
		}
	};
	const auto button = static_cast<Ui::AbstractButton*>(_wrap.entity());
	button->resize(0, st::historyTranslateBarHeight);
	button->setAttribute(Qt::WA_OpaquePaintEvent);

	button->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		QPainter(button).fillRect(clip, st::historyComposeButtonBg);
	}, button->lifetime());

	button->setClickedCallback([=] {
		translateTo(history->translatedTo() ? LanguageId() : _to.current());
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
	settings->setClickedCallback([=] { showMenu(createMenu(settings)); });

	const auto updateLabelGeometry = [=] {
		const auto full = _wrap.width() - icon->width();
		const auto skip = st::semiboldFont->spacew * 2;
		const auto natural = label->textMaxWidth();
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

	_overridenTo = history->translatedTo();
	_to = rpl::combine(
		Core::App().settings().translateToValue(),
		Core::App().settings().skipTranslationLanguagesValue(),
		history->session().changes().historyFlagsValue(
			history,
			Data::HistoryUpdate::Flag::TranslateFrom),
		_overridenTo.value()
	) | rpl::map([=](
			LanguageId to,
			const std::vector<LanguageId> &skip,
			const auto &,
			LanguageId overridenTo) {
		return overridenTo
			? overridenTo
			: Ui::ChooseTranslateTo(history, to, skip);
	}) | rpl::distinct_until_changed();

	_to.value(
	) | rpl::filter([=](LanguageId should) {
		const auto now = history->translatedTo();
		return now && (now != should);
	}) | rpl::start_with_next([=](LanguageId should) {
		translateTo(should);
	}, _wrap.lifetime());

	rpl::combine(
		_to.value(),
		history->session().changes().historyFlagsValue(
			history,
			(Data::HistoryUpdate::Flag::TranslatedTo
				| Data::HistoryUpdate::Flag::TranslateFrom)),
		history->session().changes().peerFlagsValue(
			history->peer,
			Data::PeerUpdate::Flag::TranslationDisabled)
	) | rpl::map([=](
			LanguageId to,
			const auto&,
			const auto&) {
		using Flag = PeerData::TranslationFlag;
		return (history->peer->translationFlag() != Flag::Enabled)
			? rpl::single(QString())
			: history->translatedTo()
			? tr::lng_translate_show_original()
			: history->translateOfferedFrom()
			? Ui::TranslateBarTo(to)
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

	const auto guard = Ui::MakeWeak(&_wrap);
	const auto now = _history->translatedTo();
	const auto to = now ? now : Ui::ChooseTranslateTo(_history);
	const auto weak = base::make_weak(_controller);
	const auto chooseCallback = [=] {
		if (const auto strong = weak.get()) {
			strong->show(Ui::ChooseTranslateToBox(
				to,
				crl::guard(guard, [=](LanguageId id) { _overridenTo = id; })
			));
		}
	};
	_menu->addAction(MakeTranslateToItem(
		_menu->menu(),
		Ui::LanguageName(to ? to : Ui::ChooseTranslateTo(_history)),
		chooseCallback));
	_menu->addSeparator();
	const auto history = _history;
	if (const auto translateOfferedFrom = _history->translateOfferedFrom()) {
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
			Ui::TranslateMenuDont(tr::now, translateOfferedFrom),
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
	st->padding.setRight(st::historyPremiumViewSet.style.font->width(buttonText)
		- st::historyPremiumViewSet.width);

	const auto weak = Ui::Toast::Show(_wrap.window(), Ui::Toast::Config{
		.text = std::move(text),
		.st = st.get(),
		.attach = RectPart::Bottom,
		.acceptinput = true,
		.duration = kToastDuration,
	});
	const auto strong = weak.get();
	if (!strong) {
		return;
	}
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
	} else if (!_wrap.isHidden() && !_wrap.animating()) {
		_wrap.hide(anim::type::instant);
	}
}

void TranslateBar::hide() {
	if (_forceHidden) {
		return;
	}
	_forceHidden = true;
	_wrap.hide(anim::type::instant);
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
		? st::historyTranslateBarHeight
		: 0;
}

rpl::producer<int> TranslateBar::heightValue() const {
	return _wrap.heightValue();
}

} // namespace Ui
