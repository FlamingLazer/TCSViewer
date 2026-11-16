"""Download verified official portable OBS representatives into qa-runtimes."""
import hashlib
import json
from pathlib import Path
import urllib.request
import zipfile


def main():
    root = Path(__file__).resolve().parent / "qa-runtimes"
    root.mkdir(exist_ok=True)
    for version in ("32.0.4", "32.2.2"):
        metadata = root / (version + "-release.json")
        if not metadata.exists():
            request = urllib.request.Request(
                "https://api.github.com/repos/obsproject/obs-studio/releases/tags/" + version,
                headers={"User-Agent": "TCSViewer-compatibility-tests"})
            with urllib.request.urlopen(request, timeout=60) as response:
                metadata.write_bytes(response.read())
        release = json.loads(metadata.read_text(encoding="utf-8-sig"))
        asset = next(a for a in release["assets"] if a["name"] == f"OBS-Studio-{version}-Windows-x64.zip")
        archive = root / asset["name"]
        if not archive.exists():
            partial = archive.with_suffix(".zip.part")
            urllib.request.urlretrieve(asset["browser_download_url"], partial)
            partial.replace(archive)
        with archive.open("rb") as source:
            actual = "sha256:" + hashlib.file_digest(source, "sha256").hexdigest()
        if not asset.get("digest") or actual != asset["digest"]:
            raise RuntimeError(f"{version}: missing or mismatched official SHA256")
        destination = root / version
        with zipfile.ZipFile(archive) as package:
            for entry in package.infolist():
                if not (destination / entry.filename).resolve().is_relative_to(destination.resolve()):
                    raise RuntimeError("Unsafe archive path")
            package.extractall(destination)
        print(version, "verified and extracted:", destination, flush=True)


if __name__ == "__main__":
    main()
