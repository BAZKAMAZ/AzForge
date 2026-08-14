"""Reads a .room and gives up its grid.

    python room_grid.py <file.room> [more...]
    python room_grid.py "C:\\Games\\az_orginal\\data\\levels\\orange\\*.room"

Prints the grid and writes <folder>#<name>.grid.txt beside this script.

Everything here was measured on all 179 rooms the game ships, not read off a document. The header
is 0xC8 bytes: +0x04 is the real length and +0x08 says where the grid sits. A pak entry is padded,
so the file's own length is the one to trust, and the grid offset is read rather than assumed even
though it is 200 in every shipped room.

A cell is eight bytes and the game reads one of them, the type at +0x00. Bytes 1 and 2 come out of
the file and nothing has been found to read them. Bytes 3 to 7 are zero in every shipped room,
which is why AzForge hands those five to mods: RoomEdit paints material and the carve mark there,
Depth reads the floor depth.
"""

import collections
import glob
import os
import struct
import sys

COLUMNS = 40
ROWS = 29
CELL = 8
GRID = COLUMNS * ROWS * CELL

HEAD_GRID = 0x08

# The five bytes of a cell the game never reads, as AzForge numbers them
FREE_FIRST = 3
FREE_COUNT = 5

# What RoomEdit and Depth put in the first three of them
LAYERS = (("material", 0), ("depth", 1), ("carved", 2))

# One character a cell, so a layer of numbers prints the same shape as the letters do
DIGITS = "0123456789abcdefghijklmnopqrstuvwxyz"


class Room(object):
    def __init__(self, path):
        self.path = path

        with open(path, "rb") as handle:
            self.image = handle.read()

        if len(self.image) < HEAD_GRID + 4:
            raise ValueError("%d byte(s), which is no room" % len(self.image))

        self.magic, self.length, self.gridAt = struct.unpack_from("<3I", self.image, 0)

        if self.length == 0 or self.length > len(self.image):
            raise ValueError("says it is %d byte(s) and is %d" % (self.length, len(self.image)))

        if self.gridAt + GRID > self.length:
            raise ValueError("puts its grid at %d, which does not fit in %d" % (self.gridAt, self.length))

        self.grid = self.image[self.gridAt:self.gridAt + GRID]

    def cell(self, column, row):
        at = (row * COLUMNS + column) * CELL
        return self.grid[at:at + CELL]

    def type(self, column, row):
        return chr(self.cell(column, row)[0])

    def free(self, column, row, which):
        return self.cell(column, row)[FREE_FIRST + which]


def digit(value):
    if value == 0:
        return "."

    return DIGITS[value] if value < len(DIGITS) else "#"


def layer(pick):
    lines = ["    " + "".join(str(column // 10) for column in range(COLUMNS)),
             "    " + "".join(str(column % 10) for column in range(COLUMNS))]

    for row in range(ROWS):
        lines.append("%3d %s" % (row, "".join(pick(column, row) for column in range(COLUMNS))))

    return lines


def render(room):
    out = [room.path,
           "%d byte(s) on disk, %d by its own header (magic 0x%08X)" % (len(room.image), room.length, room.magic),
           "grid at +0x%X, %d x %d cells of %d bytes" % (room.gridAt, COLUMNS, ROWS, CELL),
           "",
           "TYPE     the one byte the game itself reads, '.' is empty"]

    out += layer(lambda column, row: room.type(column, row).replace(" ", "."))

    counted = collections.Counter(room.type(column, row)
                                  for row in range(ROWS) for column in range(COLUMNS))

    out.append("")
    out.append("    " + "   ".join("'%s' %d" % (letter.replace(" ", "."), many)
                                   for letter, many in sorted(counted.items())))

    for name, which in LAYERS:
        held = [room.free(column, row, which) for row in range(ROWS) for column in range(COLUMNS)]

        out.append("")

        if not any(held):
            out.append("%-8s free byte %d - nothing painted here" % (name.upper(), which))
            continue

        out.append("%-8s free byte %d, '.' is 0, then 1-9 a-z, '#' past 35" % (name.upper(), which))
        out += layer(lambda column, row, this=which: digit(room.free(column, row, this)))

    # Said out loud rather than left out, so a room carrying something unexpected shows it
    spare = collections.Counter()

    for row in range(ROWS):
        for column in range(COLUMNS):
            record = room.cell(column, row)
            spare[(record[1], record[2], record[FREE_FIRST + 3], record[FREE_FIRST + 4])] += 1

    out.append("")
    out.append("REST     the file's own bytes 1 and 2, and free bytes 3 and 4")

    for what, many in spare.most_common():
        out.append("    %s x %d" % (" ".join("%3d" % byte for byte in what), many))

    return out


def savedName(path):
    # Rooms are all called 002.room, so the folder comes with the name - the way RoomEdit saves them
    full = os.path.abspath(path)
    folder = os.path.basename(os.path.dirname(full))
    stem = os.path.splitext(os.path.basename(full))[0]

    return "%s#%s.grid.txt" % (folder, stem) if folder else "%s.grid.txt" % stem


def main(argv):
    paths = []

    for arg in argv:
        found = sorted(glob.glob(arg, recursive=True))
        paths += found if found else [arg]

    if not paths:
        print(__doc__)
        return 2

    beside = os.path.dirname(os.path.abspath(__file__))
    bad = 0

    for path in paths:
        try:
            room = Room(path)
        except (IOError, OSError, ValueError) as why:
            print("%s: %s" % (path, why))
            bad += 1
            continue

        text = "\n".join(render(room))
        where = os.path.join(beside, savedName(path))

        with open(where, "w", encoding="utf-8") as handle:
            handle.write(text + "\n")

        print(text)
        print("\nwritten to %s\n" % where)

    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
