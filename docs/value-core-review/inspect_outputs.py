"""Read the review probe's MIDI and SF2 independently of production encoders."""

from pathlib import Path
import struct


def program_changes(path):
    data = path.read_bytes()
    assert data[:4] == b"MThd"
    position = 8 + int.from_bytes(data[4:8], "big")
    programs = []
    while position < len(data):
        assert data[position:position + 4] == b"MTrk"
        size = int.from_bytes(data[position + 4:position + 8], "big")
        track = data[position + 8:position + 8 + size]
        index = 0
        running = None

        def varlen():
            nonlocal index
            value = 0
            while True:
                byte = track[index]
                index += 1
                value = (value << 7) | (byte & 127)
                if not byte & 128:
                    return value

        while index < len(track):
            varlen()  # Delta time.
            if track[index] & 128:
                status = track[index]
                index += 1
                if status < 240:
                    running = status
            else:
                assert running is not None
                status = running
            if status in (255, 240, 247):
                if status == 255:
                    index += 1  # Meta type.
                size = varlen()
                index += size
            else:
                if status & 240 == 192:
                    programs.append(track[index])
                index += 1 if status & 240 in (192, 208) else 2
        position += 8 + len(track)
    return programs


def sf2_presets(path):
    data = path.read_bytes()
    assert data[:4] == b"RIFF" and data[8:12] == b"sfbk"

    def chunks(start, end):
        while start + 8 <= end:
            kind = data[start:start + 4]
            size = struct.unpack_from("<I", data, start + 4)[0]
            begin = start + 8
            assert begin + size <= end
            yield kind, begin, begin + size
            start = begin + size + (size & 1)

    for kind, begin, end in chunks(12, len(data)):
        if kind != b"LIST" or data[begin:begin + 4] != b"pdta":
            continue
        for child, first, last in chunks(begin + 4, end):
            if child == b"phdr":
                assert (last - first) % 38 == 0
                # Exclude the terminal EOP record.
                return [struct.unpack_from("<HH", data, offset + 20)
                        for offset in range(first, last - 38, 38)]
    raise AssertionError("No preset header table")


if __name__ == "__main__":
    output = Path("/tmp/vgmtrans-value-review")
    for name in ("direct.mid", "ignore.mid", "paired.mid"):
        print(name, "program changes:", program_changes(output / name))
    for name in ("standalone.sf2", "paired.sf2"):
        print(name, "(program, bank):", sf2_presets(output / name))
    assert program_changes(output / "direct.mid") == [5, 0]
    assert program_changes(output / "ignore.mid") == [5]
    assert program_changes(output / "paired.mid") == [5, 0]
    assert sf2_presets(output / "standalone.sf2") == [(5, 0)]
    assert sf2_presets(output / "paired.sf2") == [(5, 0), (0, 0)]
    print("Confirmed: the direct MIDI's generated program is absent from the standalone bank.")
