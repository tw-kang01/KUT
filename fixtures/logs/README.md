# D3 synthetic log fixtures

Each input row is monotonic_seconds|verbosity|category|message; the test clock is 1000 seconds.
The matching .expected.txt is compared exactly after line-ending normalization.
All paths, errors and tokens are synthetic; no real key or private user log is stored.
Old and future entries verify time filtering, case differences verify safe grouping, and renderer errors survive noise filtering.
