/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

namespace Ui {

class AbstractButton;
class VerticalLayout;

void AddCopyShareLinkButtons(
	not_null<VerticalLayout*> container,
	Fn<void()> copyLink,
	Fn<void()> shareLink);

void AddReactivateLinkButton(
	not_null<VerticalLayout*> container,
	Fn<void()> editLink);

void AddDeleteLinkButton(
	not_null<VerticalLayout*> container,
	Fn<void()> deleteLink);

struct JoinedCountContent {
	int count = 0;
	QImage userpics;
};

not_null<AbstractButton*> AddJoinedCountButton(
	not_null<VerticalLayout*> container,
	rpl::producer<JoinedCountContent> content,
	style::margins padding);

} // namespace Ui
