#!/usr/bin/env python3
"""Downloads makerom and bannertool (needed only for the .cia) into tools/bin.
Picks the right file for this computer from each project's latest GitHub release."""
import io, json, os, platform, stat, sys, urllib.request, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
BIN = os.path.join(HERE, "bin")
SYSTEM = platform.system()
WANT = {"Linux": ["ubuntu", "linux"], "Darwin": ["macos", "mac", "darwin", "osx"], "Windows": ["win"]}.get(SYSTEM, [])
ARM = platform.machine().lower() in ("arm64", "aarch64")

def api(url):
    req = urllib.request.Request(url, headers={"Accept": "application/vnd.github+json", "User-Agent": "adc-build"})
    token = os.environ.get("GITHUB_TOKEN")
    if token:
        req.add_header("Authorization", "Bearer " + token)
    return json.load(urllib.request.urlopen(req))

def download(url):
    req = urllib.request.Request(url, headers={"User-Agent": "adc-build"})
    return urllib.request.urlopen(req).read()

def score(name):
    """Higher is better; -1 means this file is not for this computer."""
    n = name.lower()
    if not any(w in n for w in WANT):
        return -1
    s = 1
    if ARM and ("arm64" in n or "aarch64" in n):
        s += 2
    if not ARM and ("x86_64" in n or "amd64" in n or "x64" in n):
        s += 2
    if ARM and SYSTEM == "Darwin" and "x86_64" in n:
        s += 1  # runs under Rosetta
    return s

def install(data, tool):
    """`data` is a zip; pull out the best matching binary called `tool`."""
    z = zipfile.ZipFile(io.BytesIO(data))
    exe = tool + (".exe" if SYSTEM == "Windows" else "")
    members = [m for m in z.namelist() if os.path.basename(m) == exe]
    if not members:
        return False
    if len(members) > 1:
        ranked = sorted(members, key=score, reverse=True)
        if score(ranked[0]) < 0:
            return False
        members = ranked
    os.makedirs(BIN, exist_ok=True)
    out = os.path.join(BIN, exe)
    with open(out, "wb") as f:
        f.write(z.read(members[0]))
    os.chmod(out, os.stat(out).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    print("installed", out, "from", members[0])
    return True

def fetch(tool, repo, tag_prefix=""):
    releases = api("https://api.github.com/repos/%s/releases?per_page=30" % repo)
    for rel in releases:
        if tag_prefix and not rel["tag_name"].startswith(tag_prefix):
            continue
        assets = [a for a in rel["assets"] if a["name"].lower().endswith(".zip")]
        # zips named for this platform first, then generic all-platform zips
        ranked = sorted(assets, key=lambda a: score(a["name"]), reverse=True)
        for a in ranked:
            try:
                if install(download(a["browser_download_url"]), tool):
                    return True
            except zipfile.BadZipFile:
                continue
    return False

def main():
    ok = True
    for tool, repo, prefix in (("makerom", "3DSGuy/Project_CTR", "makerom"), ("bannertool", "Epicpkmn11/bannertool", "")):
        exe = os.path.join(BIN, tool + (".exe" if SYSTEM == "Windows" else ""))
        if os.path.exists(exe):
            print(tool, "already in tools/bin")
            continue
        try:
            if not fetch(tool, repo, prefix):
                print("Could not find a %s download for %s. Get it from https://github.com/%s/releases and put it in tools/bin."
                      % (tool, SYSTEM, repo))
                ok = False
        except Exception as e:  # network trouble, rate limit...
            print("Could not download %s (%s). Get it from https://github.com/%s/releases and put it in tools/bin." % (tool, e, repo))
            ok = False
    sys.exit(0 if ok else 1)

main()
