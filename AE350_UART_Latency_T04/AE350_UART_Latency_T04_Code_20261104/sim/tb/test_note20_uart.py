#!/usr/bin/env python3
"""Offline fixtures and mocked serial passivity for the simple N20 UART test."""
from __future__ import annotations
import binascii
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest

ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/'scripts/read_note20_uart.py'
spec=importlib.util.spec_from_file_location('note20_uart_decoder',SOURCE)
decoder=importlib.util.module_from_spec(spec);spec.loader.exec_module(decoder)


def put64(words,index,value):
    words[index]=value&0xffffffff;words[index+1]=(value>>32)&0xffffffff


def fixture(row_id=1,*,failure=False):
    words=[0]*32;i=row_id-1
    left,right=(0x123457,0) if i==0 else (0,-0x345679) if i==1 else (1<<(i%24),-(0x345601+i))
    words[0]=0x4e323031;words[1]=3|((abs(left>>8)>1)<<8)|((abs(right>>8)>1)<<9)
    words[2]=row_id;words[3]=(i%3)|((i%3)<<2)|((i%16)<<4)|((40+i)<<8)|(100<<15)
    put64(words,4,1000+i*100);put64(words,6,1002+i*100);put64(words,10,1050+i*100)
    put64(words,16,0x100000000+i);put64(words,20,0xabcdef0000000000+i)
    words[22]=(left<<8)&0xffffffff;words[23]=(right<<8)&0xffffffff
    words[30]=50_000_000;words[31]=50_000_000
    if failure and row_id==3:
        words[1]=3|4|8
        for n in (10,11,16,17,20,21,22,23):words[n]=0
    elif failure and row_id==4:words[1]|=(1<<3)|(1<<4)
    return words


def encode(words):
    crc=binascii.crc_hqx(struct.pack('<32I',*words),0xffff)
    return ('N20 '+''.join(f'{word:08x}' for word in words)+f' {crc:04x}\r\n').encode()


def complete(*,failure=False):
    return b''.join(encode(fixture(i,failure=failure)) for i in range(1,21))


def stage_fixture(row_id=1,*,engine=0,first=100):
    """Eight hand-checked ticks: 100,102,107,118,131,150,180,190."""
    words=fixture(row_id);words[0]=0x4e323033
    words[1]=3|(1<<8)|(0x1f<<10)
    words[3]=1|(engine<<2)|(60<<8)|(64<<15)
    for index,offset in ((4,0),(6,2),(8,7),(18,18),(12,31),(14,50),(10,80),(24,90)):
        put64(words,index,(first+offset)&0xffffffffffffffff)
    words[22]=1<<8;words[23]=0
    words[26]=0x12345678;words[27]=0x9abcdef0;words[28]=17
    return words


