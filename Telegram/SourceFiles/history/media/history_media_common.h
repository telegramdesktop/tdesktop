/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace HistoryView {
class Element;
} // namespace HistoryView

namespace Data {
class Media;
} // namespace Data

class DocumentData;
class PhotoData;
class HistoryMedia;

int documentMaxStatusWidth(DocumentData *document);

void PaintInterpolatedIcon(
	Painter &p,
	const style::icon &a,
	const style::icon &b,
	float64 b_ratio,
	QRect rect);

std::unique_ptr<HistoryMedia> CreateAttach(
	not_null<HistoryView::Element*> parent,
	DocumentData *document,
	PhotoData *photo,
	const std::vector<std::unique_ptr<Data::Media>> &collage = {});
int unitedLineHeight();
