/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/box_content.h"
#include "base/binary_guard.h"
#include "history/admin_log/history_admin_log_item.h"
#include "history/view/history_view_element.h"
#include "ui/effects/animations.h"
#include "ui/effects/radial_animation.h"
#include "data/data_wall_paper.h"

namespace Data {
class DocumentMedia;
} // namespace Data

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
class Checkbox;
class ChatStyle;
class MediaSlider;
template <typename Widget>
class SlideWrap;
} // namespace Ui

struct BackgroundPreviewArgs {
	PeerData *forPeer = nullptr;
	FullMsgId fromMessageId;
};

class BackgroundPreviewBox
	: public Ui::BoxContent
	, private HistoryView::SimpleElementDelegate {
public:
	BackgroundPreviewBox(
		QWidget*,
		not_null<Window::SessionController*> controller,
		const Data::WallPaper &paper,
		BackgroundPreviewArgs args = {});
	~BackgroundPreviewBox();

	static bool Start(
		not_null<Window::SessionController*> controller,
		const QString &slug,
		const QMap<QString, QString> &params);

protected:
	void prepare() override;

	void paintEvent(QPaintEvent *e) override;

private:
	struct OverridenStyle;

	using Element = HistoryView::Element;
	not_null<HistoryView::ElementDelegate*> delegate();
	HistoryView::Context elementContext() override;

	void apply();
	void applyForPeer();
	void applyForEveryone();
	void uploadForPeer();
	void setExistingForPeer(const Data::WallPaper &paper);
	void share();
	void radialAnimationCallback(crl::time now);
	QRect radialRect() const;

	void generateBackground();
	void checkLoadedDocument();
	void setScaledFromThumb();
	void setScaledFromImage(QImage &&image, QImage &&blurred);
	void updateServiceBg(const std::vector<QColor> &bg);
	void paintImage(Painter &p);
	void paintRadial(Painter &p);
	void paintTexts(Painter &p, crl::time ms);
	void recreateBlurCheckbox();
	int textsTop() const;
	void startFadeInFrom(QPixmap previous);
	void checkBlurAnimationStart();

	[[nodiscard]] const style::Box &overridenStyle(bool dark);
	void paletteReady();
	void applyDarkMode(bool dark);
	[[nodiscard]] OverridenStyle prepareOverridenStyle(bool dark);

	void resetTitle();
	void rebuildButtons(bool dark);
	void createDimmingSlider(bool dark);

	const not_null<Window::SessionController*> _controller;
	PeerData * const _forPeer = nullptr;
	FullMsgId _fromMessageId;
	std::unique_ptr<Ui::ChatStyle> _chatStyle;
	const not_null<History*> _serviceHistory;
	AdminLog::OwnedItem _service;
	AdminLog::OwnedItem _text1;
	AdminLog::OwnedItem _text2;
	Data::WallPaper _paper;
	std::shared_ptr<Data::DocumentMedia> _media;
	QImage _full;
	QPixmap _generated, _scaled, _blurred, _fadeOutThumbnail;
	Ui::Animations::Simple _fadeIn;
	Ui::RadialAnimation _radial;
	base::binary_guard _generating;
	std::optional<QColor> _serviceBg;
	object_ptr<Ui::Checkbox> _blur = { nullptr };

	rpl::variable<bool> _appNightMode;
	rpl::variable<bool> _boxDarkMode;
	std::unique_ptr<OverridenStyle> _light, _dark;
	std::unique_ptr<style::palette> _lightPalette, _darkPalette;
	bool _waitingForPalette = false;

	object_ptr<Ui::SlideWrap<Ui::RpWidget>> _dimmingWrap = { nullptr };
	Ui::RpWidget *_dimmingContent = nullptr;
	Ui::MediaSlider *_dimmingSlider = nullptr;
	int _dimmingIntensity = 0;
	rpl::variable<int> _dimmingHeight = 0;
	bool _dimmed = false;
	bool _dimmingToggleScheduled = false;

	FullMsgId _uploadId;
	float64 _uploadProgress = 0.;
	rpl::lifetime _uploadLifetime;

	rpl::variable<QColor> _paletteServiceBg;
	rpl::lifetime _serviceBgLifetime;

};
