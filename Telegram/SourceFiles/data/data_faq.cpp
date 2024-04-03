/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_faq.h"

#include "apiwrap.h"
#include "core/application.h"
#include "data/data_session.h"
#include "data/data_web_page.h"
#include "iv/iv_instance.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "main/session/session_show.h"
#include "ui/basic_click_handlers.h"

namespace Data {

void Faq::open(std::shared_ptr<Main::SessionShow> show) {
	if (!_id) {
		show->session().api().request(MTPmessages_GetWebPage(
			MTP_string(tr::lng_settings_faq_link(tr::now)),
			MTP_int(0)
		)).done([=](const MTPmessages_WebPage &result) {
			show->session().data().processUsers(result.data().vusers());
			show->session().data().processChats(result.data().vchats());
			const auto page = show->session().data().processWebpage(
				result.data().vwebpage());
			if (page && page->iv) {
				_id = page->id;
				open(show);
			} else {
				UrlClickHandler::Open(tr::lng_settings_faq_link(tr::now));
			}
		}).fail([=] {
			UrlClickHandler::Open(tr::lng_settings_faq_link(tr::now));
		}).send();
	} else {
		const auto page = show->session().data().webpage(_id);
		if (page && page->iv) {
			const auto parts = tr::lng_settings_faq_link(tr::now).split('#');
			const auto hash = (parts.size() > 1) ? parts[1] : u""_q;
			Core::App().iv().show(show, page->iv.get(), hash);
		} else {
			UrlClickHandler::Open(tr::lng_settings_faq_link(tr::now));
		}
	}
}

} // namespace Data
