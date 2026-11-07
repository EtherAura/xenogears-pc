#!/usr/bin/env python3
"""Payload gate regressions on synthetic COFF/ELF files, not retail assets."""
from contextlib import redirect_stdout
import io
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import payloadcheck as P
import payload_pe as PE

PAYLOAD = bytes((i * 73 + 19) % 251 + 1 for i in range(80))
ROOT = Path(__file__).resolve().parents[1]
MINGW = shutil.which("x86_64-w64-mingw32-gcc")


class MapTests(unittest.TestCase):
    def test_wrapped_input_name_and_archive(self):
        text = """ .rdata$long_name
                0x00004000 0x20 CMakeFiles/ui.dir/src/ui/name with spaces.cpp.obj
 .rdata 0x00004020 0x20 /prefix with spaces/libfixture.a(tables.obj)
 .data 0x00000000 0x20 discarded.obj
"""
        spans = PE.map_spans(text)
        self.assertEqual([(s.address, s.size) for s in spans], [(0x4000,32),(0x4020,32)])
        self.assertIn("name with spaces", spans[0].obj)

    def test_overlapping_inputs_refused(self):
        with self.assertRaisesRegex(ValueError,"overlapping"):
            PE.Origins(Path('.'), ' .data 0x4000 0x20 one.obj\n .data 0x4010 0x20 two.obj\n', [])

    def test_empty_map_refused(self):
        with self.assertRaisesRegex(ValueError,"no input"):
            PE.Origins(Path('.'), '', [])

    def test_damaged_archive_refused(self):
        for data in (b'', b'!<arch>\ncut', b'!<arch>\n' + b' ' * 60):
            with self.assertRaises(ValueError): PE.archive_members(data)

    def test_damaged_coff_refused(self):
        for data in (b'', bytes(20), b'MZ' + bytes(100)):
            with self.assertRaises(ValueError): PE.Coff(data)


