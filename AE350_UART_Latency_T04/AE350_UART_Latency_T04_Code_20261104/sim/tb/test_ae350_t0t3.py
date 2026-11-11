#!/usr/bin/env python3
"""Strict offline AT04 decoder and mean verification, no device access."""
import binascii
import importlib.util
from pathlib import Path
import struct
import unittest

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('at04',ROOT/'scripts/read_ae350_t0t3.py')
d=importlib.util.module_from_spec(spec);spec.loader.exec_module(d)

def put64(w,index,value):
    value&=d.MASK64;w[index]=value&0xffffffff;w[index+1]=value>>32

def fixture(row_id=1,engine=0,base=0x1fffffff0,t3=30):
    w=[0]*26;w[0]=d.SIGNATURE;w[1]=row_id;w[2]=engine|(60<<6)|(64<<13);w[3]=31
    w[12]=1;w[13]=7;w[14]=0;w[15]=48_000_000;w[16]=256
    for index,offset in ((4,0),(6,3),(8,20),(10,t3)):put64(w,index,base+offset)
    derive(w);return w

def derive(w):
    for i,(a,b) in enumerate(d.PAIRS):
        value=(d.u64(w,4+2*b)-d.u64(w,4+2*a))&d.MASK64 if w[3]&(1<<a) and w[3]&(1<<b) else 0
        put64(w,18+2*i,value)

def encode(w):
    crc=binascii.crc_hqx(struct.pack('<26I',*w),0xffff)
    return ('AT04 '+''.join(f'{v:08x}' for v in w)+f' {crc:04x}\r\n').encode()

