# Account fixture secrets

[Harness guide](../README.md) · [Helper index](helpers/README.md)

The golden `test_TelegramForcePortable` folder may carry secrets beside
`tdata`. Today those are `2svpassword.txt` and `test_gram_account.txt`.
`2svpassword.txt` is the test account's two-step verification (cloud)
password, which the server demands on `wallet.exportSecretPhrase`,
`wallet.replaceWallet` and the password route of `wallet.disableBackup`
(`PASSWORD_MISSING` without it, `SRP_ID_INVALID` for a fabricated proof).
`wallet.enableBackup`, which has no password field, and the proof route of
`wallet.disableBackup` prove key ownership instead of sending the password. A
scenario reads it at runtime through
`Test::TwoStepPassword()` — whitespace-trimmed, `std::nullopt` when the file
is absent or blank, always `std::nullopt` outside test-agent mode — and the
generic `Test::FixtureSecret(name)` reads any sibling file the same way. Both
look in the live portable folder first and then in the golden sibling, so a
file added to the golden folder after a marked live copy was made is still
found.

Type the value into the product's own `PasscodeBox` so the real SRP path
computes the proof; never assemble an `InputCheckPasswordSRP` by hand. When a
selected check needs the password, the file's presence is gated before the
scenario is authored and its absence blocks the task (`pipeline.md`, Phase 3
step 5); a scenario that still reaches a missing file at runtime refuses with
a named fixture gate (`fixture gate: 2svpassword.txt absent`), never a
product FAIL, and never substitutes a driven answer for the leg. The value
never leaves the process: no overlay literal, `Note`, check `details`,
capture, evidence file, `work/` artifact, receipt, prompt, or environment
variable recorded anywhere may carry it. The overlay is published into the
task repository with the result, so a literal there is a leak, not a
convenience.

`test_gram_account.txt` is the owner's funded golden wallet: the test
account's server-created wallet, recorded by hand and funded at its address.
The format is the word lines until the first empty line, then the address as
the app shows it; never assume a fixed word count, because a confirmed key
rotation lengthens it. A scenario reads it at runtime through
`Test::GramAccount()` — the live copy first, then the golden sibling — which
returns the words, the address line and `addressRaw`, the address normalized
through `Wallet::CanonicalAddress` so either form compares; `std::nullopt`
when the file is absent, has no word line or no address line, and always
outside test-agent mode. The golden sibling is consulted only when the live
copy is absent or blank — a present but malformed live copy answers
`std::nullopt` rather than masking a different golden file, because after a
rotation the live copy is the authority. The words are under the same rule as
the password: typed or compared in process only, never in overlay code, a
`Note`, check `details`, a capture, an evidence file, a `work/` artifact, a
receipt or a prompt. Only the COUNT of the words and the address may be
recorded.

A campaign that declares it needs the funded wallet is gated on it at
campaign start, before anything is spent: the file must be present;
`GramAccount()->addressRaw` must equal `*wallet.address()` once
`WalletRefreshSettled` accepted the served state — a mismatch FAILS the run
and is never repaired by `/wallet_reset`, `wallet.replaceWallet` or a minted
wallet; and the balance must cover every planned live leg plus a margin — a
shortfall refuses with a named fixture gate (`fixture gate: golden wallet
balance short`) and the performer publishes the task-local Block naming the
address and the amount required, never a driven workaround. Campaigns on the
golden wallet never reset it and spend only what a leg needs; state the count
of live legs the run issued. A live leg also satisfies the effective
`wallet_transfer_min_nanos` floor in force for that account, in addition to
the conservative 0.3 GRAM evidence guidance, which the configured floor does
not replace. No campaign spends a sub-floor amount to observe how the
backend answers: the client's own amount refusal is established from
controlled configuration and local send fixtures, and an at-the-floor
control may stop at fee preview.

After a confirmed key rotation the scenario calls
`Test::RewriteGramAccountWords(newWords, addressRaw)` from the stage that
observed the confirmation, before any later leg can cut the run off. It
rewrites the word lines of every existing copy — the marked live copy AND the
golden sibling — through `QSaveFile`, keeping the empty line and the address
line; each copy is parsed on its own and rewritten only when its own address
is the rotated wallet's (the scenario passes the served raw address); a copy
naming another wallet is left alone and reported `false`. It returns which
copies were written; the stage checks both and re-reads `Test::GramAccount()`
to compare the count and the words in process. The golden write is the one
owner-decided exception to the read-only golden folder; nothing else in the
harness or in a campaign ever writes there.

## Required wallet companion references

After the address gate, use [P0 reconciliation](helpers/test_gram_reconcile.md)
before any wallet leg. Read [secrecy proof](helpers/test_secrecy_scan.md) before
a secret-bearing run; the account prerequisites here do not replace its log
selection, control and canary requirements.

## Public identity and log coverage

For publication-identity or launch-log coverage checks, read the separate
[publication and coverage contract](helpers/test_publication_identity_scan.md).
Those claims do not certify fixture secrecy.
