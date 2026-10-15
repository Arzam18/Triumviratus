# Contributing to Triumviratus

Two branches: **`main`** holds the released code (8.0), **`dev`** holds the work toward 8.1. Pull requests go to
`dev`; every pull request is reviewed and merged by hand.

## Code contributions

1. Fork the repository and branch from `dev`. Changes that keep the search identical can go together in one pull
   request, listed in its description; each change to the search goes in its own pull request.
2. Build and run the bench (`source/Makefile`, then `bench` in the engine). Put the result in the last commit
   message as `Bench: <nodes>`. The check on the pull request builds the engine and compares the two numbers.
3. Say which kind of change it is:
   - **Same search** (speed, refactoring, cleanup): the bench must not change. Give the speed gain you measured and
     on which CPU. We repeat the measurement on our machines and run a short non-regression test.
   - **Different search** (pruning, reductions, time management, evaluation): the bench changes. We test it with an
     SPRT on our machines, and it is merged only if it passes.
4. Say whether the code is your own, ported from another engine (which one, and under which licence), or generated
   with an AI tool.

Contributions are accepted under the project's licence, GPLv3. Every merged contribution is credited in the README
under the name the contributor chooses.

## Testing

Matches against other engines and test-suite runs are welcome. Open an issue with the **Test report** template, or
post in the project's Discord. A useful report gives:

- the Triumviratus build (version, file name or SHA-256) and the opponents' versions;
- the hardware, threads and hash per engine, and the time control or time per position;
- the opening book and the number of games, or the test suite;
- the result, and the PGN file or the list of failed positions when possible.

Results from testers are published in [`tests/`](tests/README.md) with their names, only with their agreement.