class StageProtocolTests(unittest.TestCase):
    def test_n203_all_engines_preserve_timestamps_identity_and_phase_values(self):
        expected={'input_assembly':2,'ae350_response':5,'command_group_submit':11,
                  'commit_dispatch_and_return':13,'dsp_after_ack':19,
                  'audio_output_wait':30,'serial_transmission':10,
                  'first_input_to_pair':80,'complete_note_on_to_pair':78}
        for engine,name in enumerate(('SUB','KS','FM')):
            with self.subTest(engine=name):
                row=decoder.decode_line(encode(stage_fixture(engine=engine)).decode())
                self.assertTrue(row['successful']);self.assertEqual(row['record_version'],'N203')
                self.assertEqual(row['engine_name'],name)
                self.assertEqual(row['measurement_policy'],'first_nonzero_pcm24')
                self.assertEqual(row['command_identity'],{'revision':0x12345678,'generation':0x9abcdef0,'voice':17})
                self.assertEqual(row['timestamps_cycles'],{'first_input':100,'complete_note_on':102,
                    'begin':107,'commit_queued':118,'ack_success_return':131,
                    'dsp_first_nonzero_pcm24':150,'pair':180,'wire_end':190})
                self.assertEqual(row['raw_timestamps_cycles'],row['timestamps_cycles'])
                for phase,cycles in expected.items():
                    self.assertEqual(row['raw_deltas_cycles'][phase],cycles,phase)
                    self.assertEqual(row['signed_deltas_cycles'][phase],cycles,phase)
                    self.assertTrue(row['interval_ordered'][phase],phase)
                    self.assertEqual(row['intervals'][phase],{'cycles':cycles,'microseconds':cycles/50},phase)
                self.assertEqual((row['pcm24_left'],row['pcm24_right']),(1,0))
                self.assertIn('queue',row['ae350_response_scope'])
                self.assertIn('not pure CPU',row['ae350_response_scope'])

    def test_n203_zero_ticks_require_presence_flags_and_remain_valid(self):
        words=stage_fixture()
        for index in (4,6,8,18,12,14,10,24):put64(words,index,0)
        row=decoder.decode_line(encode(words).decode())
        self.assertTrue(row['successful'])
        self.assertTrue(all(tick==0 for tick in row['timestamps_cycles'].values()))
        self.assertTrue(all(item['cycles']==0 for item in row['intervals'].values()))
        self.assertEqual(row['command_identity']['voice'],17)

    def test_n203_missing_stage_without_error_flag_is_rejected(self):
        for bit,index in ((10,8),(11,18),(12,12),(13,14),(14,24)):
            with self.subTest(bit=bit):
                words=stage_fixture();words[1]&=~(1<<bit);put64(words,index,0)
                if bit==10:words[26]=words[27]=words[28]=0
                with self.assertRaisesRegex(ValueError,'missing.*stage'):
                    decoder.decode_line(encode(words).decode())

    def test_n203_missing_stage_on_timeout_keeps_partial_raw_evidence(self):
        for bit,index,key in ((10,8,'begin'),(11,18,'commit_queued'),(12,12,'ack_success_return'),
                              (13,14,'dsp_first_nonzero_pcm24'),(14,24,'wire_end')):
            with self.subTest(stage=key):
                words=stage_fixture();words[1]=(words[1]&~(1<<bit))|4|8;put64(words,index,0)
                if bit==10:words[26]=words[27]=words[28]=0
                row=decoder.decode_line(encode(words).decode())
                self.assertFalse(row['successful']);self.assertEqual(row['status'],'TIMEOUT')
                self.assertIsNone(row['timestamps_cycles'][key]);self.assertEqual(row['raw_timestamps_cycles'][key],0)
                self.assertEqual(row['raw_timestamps_cycles']['pair'],180)
                self.assertEqual(row['command_identity']['generation'],0 if bit==10 else 0x9abcdef0)
                self.assertTrue(all(value is None for value in row['intervals'].values()))
                self.assertEqual(row['raw_deltas_cycles']['first_input_to_pair'],80)

    def test_n203_nonzero_stage_without_presence_bit_is_rejected(self):
        for bit in range(10,15):
            words=stage_fixture();words[1]=(words[1]&~(1<<bit))|4|8
            with self.subTest(bit=bit),self.assertRaisesRegex(ValueError,'absent.*stage'):
                decoder.decode_line(encode(words).decode())

    def test_n203_identity_without_begin_presence_is_rejected(self):
        for index in (26,27,28):
            words=stage_fixture();words[1]=(words[1]&~(1<<10))|4|8;put64(words,8,0)
            words[26]=words[27]=words[28]=0;words[index]=1
            with self.subTest(identity_word=index),self.assertRaisesRegex(ValueError,'identity without BEGIN'):
                decoder.decode_line(encode(words).decode())

    def test_n203_present_begin_with_zero_identity_is_valid(self):
        for engine in range(3):
            words=stage_fixture(engine=engine);words[26]=words[27]=words[28]=0
            with self.subTest(engine=engine):
                row=decoder.decode_line(encode(words).decode())
                self.assertTrue(row['successful']);self.assertEqual(row['timestamps_cycles']['begin'],107)
                self.assertEqual(row['command_identity'],{'revision':0,'generation':0,'voice':0})

    def test_n203_absent_endpoint_keeps_stages_but_rejects_snapshot_payload(self):
        words=stage_fixture();words[1]=(words[1]&~(3<<8))|4|8
        for index in (10,11,16,17,20,21,22,23):words[index]=0
        row=decoder.decode_line(encode(words).decode())
        self.assertFalse(row['successful']);self.assertIsNone(row['timestamps_cycles']['pair'])
        self.assertEqual(row['timestamps_cycles']['begin'],107)
        self.assertEqual(row['raw_timestamps_cycles']['pair'],0)
        self.assertEqual(row['raw_deltas_cycles']['ae350_response'],5)
        self.assertNotIn('audio_output_wait',row['raw_deltas_cycles'])
        words[22]=1<<8
        with self.assertRaisesRegex(ValueError,'absent endpoint'):decoder.decode_line(encode(words).decode())

    def test_n203_reserved_identity_and_unknown_flags_remain_strict(self):
        for index,value in ((29,1),(28,32),(28,0x80000000),(1,3|(0x1f<<10)|(1<<8)|(1<<15)),
                            (1,3|(0x1f<<10)|(1<<8)|(1<<17))):
            with self.subTest(index=index,value=value):
                words=stage_fixture();words[index]=value
                with self.assertRaises(ValueError):decoder.decode_line(encode(words).decode())
        words=stage_fixture();words[26]=words[27]=0xffffffff;words[28]=31
        self.assertEqual(decoder.decode_line(encode(words).decode())['command_identity'],
                         {'revision':0xffffffff,'generation':0xffffffff,'voice':31})

    def test_legacy_versions_reject_new_timestamp_words_and_presence_bits(self):
        for version in (0x4e323031,0x4e323032):
            for index in (8,9,12,13,14,15,18,19,24,25,26,27,28,29):
                words=fixture();words[0]=version;words[index]=1
                with self.subTest(version=version,index=index),self.assertRaisesRegex(ValueError,'reserved'):
                    decoder.decode_line(encode(words).decode())
            for bit in range(10,15):
                words=fixture();words[0]=version;words[1]|=1<<bit
                with self.subTest(version=version,bit=bit),self.assertRaisesRegex(ValueError,'flags'):
                    decoder.decode_line(encode(words).decode())

    def test_dsp_before_ack_retains_signed_order_without_rejecting_pcm_causality(self):
        words=stage_fixture();put64(words,12,160)
        row=decoder.decode_line(encode(words).decode())
        self.assertTrue(row['successful']);self.assertEqual(row['status'],'SUCCESS')
        self.assertEqual(row['raw_deltas_cycles']['dsp_after_ack'],18446744073709551606)
        self.assertEqual(row['signed_deltas_cycles']['dsp_after_ack'],-10)
        self.assertFalse(row['interval_ordered']['dsp_after_ack'])
        self.assertIn('dsp_after_ack',row['unordered_intervals'])
        self.assertEqual(row['causal_order_violations'],[])
        self.assertIsNone(row['intervals']['dsp_after_ack'])
        self.assertEqual(row['intervals']['first_input_to_pair'],{'cycles':80,'microseconds':1.6})
        self.assertEqual(row['intervals']['commit_dispatch_and_return']['cycles'],42)

    def test_primary_stage_order_violations_remain_invalid(self):
        for index,tick,violation in ((6,99,'input_assembly'),(8,101,'ae350_response'),
                                    (18,106,'command_group_submit'),(12,117,'commit_dispatch_and_return'),
                                    (14,117,'commit_queued_to_dsp'),(10,149,'audio_output_wait'),
                                    (24,179,'serial_transmission')):
            with self.subTest(violation=violation):
                words=stage_fixture();put64(words,index,tick)
                row=decoder.decode_line(encode(words).decode())
                self.assertFalse(row['successful']);self.assertIn(violation,row['causal_order_violations'])
                self.assertTrue(all(value is None for value in row['intervals'].values()))
                self.assertEqual(row['raw_timestamps_cycles'][{6:'complete_note_on',8:'begin',18:'commit_queued',
                    12:'ack_success_return',14:'dsp_first_nonzero_pcm24',10:'pair',24:'wire_end'}[index]],tick)

    def test_phase_sums_and_u64_wrap_use_the_same_counter_domain(self):
        words=stage_fixture(first=18446744073709551612)
        row=decoder.decode_line(encode(words).decode());self.assertTrue(row['successful'])
        self.assertEqual(row['timestamps_cycles']['first_input'],18446744073709551612)
        self.assertEqual(row['timestamps_cycles']['begin'],3)
        self.assertEqual(row['timestamps_cycles']['wire_end'],86)
        phases=('ae350_response','command_group_submit','commit_dispatch_and_return','dsp_after_ack','audio_output_wait')
        self.assertEqual(sum(row['intervals'][key]['cycles'] for key in phases),78)
        self.assertEqual(row['intervals']['first_input_to_pair']['cycles'],80)
        self.assertEqual(row['intervals']['serial_transmission']['cycles'],10)

    def test_error_flags_preserve_all_stage_counters_without_qualified_values(self):
        for bit in (2,3,4,5,6,7,18):
            words=stage_fixture();words[1]|=1<<bit
            with self.subTest(bit=bit):
                row=decoder.decode_line(encode(words).decode())
                self.assertFalse(row['successful']);self.assertEqual(row['timestamps_cycles']['wire_end'],190)
                self.assertEqual(row['raw_deltas_cycles']['serial_transmission'],10)
                self.assertTrue(all(value is None for value in row['intervals'].values()))


