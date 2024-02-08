/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/intro_code_input.h"

#include "lang/lang_keys.h"
#include "ui/abstract_button.h"
#include "ui/effects/shake_animation.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_intro.h"
#include "styles/style_layers.h" // boxRadius

#include <QtCore/QRegularExpression>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

namespace Ui {
namespace {

constexpr auto kDigitNone = int(-1);

[[nodiscard]] int Circular(int left, int right) {
	return ((left % right) + right) % right;
}

class Shaker final {
public:
	explicit Shaker(not_null<Ui::RpWidget*> widget);

	void shake();

private:
	const not_null<Ui::RpWidget*> _widget;
	Ui::Animations::Simple _animation;

};

Shaker::Shaker(not_null<Ui::RpWidget*> widget)
: _widget(widget) {
}

void Shaker::shake() {
	if (_animation.animating()) {
		return;
	}
	_animation.start(DefaultShakeCallback([=, x = _widget->x()](int shift) {
		_widget->moveToLeft(x + shift, _widget->y());
	}), 0., 1., st::shakeDuration);
}

} // namespace

class CodeDigit final : public Ui::AbstractButton {
public:
	explicit CodeDigit(not_null<Ui::RpWidget*> widget);

	void setDigit(int digit);
	[[nodiscard]] int digit() const;

	void setBorderColor(const QBrush &brush);
	void shake();

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	Shaker _shaker;
	Ui::Animations::Simple _animation;
	int _dataDigit = kDigitNone;
	int _viewDigit = kDigitNone;

	QPen _borderPen;

};

CodeDigit::CodeDigit(not_null<Ui::RpWidget*> widget)
: Ui::AbstractButton(widget)
, _shaker(this) {
	setBorderColor(st::windowBgRipple);
}

void CodeDigit::setDigit(int digit) {
	if ((_dataDigit == digit) && _animation.animating()) {
		return;
	}
	_dataDigit = digit;
	if (_viewDigit != digit) {
		constexpr auto kDuration = st::introCodeDigitAnimatioDuration;
		_animation.stop();
		if (digit == kDigitNone) {
			_animation.start([=](float64 value) {
				update();
				if (!value) {
					_viewDigit = digit;
				}
			}, 1., 0., kDuration);
		} else {
			_viewDigit = digit;
			_animation.start([=] { update(); }, 0., 1., kDuration);
		}
	}
}

int CodeDigit::digit() const {
	return _dataDigit;
}

void CodeDigit::setBorderColor(const QBrush &brush) {
	_borderPen = QPen(brush, st::introCodeDigitBorderWidth);
	update();
}

void CodeDigit::shake() {
	_shaker.shake();
}

void CodeDigit::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	auto clipPath = QPainterPath();
	clipPath.addRoundedRect(rect(), st::boxRadius, st::boxRadius);
	p.setClipPath(clipPath);

	p.fillRect(rect(), st::windowBgOver);
	{
		auto hq = PainterHighQualityEnabler(p);
		p.strokePath(clipPath, _borderPen);
	}

	if (_viewDigit == kDigitNone) {
		return;
	}
	const auto hiding = (_dataDigit == kDigitNone);
	const auto progress = _animation.value(1.);

	if (hiding) {
		p.setOpacity(progress * progress);
		const auto center = rect().center();
		p.setTransform(QTransform()
			.translate(center.x(), center.y())
			.scale(progress, progress)
			.translate(-center.x(), -center.y()));
	} else {
		p.setOpacity(progress);
		constexpr auto kSlideDistanceRatio = 0.2;
		const auto distance = rect().height() * kSlideDistanceRatio;
		p.translate(0, (distance * (1. - progress)));
	}
	p.setFont(st::introCodeDigitFont);
	p.setPen(st::windowFg);
	p.drawText(rect(), QString::number(_viewDigit), style::al_center);
}

CodeInput::CodeInput(QWidget *parent)
: Ui::RpWidget(parent) {
	setFocusPolicy(Qt::StrongFocus);
}

void CodeInput::setDigitsCountMax(int digitsCount) {
	_digitsCountMax = digitsCount;

	_digits.clear();
	_currentIndex = 0;

	constexpr auto kWidthRatio = 0.8;
	const auto digitWidth = st::introCodeDigitHeight * kWidthRatio;
	const auto padding = Margins(st::introCodeDigitSkip);
	resize(
		padding.left()
			+ digitWidth * digitsCount
			+ st::introCodeDigitSkip * (digitsCount - 1)
			+ padding.right(),
		st::introCodeDigitHeight);

	for (auto i = 0; i < digitsCount; i++) {
		const auto widget = Ui::CreateChild<CodeDigit>(this);
		widget->setPointerCursor(false);
		widget->setClickedCallback([=] { unfocusAll(_currentIndex = i); });
		widget->resize(digitWidth, st::introCodeDigitHeight);
		widget->moveToLeft(
			padding.left() + (digitWidth + st::introCodeDigitSkip) * i,
			0);
		_digits.emplace_back(widget);
	}
}

void CodeInput::setCode(QString code) {
	using namespace TextUtilities;
	code = code.remove(RegExpDigitsExclude()).mid(0, _digitsCountMax);
	for (int i = 0; i < _digits.size(); i++) {
		if (i >= code.size()) {
			return;
		}
		_digits[i]->setDigit(code.at(i).digitValue());
	}
}

