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
		const style::FlatLabel &st);

protected:
	int resizeGetHeight(int newWidth) override;

private:
	const QString _address;
	int _unwrappedWidth = 0;
	int _lineCount = 0;
	bool _refreshing = true;

};

[[nodiscard]] TextWithEntities DetailsAddressValue(
		const QString &address,
		const style::font &font,
		int width) {
	auto groups = QStringList();
	for (auto offset = 0; offset < address.size(); offset += kAddressGroup) {
		groups.append(address.mid(offset, kAddressGroup));
	}
	const auto count = int(groups.size());
	for (auto lines = 1; lines <= count; ++lines) {
		auto result = tr::marked();
		auto fits = true;
		auto offset = 0;
		for (auto i = 0; i != lines; ++i) {
			const auto size = (count / lines) + (i < count % lines ? 1 : 0);
			const auto line = groups.mid(offset, size).join(QChar(' '));
			offset += size;
			if (i) {
				result.text += QChar('\n');
			}
			result.entities.push_back({
				EntityType::Code,
				int(result.text.size()),
				int(line.size()),
			});
			result.text += line;
			fits = fits && (width <= 0 || font->width(line) <= width);
		}
		if (fits || lines == count) {
			return result;
		}
	}
	return tr::marked();
}

AddressLabel::AddressLabel(
	QWidget *parent,
	const QString &address,
	const style::FlatLabel &st)
: FlatLabel(parent, st)
, _address(address) {
	const auto text = DetailsAddressValue(
		_address,
		st.style.font->monospace(),
		0);
	_lineCount = int(text.entities.size());
	setMarkedText(text);
	_unwrappedWidth = textMaxWidth();
	_refreshing = false;
}

int AddressLabel::resizeGetHeight(int newWidth) {
	if (_refreshing || newWidth <= 0) {
		return FlatLabel::resizeGetHeight(newWidth);
	}
	_refreshing = true;
	const auto guard = gsl::finally([&] { _refreshing = false; });
	const auto text = DetailsAddressValue(
		_address,
		st().style.font->monospace(),
		newWidth);
	const auto lines = int(text.entities.size());
	if (_lineCount != lines) {
		_lineCount = lines;
		setMarkedText(text);
		setNaturalWidth(_unwrappedWidth);
	}
	return FlatLabel::resizeGetHeight(newWidth);
}

} // namespace

object_ptr<Ui::FlatLabel> MakeAddressLabel(
		not_null<QWidget*> parent,
		const QString &address,
		const style::FlatLabel &st) {
	return object_ptr<AddressLabel>(parent, address, st);
}

} // namespace Wallet
