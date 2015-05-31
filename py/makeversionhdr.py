"""
Generate header file with macros defining MicroPython version info.

This script works with Python 2.6, 2.7, 3.3 and 3.4.
"""

from __future__ import print_function

import sys
import os
import datetime
import subprocess

def get_version_info_from_git():
    # Python 2.6 doesn't have check_output, so check for that
    try:
        subprocess.check_output
        subprocess.check_call
    except AttributeError:
        return None

    # Note: git describe doesn't work if no tag is available
    try:
        git_tag = subprocess.check_output(["git", "describe", "--dirty", "--always"], universal_newlines=True).strip()
    except subprocess.CalledProcessError:
        git_tag = ""
    except OSError:
        return None
    try:
        git_hash = subprocess.check_output(["git", "rev-parse", "--short", "HEAD"], stderr=subprocess.STDOUT, universal_newlines=True).strip()
    except subprocess.CalledProcessError:
        git_hash = "unknown"
    except OSError:
        return None

    try:
        # Check if there are any modified files.
        subprocess.check_call(["git", "diff", "--no-ext-diff", "--quiet", "--exit-code"], stderr=subprocess.STDOUT)
        # Check if there are any staged files.
        subprocess.check_call(["git", "diff-index", "--cached", "--quiet", "HEAD", "--"], stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError:
        git_hash += "-dirty"
    except OSError:
        return None

    # Try to extract MicroPython version from git tag
    if git_tag.startswith("v"):
        ver = git_tag[1:].split("-")[0].split(".")
        if len(ver) == 2:
            ver.append("0")
    else:
        ver = ["0", "0", "1"]

    return git_tag, git_hash, ver

def get_version_info_from_docs_conf():
    with open("%s/docs/conf.py" % sys.argv[0].rsplit("/", 2)[0]) as f:
        for line in f:
            if line.startswith("release = '"):
                ver = line.strip()[10:].strip("'")
                git_tag = "v" + ver
                ver = ver.split(".")
                if len(ver) == 2:
                    ver.append("0")
                return git_tag, "<no hash>", ver
    return None

def make_version_header(filename):
    # Get version info using git, with fallback to docs/conf.py
    info = get_version_info_from_git()
    if info is None:
        info = get_version_info_from_docs_conf()

    git_tag, git_hash, ver = info

    # Generate the file with the git and version info
    file_data = """\
// This file was generated by py/makeversionhdr.py
#define MICROPY_GIT_TAG "%s"
#define MICROPY_GIT_HASH "%s"
#define MICROPY_BUILD_DATE "%s"
#define MICROPY_VERSION_MAJOR (%s)
#define MICROPY_VERSION_MINOR (%s)
#define MICROPY_VERSION_MICRO (%s)
#define MICROPY_VERSION_STRING "%s.%s.%s"
""" % (git_tag, git_hash, datetime.date.today().strftime("%Y-%m-%d"),
    ver[0], ver[1], ver[2], ver[0], ver[1], ver[2])

    # Check if the file contents changed from last time
    write_file = True
    if os.path.isfile(filename):
        with open(filename, 'r') as f:
            existing_data = f.read()
        if existing_data == file_data:
            write_file = False

    # Only write the file if we need to
    if write_file:
        print("Generating %s" % filename)
        with open(filename, 'w') as f:
            f.write(file_data)

if __name__ == "__main__":
    make_version_header(sys.argv[1])
