/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_row_address_chips.h"

#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"

#include "styles/style_wallet.h"
#include "styles/style_widgets.h"

namespace Wallet {
namespace {

constexpr auto kChipChars = 4;
constexpr auto kMiddleChars = kChipChars / 2;
constexpr auto kFadedChip = 1;
constexpr auto kMiddleFadeFull = 0.2;
constexpr auto kMiddleFadeClear = 0.06;

struct ChipsGeometry {
	style::font font;
	QPointF origin;
	float64 scale = 1.;
	int plateWidth = 0;
	int plateHeight = 0;
	int skip = 0;
};

class AddressChipsLabel final : public Ui::FixedHeightWidget {
public:
	AddressChipsLabel(
		QWidget *parent,
		AddressChips chips,
		QString name,
		int height);

	[[nodiscard]] const AddressChips &chips() const;

	QAccessible::Role accessibilityRole() override;
	QString accessibilityName() override;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	const AddressChips _chips;
	const QString _name;

};

[[nodiscard]] std::optional<ChipsGeometry> ComputeChipsGeometry(
		const std::array<QString, 3> &texts,
		QRect line) {
	if (line.width() <= 0 || line.height() <= 0) {
		return std::nullopt;
	}
	const auto font = st::walletRowTitleLabel.style.font->monospace();
	auto textWidth = 0;
	for (const auto &text : texts) {
		textWidth = std::max(textWidth, font->width(text));
	}
	const auto count = int(texts.size());
	const auto plateWidth = textWidth + 2 * st::walletRowAddressChipInset;
	const auto skip = st::walletRowAddressChipSkip;
	const auto full = count * plateWidth + (count - 1) * skip;
	const auto scale = std::min(1., line.width() / float64(full));
	const auto left = style::RightToLeft()
		? (line.x() + line.width() - full * scale)
		: float64(line.x());
	return ChipsGeometry{
		.font = font,
		.origin = QPointF(left, line.y() + line.height() * (1. - scale) / 2.),
		.scale = scale,
		.plateWidth = plateWidth,
		.plateHeight = line.height(),
		.skip = skip,
	};
}

void ApplyChipsGeometry(QPainter &p, const ChipsGeometry &geometry) {
	p.translate(geometry.origin);
	p.scale(geometry.scale, geometry.scale);
}

[[nodiscard]] int ChipLeft(const ChipsGeometry &geometry, int index) {
	return index * (geometry.plateWidth + geometry.skip);
}

[[nodiscard]] QLinearGradient MiddleFade(QColor color, int left, int width) {
	auto clear = color;
	clear.setAlpha(0);
	auto result = QLinearGradient(QPointF(left, 0), QPointF(left + width, 0));
	result.setStops({
		{ 0., color },
		{ kMiddleFadeFull, color },
		{ 0.5 - kMiddleFadeClear, clear },
		{ 0.5 + kMiddleFadeClear, clear },
		{ 1. - kMiddleFadeFull, color },
		{ 1., color },
	});
	return result;
}

AddressChipsLabel::AddressChipsLabel(
	QWidget *parent,
	AddressChips chips,
	QString name,
	int height)
: FixedHeightWidget(parent, height)
, _chips(std::move(chips))
, _name(std::move(name)) {
}

const AddressChips &AddressChipsLabel::chips() const {
	return _chips;
}

QAccessible::Role AddressChipsLabel::accessibilityRole() {
	return QAccessible::Role::StaticText;
}

QString AddressChipsLabel::accessibilityName() {
	return _name;
}

void AddressChipsLabel::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	_chips.paintTexts(p, rect());
}

} // namespace

AddressChips::AddressChips(std::array<QString, 3> texts)
: _texts(std::move(texts)) {
}

std::optional<AddressChips> AddressChips::FromAddress(
		const QString &address) {
	const auto size = int(address.size());
	if (size < kChipChars * 3) {
		return std::nullopt;
	}
	return AddressChips({
		address.left(kChipChars),
		(address.mid(kChipChars, kMiddleChars)
			+ address.mid(size - kChipChars - kMiddleChars, kMiddleChars)),
		address.right(kChipChars),
	});
}

void AddressChips::paintPlates(QPainter &p, QRect line) const {
	const auto geometry = ComputeChipsGeometry(_texts, line);
	if (!geometry) {
		return;
	}
	const auto radius = st::walletRowAddressChipRadius;
	p.save();
	ApplyChipsGeometry(p, *geometry);
	{
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		for (auto i = 0; i != int(_texts.size()); ++i) {
			p.drawRoundedRect(
				QRect(
					ChipLeft(*geometry, i),
					0,
					geometry->plateWidth,
					geometry->plateHeight),
				radius,
				radius);
		}
	}
	p.restore();
}

void AddressChips::paintTexts(QPainter &p, QRect line) const {
	const auto geometry = ComputeChipsGeometry(_texts, line);
	if (!geometry) {
		return;
	}
	const auto &font = geometry->font;
	const auto color = st::walletRowTitleLabel.textFg->c;
	const auto baseline = int(base::SafeRound(
		(geometry->plateHeight + font->metrics().capHeight()) / 2.));
	p.save();
	ApplyChipsGeometry(p, *geometry);
	p.setFont(font);
	p.setLayoutDirection(Qt::LeftToRight);
	for (auto i = 0; i != int(_texts.size()); ++i) {
		const auto &text = _texts[i];
		const auto width = font->width(text);
		const auto left = ChipLeft(*geometry, i)
			+ (geometry->plateWidth - width) / 2;
		if (i == kFadedChip) {
			p.setPen(QPen(QBrush(MiddleFade(color, left, width)), 0));
		} else {
			p.setPen(color);
		}
		p.drawText(left, baseline, text);
	}
	p.restore();
}

void AddressChips::paint(QPainter &p, QRect line) const {
	paintPlates(p, line);
	paintTexts(p, line);
}

not_null<Ui::RpWidget*> AddHistoryRowTitle(
		not_null<Ui::VerticalLayout*> container,
		const QString &title,
		const QString &address,
		int lineHeight,
		style::margins margins,
		not_null<QWidget*> button,
		Fn<void(Fn<void(Painter&)>)> paintUnderRipple) {
	if (auto chips = AddressChips::FromAddress(address)) {
		const auto label = container->add(
			object_ptr<AddressChipsLabel>(
				container,
				std::move(*chips),
				title,
				lineHeight),
			margins);
		paintUnderRipple([=](Painter &p) {
			label->chips().paintPlates(
				p,
				Ui::MapFrom(button, label, label->rect()));
		});
		return label;
	}
	const auto label = container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			title,
			st::walletRowTitleLabel),
		margins);
	label->setBreakEverywhere(true);
	return label;
}

void PaintSendingRowTitle(
		QPainter &p,
		QRect line,
		const std::optional<AddressChips> &now,
		const std::optional<AddressChips> *next,
		bool same,
		float64 progress,
		Fn<void(bool next, float64 opacity)> text) {
	const auto paint = [&](
			const std::optional<AddressChips> &chips,
			bool isNext,
			float64 opacity) {
		if (!chips) {
			text(isNext, opacity);
		} else if (opacity > 0.) {
			p.setOpacity(opacity);
			chips->paint(p, line);
		}
	};
	if (!next) {
		paint(now, false, 1.);
	} else if (same) {
		paint(*next, true, 1.);
	} else {
		paint(now, false, 1. - progress);
		paint(*next, true, progress);
	}
}

} // namespace Wallet
