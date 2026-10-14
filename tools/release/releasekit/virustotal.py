"""Step 4 (optional), VirusTotal: scan the release files and put the links in the notes.

ADR-0007: every published binary is scanned before release, and an upload makes the
file public, so each run asks for a typed confirmation before uploading. Files that
VirusTotal already knows (looked up by SHA-256) are not uploaded again.

Scanned: the release ZIP and every ``.exe``/``.dll`` inside it. The API key is read only
from the environment variable named in the configuration (default ``VT_API_KEY``); it
is sent as the ``x-apikey`` header and redacted from all output and logs. Results go to
``virustotal.json`` and ``virustotal.md`` in the output folder; the GitHub step puts the
Markdown into the release notes (``--update-notes`` refreshes an existing draft).
"""

from __future__ import annotations

import io
import json
import secrets
import urllib.error
import urllib.request
import zipfile
from dataclasses import dataclass
from pathlib import PurePosixPath
from typing import TYPE_CHECKING, Protocol

from releasekit import github
from releasekit.console import Status
from releasekit.package import sha256_bytes

if TYPE_CHECKING:
    from collections.abc import Mapping

    from releasekit.context import Context

STEP = "virustotal"
"""Step name in the summary."""

API = "https://www.virustotal.com/api/v3"
"""VirusTotal API v3 base URL."""

GUI_FILE = "https://www.virustotal.com/gui/file/"
"""Public report page prefix (followed by the SHA-256)."""

SCANNED_SUFFIXES = (".exe", ".dll")
"""ZIP entries that are scanned on their own."""

HTTP_OK = 200
"""HTTP status of a successful request."""

HTTP_NOT_FOUND = 404
"""HTTP status of an unknown file."""

MAX_DIRECT_UPLOAD = 32 * 1024 * 1024
"""Largest file the plain ``/files`` endpoint accepts (bytes)."""

RESULTS_JSON = "virustotal.json"
"""Machine-readable results in the output folder."""


