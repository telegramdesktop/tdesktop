/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "ui/effects/peer_qr_surface.h"
#include "ui/rp_widget.h"

namespace Ui {

class ElasticScroll;
class PeerQrPanel;

struct PeerQrThemeKey {
	enum class Type : uchar {
		Default,
		Color,
		Backdrop,
		Emoji,
		Other,
	};
	Type type = Type::Default;
	int id = 0;
	std::vector<QString> emoji;

	friend inline bool operator==(
		const PeerQrThemeKey &,
		const PeerQrThemeKey &) = default;
};

struct PeerQrSwatch {
	PeerQrThemeKey key;
	PeerQrAppearance appearance;

	friend inline bool operator==(
		const PeerQrSwatch &,
		const PeerQrSwatch &) = default;
};

class PeerQrColorsPanel final : public RpWidget {
public:
	explicit PeerQrColorsPanel(not_null<PeerQrPanel*> panel);
	~PeerQrColorsPanel();

	[[nodiscard]] static int HeightForWidth(int width);

	void setSwatches(std::vector<PeerQrSwatch> swatches);
	void setSelected(std::optional<PeerQrThemeKey> key, anim::type animated);
	void scrollToSelected();

	[[nodiscard]] rpl::producer<PeerQrThemeKey> picks() const;

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	class Inner;

	const not_null<ElasticScroll*> _scroll;
	const not_null<Inner*> _inner;
	rpl::event_stream<PeerQrThemeKey> _picks;

};

} // namespace Ui