class StageSessionTests(unittest.TestCase):
    def test_n202_and_n203_same_endpoint_still_fail_mixed_version_session(self):
        old=fixture();old[0]=0x4e323032
        raw=encode(old)+b''.join(encode(stage_fixture(i,engine=i%3)) for i in range(2,21))
        stream=decoder.Note20UartDecoder();stream.feed(raw);result=stream.finish()
        self.assertEqual(result['status'],'FAIL');self.assertEqual(len(result['records']),20)
        self.assertEqual(result['record_versions'],['N202','N203'])
        self.assertTrue(any('mixed record versions' in error['error'] for error in result['errors']))

    def test_three_engine_twenty_row_sessions_report_per_stage_statistics(self):
        for engine,name in enumerate(('SUB','KS','FM')):
            with self.subTest(engine=name):
                stream=decoder.Note20UartDecoder()
                for row_id in range(1,21):
                    words=stage_fixture(row_id,engine=engine)
                    if row_id==20:put64(words,12,160)
                    stream.feed(encode(words))
                result=stream.finish();self.assertEqual(result['status'],'PASS')
                self.assertEqual(result['record_versions'],['N203'])
                stats=result['interval_statistics']
                self.assertEqual(stats['ae350_response'],{'count':20,'cycles':{'min':5,'max':5,'mean':5.0},
                                   'microseconds':{'min':0.1,'max':0.1,'mean':0.1}})
                self.assertEqual(stats['dsp_after_ack']['count'],19)
                self.assertEqual(stats['commit_dispatch_and_return']['cycles'],{'min':13,'max':42,'mean':14.45})
                self.assertEqual(result['engine_interval_statistics'][name],stats)

    def test_statistics_exclude_invalid_rows_and_empty_stage_is_explicit(self):
        stream=decoder.Note20UartDecoder()
        words=stage_fixture();words[1]|=4|8
        stream.feed(encode(words));result=stream.finish()
        self.assertEqual(result['successful_records'],0)
        self.assertEqual(result['interval_statistics']['ae350_response'],{'count':0,
                         'cycles':{'min':None,'max':None,'mean':None},
                         'microseconds':{'min':None,'max':None,'mean':None}})
        self.assertEqual(result['records'][0]['raw_deltas_cycles']['ae350_response'],5)

    def test_statistics_use_each_records_frequency_for_microseconds(self):
        stream=decoder.Note20UartDecoder();a=stage_fixture();b=stage_fixture(2);b[30]=100_000_000
        stream.feed(encode(a)+encode(b));stats=stream.finish()['interval_statistics']['ae350_response']
        self.assertEqual(stats['cycles'],{'min':5,'max':5,'mean':5.0})
        self.assertEqual(stats['microseconds']['min'],0.05)
        self.assertEqual(stats['microseconds']['max'],0.1)
        self.assertAlmostEqual(stats['microseconds']['mean'],0.075)

    def test_n203_crc_framing_and_conflicts_are_not_hidden_by_valid_records(self):
        corrupt=bytearray(encode(stage_fixture()));corrupt[20]^=1
        changed=stage_fixture();changed[26]+=1
        good=b''.join(encode(stage_fixture(i)) for i in range(1,21))
        stream=decoder.Note20UartDecoder();stream.feed(bytes(corrupt)+b'N20 0\r\n'+good+encode(changed))
        result=stream.finish();self.assertEqual(result['status'],'FAIL')
        self.assertEqual(len(result['records']),20);self.assertEqual(len(result['errors']),3)
        self.assertEqual(len(result['conflicting_records']),1)
        self.assertTrue(any('CRC16' in error['error'] for error in result['errors']))