@dataclass(frozen=True)
class HttpResponse:
    """Status and body of an HTTP response."""

    status: int
    body: bytes

    def json(self) -> dict[str, object]:
        """Decode a JSON object body.

        Returns:
            The object (empty when the body is not a JSON object).
        """
        try:
            value = json.loads(self.body.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            return {}
        if not isinstance(value, dict):
            return {}
        return {str(key): item for key, item in value.items()}


class HttpTransport(Protocol):
    """Sends HTTP requests."""

    def request(
        self, method: str, url: str, headers: Mapping[str, str], body: bytes | None = None
    ) -> HttpResponse:
        """Send one request.

        Args:
            method: HTTP method.
            url: HTTPS URL.
            headers: Request headers.
            body: Request body.

        Returns:
            The response (HTTP errors are responses, not exceptions).
        """
        ...


class UrllibTransport:
    """The real :class:`HttpTransport` (standard library, HTTPS only)."""

    def request(
        self, method: str, url: str, headers: Mapping[str, str], body: bytes | None = None
    ) -> HttpResponse:
        """Send one request.

        Args:
            method: HTTP method.
            url: HTTPS URL.
            headers: Request headers.
            body: Request body.

        Returns:
            The response; network failures give status 0.

        Raises:
            ValueError: The URL is not HTTPS.
        """
        if not url.startswith("https://"):
            msg = f"refusing a non-HTTPS URL: {url}"
            raise ValueError(msg)
        request = urllib.request.Request(url, data=body, headers=dict(headers), method=method)  # noqa: S310 - HTTPS checked above
        try:
            with urllib.request.urlopen(request, timeout=120) as response:  # noqa: S310 - HTTPS checked above
                return HttpResponse(response.status, response.read())
        except urllib.error.HTTPError as error:
            return HttpResponse(error.code, error.read())
        except OSError as error:
            return HttpResponse(0, str(error).encode())


@dataclass(frozen=True)
class ScanTarget:
    """One file to scan."""

    name: str
    data: bytes

    @property
    def sha256(self) -> str:
        """Lower-case SHA-256 (VirusTotal's form).

        Returns:
            Lower-case SHA-256 (VirusTotal's form).
        """
        return sha256_bytes(self.data).lower()


@dataclass(frozen=True)
class ScanResult:
    """Result for one file."""

    name: str
    sha256: str
    flagged: int | None
    engines: int | None
    uploaded: bool

    @property
    def link(self) -> str:
        """Public report URL.

        Returns:
            Public report URL.
        """
        return GUI_FILE + self.sha256


def targets_from_zip(name: str, data: bytes) -> list[ScanTarget]:
    """The ZIP itself and every executable inside it.

    Args:
        name: ZIP file name.
        data: ZIP bytes.

    Returns:
        Scan targets, ZIP first.
    """
    found = [ScanTarget(name, data)]
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        found.extend(
            ScanTarget(info.filename, archive.read(info))
            for info in archive.infolist()
            if PurePosixPath(info.filename).suffix.lower() in SCANNED_SUFFIXES
        )
    return found


def stats(report: Mapping[str, object]) -> tuple[int, int] | None:
    """Flagged and total engine counts from a file or analysis object.

    Args:
        report: ``data`` object of a VirusTotal response.

    Returns:
        ``(malicious + suspicious, all engines)`` or ``None`` when absent.
    """
    attributes = report.get("attributes")
    if not isinstance(attributes, dict):
        return None
    counts = attributes.get("last_analysis_stats", attributes.get("stats"))
    if not isinstance(counts, dict) or not counts:
        return None
    numbers: dict[str, int] = {str(k): int(v) for k, v in counts.items() if isinstance(v, int)}
    flagged = numbers.get("malicious", 0) + numbers.get("suspicious", 0)
    return flagged, sum(numbers.values())


def multipart(field: str, filename: str, data: bytes) -> tuple[bytes, str]:
    """Encode one file as ``multipart/form-data``.

    Args:
        field: Form field name.
        filename: File name sent to the server.
        data: File bytes.

    Returns:
        Body and its ``Content-Type`` header value.
    """
    boundary = "releasekit-" + secrets.token_hex(16)
    safe_name = filename.replace('"', "_").replace("/", "_")
    head = (
        f"--{boundary}\r\nContent-Disposition: form-data; "
        f'name="{field}"; filename="{safe_name}"\r\n'
        "Content-Type: application/octet-stream\r\n\r\n"
    ).encode()
    return (
        head + data + f"\r\n--{boundary}--\r\n".encode(),
        f"multipart/form-data; boundary={boundary}",
    )


class VirusTotalClient:
    """Minimal VirusTotal API v3 client."""

    def __init__(self, transport: HttpTransport, api_key: str) -> None:
        """Create the client.

        Args:
            transport: HTTP adapter.
            api_key: API key (sent only as a header).
        """
        self._transport = transport
        self._headers = {"x-apikey": api_key, "accept": "application/json"}

    def lookup(self, sha256: str) -> HttpResponse:
        """Ask for an existing report.

        Args:
            sha256: File hash.

        Returns:
            The response (404 = unknown file).
        """
        return self._transport.request("GET", f"{API}/files/{sha256}", self._headers)

    def upload(self, target: ScanTarget) -> HttpResponse:
        """Upload a file for analysis.

        Args:
            target: File.

        Returns:
            The response (``data.id`` is the analysis ID).
        """
        body, content_type = multipart("file", PurePosixPath(target.name).name, target.data)
        return self._transport.request(
            "POST", f"{API}/files", {**self._headers, "content-type": content_type}, body
        )

    def analysis(self, analysis_id: str) -> HttpResponse:
        """Read an analysis.

        Args:
            analysis_id: ID from :meth:`upload`.

        Returns:
            The response.
        """
        return self._transport.request("GET", f"{API}/analyses/{analysis_id}", self._headers)


def _data(response: HttpResponse) -> dict[str, object]:
    """The ``data`` object of a response.

    Args:
        response: Response.

    Returns:
        The object (empty when absent).
    """
    data = response.json().get("data")
    return {str(k): v for k, v in data.items()} if isinstance(data, dict) else {}


def wait_for_analysis(
    ctx: Context, client: VirusTotalClient, analysis_id: str
) -> tuple[int, int] | None:
    """Poll an analysis until it is completed or the configured time is up.

    Args:
        ctx: Run context.
        client: API client.
        analysis_id: Analysis ID.

    Returns:
        Engine counts, or ``None`` when it did not finish in time.
    """
    waited = 0.0
    config = ctx.config.virustotal
    while waited <= config.max_wait_seconds:
        ctx.sleep(config.poll_seconds)
        waited += config.poll_seconds
        data = _data(client.analysis(analysis_id))
        attributes = data.get("attributes")
        if isinstance(attributes, dict) and attributes.get("status") == "completed":
            return stats(data)
    return None


def render_markdown(results: list[ScanResult]) -> str:
    """The VirusTotal section of the release notes.

    Args:
        results: Scan results.

    Returns:
        Markdown list, one line per file.
    """
    lines = []
    for result in results:
        if result.flagged is None or result.engines is None:
            verdict = "analysis pending"
        else:
            verdict = f"{result.flagged}/{result.engines} engines flagged it"
        lines.append(f"- `{result.name}`: [{verdict}]({result.link})")
    return "\n".join(lines)


def run(ctx: Context) -> bool:
    """Run the whole VirusTotal step.

    Args:
        ctx: Run context.

    Returns:
        True when nothing failed.
    """
    reporter = ctx.reporter
    reporter.heading("VirusTotal (optional, uploads make files public)")
    zip_name = ctx.config.bundle.zip_name.format(version=ctx.version)
    zip_path = ctx.out_dir / zip_name
    env_name = ctx.options.api_key_env or ctx.config.virustotal.api_key_env
    api_key = ctx.env.get(env_name, "")
    reporter.redact(api_key)
    if not zip_path.is_file():
        status = Status.PLAN if ctx.options.dry_run else Status.FAIL
        reporter.record(STEP, status, f"{zip_path} not built yet")
        return ctx.options.dry_run
    targets = targets_from_zip(zip_name, zip_path.read_bytes())
    if ctx.options.dry_run:
        for target in targets:
            reporter.line(
                Status.PLAN,
                f"look up, and upload after confirmation if unknown: {target.name} {target.sha256}",
            )
        if not api_key:
            reporter.record(STEP, Status.WARN, f"environment variable {env_name} is not set")
        return True
    if not api_key:
        reporter.record(
            STEP, Status.FAIL, f"set the API key in the environment variable {env_name}"
        )
        return False
    client = VirusTotalClient(ctx.transport, api_key)
    results, unknown = _lookup_all(ctx, client, targets)
    if unknown:
        listing = ", ".join(f"{t.name} ({len(t.data)} bytes)" for t in unknown)
        if ctx.prompter.confirm(
            f"Upload {len(unknown)} file(s) to VirusTotal? They become public: {listing}", "upload"
        ):
            results += _upload_all(ctx, client, unknown)
        else:
            reporter.record(
                STEP, Status.SKIP, f"upload not confirmed: {len(unknown)} file(s) not scanned"
            )
    _write_results(ctx, results)
    if ctx.options.update_notes and results:
        github.update_notes(ctx)
    return not reporter.failed(STEP)


def _lookup_all(
    ctx: Context, client: VirusTotalClient, targets: list[ScanTarget]
) -> tuple[list[ScanResult], list[ScanTarget]]:
    """Look every file up by hash.

    Args:
        ctx: Run context.
        client: API client.
        targets: Files.

    Returns:
        Results of known files and the files VirusTotal does not know.
    """
    results: list[ScanResult] = []
    unknown: list[ScanTarget] = []
    for index, target in enumerate(targets):
        if index:
            ctx.sleep(ctx.config.virustotal.request_gap_seconds)
        response = client.lookup(target.sha256)
        if response.status == HTTP_NOT_FOUND:
            unknown.append(target)
            continue
        if response.status != HTTP_OK:
            ctx.reporter.record(
                STEP, Status.FAIL, f"lookup of {target.name} failed (HTTP {response.status})"
            )
            continue
        counts = stats(_data(response))
        result = ScanResult(target.name, target.sha256, *(counts or (None, None)), uploaded=False)
        results.append(result)
        ctx.reporter.record(
            STEP, Status.OK, f"{target.name}: already known, {render_markdown([result])}"
        )
    return results, unknown


def _upload_all(
    ctx: Context, client: VirusTotalClient, targets: list[ScanTarget]
) -> list[ScanResult]:
    """Upload files and wait for their analyses.

    Args:
        ctx: Run context.
        client: API client.
        targets: Confirmed files.

    Returns:
        Their results.
    """
    results = []
    for target in targets:
        if len(target.data) > MAX_DIRECT_UPLOAD:
            ctx.reporter.record(
                STEP,
                Status.FAIL,
                f"{target.name} is larger than 32 MB (upload_url not implemented)",
            )
            continue
        ctx.sleep(ctx.config.virustotal.request_gap_seconds)
        response = client.upload(target)
        analysis_id = _data(response).get("id")
        if response.status != HTTP_OK or not isinstance(analysis_id, str):
            ctx.reporter.record(
                STEP, Status.FAIL, f"upload of {target.name} failed (HTTP {response.status})"
            )
            continue
        counts = wait_for_analysis(ctx, client, analysis_id)
        result = ScanResult(target.name, target.sha256, *(counts or (None, None)), uploaded=True)
        results.append(result)
        status = Status.OK if counts is not None else Status.WARN
        ctx.reporter.record(STEP, status, f"{target.name}: uploaded, {render_markdown([result])}")
    return results


def _write_results(ctx: Context, results: list[ScanResult]) -> None:
    """Store the results next to the release files.

    Args:
        ctx: Run context.
        results: Scan results.
    """
    if not results:
        return
    out = ctx.out_dir
    payload = [
        {
            "name": r.name,
            "sha256": r.sha256,
            "flagged": r.flagged,
            "engines": r.engines,
            "uploaded": r.uploaded,
            "link": r.link,
        }
        for r in results
    ]
    (out / RESULTS_JSON).write_text(
        json.dumps(payload, indent=2) + "\n", encoding="utf-8", newline="\n"
    )
    (out / github.VIRUSTOTAL_NOTES_NAME).write_text(
        render_markdown(results) + "\n", encoding="utf-8", newline="\n"
    )
    flagged = [r.name for r in results if r.flagged is not None and r.flagged > 0]
    if flagged:
        ctx.reporter.record(
            STEP,
            Status.WARN,
            f"flagged by at least one engine: {', '.join(flagged)} "
            "(explain in the notes or investigate)",
        )
