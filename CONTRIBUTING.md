# Contributing

Bug reports, focused fixes, test coverage and documentation improvements are welcome.
For a larger feature or a change to the widget's behavior, open an issue first
to describe the problem and proposed approach.

## Before making changes

- Read the [README](README.md), including compatibility and verification limits.
- Search existing issues and pull requests for the same problem.
- Keep each pull request focused. Separate unrelated fixes and formatting changes.
- Keep discussions about the code and observable behavior. Respect other contributors.
- Report suspected vulnerabilities using [SECURITY.md](SECURITY.md).

## Development

Use Windows 11, Python 3.10 or newer, and Windhawk with its bundled compiler and
engine libraries. Run these commands from the repository root in PowerShell:

```powershell
python .\tests\validate-source.py
.\build.ps1
.\build.ps1 -Architecture aarch64 -OutputDirectory .\build-arm64
.\tests\run-regression.ps1
.\tests\run-ui-smoke.ps1
.\tests\run-metrics-smoke.ps1
```

The Python validator uses only the standard library. The build, regression and
UI scripts accept `-WindhawkRoot` for a custom installation. The metrics script
currently expects `C:\Program Files\Windhawk`.

For code changes, run source validation, both builds and regression tests. Run
the UI smoke test for layout, rendering or move changes, and the metrics smoke
test for collector changes. For documentation-only changes, check links, commands
and consistency with the source; native builds are not needed.

The UI smoke test runs in an isolated host. The metrics smoke test reads the
local machine's Windows counters and adapters. Neither proves behavior in a live
Explorer session or on different hardware. For taskbar changes, also check the
affected flow in Explorer and record Windows build, taskbar setup, monitor DPI
and other taskbar mods. Read the [verification details](README.md#development-and-verification)
before interpreting results.

## Pull requests

Create a branch from `main` in this repository or your fork, then open a pull
request against `main`. Direct pushes, force pushes and deletion of `main` are
blocked. Resolve review conversations before merging.

- Explain the problem and the resulting behavior.
- List the exact checks you ran and their results. State skipped checks and why.
- For UI changes, include before/after images and the tested display setup.
- Preserve existing contributor credits and keep new attribution accurate.
- Use English commit messages with a specific Conventional Commit title, such as
  `fix(taskbar): restore widget after available space increases`.

Do not describe an ARM64 build as an ARM64 hardware test, or an isolated render
as a live taskbar test. See [LICENSE](LICENSE) for the project's GPL-3.0 terms.