class ProtocolTests(unittest.TestCase):
    def test_n202_uses_every_full_pcm24_bit_for_onset(self):
        for left,right in [(1,0),(0,-1),(255,-255),(-8388608,8388607)]:
            words=fixture();words[0]=0x4e323032
            words[1]=3|((left!=0)<<8)|((right!=0)<<9)
            words[22]=(left<<8)&0xffffffff;words[23]=(right<<8)&0xffffffff
            row=decoder.decode_line(encode(words).decode())
            self.assertTrue(row['successful'])
            self.assertEqual(row['measurement_policy'],'first_nonzero_pcm24')
            self.assertEqual((row['pcm24_left'],row['pcm24_right']),(left,right))
            self.assertEqual(row['threshold_pcm24_lsb'],0)
        words[22]=words[23]=0
        with self.assertRaisesRegex(ValueError,'nonzero PCM24'):
            decoder.decode_line(encode(words).decode())

    def test_legacy_endpoint_definition_stays_explicit(self):
        row=decoder.decode_line(encode(fixture()).decode())
        self.assertEqual(row['measurement_policy'],'legacy_abs_pcm16_gt1')
        self.assertEqual(row['threshold_pcm16_lsb'],1)

    def test_mixed_endpoint_definitions_cannot_form_one_batch(self):
        words=fixture(2);words[0]=0x4e323032
        words[1]=3|((words[22]!=0)<<8)|((words[23]!=0)<<9)
        stream=decoder.Note20UartDecoder();stream.feed(encode(fixture())+encode(words))
        result=stream.finish()
        self.assertEqual(result['status'],'FAIL')
        self.assertTrue(any('mixed measurement policies' in x['error'] for x in result['errors']))

    def test_line_length_crc_and_binary_byte_order(self):
        line=encode(fixture())
        self.assertEqual(len(line),267)
        self.assertEqual(binascii.crc_hqx(b'123456789',0xffff),0x29b1)
        self.assertTrue(decoder.decode_line(line.decode())['successful'])
        wrong=binascii.crc_hqx(struct.pack('>32I',*fixture()),0xffff)
        bad=line[:261]+f'{wrong:04x}\r\n'.encode()
        with self.assertRaisesRegex(ValueError,'CRC16'):decoder.decode_line(bad.decode())

    def test_full_24bit_independent_channels_and_raw_values(self):
        a=decoder.decode_line(encode(fixture(1)).decode());b=decoder.decode_line(encode(fixture(2)).decode())
        self.assertEqual((a['pcm24_left'],a['pcm24_right']),(0x123457,0))
        self.assertEqual((b['pcm24_left'],b['pcm24_right']),(0,-0x345679))
        self.assertNotIn('right_over_pcm16_threshold',a['flags'])
        self.assertEqual(a['intervals']['first_input_to_pair'],{'cycles':50,'microseconds':1.0})
        self.assertEqual(a['pair_sample_index'],0x100000000)
        self.assertEqual(a['pair_tag'],0xabcdef0000000000)

    def test_low_bits_and_most_negative_pcm24(self):
        value=fixture(3);value[22]=0x80000000;value[1]|=1<<8
        row=decoder.decode_line(encode(value).decode())
        self.assertEqual(row['pcm24_left'],-8388608)
        self.assertEqual(decoder.decode_line(encode(fixture(3)).decode())['pcm24_left'],4)

    def test_failures_retained_without_qualified_us(self):
        d=decoder.Note20UartDecoder();d.feed(complete(failure=True));r=d.finish()
        self.assertEqual(r['status'],'CAPTURED_WITH_INVALID_MEASUREMENTS')
        self.assertEqual(len(r['records']),20);self.assertEqual(r['invalid_records'],2)
        self.assertEqual(r['records'][2]['status'],'TIMEOUT')
        self.assertIsNone(r['records'][2]['timestamps_cycles']['pair'])
        self.assertIsNone(r['records'][3]['intervals']['first_input_to_pair'])
        self.assertIsNotNone(r['records'][3]['raw_deltas_cycles']['first_input_to_pair'])

    def test_zero_and_counter_wrap(self):
        words=fixture()
        for i in (4,5,6,7,10,11,16,17,20,21):words[i]=0
        row=decoder.decode_line(encode(words).decode())
        self.assertTrue(row['successful']);self.assertEqual(row['timestamps_cycles']['pair'],0)
        put64(words,4,(1<<64)-4);put64(words,6,(1<<64)-2);put64(words,10,5)
        row=decoder.decode_line(encode(words).decode());self.assertTrue(row['successful'])
        self.assertEqual(row['intervals']['first_input_to_pair']['cycles'],9)
        put64(words,10,(1<<64)-5)
        row=decoder.decode_line(encode(words).decode());self.assertFalse(row['successful'])
        self.assertIsNone(row['intervals']['first_input_to_pair'])

    def test_format_and_crc_rejections(self):
        valid=encode(fixture()).decode()
        for text in ('N20 0\r\n',valid[:-2]+'\n',valid.replace('4e32','ge32',1),
                     'prefix'+valid,valid[:261]+('0' if valid[261]!='0' else '1')+valid[262:]):
            with self.subTest(text=text[:30]):
                with self.assertRaises(ValueError):decoder.decode_line(text)
        self.assertIsNone(decoder.decode_line('AE35 S=00000000\r\n'))

    def test_reserved_fields_metadata_and_configuration(self):
        for index,value in ((0,0),(1,3|(1<<10)),(1,1),(2,0),(2,21),(3,3|(100<<15)),
                            (3,12|(100<<15)),(3,60<<8),(3,1<<31),(8,1),(22,1),(30,0),(31,0)):
            with self.subTest(index=index,value=value):
                words=fixture();words[index]=value
                with self.assertRaises(ValueError):decoder.decode_line(encode(words).decode())

    def test_snapshot_threshold_agrees_with_channel_flags(self):
        words=fixture();words[1]|=1<<9
        with self.assertRaisesRegex(ValueError,'right onset'):decoder.decode_line(encode(words).decode())
        words=fixture();words[1]&=~(1<<8)
        with self.assertRaisesRegex(ValueError,'absent endpoint'):decoder.decode_line(encode(words).decode())


