"""Address-model boundaries and read-only MCB inspection; no game fixtures."""
import json
from pathlib import Path
import struct
import tempfile
import unittest

from retail_heap import AddressHeap, read_heap, replay
from retail_heap_trace import FUNCTIONS, trace_lua

START, END = 0x80100000, 0x80101000


class AddressTests(unittest.TestCase):
    def test_four_byte_rounding_and_top_down_header(self):
        h = AddressHeap(START, END)
        self.assertEqual(h.allocate(5, 0), START + 8)
        self.assertEqual(h.allocate(5, 0), START + 24)
        self.assertEqual(h.allocate(5, 1), END - 16)

    def test_zero_size_and_end_sentinel(self):
        h = AddressHeap(START, START + 16)
        self.assertEqual(h.allocate(0, 0), START + 8)
        self.assertEqual(h.allocate(0, 0), 0)
        self.assertEqual(h.blocks[-1].user, 1)

    def test_exact_fit_consumes_four_byte_slack(self):
        for mode in (0, 1, 2):
            with self.subTest(mode=mode):
                h = AddressHeap(START, START + 36)
                self.assertEqual(h.allocate(16, mode), START + 8)
                self.assertEqual(h.blocks[0].size, 20)
                self.assertEqual(len(h.blocks), 2)

    def test_lazy_merge_recovers_headers(self):
        h = AddressHeap(START, END)
        a, b, c = [h.allocate(16, 0) for _ in range(3)]
        h.free(a)
        h.free(b)
        self.assertEqual(len(h.blocks), 5)
        self.assertEqual(h.allocate(40, 0), a)
        self.assertEqual(h.find(c).user, 10)

    def test_best_fit_and_top_down_select_different_holes(self):
        for mode in (0, 1, 2):
            h = AddressHeap(START, START + 176)
            a = h.allocate(40, 0)
            h.allocate(8, 0)
            b = h.allocate(24, 0)
            h.allocate(64, 0)  # consumes the high free block exactly
            h.free(a)
            h.free(b)
            expected = {0: a, 1: b + 16, 2: b}[mode]
            self.assertEqual(h.allocate(8, mode), expected)

    def test_top_down_prefers_highest_exact_block(self):
        h = AddressHeap(START, START + 120)
        a = h.allocate(16, 0)
        h.allocate(8, 0)
        b = h.allocate(16, 0)
        h.allocate(8, 0)
        h.allocate(8, 0)
        h.free(a)
        h.free(b)
        self.assertEqual(h.allocate(16, 1), b)

    def test_pins_survive_rebase(self):
        h = AddressHeap(START, END)
        high = h.allocate(32, 1)
        h.pin(high, True)
        self.assertEqual(h.free(high), -1)
        h.allocate(32, 0)
        h.reset(START + 64)
        self.assertEqual(h.allocate(32, 0), START + 72)
        self.assertTrue(h.find(high).pinned)
        h.pin(high, False)
        self.assertEqual(h.free(high), 0)

    def test_shrink_threshold_and_return_is_header(self):
        h = AddressHeap(START, END)
        a = h.allocate(64, 0)
        self.assertEqual(h.shrink(a, 48), 0)
        self.assertEqual(h.shrink(a, 44), a - 8)
        self.assertEqual(h.allocate(12, 0), a + 52)

    def test_restore_json_before_lazy_merge_and_pin_release(self):
        h = AddressHeap(START, END)
        a = h.allocate(12, 0)
        b = h.allocate(32, 1)
        h.pin(b, True)
        h.free(a)
        copy = AddressHeap.restore(json.loads(json.dumps(h.state())))
        for heap in (h, copy):
            self.assertEqual(heap.allocate(12, 0), a)
            heap.pin(b, False)
            heap.free(b)
            heap.reset(START + 0x80)
            heap.allocate(40, 2)
        self.assertEqual(h.state(), copy.state())
        copy.allocate(12, 0)
        self.assertNotEqual(h.state(), copy.state())

    def test_rejects_unsupported_address_inputs(self):
        h = AddressHeap(START, END)
        with self.assertRaises(ValueError):
            h.allocate(-1, 0)
        with self.assertRaises(ValueError):
            h.free(END)
        with self.assertRaises(ValueError):
            h.shrink(START + 8, 3)
        with self.assertRaises(ValueError):
            h.pin(0, True)
        high = h.allocate(32, 1)
        h.pin(high, True)
        with self.assertRaises(ValueError):
            h.reset(high)


