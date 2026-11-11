// SPDX-License-Identifier: MIT
package midi_pkg;
  typedef struct packed {
    logic [7:0] status, data1, data2;
    logic [1:0] nbytes;
    logic [7:0] source;
    logic [31:0] seq;
    logic [63:0] start_tick, end_tick;
  } midi_event_t;
  typedef struct packed {
    logic [23:0] data;
    logic [1:0] count;
    logic first, last, aborted;
    logic [7:0] source;
    logic [31:0] seq;
    logic [63:0] start_tick, end_tick;
  } midi_sysex_t;
endpackage
