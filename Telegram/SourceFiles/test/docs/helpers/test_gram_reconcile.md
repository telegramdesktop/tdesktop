# test_gram_reconcile

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_gram_reconcile.h](../../test_gram_reconcile.h).

## Use when

Reconcile golden-wallet custody and words. Read the contract below and the source header before use.

## Contract and usage

The P0 fixture reconciliation in [account fixtures](../account-fixtures.md):
`ReconcileGramAccount` / the one-stage `AppendGramReconcile` reconcile the
golden `test_gram_account.txt` with the marked live copy's own custody through
the product's local reveal and restore only - `NoOp`, case (b)
`RewroteFromReveal`, case (a) `RestoredFromFile`, or a refusal by a named
`GramReconcileGate` that writes nothing - and print one public
`GRAM_RECONCILE:` row; `GramReconcileResult::phrases` and
`GramCustodySecretTokens` hand the secrets to the secrecy scan in memory;
`GramReconcileDrive` is the self-test seam. Its own acceptance on the marked
live copy is `AppendGramReconcileSelfTest`.

## P0 reconciliation

The golden `tdata` is never modified while the file is, so a later campaign's
P0 reconciles the two before anything else — in process, after the address
gate has already passed. Do not hand-roll it: call `Test::AppendGramReconcile`
(one stage) or `Test::ReconcileGramAccount` (the flow) from
`test_gram_reconcile.h` right after the address gate. The two cases remain the
definition: (a) the local custody has FEWER words than the file: the golden
`tdata` predates a confirmed rotation, so restore custody from the file's
words through the product's own import (`Wallet::Session::restoreFromPhrase`)
before any other stage; (b) the local reveal differs from the file with an
equal or greater count: the live copy was promoted after a chain-confirmed
rotation but the rewrite was cut off, so rewrite the file from the local
reveal.

A phrase *qualifies* when it signs for the served key (its signing key — words
13-24 of a 24-word phrase, the anchor of a 12-word one — is
`Wallet::Session::publicKey()`) and derives the served address (its anchor,
words 1-12, derives `*wallet.address()`). The helper decides in this order,
and each refusal is a named gate (`GramReconcileGate`) that writes nothing:

1. Preconditions: `NotActive`; `ServedUnknown` (no served address or no
   32-byte served key); `FixtureAbsent`; `FixtureOtherWallet`;
   `CustodyUnreadable`; `CustodyBusy` (`custodyBusy()`, a pending rotation,
   or a record still awaiting its server key — such a record "signs with" any
   served key, so it cannot certify the file).
2. `NoOp`: the file qualifies and a readable current record exists. Nothing is
   written.
3. Case (a), `RestoredFromFile`: the file qualifies, no current record exists,
   and the local count is below the file's. The local count is 0 when no
   record of the served wallet is held — the fresh golden copy — and otherwise
   implied by the held obsolete record's keys: 12 words when its signing key
   is its anchor, else 24. It is never read by a local reveal: under the
   rotation-following custody model an obsolete record is not the current
   record, so `revealPhrase` would fall through to the server's
   `wallet.exportSecretPhrase`. The helper restores with the silent Open grant
   (no chooser opens), then requires a local reveal equal to the file
   (`RestoreFailed` / `ConfirmMismatch` otherwise).
4. The file qualifies but (a) does not apply: `NoReadableCurrentRecord`, the
   detail naming why — the current record is unreadable, the held obsolete
   record is not shorter than the file, or a pre-v4 unresolved record whose
   count is unknown (never `revealParked`, whose reveal writes custody).
5. The file does not qualify: `NoReadableCurrentRecord` unless
   `revealsLocally()`; `NoOpenGrant` without a silent grant; otherwise the
   local reveal (`RevealFailed <PHRASE_*>` when it fails), then
   `RevealedNotServedKey`, `RevealedNotServedAddress` or
   `RevealedShorterThanFile`. Otherwise case (b), `RewroteFromReveal`:
   `RewriteGramAccountWords(revealed, served address)`, then `GramAccount()`
   is re-read and compared in process (`RewriteFailed` when the live copy was
   not written, `RereadMismatch` when the re-read differs).

The stage writes one public `GRAM_RECONCILE:` row (case, gate, counts,
booleans, public keys and addresses), reports `NoOp`, (a) and (b) as `PASS`,
and a refusal as `TEST_RESULT: N/A` with `fixture gate: P0 <gate> - <detail>`;
a campaign's funded-wallet legs gate on `GramReconcileResult::reconciled()`.
A fresh golden copy whose keyring is not Open refuses `NoOpenGrant` (the
detail names the keyring kind) until the protection chooser has been driven
once on that copy. The helper never calls `wallet.replaceWallet`,
`wallet.exportSecretPhrase` or any other server method that mutates or
reveals the wallet, never resets and never mints. Its `GramReconcileDrive`
seam fakes only the served identity and the record facts it names, never
writes a fixture file (`DrivenWriteSuppressed`), and restores only when the
undriven facts show a same-phrase re-import. Its own acceptance is
`Test::AppendGramReconcileSelfTest`, on the marked live copy. Every stage of it
that reaches a reveal or a restore is a fixture gate, `NoOpenGrant
keyringKind=<N>`, without the silent Open grant, and a live copy staged to 12
words that case (b) did not rewrite is put back to its earlier words through
`RestoreGramAccountLiveWords` (the `gram_reconcile_restore_staged` stage, and
again when the scenario finishes).

Never delete or reset the marked live copy while a campaign may have left a
rotation in flight or unrewritten: SETUP keeps a marked live copy, and only a
manual wipe or `test-account-reset` discards it — and with it the only copy of
a not-yet-rewritten signing half.

## Related guidance

[Account prerequisites](../account-fixtures.md); [secrecy proof](test_secrecy_scan.md).
