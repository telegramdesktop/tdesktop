# test_web_cache

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_web_cache.h](../../test_web_cache.h).

## Use when

Clear the exact web-document cache entry. Read the contract below and the source header before use.

## Contract and usage

Remove a web document's persistent cache entry before a leg that must reach
the network. `Data::WebDocumentCacheKey` is the SHA-256 of the URL alone -
never the access hash - and the marked live portable folder is reused from run
to run with its caches, so a URL an earlier run loaded is a local hit that
`FileLoader` finishes with no `upload.getWebFile` at all.
`Test::RemoveWebDocumentCache(session, url)` removes that key from
`session->data().cache()` - where web documents land - and from
`cacheBigFile()`, which no current writer uses for them and which the removal
guards against a future one, and returns a `WebCacheRemoval` reading written
on main only: poll `finished()` (true only after both databases answered) from
a pure `until`, and judge `ok()` in `then` with `WebCacheRemovalText` as
details. It holds only a `base::weak_ptr` to the session: once the session is
gone nothing completes and the text says `session=gone`. Removing an absent
key completes `ok()`, so a campaign may remove unconditionally. It clears the
persistent caches only, not a media object that already holds the bytes in
this process. Own self-test over fabricated `.example.invalid` URLs with
synthetic bytes and no network, beside an unrelated key that must survive:
`AppendWebDocumentCacheSelfTest`.
