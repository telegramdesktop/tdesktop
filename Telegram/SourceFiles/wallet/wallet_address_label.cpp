/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_address_label.h"

#include "lang/lang_text_entity.h"
#include "ui/widgets/labels.h"

namespace Wallet {
namespace {

constexpr auto kAddressGroup = 4;

class AddressLabel final : public Ui::FlatLabel {
public:
	AddressLabel(
		QWidget *parent,
		const QString &address,
		const style::FlatLabel &st,
		Fn<void()> clicked);

protected:
	int resizeGetHeight(int newWidth) override;

private:
	void setLines(const QStringList &lines);

	const QString _address;
	const ClickHandlerPtr _link;
	int _unwrappedWidth = 0;
	int _lineCount = 0;
	bool _refreshing = true;

};

[[nodiscard]] QStringList DetailsAddressLines(
		const QString &address,
		const style::font &font,
		int width) {
	auto groups = QStringList();
	for (auto offset = 0; offset < address.size(); offset += kAddressGroup) {
		groups.append(address.mid(offset, kAddressGroup));
	}
	const auto count = int(groups.size());
	for (auto lines = 1; lines <= count; ++lines) {
		auto result = QStringList();
		auto fits = true;
		auto offset = 0;
		for (auto i = 0; i != lines; ++i) {
			const auto size = (count / lines) + (i < count % lines ? 1 : 0);
			result.append(groups.mid(offset, size).join(QChar(' ')));
			offset += size;
			fits = fits && (width <= 0 || font->width(result.back()) <= width);
		}
		if (fits || lines == count) {
			return result;
		}
	}
	return {};
}

[[nodiscard]] TextWithEntities DetailsAddressValue(const QStringList &lines) {
	auto result = tr::marked(lines.join(QChar('\n')));
	if (result.empty()) {
		return result;
	}
	// WHY: a Code entity spanning newlines gets no click handler, so one
	// link over all lines makes the whole address a single press target.
	result.entities.push_back({
		EntityType::Code,
		0,
		int(result.text.size()),
	});
	return tr::link(std::move(result), 1);
}

AddressLabel::AddressLabel(
	QWidget *parent,
	const QString &address,
	const style::FlatLabel &st,
	Fn<void()> clicked)
: FlatLabel(parent, st)
, _address(address)
, _link(std::make_shared<LambdaClickHandler>(std::move(clicked))) {
	setLines(DetailsAddressLines(_address, st.style.font->monospace(), 0));
	_unwrappedWidth = textMaxWidth();
	_refreshing = false;
}

void AddressLabel::setLines(const QStringList &lines) {
	_lineCount = int(lines.size());
	setMarkedText(DetailsAddressValue(lines));
	setLink(1, _link);
}

int AddressLabel::resizeGetHeight(int newWidth) {
	if (_refreshing || newWidth <= 0) {
		return FlatLabel::resizeGetHeight(newWidth);
	}
	_refreshing = true;
	const auto guard = gsl::finally([&] { _refreshing = false; });
	const auto lines = DetailsAddressLines(
		_address,
		st().style.font->monospace(),
		newWidth);
	if (_lineCount != int(lines.size())) {
		setLines(lines);
		setNaturalWidth(_unwrappedWidth);
	}
	return FlatLabel::resizeGetHeight(newWidth);
}

} // namespace

object_ptr<Ui::FlatLabel> MakeAddressLabel(
		not_null<QWidget*> parent,
		const QString &address,
		const style::FlatLabel &st,
		Fn<void()> clicked) {
	return object_ptr<AddressLabel>(parent, address, st, std::move(clicked));
}

} // namespace Wallet
