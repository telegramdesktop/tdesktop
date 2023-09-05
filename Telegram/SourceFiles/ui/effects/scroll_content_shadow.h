/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

namespace Ui {

class RpWidget;
class ScrollArea;

void SetupShadowsToScrollContent(
	not_null<Ui::RpWidget*> parent,
	not_null<Ui::ScrollArea*> scroll,
	rpl::producer<int> &&innerHeightValue);

} // namespace Ui
