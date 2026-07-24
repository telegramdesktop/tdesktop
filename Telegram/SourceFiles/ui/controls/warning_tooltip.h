/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "ui/rect_part.h"
#include "ui/text/text_entity.h"

#include <QtCore/QPointer>

namespace style {
struct ImportantTooltip;
struct FlatLabel;
} // namespace style

namespace anim {
enum class type : uchar;
} // namespace anim

namespace Ui {

class ImportantTooltip;
class RpWidget;

class WarningTooltip final {
public:
	struct Args {
		not_null<RpWidget*> parent;
		not_null<QWidget*> target;
		rpl::producer<TextWithEntities> text;
		RectParts side = RectPart::Top;
		int maxWidth = 0;
		Fn<QPoint(QSize)> countPosition;
		crl::time duration = 0;
		const style::ImportantTooltip *st = nullptr;
		const style::FlatLabel *labelSt = nullptr;
	};

	WarningTooltip();
	~WarningTooltip();

	void show(Args &&args);
	void hide(anim::type animated);

private:
	struct Entry {
		QPointer<ImportantTooltip> tooltip;
	};
	std::unique_ptr<Entry> _current;

};

} // namespace Ui
