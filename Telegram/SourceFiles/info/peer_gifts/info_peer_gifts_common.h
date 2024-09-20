/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/abstract_button.h"
#include "ui/text/text.h"

namespace HistoryView {
class StickerPlayer;
} // namespace HistoryView

namespace Window {
class SessionController;
} // namespace Window

namespace Info::PeerGifts {

struct GiftTypePremium {
	int64 cost = 0;
	QString currency;
	int months = 0;
	int discountPercent = 0;

	[[nodiscard]] friend inline bool operator==(
		const GiftTypePremium &,
		const GiftTypePremium &) = default;
};

struct GiftTypeStars {
	uint64 id = 0;
	int64 stars = 0;
	int64 convertStars = 0;
	DocumentData *document = nullptr;
	PeerData *from = nullptr;
	bool limited = false;
	bool userpic = false;

	[[nodiscard]] friend inline bool operator==(
		const GiftTypeStars&,
		const GiftTypeStars&) = default;
};

struct GiftDescriptor : std::variant<GiftTypePremium, GiftTypeStars> {
	using variant::variant;

	[[nodiscard]] friend inline bool operator==(
		const GiftDescriptor&,
		const GiftDescriptor&) = default;
};

class GiftButtonDelegate {
public:
	[[nodiscard]] virtual TextWithEntities star() = 0;
	[[nodiscard]] virtual std::any textContext() = 0;
	[[nodiscard]] virtual QSize buttonSize() = 0;
	[[nodiscard]] virtual QMargins buttonExtend() = 0;
	[[nodiscard]] virtual QImage background() = 0;
	[[nodiscard]] virtual DocumentData *lookupSticker(
		const GiftDescriptor &descriptor) = 0;
};

class GiftButton final : public Ui::AbstractButton {
public:
	GiftButton(QWidget *parent, not_null<GiftButtonDelegate*> delegate);
	~GiftButton();

	void setDescriptor(const GiftDescriptor &descriptor);
	void setGeometry(QRect inner, QMargins extend);

private:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void setDocument(not_null<DocumentData*> document);

	const not_null<GiftButtonDelegate*> _delegate;
	GiftDescriptor _descriptor;
	Ui::Text::String _text;
	Ui::Text::String _price;
	QRect _button;
	QMargins _extend;

	std::unique_ptr<HistoryView::StickerPlayer> _player;
	rpl::lifetime _mediaLifetime;

};

class Delegate final : public GiftButtonDelegate {
public:
	explicit Delegate(not_null<Window::SessionController*> window);

	TextWithEntities star() override;
	std::any textContext() override;
	QSize buttonSize() override;
	QMargins buttonExtend() override;
	QImage background() override;
	DocumentData *lookupSticker(const GiftDescriptor &descriptor) override;

private:
	const not_null<Window::SessionController*> _window;
	QSize _single;
	QImage _bg;

};

} // namespace Info::PeerGifts
