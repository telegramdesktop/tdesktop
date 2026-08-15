/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"
#include "base/basic_types.h"
#include "base/object_ptr.h"
#include "ui/widgets/rp_window.h"

#include <rpl/event_stream.h>

#include <memory>

namespace anim {
enum class type : uchar;
} // namespace anim

namespace Ui {
class BoxContent;
class LayerManager;
class LayerWidget;
class Show;
enum class LayerOption;
using LayerOptions = base::flags<LayerOption>;
} // namespace Ui

namespace Iv::Editor {

class Window final : public Ui::RpWindow {
public:
	explicit Window(QWidget *parent = nullptr);
	~Window();

	[[nodiscard]] rpl::producer<> imeCompositionStarts() const;
	void imeCompositionStartReceived();
	void setCloseRequestHandler(Fn<bool()> handler);

	void showBox(
		object_ptr<Ui::BoxContent> box,
		Ui::LayerOptions options,
		anim::type animated);
	void showLayer(
		std::unique_ptr<Ui::LayerWidget> layer,
		Ui::LayerOptions options,
		anim::type animated);
	void hideLayer(anim::type animated);
	[[nodiscard]] bool isLayerShown() const;
	[[nodiscard]] rpl::producer<bool> layerShownValue() const;
	[[nodiscard]] std::shared_ptr<Ui::Show> uiShow();

protected:
	bool eventHook(QEvent *event) override;

#ifdef Q_OS_WIN
	bool nativeEvent(
		const QByteArray &eventType,
		void *message,
		native_event_filter_result *result) override;
#elif defined Q_OS_MAC // Q_OS_WIN
	bool nativeEvent(
		const QByteArray &eventType,
		void *message,
		qintptr *result) override;
#endif // Q_OS_WIN || Q_OS_MAC

private:
	const std::unique_ptr<Ui::LayerManager> _layers;
	rpl::event_stream<> _imeCompositionStartReceived;
	Fn<bool()> _closeRequestHandler;

};

bool CloseActiveWindow();

} // namespace Iv::Editor
