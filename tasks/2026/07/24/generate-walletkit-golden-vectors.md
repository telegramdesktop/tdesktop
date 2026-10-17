# Generate WalletKit golden vectors for transfers and hashes

No client code changes: this task generated the WalletKit golden vectors
that the upcoming td_gram wallet library will be pinned against, using a
one-off tsx script run against the local WalletKit checkout
(packages/walletkit, @ton/walletkit 1.1.0-beta.0, with @ton/core 0.63.1
and @ton/crypto 3.3.0).

The vectors pin, for the W5R1 fixture wallet (fixture mnemonic, walletId
2147483409, seqno 5, fixed validUntil 1753300000, 10000000 nano to the
wallet's own bounceable address EQDSLOFVamNZzdy4LulclcCBEFkRReZ7WscBCLAw
3Pg53kAk with plaintext comment "test", send mode 3, StateInit attached):

- the signed external-transfer BOC and its FakeSign twin,
- the TEP-467 normalized external-message hashes of both (they differ,
  because normalization keeps the body and the W5 body embeds the
  signature),
- the W5R1 code cell representation hash,
- the mnemonic-derived 64-byte secret and 32-byte public key.

The generator script, the vector data files, and the regeneration notes
live in the AI task's work/ directory (work/generate-vectors.mts,
work/vectors/, work/notes.md). Future td_gram tests (gram/tests/) should
copy these vectors into their committed fixtures and must reproduce the
signed-transfer bytes and hashes exactly.