class ChainTests(unittest.TestCase):
    def reader(self, data):
        return lambda addr, size: data[addr - START:addr - START + size]

    def test_reads_metadata_and_intervals_without_mutating_memory(self):
        raw = bytearray(64)
        bits = (0x1234 >> 2) | (8 << 21) | (1 << 25) | (0x20 << 26)
        struct.pack_into("<II", raw, 0, START + 64, bits)
        struct.pack_into("<II", raw, 56, START + 64, 1 << 21)
        before = bytes(raw)
        blocks = read_heap(self.reader(raw), START + 8, START + 64)
        self.assertEqual(bytes(raw), before)
        self.assertEqual(blocks[0].size, 48)
        self.assertEqual(blocks[0].caller, 0x80001234)
        self.assertEqual(blocks[0].user, 8)
        self.assertTrue(blocks[0].pinned)
        self.assertEqual(blocks[0].type, 0x20)
        self.assertEqual(blocks[-1].size, 0)

    def test_rejects_cycle_unaligned_and_out_of_bounds_next(self):
        for nxt in (START + 8, START + 17, END + 4):
            with self.subTest(nxt=nxt), self.assertRaises(ValueError):
                read_heap(self.reader(struct.pack("<II", nxt, 0)), START + 8)

    def test_rejects_short_chain_wrong_sentinel_and_limit(self):
        with self.assertRaises(ValueError):
            read_heap(lambda a, n: b"", START + 8)
        raw = struct.pack("<II", START + 16, 1 << 21)
        with self.assertRaises(ValueError):
            read_heap(self.reader(raw), START + 8, START + 16)
        raw = struct.pack("<II", START + 16, 0)
        with self.assertRaises(ValueError):
            read_heap(self.reader(raw), START + 8, limit=1)


class ReplayTests(unittest.TestCase):
    def run_trace(self, lines, **kwargs):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "events.txt"
            p.write_text(lines)
            return replay(p, **kwargs)

    def test_reset_waits_for_nested_frees(self):
        result = self.run_trace("""initHeap 0 80100000 80101000 80000100
allocateHeap 1 00000010 00000000 80000100 80100008
resetHeapBegin 2 80100080 00000000 80000100
freeHeap 2 80100008 00000000 80000100 00000000
resetHeapEnd 2 80100080 00000000 80000100
allocateHeap 3 00000010 00000000 80000100 80100088
""")
        self.assertEqual(result["modeled_returns"], 3)
        self.assertFalse(result["unlinked_frees"])

    def test_unlinked_payload_header_is_explicitly_unmodeled(self):
        trace = """initHeap 0 80100000 80101000 80000100
freeHeap 1 80100800 00000000 80000100 00000000
shrinkAllocation 2 80100700 00000010 80000100 00000000
"""
        with self.assertRaises(ValueError):
            self.run_trace(trace)
        r = self.run_trace(trace, allow_unlinked_frees=True)
        self.assertEqual(r["modeled_returns"], 0)
        self.assertEqual(len(r["unlinked_frees"]), 1)
        self.assertEqual(len(r["unlinked_shrinks"]), 1)

    def test_wrong_return_and_unknown_events_fail(self):
        for line in ("allocateHeap 1 00000010 00000000 80000100 80100010",
                     "mystery 1 00000000 00000000 80000100"):
            with self.subTest(line=line), self.assertRaises(ValueError):
                self.run_trace("initHeap 0 80100000 80101000 80000100\n" + line)

    def test_restore_during_reset_matches_uninterrupted_replay(self):
        trace = """initHeap 0 80100000 80101000 80000100
allocateHeap 1 00000010 00000001 80000100 80100FE8
flagAllocation 1 80100FE8 00000000 80000100
resetHeapBegin 2 80100080 00000000 80000100
freeHeap 2 80100FE8 00000000 80000100 FFFFFFFF
resetHeapEnd 2 80100080 00000000 80000100
allocateHeap 3 00000010 00000000 80000100 80100088
"""
        self.assertEqual(self.run_trace(trace), self.run_trace(trace, restore_every=1))

    def test_memory_stats_sets_allocation_owner(self):
        r = self.run_trace("""initHeap 0 80100000 80101000 80000100
resetMemoryAllocStats 1 00000008 00000000 80000100
allocateHeap 1 00000010 00000000 80000100 80100008
""")
        self.assertEqual(r["state"]["blocks"][0]["user"], 8)

    def test_explicit_merge_without_a_later_allocation(self):
        r = self.run_trace("""initHeap 0 80100000 80101000 80000100
allocateHeap 1 00000010 00000000 80000100 80100008
allocateHeap 1 00000010 00000000 80000100 80100020
freeHeap 2 80100008 00000000 80000100 00000000
freeHeap 2 80100020 00000000 80000100 00000000
mergeFreeHeapBlocks 2 00000000 00000000 80000100
""")
        self.assertEqual(len(r["state"]["blocks"]), 2)
        self.assertFalse(r["state"]["dirty"])


class CollectorTests(unittest.TestCase):
    def test_logger_resolves_allocator_load_delay_and_brackets_reset(self):
        syms = {name: 0x80030000 + i * 0x100 for i, name in enumerate(FUNCTIONS)}
        code = trace_lua(Path("/tmp/STOP SITES OUTPUT Fei's 日本"), syms, 1900)
        self.assertIn("{'allocateHeap',%d,2}" % (syms["allocateHeap"] + 4), code)
        self.assertIn("{'freeHeap',%d,1}" % syms["freeHeap"], code)
        self.assertIn("resetHeapBegin", code)
        self.assertIn("resetHeapEnd", code)
        self.assertIn("日本", code)
        self.assertIn("STOP SITES OUTPUT Fei's", code)
        self.assertNotIn("\\u", code)
        self.assertIn("XG.vsyncs>=1900 and not active and not resetActive", code)


if __name__ == "__main__":
    unittest.main()
