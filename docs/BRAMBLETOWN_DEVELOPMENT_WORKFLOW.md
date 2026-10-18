# Brambletown module development workflow

This process applies to Brambletown-maintained AzerothCore modules and changes
to third-party forks. Keep the working production code separate from proposed changes.

## 1. Establish the source of truth

- Confirm the repository source matches the **currently deployed module**,
  including local uncommitted edits. Do not overwrite or assume a GitHub fork
  equals the running server; compare the live directory against the repository.
- Record the AzerothCore revision, module dependencies, current configuration
  compatibility, and a known-working commit or release tag.
- Before publishing code to a public repository, inspect tracked files and
  history for credentials, webhook URLs, private server details, private data,
  and license constraints. Keep runtime config, database dumps, and credentials
  outside the repository. Rotate any secrets that were previously published.
- Keep each custom module in its own repository unless a shared component
  justifies a dedicated shared library or a separate reviewed monorepo.

## 2. Develop as a proposed change

1. Branch from the stable target branch:
   `feature/<short-topic>`, `fix/<short-topic>`, or `chore/<short-topic>`.
2. Implement one coherent change and commit with a descriptive message.
3. Build against the server's actual AzerothCore revision and module set.
4. Run specific game tests and document both passed and untested cases.
5. Open a pull request to the stable branch and complete the PR template.
6. Do not change running production files as part of submitting a review.

## 3. Obtain independent CodeRabbit review

- Install/authorize the CodeRabbit GitHub App for the repository via
  https://app.coderabbit.ai/ . A `.coderabbit.yaml` file alone does **not**
  install the app or trigger reviews.
- For public repositories with fewer than ten stars, submit
  `@coderabbitai review` as a PR comment if the review does not start.
  Use `@coderabbitai full review` when the entire PR needs re-evaluation.
- Review every material finding, fix defects, and document any finding
  judged inapplicable (including the evidence for that decision).
- Do not assume CodeRabbit can verify custom AzerothCore hooks, database
  state, runtime configuration, NPC spawning, or playerbot behavior without
  a matching build and live-style test.

## 4. Merge and deploy

- Merge only when the targeted build/checks pass, CodeRabbit feedback is
  handled (or its unavailability is recorded), and a human has approved.
- Record the merged SHA and tag an intentional known-good release where useful.
- Back up changed configuration or other relevant files before deployment.
- Deploy that exact reviewed commit; note it in the release/PR record.
- Perform a smoke test after restart or hot reload, as appropriate to the
  change. Do not assume configuration reload replaces code compilation.
- Revert to the last known-good commit when a release fails verification.

## Suggested module-specific regression checks

- First-login/starter modules: new account/character, existing character,
  character previously operated as a playerbot, repeated login, full bags,
  equipment eligibility, and one-time flags.
- NPC Services: GM/regular player permissions, dead/combat/instance limits,
  provider enable flags, cooldowns, configuration reload, playerbot feast
  behavior, and menu continuity.
- Playerbot changes: human-online lifecycle, group roles, combat and travel
  behavior, pathing restrictions, and regression tests for upstream updates.

## GitHub conventions

- Preserve upstream attribution and licenses; avoid mixing an upstream sync
  with a custom behavior change in the same pull request.
- Never force-push or overwrite the stable branch to hide failed reviews.
- CI failures and CodeRabbit findings are separately evaluated.
- Auto-merge and automated production deployment stay disabled until
  explicitly approved. GitHub code state is not proof of server deployment.

This repository has an existing AzerothCore module build GitHub Actions
workflow; use its pull-request result as one signal, not the only release gate.