void CodeInput::requestCode() {
	const auto result = collectDigits();
	if (result.size() == _digitsCountMax) {
		_codeCollected.fire_copy(result);
	} else {
		findEmptyAndPerform([&](int i) { _digits[i]->shake(); });
	}
}

rpl::producer<QString> CodeInput::codeCollected() const {
	return _codeCollected.events();
}

void CodeInput::clear() {
	for (const auto &digit : _digits) {
		digit->setDigit(kDigitNone);
	}
	unfocusAll(_currentIndex = 0);
}

void CodeInput::showError() {
	clear();
	for (const auto &digit : _digits) {
		digit->shake();
		digit->setBorderColor(st::activeLineFgError);
	}
}

void CodeInput::focusInEvent(QFocusEvent *e) {
	unfocusAll(_currentIndex);
}

void CodeInput::focusOutEvent(QFocusEvent *e) {
	unfocusAll(kDigitNone);
}

void CodeInput::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.fillRect(rect(), st::windowBg);
}

void CodeInput::keyPressEvent(QKeyEvent *e) {
	const auto key = e->key();
	if (key == Qt::Key_Down || key == Qt::Key_Right || key == Qt::Key_Space) {
		_currentIndex = Circular(_currentIndex + 1, _digits.size());
		unfocusAll(_currentIndex);
	} else if (key == Qt::Key_Up || key == Qt::Key_Left) {
		_currentIndex = Circular(_currentIndex - 1, _digits.size());
		unfocusAll(_currentIndex);
	} else if (key >= Qt::Key_0 && key <= Qt::Key_9) {
		const auto index = int(key - Qt::Key_0);
		_digits[_currentIndex]->setDigit(index);
		_currentIndex = Circular(_currentIndex + 1, _digits.size());
		if (!_currentIndex) {
			const auto result = collectDigits();
			if (result.size() == _digitsCountMax) {
				_codeCollected.fire_copy(result);
				_currentIndex = _digits.size() - 1;
			} else {
				findEmptyAndPerform([&](int i) { _currentIndex = i; });
			}
		}
		unfocusAll(_currentIndex);
	} else if (key == Qt::Key_Delete) {
		_digits[_currentIndex]->setDigit(kDigitNone);
	} else if (key == Qt::Key_Backspace) {
		const auto wasDigit = _digits[_currentIndex]->digit();
		_digits[_currentIndex]->setDigit(kDigitNone);
		_currentIndex = std::clamp(_currentIndex - 1, 0, int(_digits.size()));
		if (wasDigit == kDigitNone) {
			_digits[_currentIndex]->setDigit(kDigitNone);
		}
		unfocusAll(_currentIndex);
	} else if (key == Qt::Key_Enter || key == Qt::Key_Return) {
		requestCode();
	} else if (e == QKeySequence::Paste) {
		insertCodeAndSubmit(QGuiApplication::clipboard()->text());
	} else if (key >= Qt::Key_A && key <= Qt::Key_Z) {
		_digits[_currentIndex]->shake();
	} else if (key == Qt::Key_Home || key == Qt::Key_PageUp) {
		unfocusAll(_currentIndex = 0);
	} else if (key == Qt::Key_End || key == Qt::Key_PageDown) {
		unfocusAll(_currentIndex = (_digits.size() - 1));
	}
}

void CodeInput::contextMenuEvent(QContextMenuEvent *e) {
	if (_menu) {
		return;
	}
	_menu = base::make_unique_q<Ui::PopupMenu>(this, st::defaultPopupMenu);
	_menu->addAction(tr::lng_mac_menu_paste(tr::now), [=] {
		insertCodeAndSubmit(QGuiApplication::clipboard()->text());
	})->setEnabled(!QGuiApplication::clipboard()->text().isEmpty());
	_menu->popup(QCursor::pos());
}

void CodeInput::insertCodeAndSubmit(const QString &code) {
	if (code.isEmpty()) {
		return;
	}
	setCode(code);
	_currentIndex = _digits.size() - 1;
	findEmptyAndPerform([&](int i) { _currentIndex = i; });
	unfocusAll(_currentIndex);
	if ((_currentIndex == _digits.size() - 1)
		&& _digits[_currentIndex]->digit() != kDigitNone) {
		requestCode();
	}
}

QString CodeInput::collectDigits() const {
	auto result = QString();
	for (const auto &digit : _digits) {
		if (digit->digit() != kDigitNone) {
			result += QString::number(digit->digit());
		}
	}
	return result;
}

void CodeInput::unfocusAll(int except) {
	for (auto i = 0; i < _digits.size(); i++) {
		const auto focused = (i == except);
		_digits[i]->setBorderColor(focused
			? st::windowActiveTextFg
			: st::windowBgRipple);
	}
}

void CodeInput::findEmptyAndPerform(const Fn<void(int)> &callback) {
	for (auto i = 0; i < _digits.size(); i++) {
		if (_digits[i]->digit() == kDigitNone) {
			callback(i);
			break;
		}
	}
}

} // namespace Ui
