#!/usr/bin/env python3
# Collects the LiteRT NPU runtime for one phone into a local folder,
# ./litert/ unless --out says otherwise. Copying it to /litert/ on the
# phone is up to the user.
#
# Usage:
#   tools/litert-npu.py 2.3.0,Mediatek,MT6878
#   tools/litert-npu.py 2.3.0,QTI,SM8650 --qairt ~/qairt/2.50.0.260828
# The first argument is litert.version() on the phone.
#
# MediaTek and Google Tensor: the runtime AAR from Google Maven.
# Qualcomm: the runtime AAR, plus QNN from the user's own QAIRT SDK
# (Qualcomm licence; never downloaded here).
import argparse
import html
import os
import re
import shutil
import sys
import urllib.error
import urllib.request
import zipfile

MAVEN = "https://dl.google.com/android/maven2/com/google/ai/edge/litert"
CACHE = os.path.join(os.path.expanduser("~"), ".cache", "upy-android")

# ro.soc.manufacturer -> Google Maven artifact suffix.
RUNTIMES = {
    "mediatek": "mediatek",
    "google": "google-tensor",
    "qti": "qualcomm",
}

# Qualcomm's table of SoC and Hexagon version, in the QAIRT docs.
QAIRT_SOC_TABLE = "docs/QAIRT-Docs/QNN/general/overview.html"


def fail(msg):
    sys.stderr.write("litert-npu: %s\n" % msg)
    sys.exit(1)


def fetch_aar(version, runtime):
    name = "litert-npu-runtime-%s-%s.aar" % (runtime, version)
    path = os.path.join(CACHE, name)
    if os.path.exists(path):
        return path
    url = "%s/litert-npu-runtime-%s/%s/%s" % (MAVEN, runtime, version, name)
    print("downloading", url)
    os.makedirs(CACHE, exist_ok=True)
    try:
        r = urllib.request.urlopen(url)
    except urllib.error.HTTPError as e:
        fail("%s: HTTP %d (no %s runtime for LiteRT %s?)" % (url, e.code, runtime, version))
    with r, open(path + ".part", "wb") as f:
        shutil.copyfileobj(r, f)
    os.replace(path + ".part", path)
    return path


def extract_aar(aar, out):
    names = []
    with zipfile.ZipFile(aar) as z:
        for n in z.namelist():
            if n.startswith("jni/arm64-v8a/") and n.endswith(".so"):
                name = os.path.basename(n)
                with z.open(n) as src, open(os.path.join(out, name), "wb") as dst:
                    shutil.copyfileobj(src, dst)
                names.append(name)
    if not names:
        fail("%s has no arm64-v8a libraries" % aar)
    return names


def hexagon_version(qairt, model):
    path = os.path.join(qairt, QAIRT_SOC_TABLE)
    try:
        with open(path, encoding="utf-8") as f:
            text = f.read()
    except OSError:
        fail("%s not found: give --hexagon" % path)
    text = html.unescape(re.sub(r"\s+", " ", re.sub(r"<[^>]+>", " ", text)))
    # Rows: "SD 8 Gen 3 (SM8650) aarch64-android 57 V75 –",
    # "750G (SM7225) 690 (SM6350) aarch64-android 29 V66 V66 –"
    m = re.search(r"\b%s\b.{0,120}?\b(V\d\d|N/A)\b" % re.escape(model), text)
    if not m:
        fail("%s is not in the QAIRT SoC table: give --hexagon" % model)
    if m.group(1) == "N/A":
        fail("%s has no Hexagon NPU (QAIRT SoC table)" % model)
    return m.group(1)[1:]


def qairt_files(qairt, hexagon):
    files = [
        "lib/aarch64-android/libQnnHtp.so",
        "lib/aarch64-android/libQnnSystem.so",
        "lib/aarch64-android/libQnnHtpV%sStub.so" % hexagon,
        "lib/hexagon-v%s/unsigned/libQnnHtpV%sSkel.so" % (hexagon, hexagon),
    ]
    paths = [os.path.join(qairt, rel) for rel in files]
    for path in paths:
        if not os.path.exists(path):
            fail("%s not found: is --qairt the QAIRT version folder?" % path)
    return paths


def main():
    p = argparse.ArgumentParser(description="Collect the LiteRT NPU runtime for one phone.")
    p.add_argument("version", metavar="VERSION,MAKER,MODEL", help="litert.version() on the phone")
    p.add_argument("--out", default="litert", help="folder to write the files to (default: litert)")
    p.add_argument("--qairt", help="Qualcomm: the QAIRT version folder, e.g. ~/qairt/2.50.0.260828")
    p.add_argument("--hexagon", help="Qualcomm: Hexagon version, e.g. 73, if the SoC is not in the QAIRT table")
    a = p.parse_args()

    fields = a.version.split(",")
    if len(fields) != 3:
        fail("expected VERSION,MAKER,MODEL, e.g. 2.3.0,Mediatek,MT6878")
    version, maker, model = fields
    if "unknown" in (maker, model):
        fail("the phone does not report its chip (Android 11 or older?)")
    runtime = RUNTIMES.get(maker.lower())
    if runtime is None:
        fail("no LiteRT NPU runtime for maker %r" % maker)
    if runtime == "qualcomm" and not a.qairt:
        fail("Qualcomm needs QNN from the QAIRT SDK: download it from Qualcomm and give --qairt")

    if os.path.isdir(a.out) and os.listdir(a.out):
        fail("%s is not empty: remove it first, so files of another phone do not end up on this one" % a.out)
    # Everything that can fail comes before the first file is written.
    qnn = []
    if runtime == "qualcomm":
        qairt = os.path.expanduser(a.qairt)
        hexagon = (a.hexagon or "").lstrip("vV") or hexagon_version(qairt, model)
        qnn = qairt_files(qairt, hexagon)
    aar = fetch_aar(version, runtime)

    os.makedirs(a.out, exist_ok=True)
    names = extract_aar(aar, a.out)
    for path in qnn:
        shutil.copyfile(path, os.path.join(a.out, os.path.basename(path)))
        names.append(os.path.basename(path))

    print("written to %s:" % a.out)
    for n in names:
        print("  " + n)
    print("""
Next:
  1. Copy these files to /litert/ on the phone, for example over SFTP
     (Settings > Enable SSH):
       sftp -P 2222 user@<phone-ip>
       sftp> mkdir /litert
       sftp> put %s/*.so /litert/
  2. Settings > Load accelerators from /litert/, then restart the app.""" % a.out)


if __name__ == "__main__":
    main()