class StreamTests(unittest.TestCase):
    def test_all_single_line_split_offsets(self):
        raw=encode(fixture())
        for split in range(1,len(raw)):
            d=decoder.Note20UartDecoder();d.feed(raw[:split]);d.feed(raw[split:])
            r=d.finish();self.assertEqual(r['received_ids'],[1],split);self.assertEqual(r['errors'],[],split)

    def test_byte_fragments_twenty_and_identical_repeats(self):
        d=decoder.Note20UartDecoder()
        for b in complete()+encode(fixture(1)):d.feed(bytes([b]))
        r=d.finish();self.assertEqual(r['status'],'PASS');self.assertEqual(r['identical_repeats'],1)
        self.assertEqual(r['received_ids'],list(range(1,21)))

    def test_missing_ids_are_not_twenty_successes(self):
        d=decoder.Note20UartDecoder();d.feed(encode(fixture(4))+encode(fixture(20)))
        r=d.finish();self.assertEqual(r['status'],'INCOMPLETE');self.assertEqual(r['successful_records'],2)
        self.assertEqual(r['missing_ids'],[i for i in range(1,21) if i not in (4,20)])

    def test_changed_id_without_session_proof_is_failure(self):
        words=fixture();words[22]^=1<<8
        d=decoder.Note20UartDecoder();d.feed(encode(fixture())+encode(words));r=d.finish()
        self.assertEqual(r['status'],'FAIL');self.assertEqual(len(r['conflicting_records']),1)
        self.assertEqual(r['records'][0]['pcm24_left'],0x123457)

    def test_error_not_hidden_by_later_valid_line(self):
        corrupt=bytearray(encode(fixture()));corrupt[20]^=1
        d=decoder.Note20UartDecoder();d.feed(bytes(corrupt)+complete());r=d.finish()
        self.assertEqual(r['status'],'FAIL');self.assertEqual(len(r['records']),20)
        self.assertTrue(r['errors'])

    def test_truncation_finish_idempotence_and_no_more_feed(self):
        d=decoder.Note20UartDecoder();d.feed(encode(fixture())+b'N20 123')
        r=d.finish();self.assertEqual(r['status'],'FAIL');self.assertEqual(r,d.finish())
        with self.assertRaises(RuntimeError):d.feed(b'')


