/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/business/data_business_common.h"

class FilterChatsPreview;

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Settings {

struct BusinessExceptionsDescriptor {
	Data::BusinessExceptions current;
	Fn<void(const Data::BusinessExceptions&)> save;
	bool allow = false;
};
void EditBusinessExceptions(
	not_null<Window::SessionController*> window,
	BusinessExceptionsDescriptor &&descriptor);

not_null<FilterChatsPreview*> SetupBusinessExceptionsPreview(
	not_null<Ui::VerticalLayout*> content,
	not_null<rpl::variable<Data::BusinessExceptions>*> data);

} // namespace Settings
