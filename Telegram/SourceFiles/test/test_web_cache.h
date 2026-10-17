/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "storage/cache/storage_cache_types.h"

namespace Main {
class Session;
} // namespace Main

namespace Test {

class Runner;

// Removing a web document's persistent local cache entry before a leg that
// must reach the network.
//
// Data::WebDocumentCacheKey (data/data_types.cpp) is the SHA-256 of the
// document's URL alone - the access hash never enters it. Every -testagent
// run reuses the marked live portable folder together with its caches, so a
// canned or fabricated URL that an earlier run (or an earlier leg of this
// one) loaded through upload.getWebFile is found by FileLoader::loadLocal in
// session->data().cache() and finished locally: no request is issued at
// all. A leg that expects a held or counted upload.getWebFile then sees
// none, a canned answer finds no registered request id, and a "no request
// was issued" or zero-count check reads true for the wrong reason.
//
// This clears the two persistent Storage::Cache::Database files only. Web
// documents land in cache() today; the cacheBigFile() removal guards a
// future writer, since no current path puts a WebDocumentCacheKey there. A
// media object that already holds the bytes in this process (a live
// DocumentMedia or CloudFile view) is not evicted by it.

// The reading of one removal. Written on main only.
struct WebCacheRemoval {
	Storage::Cache::Key key;
	base::weak_ptr<Main::Session> session;
	bool smallDone = false;
	bool bigDone = false;
	Storage::Cache::Error smallError;
	Storage::Cache::Error bigError;

	// True only after BOTH databases answered.
	[[nodiscard]] bool finished() const;

	// finished() with no error from either database.
	[[nodiscard]] bool ok() const;
};

// "key=<high hex>:<low hex> small=<pending|ok|io:<path>|...> big=<...>
// session=<alive|gone> finished=<0|1>", the one formatter for Check details
// and timeoutDetails, so a pass prints what a timeout would.
[[nodiscard]] QString WebCacheRemovalText(const WebCacheRemoval &removal);

// Removes Data::WebDocumentCacheKey(WebFileLocation(url, 0)) from
// session->data().cache() and session->data().cacheBigFile(). The session
// is touched only inside this call; each database answers on its own queue
// thread and hops to main through a base::weak_ptr to the session before it
// writes the reading, so the reading is never written before this call
// returns and nothing is written once the session is gone (the reading then
// never finishes and its text says session=gone, so a bounded wait times
// out with a named reason). An absent key completes ok(), so a campaign may
// remove unconditionally. Call it in a stage's run, poll finished() from a
// pure until, and judge ok() in then with WebCacheRemovalText as details.
[[nodiscard]] std::shared_ptr<const WebCacheRemoval> RemoveWebDocumentCache(
	not_null<Main::Session*> session,
	const QByteArray &url);

// The helper's own self-test. It writes synthetic bytes under a fabricated
// https://web-cache-selftest.example.invalid/ subject URL's key and an
// unrelated control URL's key in both caches, reads them back as a
// positive control, removes the subject through RemoveWebDocumentCache,
// and then reads the subject key back empty from both caches while the
// control key still returns its bytes. No loader is ever handed either
// URL, so it makes no network request. It waits for the session itself,
// ends with a teardown that removes both keys, and emits no deliberate
// failure - every stage is expected to PASS on a healthy harness.
void AppendWebDocumentCacheSelfTest(not_null<Runner*> runner);

} // namespace Test
