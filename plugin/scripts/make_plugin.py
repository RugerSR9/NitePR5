#!/usr/bin/env python3
"""Vendored from etaHEN-Plugins lib/make_plugin.py.

Wraps an ELF as an etaHEN .plugin: header etaHEN_PLUGIN\\0TID\\0version\\0 plus ELF bytes.
"""

import re
import sys


def is_valid_version(version):
    return re.match(r"^\d\.\d{2}$", version) is not None


def is_valid_tid(tid):
    # etaHEN-Plugins: 4 letters + 5 digits. Locked NitePR5 title NPR500001 is 3+6.
    if re.match(r"^[A-Za-z]{4}\d{5}$", tid):
        return True
    return tid == "NPR500001"


def add_header_to_elf(elf_filename, tid, version):
    if not is_valid_version(version):
        print("Error: Version must be in the format x.xx (e.g., 1.00)")
        sys.exit(1)
    if not is_valid_tid(tid):
        print("Error: TID must be the first 4 letters followed by 5 numbers (e.g., ABCD12345)")
        sys.exit(1)

    header_prefix = b"etaHEN_PLUGIN"
    header = header_prefix + b"\0" + tid.encode("ascii") + b"\0" + version.encode("ascii") + b"\0"
    if ".elf" in elf_filename:
        new_filename = elf_filename.rsplit(".elf", 1)[0] + ".plugin"
    else:
        new_filename = elf_filename + ".plugin"

    try:
        with open(elf_filename, "rb") as elf_file:
            elf_contents = elf_file.read()
        with open(new_filename, "wb") as new_elf_file:
            new_elf_file.write(header + elf_contents)

        with open(new_filename, "rb") as file:
            probe = file.read(len(header_prefix))
        if probe.startswith(b"etaHEN_PLUGIN"):
            print("Verification: Header correctly added.")
        else:
            print("Verification: Header not found.")
            sys.exit(1)

        print(f"Header added to {elf_filename} and saved as {new_filename}.")
        print(f"Plugin Info: {tid} {version}")
    except FileNotFoundError:
        print(f"Error: The file {elf_filename} was not found.")
        sys.exit(1)
    except Exception as e:
        print(f"An error occurred: {str(e)}")
        sys.exit(1)


if __name__ == "__main__":
    if len(sys.argv) < 4:
        print("Usage: python3 make_plugin.py <elf> <TID> <version>")
        sys.exit(1)

    elf_filename = sys.argv[1]
    tid = sys.argv[2]
    version = sys.argv[3]

    if is_valid_version(version) and is_valid_tid(tid):
        add_header_to_elf(elf_filename, tid, version)
    else:
        print("Invalid TID or version format.")
        sys.exit(1)
