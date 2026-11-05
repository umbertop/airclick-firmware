# Load the remaining AirClick app segments into the current program.
# Run this in Ghidra's Script Manager with the seg2 program open.
#@category AirClick
#@menupath Tools.AirClick.Load Segments

from ghidra.program.model.mem import MemoryBlockType
from java.io import ByteArrayInputStream

SEGMENTS = [
    ("seg0_rodata", "/home/umberto/airclick-firmware/reference/app0_seg0_0x3c110020.bin", 0x3C110020, False),
    ("seg1_data",   "/home/umberto/airclick-firmware/reference/app0_seg1_0x3fc8da00.bin", 0x3FC8DA00, False),
    ("seg3_data",   "/home/umberto/airclick-firmware/reference/app0_seg3_0x3fc8df18.bin", 0x3FC8DF18, False),
    ("seg4_iram",   "/home/umberto/airclick-firmware/reference/app0_seg4_0x40380000.bin", 0x40380000, True),
]

program = getCurrentProgram()
memory = program.getMemory()
space = program.getAddressFactory().getDefaultAddressSpace()

for name, path, addr, executable in SEGMENTS:
    address = space.getAddress(addr)
    # Skip if a block already covers this address (don't double-load)
    existing = memory.getBlock(address)
    if existing is not None:
        print("SKIP %s (already at %s)" % (name, existing.getName()))
        continue

    with open(path, "rb") as f:
        data = f.read()

    block = memory.createInitializedBlock(
        name, address, ByteArrayInputStream(data), len(data),
        None, None, False)  # name, addr, stream, size, comment, source, overlay
    block.setRead(True)
    block.setWrite(False)
    block.setExecute(executable)
    block.setVolatile(False)
    print("LOADED %s at 0x%08X (%d bytes)" % (name, addr, len(data)))

print("Done. Now run Analysis -> Auto Analyze to disassemble + build xrefs.")