class SerialPassivityTests(unittest.TestCase):
    def test_controls_set_before_open_and_no_writes(self):
        events=[]
        class FakeSerial:
            def __init__(self,**kwargs):events.append(('construct',kwargs))
            def __setattr__(self,name,value):
                if name in ('dtr','rts','port'):events.append((name,value))
                object.__setattr__(self,name,value)
            def open(self):
                self.assert_open()
                events.append(('open',self.port))
            def assert_open(self):
                if self.dtr or self.rts:raise AssertionError('DTR/RTS asserted before open')
            def read(self,count):events.append(('read',count));return encode(fixture())
            def close(self):events.append(('close',))
            def write(self,*args):raise AssertionError('serial write forbidden')
            reset_input_buffer=reset_output_buffer=write
        reader=decoder.SerialReader('FAKE',200000,serial_module=SimpleNamespace(Serial=FakeSerial))
        self.assertEqual(reader.read(267),encode(fixture()));reader.close()
        self.assertEqual(events,[('construct',{'port':None,'baudrate':200000,'timeout':0.1,'exclusive':True}),
                                 ('dtr',False),('rts',False),('port','FAKE'),('open','FAKE'),('read',267),('close',)])


class SerialDeadlineTests(unittest.TestCase):
    class Clock:
        def __init__(self):self.now=0.0
        def __call__(self):return self.now

    class Reader:
        def __init__(self,clock,data=b'',step=0.001):
            self.clock=clock;self.pending=bytearray(data);self.step=step;self.calls=[]
        def read(self,count,*,timeout_seconds=None):
            self.calls.append((count,timeout_seconds))
            if self.pending:
                self.clock.now+=min(self.step,timeout_seconds)
                result=bytes(self.pending[:count]);del self.pending[:count];return result
            self.clock.now+=timeout_seconds
            return b''

    def test_deadline_completes_only_existing_repeated_line(self):
        line=encode(fixture());d=decoder.Note20UartDecoder();d.feed(complete()+line[:100])
        clock=self.Clock();reader=self.Reader(clock,line[100:]+encode(fixture(2)));raw=io.BytesIO()
        tail=decoder.finish_pending_serial_line(reader,d,raw,monotonic=clock)
        self.assertTrue(tail['completed_line']);self.assertEqual(tail['read_bytes'],167)
        self.assertEqual(raw.getvalue(),line[100:]);self.assertEqual(bytes(reader.pending),encode(fixture(2)))
        self.assertTrue(all(count==1 for count,_ in reader.calls))
        self.assertEqual(d.finish()['status'],'PASS');self.assertEqual(d.finish()['identical_repeats'],1)

    def test_empty_pending_does_not_start_another_line(self):
        d=decoder.Note20UartDecoder();d.feed(complete());clock=self.Clock()
        reader=self.Reader(clock,encode(fixture()));tail=decoder.finish_pending_serial_line(reader,d,io.BytesIO(),monotonic=clock)
        self.assertFalse(tail['attempted']);self.assertEqual(reader.calls,[])
        self.assertEqual(d.finish()['status'],'PASS')

    def test_no_lf_by_deadline_remains_failure(self):
        d=decoder.Note20UartDecoder();d.feed(complete()+b'N20 123');clock=self.Clock();reader=self.Reader(clock)
        tail=decoder.finish_pending_serial_line(reader,d,io.BytesIO(),monotonic=clock)
        self.assertFalse(tail['completed_line']);self.assertLessEqual(clock.now,0.3)
        self.assertEqual(d.finish()['status'],'FAIL')

    def test_tail_byte_cap_does_not_read_beyond_one_line(self):
        d=decoder.Note20UartDecoder();d.feed(complete()+b'N20 '+b'0'*262)
        clock=self.Clock();reader=self.Reader(clock,b'x'*1000)
        tail=decoder.finish_pending_serial_line(reader,d,io.BytesIO(),monotonic=clock)
        self.assertEqual(tail['read_bytes'],1);self.assertEqual(len(d.pending),267)
        self.assertFalse(tail['completed_line']);self.assertEqual(d.finish()['status'],'FAIL')

    def test_prior_crc_error_is_not_cleared_by_tail_completion(self):
        bad=bytearray(encode(fixture()));bad[20]^=1
        d=decoder.Note20UartDecoder();line=encode(fixture());d.feed(bytes(bad)+complete()+line[:260])
        clock=self.Clock();reader=self.Reader(clock,line[260:])
        decoder.finish_pending_serial_line(reader,d,io.BytesIO(),monotonic=clock)
        result=d.finish();self.assertEqual(result['status'],'FAIL')
        self.assertTrue(any('CRC16' in error['error'] for error in result['errors']))

    def test_offline_truncation_even_one_character_is_still_failure(self):
        for tail in (b'N',b'N2',b'N20 ',encode(fixture())[:266]):
            d=decoder.Note20UartDecoder();d.feed(complete()+tail)
            self.assertEqual(d.finish()['status'],'FAIL',tail)

    def test_serial_reader_bounds_and_restores_local_timeout(self):
        events=[]
        class FakeSerial:
            def __init__(self,**kwargs):self.timeout=kwargs['timeout']
            def open(self):pass
            def read(self,count):events.append((count,self.timeout));return b'x'
            def close(self):pass
        reader=decoder.SerialReader('FAKE',200000,serial_module=SimpleNamespace(Serial=FakeSerial))
        self.assertEqual(reader.read(1,timeout_seconds=0.025),b'x')
        self.assertEqual(events,[(1,0.025)]);self.assertEqual(reader.serial.timeout,0.1)
        reader.close()


class CliTests(unittest.TestCase):
    def test_offline_cli_and_no_evidence_overwrite(self):
        scratch=Path(tempfile.gettempdir())
        scratch.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='note20_uart_decoder_',dir=scratch) as folder:
            folder=Path(folder);raw=folder/'capture.txt';out=folder/'decoded.json';raw.write_bytes(complete())
            cmd=[sys.executable,'-B',str(SOURCE),'--input',str(raw),'--output',str(out)]
            result=subprocess.run(cmd,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertEqual(json.loads(out.read_text())['status'],'PASS');saved=out.read_bytes()
            again=subprocess.run(cmd,capture_output=True,text=True);self.assertNotEqual(again.returncode,0)
            self.assertEqual(out.read_bytes(),saved)


def run_tests():
    text=io.StringIO();suite=unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    result=unittest.TextTestRunner(stream=text,verbosity=2).run(suite)
    print(text.getvalue(),end='')
    return result,text.getvalue()


if __name__=='__main__':
    result,_=run_tests()
    raise SystemExit(0 if result.wasSuccessful() else 1)