class DecoderTests(unittest.TestCase):
    def test_all_engines_raw_u64_and_cpu_deltas(self):
        for engine in range(3):
            row=d.decode_line(encode(fixture(engine=engine)))
            self.assertTrue(row['successful']);self.assertEqual(row['engine_name'],d.ENGINES[engine])
            self.assertEqual(list(row['signed_deltas_cycles'].values()),[3,17,10,30])
            self.assertEqual(row['raw_timestamps_cycles']['T0'],0x1fffffff0)
            self.assertEqual(row['pcm24_left'],1)
            self.assertEqual(row['raw_words'],fixture(engine=engine))
    def test_negative_t3_minus_t2_retained(self):
        row=d.decode_line(encode(fixture(t3=10)))
        self.assertTrue(row['successful']);self.assertEqual(row['signed_deltas_cycles'][d.PHASES[2]],-10)
        self.assertFalse(row['interval_ordered'][d.PHASES[2]])
        self.assertEqual(row['cpu_raw_deltas_cycles'][d.PHASES[2]],2**64-10)
    def test_u64_wrap_is_positive_not_huge(self):
        row=d.decode_line(encode(fixture(base=2**64-10)))
        self.assertEqual(list(row['signed_deltas_cycles'].values()),[3,17,10,30])
    def test_zero_t0_tick_present(self):
        row=d.decode_line(encode(fixture(base=0)));self.assertEqual(row['timestamps_cycles']['T0'],0)
    def test_right_negative_one_fullpcm24(self):
        w=fixture();w[16]=0;w[17]=0xffffff00
        self.assertEqual(d.decode_line(encode(w))['pcm24_right'],-1)
    def test_missing_timestamps_timeout_raw_retained(self):
        for bit,index in ((1,6),(2,8),(3,10)):
            w=fixture();w[3]=(31&~(1<<bit))|32|64;put64(w,index,0)
            if bit==1:w[12]=w[13]=w[14]=0
            if bit==3:w[16]=0
            derive(w);row=d.decode_line(encode(w));self.assertFalse(row['successful'])
            self.assertIsNone(row['timestamps_cycles'][d.TIME_NAMES[bit]])
            self.assertEqual(row['raw_words'],w)
    def test_nonzero_absent_timestamp_rejected(self):
        w=fixture();w[3]&=~2;w[3]|=64;derive(w)
        with self.assertRaisesRegex(ValueError,'absent timestamp'):d.decode_line(encode(w))
    def test_missing_without_invalid_rejected(self):
        w=fixture();w[3]&=~2;put64(w,6,0);derive(w)
        with self.assertRaisesRegex(ValueError,'missing timestamp'):d.decode_line(encode(w))
    def test_cpu_delta_corruption_even_good_crc_rejected(self):
        for index in range(18,26):
            w=fixture();w[index]^=1
            with self.assertRaisesRegex(ValueError,'computed delta'):d.decode_line(encode(w))
    def test_crc_and_length_and_legacy_rejected(self):
        line=encode(fixture());bad=bytearray(line);bad[40]^=1
        for raw in (bytes(bad),line[:-1],line.replace(b'\r\n',b'\n'),b'N20 '+line[5:]):
            with self.assertRaises(ValueError):d.decode_line(raw)
    def test_signature_engine_velocity_and_reserved_rejected(self):
        for index,value in ((0,1),(1,0),(1,21),(2,3|(60<<6)|(64<<13)),(2,60<<6),
                            (2,fixture()[2]|(1<<20)),(3,31|(1<<13)),(14,32),(15,50_000_000),
                            (16,257),(16,0)):
            w=fixture();w[index]=value
            with self.assertRaises(ValueError):d.decode_line(encode(w))
    def test_voices_engine_caps(self):
        for engine,voice in ((1,16),(2,8)):
            w=fixture(engine=engine);w[14]=voice
            with self.assertRaisesRegex(ValueError,'voice outside'):d.decode_line(encode(w))
    def test_identity_without_t1_rejected(self):
        w=fixture();w[3]=(31&~2)|64;put64(w,6,0);derive(w)
        with self.assertRaisesRegex(ValueError,'identity without'):d.decode_line(encode(w))
    def test_causal_t2_before_t1_fail(self):
        w=fixture();put64(w,8,d.u64(w,4)+1);derive(w)
        with self.assertRaisesRegex(ValueError,'causal order'):d.decode_line(encode(w))
    def test_pcm_before_begin_cannot_be_note_onset(self):
        w=fixture(base=100,t3=5);put64(w,6,110);put64(w,8,120);derive(w)
        with self.assertRaisesRegex(ValueError,'causal order'):d.decode_line(encode(w))
        w[3]|=64
        row=d.decode_line(encode(w));self.assertFalse(row['successful'])
        self.assertEqual(row['raw_timestamps_cycles'],{'T0':100,'T1':110,'T2':120,'T3':105})
    def test_equal_observed_begin_and_pcm_keeps_real_negative_after_ack(self):
        row=d.decode_line(encode(fixture(t3=3)))
        self.assertTrue(row['successful'])
        self.assertEqual(row['signed_deltas_cycles'][d.PHASES[2]],-17)
    def test_first20_and_identical_repeat_pass(self):
        raw=b''.join(encode(fixture(i)) for i in range(1,21));c=d.Capture()
        for offset in range(0,len(raw),17):c.feed(raw[offset:offset+17])
        c.feed(encode(fixture()));c.finish();s=c.summary('SUB')
        self.assertEqual(s['status'],'PASS');self.assertEqual(s['identical_repeats'],1)
        stat=s['engines']['SUB']['intervals'][d.PHASES[3]]
        self.assertEqual(stat['mean_cycles_fraction'],'30');self.assertEqual(stat['mean_microseconds'],0.625)
    def test_fractional_mean_and_signed_statistics(self):
        c=d.Capture();c.feed(encode(fixture(1,t3=10)));c.feed(encode(fixture(2,t3=31)))
        s=c.summary();stat=s['engines']['SUB']['intervals'][d.PHASES[2]]
        self.assertEqual(stat['mean_cycles_fraction'],'1/2');self.assertEqual(stat['negative_count'],1)
    def test_incomplete_does_not_invent_missing_samples(self):
        c=d.Capture();c.feed(encode(fixture()));s=c.summary()
        self.assertEqual(s['status'],'INCOMPLETE');self.assertEqual(s['missing_ids'],list(range(2,21)))
    def test_conflict_preserves_original_and_bad_raw(self):
        c=d.Capture();first=fixture();other=fixture(t3=31)
        c.feed(encode(first)+encode(other));s=c.summary()
        self.assertEqual(s['records'][0]['raw_words'],first);self.assertEqual(s['status'],'FAIL')
        self.assertEqual(s['errors'][0]['raw_hex'],encode(other).hex())
    def test_invalid_rows_retained_not_averaged(self):
        c=d.Capture()
        for i in range(1,21):
            w=fixture(i)
            if i==13:w[3]|=64|128
            c.feed(encode(w))
        s=c.summary();self.assertEqual(s['status'],'FAIL');self.assertEqual(s['successful_records'],19)
        self.assertEqual(s['unique_records'],20)
    def test_mixed_engines_not_a_complete_single_batch(self):
        c=d.Capture();c.feed(b''.join(encode(fixture(i,engine=i%3)) for i in range(1,21)))
        self.assertEqual(c.summary()['status'],'FAIL');self.assertTrue(c.summary()['batch_errors'])
    def test_requested_engine_mismatch(self):
        c=d.Capture();c.feed(encode(fixture()));self.assertEqual(c.summary('FM')['status'],'FAIL')
    def test_raw_errors_legacy_truncated_nonascii_preserved(self):
        for raw in (b'N20 old\r\n',b'AT04 '+b'f'*15,b'\xff\r\n'):
            c=d.Capture();c.feed(raw);c.finish();s=c.summary()
            self.assertEqual(s['status'],'FAIL');self.assertEqual(bytes.fromhex(s['errors'][0]['raw_hex']),raw)
    def test_error_diagnostic_crc_valid_is_stream_fail(self):
        for mask in (1,2,4,8,15):
            crc=binascii.crc_hqx(struct.pack('<I',mask),0xffff)
            raw=f'AT4E {mask:08x} {crc:04x}\r\n'.encode()
            self.assertEqual(d.decode_error(raw),mask)
            c=d.Capture();c.feed(raw);self.assertEqual(c.summary()['stream_status'],'FAIL')
    def test_bad_error_diagnostic(self):
        for raw in (b'AT4E 00000004 0000\r\n',b'AT4E 00000000 0000\r\n'):
            with self.assertRaises(ValueError):d.decode_error(raw)
    def test_unterminated_overflow_is_evidence_not_silent_drop(self):
        c=d.Capture();c.feed(b'x'*4097);c.finish();self.assertEqual(len(c.errors),1)
        self.assertEqual(len(bytes.fromhex(c.errors[0]['raw_hex'])),4097)

if __name__=='__main__':unittest.main()