@unittest.skipUnless(MINGW, "MinGW compiler not installed")
class AuditTests(unittest.TestCase):
    def setUp(self):
        scratch = ROOT / 'work/payload-tests'
        scratch.mkdir(parents=True,exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(prefix='case-',dir=scratch)
        self.build = Path(self.tmp.name)
        self.allow = self.build/'allow.txt'
        self.allow.write_text('')

    def tearDown(self): self.tmp.cleanup()

    def compile(self, relative, text, cc=MINGW):
        obj = self.build/relative
        obj.parent.mkdir(parents=True,exist_ok=True)
        source = obj.with_suffix('.c')
        source.write_text(text)
        subprocess.run([cc,'-c','-O0',str(source),'-o',str(obj)],check=True,capture_output=True)
        return obj

    def fixture(self, extra=None, native=False, second=False):
        cc = 'cc' if native else MINGW
        extension = 'o' if native else 'obj'
        entry = self.compile(f'CMakeFiles/platform.dir/src/entry.c.{extension}',
            'extern const unsigned char knownTable[]; void entry(void) { volatile unsigned char v=knownTable[0]; (void)v; }',cc)
        body = ','.join(map(str,PAYLOAD))
        table = self.compile(f'tables.{extension}',f'const unsigned char knownTable[]={{ {body} }};' +
            (f'const unsigned char otherTable[]={{ {body} }};' if second else ''),cc)
        archive = self.build/'libfixture.a'
        subprocess.run(['ar' if native else 'x86_64-w64-mingw32-ar','rcs',str(archive),str(table)],check=True,capture_output=True)
        args = [cc,'-nostdlib','-Wl,-e,entry',str(entry)]
        if extra:
            where = 'modules/main.o' if extra=='module' else \
                f'CMakeFiles/game_generated.dir/gen/module.c.{extension}' if extra=='generated' else \
                f'unknown.{extension}' if extra=='unknown' else \
                f'CMakeFiles/platform.dir/src/injected.c.{extension}'
            args.append(str(self.compile(where,f'const unsigned char injected[]={{ {body} }};',cc)))
        exe = self.build/('fixture' if native else 'fixture.exe')
        args += [str(archive),'-o',str(exe)]
        if native: args += ['-no-pie']
        else: args += ['-Wl,--image-base,0x400000','-Wl,--disable-dynamicbase','-Wl,-Map,'+str(exe)+'.map']
        subprocess.run(args,check=True,capture_output=True)
        if not native: PE.record(exe)
        return exe,archive,table

    def run_audit(self, exe):
        log = io.StringIO()
        with patch.object(P,'ALLOW',self.allow), patch.object(P,'retail_images',return_value={'synthetic':PAYLOAD}), \
             patch('sys.argv',['payloadcheck','--build',str(self.build),'--exe',str(exe),'--ver','us']), redirect_stdout(log):
            code = P.main()
        return code, log.getvalue()

    def rule(self, budget=80):
        self.allow.write_text(f'pe library/libfixture.a/tables.obj:knownTable {budget} # synthetic library constant\n')

    def test_unallowed_library_fails(self):
        exe,_,_ = self.fixture()
        code,log = self.run_audit(exe)
        self.assertEqual(code,1); self.assertIn('library/libfixture.a/tables.obj (knownTable)',log)

    def test_precise_library_allowance(self):
        exe,_,_ = self.fixture(); self.rule()
        code,log = self.run_audit(exe)
        self.assertEqual(code,0,log)

    def test_budget_cannot_borrow_from_other_symbol(self):
        exe,_,_ = self.fixture(second=True); self.rule(79)
        with self.allow.open('a') as f: f.write('pe library/libfixture.a/tables.obj:otherTable 800 # separate symbol\n')
        code,log = self.run_audit(exe)
        self.assertEqual(code,1); self.assertIn('over budget:',log)

    def test_wrong_symbol_not_allowed(self):
        exe,_,_ = self.fixture()
        self.allow.write_text('pe library/libfixture.a/tables.obj:absent 800 # not this symbol\n')
        self.assertEqual(self.run_audit(exe)[0],1)

    def test_generated_module_cannot_borrow_library_allowance(self):
        exe,_,_ = self.fixture(extra='module'); self.rule()
        code,log = self.run_audit(exe)
        self.assertEqual(code,1); self.assertIn('generated',log); self.assertIn('modules/main.o',log)

    def test_port_payload_cannot_borrow_library_allowance(self):
        exe,_,_ = self.fixture(extra='port'); self.rule()
        code,log = self.run_audit(exe)
        self.assertEqual(code,1); self.assertIn('port/src/injected.c',log)

    def test_current_generated_target_cannot_borrow_library_allowance(self):
        exe,_,_ = self.fixture(extra='generated'); self.rule()
        code,log = self.run_audit(exe)
        self.assertEqual(code,1); self.assertRegex(log,r'generated\s+80 bytes')

    def test_unknown_input_cannot_borrow_unlinked_object(self):
        exe,_,_ = self.fixture(extra='unknown'); self.rule()
        # An old object can remain in a CMake directory after its source is
        # removed. Its identical bytes do not establish a linked origin.
        self.compile('CMakeFiles/platform.dir/src/decoy.c.obj',
                     'const unsigned char decoy[]={'+','.join(map(str,PAYLOAD))+'};')
        with self.allow.open('a') as f: f.write('port/src/decoy.c 80 # unlinked decoy\n')
        code,log = self.run_audit(exe)
        self.assertEqual(code,1,log)

    def test_missing_map_refused(self):
        exe,_,_ = self.fixture(); Path(str(exe)+'.map').unlink()
        code,log = self.run_audit(exe)
        self.assertEqual(code,2); self.assertIn('no verified PE linker map',log)

    def test_stale_map_refused(self):
        exe,_,_ = self.fixture()
        with Path(str(exe)+'.map').open('a') as f: f.write('\n')
        self.assertEqual(self.run_audit(exe)[0],2)

    def test_stale_executable_refused(self):
        exe,_,_ = self.fixture()
        with exe.open('ab') as f: f.write(b'changed')
        self.assertEqual(self.run_audit(exe)[0],2)

    def test_stale_member_bytes_cannot_borrow_another_object(self):
        exe,archive,_ = self.fixture(); self.rule()
        blob = archive.read_bytes(); self.assertIn(PAYLOAD,blob)
        archive.write_bytes(blob.replace(PAYLOAD,bytes([PAYLOAD[0]^1])+PAYLOAD[1:]))
        code,log = self.run_audit(exe)
        self.assertEqual(code,1); self.assertIn('mapped input bytes disagree',log)

    def test_resealed_image_mutation_still_fails_object_check(self):
        exe,_,_ = self.fixture(); self.rule()
        blob=exe.read_bytes(); self.assertIn(PAYLOAD,blob)
        # Shift the same retail bytes to a different input offset. A fresh
        # image/map hash alone must not grant the original object's rule.
        exe.write_bytes(blob.replace(PAYLOAD,PAYLOAD[1:]+PAYLOAD[:1])); PE.record(exe)
        code,log = self.run_audit(exe)
        self.assertEqual(code,1); self.assertIn('mapped input bytes disagree',log)

    def test_duplicate_archive_members_preserved(self):
        first = self.compile('one/tables.obj','const unsigned char one[]={1,2,3,4};')
        second = self.compile('two/tables.obj','const unsigned char two[]={5,6,7,8};')
        archive = self.build/'duplicate.a'
        subprocess.run(['x86_64-w64-mingw32-ar','q',str(archive),str(first),str(second)],check=True,capture_output=True)
        members = PE.archive_members(archive.read_bytes())['tables.obj']
        self.assertEqual(len(members),2)
        for blob, expected in zip(members, ('one','two')):
            self.assertTrue(any(expected==name for labels in PE.Coff(blob).labels.values() for _,name in labels))

    def test_pe_allowance_does_not_apply_to_elf(self):
        exe,_,_ = self.fixture(extra='port',native=True)
        self.allow.write_text('pe port/src/injected.c 800 # Windows only\n')
        code,log = self.run_audit(exe)
        self.assertEqual(code,1); self.assertIn('port/src/injected.c',log)


if __name__ == '__main__': unittest.main()
