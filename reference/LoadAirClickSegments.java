// Load the remaining AirClick app segments into the current program.
// Run in Script Manager with the seg2 program open. No Python/PyGhidra required.
//@category AirClick
//@menupath Tools.AirClick.Load Segments
import ghidra.app.script.GhidraScript;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.address.Address;
import java.io.File;
import java.io.FileInputStream;
import java.io.ByteArrayInputStream;

public class LoadAirClickSegments extends GhidraScript {

    private static final String DIR = "/home/umberto/airclick-firmware/reference/";

    @Override
    public void run() throws Exception {
        Memory memory = currentProgram.getMemory();
        load(memory, "seg0_rodata", DIR + "app0_seg0_0x3c110020.bin", 0x3C110020L, false);
        load(memory, "seg1_data",   DIR + "app0_seg1_0x3fc8da00.bin", 0x3FC8DA00L, false);
        load(memory, "seg3_data",   DIR + "app0_seg3_0x3fc8df18.bin", 0x3FC8DF18L, false);
        load(memory, "seg4_iram",   DIR + "app0_seg4_0x40380000.bin", 0x40380000L, true);
        println("Done. Now run Analysis -> Auto Analyze.");
    }

    private void load(Memory memory, String name, String path, long addr, boolean exec) throws Exception {
        Address address = currentProgram.getAddressFactory()
                .getDefaultAddressSpace().getAddress(addr);
        if (memory.getBlock(address) != null) {
            println("SKIP  " + name + " (already loaded)");
            return;
        }
        File f = new File(path);
        byte[] data = new byte[(int) f.length()];
        try (FileInputStream in = new FileInputStream(f)) {
            int off = 0;
            while (off < data.length) {
                int n = in.read(data, off, data.length - off);
                if (n < 0) break;
                off += n;
            }
        }
        MemoryBlock block = memory.createInitializedBlock(
                name, address, new ByteArrayInputStream(data), data.length, null, null, false);
        block.setRead(true);
        block.setWrite(false);
        block.setExecute(exec);
        println("LOADED " + name + " at " + address + " (" + data.length + " bytes)");
    }
}
