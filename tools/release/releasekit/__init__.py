"""Local release tool: preflight, build, GitHub release, VirusTotal and Steam Workshop.

``tools/release/release.py`` is the command-line entry point; this package holds the
code, one module per step plus the shared building blocks:

* :mod:`releasekit.config`: the typed view of ``release.toml`` (every path, repository
  name and Workshop item lives there, so the tool moves with the repository split).
* :mod:`releasekit.console`: coloured status lines, the log file and the final summary.
* :mod:`releasekit.runner`: the only place that starts processes (``CommandRunner``).
* :mod:`releasekit.prompts`: typed confirmations (``Prompter``).
* :mod:`releasekit.versions`, :mod:`releasekit.changelog`, :mod:`releasekit.gitinfo`:
  version sources, release notes and repository state.
* :mod:`releasekit.preflight`, :mod:`releasekit.package`, :mod:`releasekit.github`,
  :mod:`releasekit.virustotal`, :mod:`releasekit.workshop`: the five steps.
* :mod:`releasekit.signing`: RSA signature check and the installer pin block.
* :mod:`releasekit.cli`: argument parsing and the order of the steps.

Every external effect goes through an adapter (process runner, prompter, HTTP
transport, clock), so the tests replace them and nothing real is started.
"""
