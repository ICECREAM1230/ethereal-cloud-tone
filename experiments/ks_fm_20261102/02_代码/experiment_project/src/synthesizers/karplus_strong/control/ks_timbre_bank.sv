// Copyright (c) 2026 ICECREAM1230
// KS optional extended template protocol. Existing official two-bank RAM.
// SPDX-License-Identifier: MIT
`default_nettype none
module ks_timbre_bank # (parameter bit LOCAL_MIDI_BOOT=0)(
 input wire clk,rst_n,sclk,clear,input wire [3:0] flush,
 input wire fetch_busy,input wire [8:0] read_index_i,output wire [31:0] read_data_o,
 input wire perf_write_enable,input wire [15:0] perf_write_addr,input wire [31:0] perf_write_data,
 output reg [31:0] template_status,output wire template_available,output wire busy_o,
 output reg owner_clear_o,refresh_o,cancel_panic_o,
 output reg [127:0] valid_notes_o,output reg [31:0] image_crc_o,
 output reg [31:0] request_generation_o,completed_template_o,result_o,
 output wire [31:0] requested_template_generation_o,
 output wire model_valid_o,input wire model_ready_i,output wire [2047:0] model_words_o,
 input wire [31:0] model_completed_generation_i,model_result_i,model_applied_generation_i,
 input wire [1:0] model_active_id_i
);
 localparam [31:0] MAGIC=32'h4b535431;
 reg bank,commit_req,commit_ack,extended_mailbox;
 reg [31:0] generation_mailbox;
 (* async_reg="true" *) reg bank_meta,bank_sync,ack_meta,ack_sync,req_meta,req_sync;
 wire write_notes=perf_write_addr>=16'hb000&&perf_write_addr<=16'hb0ff;
 wire write_pan=perf_write_addr>=16'hb200&&perf_write_addr<=16'hb27f;
 wire write_header=perf_write_addr>=16'hb300&&perf_write_addr<=16'hb33f;
 wire table_write=perf_write_enable&&(write_notes||write_pan||write_header)&&commit_req==ack_sync;
 wire [8:0] wi=write_header?(9'd384+{3'd0,perf_write_addr[5:0]}):write_pan?{2'b10,perf_write_addr[6:0]}:{1'b0,perf_write_addr[7:0]};
 wire [9:0] wa={!bank_sync,wi};
 reg [9:0] scan_addr;
 wire [9:0] ra=busy_o?scan_addr:{bank,read_index_i};
 wire [17:0] lo,hi;wire [31:0] ram_word={hi[13:0],lo};
 assign read_data_o=ram_word;
 sdp_ram_1024x18 ram_lo(.dout(lo),.clka(sclk),.cea(table_write),.clkb(clk),.ceb(1'b1),.oce(1'b1),.reset(!rst_n),.ada(wa),.din(perf_write_data[17:0]),.adb(ra));
 sdp_ram_1024x18 ram_hi(.dout(hi),.clka(sclk),.cea(table_write),.clkb(clk),.ceb(1'b1),.oce(1'b1),.reset(!rst_n),.ada(wa),.din({4'd0,perf_write_data[31:18]}),.adb(ra));
 always @(posedge sclk or negedge rst_n) begin
  if(!rst_n)begin bank_meta<=0;bank_sync<=0;ack_meta<=0;ack_sync<=0;commit_req<=0;generation_mailbox<=0;extended_mailbox<=0;end
  else begin
   bank_meta<=bank;bank_sync<=bank_meta;ack_meta<=commit_ack;ack_sync<=ack_meta;
   if(perf_write_enable&&(perf_write_addr==16'hb3fe||perf_write_addr==16'hb3ff)&&perf_write_data!=0&&commit_req==ack_sync)begin
    generation_mailbox<=perf_write_data;extended_mailbox<=perf_write_addr==16'hb3fe;commit_req<=~commit_req;
   end
  end
 end
 typedef enum logic[5:0] {IDLE,H_R,H_W,H_C,H_DONE,L_R,L_W,L_C,M_H,M_H_NEXT,
  H_CHECK,H_MODE,H_END,S_H,M_N_R,M_N_W,M_N_C,M_N_NEXT,M_DONE,I_R,I_W,I_C,I_NEXT,I_GEN,I_DONE,
  S_R,S_W,S_C,S_A_R,S_A_W,S_A_C,S_NEXT,ISSUE,WAIT_MODEL,PUBLISH,REJECT,CRC} state_t;
 state_t state,crc_return;
 reg [31:0] header[0:63],active_header[0:63];reg [8:0] index;
 // A canceled header may never reach the core completion interface. Keep
 // its token independently of request readback, which clears on new scans.
 reg [31:0] last_canceled_generation;
 reg active_consistent,note_valid_seen,model_handed;reg [1:0] cancel_guard;reg [34:0] weight_sum;
 wire signed [17:0] mode_c=header[24+index[1:0]*4][17:0];
 wire signed [17:0] mode_s=header[25+index[1:0]*4][17:0];
 wire signed [35:0] mode_cc=mode_c*mode_c,mode_ss=mode_s*mode_s;
 wire [36:0] mode_radius={1'b0,mode_cc}+{1'b0,mode_ss};
 wire [31:0] mode_g=header[26+index[1:0]*4];
 wire [17:0] remaining_radius=18'd131072-mode_g[17:0];
 wire [35:0] injection_limit=remaining_radius*remaining_radius;
 wire [63:0] release_total={32'd0,header[16]}*{32'd0,header[15]}+{32'd0,header[17]};
 function automatic [31:0] legacy_generation(input [31:0] applied,completed,canceled_generation);
  reg [31:0] candidate;integer attempt;begin
   candidate=applied+1'b1;if(candidate==0)candidate=1;
   // Two forbidden tokens require at most two advances, including wrap.
   for(attempt=0;attempt<2;attempt=attempt+1)begin
    if(candidate==completed||candidate==canceled_generation)begin candidate=candidate+1'b1;if(candidate==0)candidate=1;end
   end
   legacy_generation=candidate;
  end
 endfunction
 reg extended,invalid,canceled;
 reg [31:0] target_template,crc,crc_shift,candidate_word;
 reg [1:0] crc_byte_index;
 function automatic [31:0] crc_byte(input [31:0] old_crc,input [7:0] value);
  reg[31:0] c;integer j;
  begin c=old_crc^{24'd0,value};for(j=0;j<8;j=j+1)c=c[0]?(c>>1)^32'hedb88320:c>>1;crc_byte=c;end
 endfunction
 wire [31:0] crc_next=crc_byte(crc,crc_shift[31:24]);
 function automatic [31:0] stable_word(input [8:0] idx,input [31:0] value);
  begin
   if(idx<256)stable_word=idx[0]?{16'd0,value[15:0]}:value;
   else if(idx<384||idx==9'd406||idx==9'd445)stable_word=0;
   else stable_word=value;
  end
 endfunction
 assign busy_o=state!=IDLE||req_sync!=commit_ack;
 assign template_available=(LOCAL_MIDI_BOOT||template_status!=0)&&active_consistent;
 assign requested_template_generation_o=target_template;
 assign model_valid_o=state==ISSUE&&!clear&&flush==0;
 for(genvar g=0;g<64;g=g+1)begin:pack_header assign model_words_o[g*32+:32]=header[g];end
 integer k;
 always @(posedge clk or negedge rst_n)begin
  if(!rst_n)begin
   last_canceled_generation<=0;req_meta<=0;req_sync<=0;bank<=0;commit_ack<=0;state<=IDLE;crc_return<=IDLE;scan_addr<=0;index<=0;
   active_consistent<=1;note_valid_seen<=0;model_handed<=0;cancel_guard<=0;weight_sum<=0;extended<=0;invalid<=0;canceled<=0;target_template<=0;crc<=32'hffffffff;crc_shift<=0;candidate_word<=0;crc_byte_index<=0;
   template_status<=0;owner_clear_o<=0;refresh_o<=0;cancel_panic_o<=0;image_crc_o<=0;request_generation_o<=0;completed_template_o<=0;result_o<=0;
   valid_notes_o<={128{1'b1}}<<19;
   for(k=0;k<64;k=k+1)begin header[k]<=0;active_header[k]<=0;end
  end else begin
   req_meta<=commit_req;req_sync<=req_meta;owner_clear_o<=0;refresh_o<=0;
   // Drain the two synchronizer stages plus the IDLE observation edge. A
   // bounded guard consumes an already-issued request even with stopped SCLK.
   if(clear||(|flush))cancel_guard<=3;else if(cancel_guard!=0)cancel_guard<=cancel_guard-1'b1;
   // A pre-ISSUE source flush drops only the bank request. It sends no
   // global safety command, so it must not replace the core's older fence.
   if((clear||((|flush)&&state==WAIT_MODEL))&&busy_o&&request_generation_o!=0)
    last_canceled_generation<=request_generation_o;
   case(state)
    IDLE:if(req_sync!=commit_ack&&(clear||(|flush)||cancel_guard!=0))begin
     target_template<=generation_mailbox;request_generation_o<=0;model_handed<=0;result_o<=1;state<=REJECT;
    end else if(req_sync!=commit_ack&&!fetch_busy)begin
     model_handed<=0;request_generation_o<=0;extended<=extended_mailbox;target_template<=generation_mailbox;index<=0;invalid<=0;canceled<=0;cancel_panic_o<=0;result_o<=0;
     for(k=0;k<64;k=k+1)header[k]<=0;
     if(generation_mailbox==completed_template_o)begin commit_ack<=req_sync;end
     else if(extended_mailbox)state<=H_R;
     else begin
      header[0]<=MAGIC;header[1]<=32'h00010001;header[2]<=0;
      header[3]<=model_active_id_i==0?model_applied_generation_i:legacy_generation(model_applied_generation_i,model_completed_generation_i,last_canceled_generation);
      header[4]<=1;header[5]<=91015625;header[6]<=948;header[13]<=16384;
      header[15]<=960;header[16]<=68;header[17]<=256;header[18]<=32768;header[21]<=480;header[22]<=3000;header[63]<=64;
      request_generation_o<=model_active_id_i==0?model_applied_generation_i:legacy_generation(model_applied_generation_i,model_completed_generation_i,last_canceled_generation);
      state<=L_R;
     end
    end
    H_R:begin scan_addr<={!bank,9'd384+index};state<=H_W;end
    H_W:state<=H_C;
    H_C:begin header[index[5:0]]<=ram_word;if(index==63)state<=H_DONE;else begin index<=index+1'b1;state<=H_R;end end
    H_DONE:begin
     request_generation_o<=header[3];index<=0;weight_sum<={3'd0,header[18]};
     if(header[0]!=MAGIC||header[1]!=32'h00010001||header[2]>2||header[3]==0||header[4]==0||
        header[5]!=91015625||header[6]!=948||header[63]!=64||header[13]>16384||header[20]>7||
        header[22]<200||header[22]>8000||header[21]!=480||header[18]>32768||
        header[15]<480||header[15]>192017||header[17]>=header[15]||release_total!=65536||
        $signed(header[14]) < -32'sd26214 || $signed(header[14]) > 32'sd26214)begin result_o<=2;state<=REJECT;end
     else if(header[2]!=0&&(header[7]<107374182||header[7]>966367642||
        header[8]<149130808||header[8]>1342177280||header[9]<70640909||header[9]>122016117||
        header[10]>32768||header[11]>32767||header[12]>32768))begin result_o<=2;state<=REJECT;end
     else if(header[2]==0&&(header[13]!=16384||header[14]!=0||header[15]!=960||header[16]!=68||
        header[17]!=256||header[18]!=32768||header[20]!=0||header[4]!=1))begin result_o<=2;state<=REJECT;end
     else state<=H_CHECK;
    end
    H_CHECK:begin
     if(((index==19||index==23||index==62||(index>=44&&index<=59))&&header[index[5:0]]!=0)||
        (header[2]==0&&((index>=7&&index<=12)||(index>=24&&index<=39))&&header[index[5:0]]!=0))invalid<=1;
     if(index==63)begin index<=0;state<=H_MODE;end else index<=index+1'b1;
    end
    H_MODE:begin
     if(header[24+index[1:0]*4]!={{14{mode_c[17]}},mode_c}||
        header[25+index[1:0]*4]!={{14{mode_s[17]}},mode_s}||
        mode_radius>=37'd17179869184||mode_g>131072||mode_radius>{1'b0,injection_limit}||
        header[27+index[1:0]*4]>32768)invalid<=1;
     weight_sum<=weight_sum+{3'd0,header[27+index[1:0]*4]};
     if(index==3)state<=H_END;else index<=index+1'b1;
    end
    H_END:begin
     if(invalid||weight_sum>32768)begin result_o<=2;state<=REJECT;end
     else begin index<=0;crc<=32'hffffffff;state<=M_H;end
    end
    L_R:begin scan_addr<={!bank,index};state<=L_W;end
    L_W:state<=L_C;
    L_C:begin
     if(ram_word[31]&&ram_word[14:0]>=2&&ram_word[14:0]<=4096)header[40+index[7:6]][index[5:1]]<=1;
     if(index==254)begin index<=0;crc<=32'hffffffff;state<=M_H;end else begin index<=index+2;state<=L_R;end
    end
    M_H:begin
     crc_shift<=(index==3||index==22||index==60||index==61)?32'd0:header[index[5:0]];
     crc_byte_index<=0;crc_return<=M_H_NEXT;state<=CRC;
    end
    M_H_NEXT:if(index==63)begin index<=0;state<=M_N_R;end else begin index<=index+1'b1;state<=M_H;end
    M_N_R:begin scan_addr<={!bank,index};state<=M_N_W;end
    M_N_W:state<=M_N_C;
    M_N_C:begin
     crc_shift<=index[0]?{16'd0,ram_word[15:0]}:ram_word;crc_byte_index<=0;crc_return<=M_N_NEXT;state<=CRC;
     if(!index[0])begin
      note_valid_seen<=ram_word[31];
      if(header[40+index[7:6]][index[5:1]]!=ram_word[31]||ram_word[30:15]!=0||
         (ram_word[31]&&(ram_word[14:0]<2||ram_word[14:0]>4096||index<38))||(!ram_word[31]&&ram_word!=0))invalid<=1;
     end else if((!note_valid_seen&&ram_word!=0)||ram_word[31:16]>32767||ram_word[15:0]==16'h8000)invalid<=1;
    end
    M_N_NEXT:if(index==255)state<=M_DONE;else begin index<=index+1'b1;state<=M_N_R;end
    M_DONE:begin
     if(invalid||(extended&&(~crc)!=header[60]))begin result_o<=2;state<=REJECT;end
     else begin if(!extended)header[60]<=~crc;crc<=32'hffffffff;index<=0;state<=I_R;end
    end
    I_R:begin scan_addr<={!bank,index};state<=I_W;end
    I_W:state<=I_C;
    I_C:begin
     crc_shift<=index==445?32'd0:index>=384?header[index-384]:ram_word;
     crc_byte_index<=0;crc_return<=I_NEXT;state<=CRC;
    end
    I_NEXT:if(index==447)state<=I_GEN;else begin index<=index+1'b1;state<=I_R;end
    I_GEN:begin crc_shift<=target_template;crc_byte_index<=0;crc_return<=I_DONE;state<=CRC;end
    I_DONE:begin
     if(extended&&(~crc)!=header[61])begin result_o<=2;state<=REJECT;end
     else begin
      if(!extended)header[61]<=~crc;
      if(!extended&&model_active_id_i==0)state<=PUBLISH;
      else if(header[3]==model_applied_generation_i)begin index<=0;state<=S_R;end
      else if(header[3]==model_completed_generation_i)begin result_o<=2;state<=REJECT;end
      else state<=ISSUE;
     end
    end
    S_R:begin scan_addr<={!bank,index};state<=S_W;end
    S_W:state<=S_C;
    S_C:begin candidate_word<=ram_word;state<=S_A_R;end
    S_A_R:begin scan_addr<={bank,index};state<=S_A_W;end
    S_A_W:state<=S_A_C;
    S_A_C:begin
     if(stable_word(index,candidate_word)!=stable_word(index,ram_word))invalid<=1;
     state<=S_NEXT;
    end
    S_NEXT:if(index==255)begin index<=0;state<=S_H;end else begin index<=index+1'b1;state<=S_R;end
    S_H:begin
     if(index!=22&&index!=61&&header[index[5:0]]!=active_header[index[5:0]])invalid<=1;
     if(index==63)begin
      if(invalid||header[63]!=active_header[63])begin result_o<=2;state<=REJECT;end else state<=PUBLISH;
     end else index<=index+1'b1;
    end
    ISSUE:if(model_valid_o&&model_ready_i)begin owner_clear_o<=1;active_consistent<=0;model_handed<=1;state<=WAIT_MODEL;end
    WAIT_MODEL:if(model_completed_generation_i==header[3])begin
     if(model_result_i==0&&model_applied_generation_i==header[3])state<=PUBLISH;else begin result_o<=model_result_i==0?32'd2:model_result_i;state<=REJECT;end
    end
    PUBLISH:begin
     bank<=!bank;active_consistent<=1;for(k=0;k<64;k=k+1)active_header[k]<=header[k];template_status<=target_template;completed_template_o<=target_template;
     image_crc_o<=header[61];valid_notes_o<={header[43],header[42],header[41],header[40]};
     commit_ack<=req_sync;refresh_o<=1;cancel_panic_o<=0;result_o<=0;state<=IDLE;
    end
    REJECT:begin
     // Only a terminal core result permits restoring an old bank after a
     // dispatched switch. Merely seeing the old applied token during FIRST
     // is not evidence that the candidate was canceled.
     if(model_handed&&model_completed_generation_i==header[3]&&model_result_i!=0&&
        model_applied_generation_i==active_header[3]&&{30'd0,model_active_id_i}==active_header[2])active_consistent<=1;
     completed_template_o<=target_template;commit_ack<=req_sync;cancel_panic_o<=0;state<=IDLE;end
    CRC:begin
     crc<=crc_next;crc_shift<={crc_shift[23:0],8'd0};
     if(crc_byte_index==3)state<=crc_return;else crc_byte_index<=crc_byte_index+1'b1;
    end
    default:state<=IDLE;
   endcase
   if((clear||(|flush))&&state!=IDLE&&state!=PUBLISH&&state!=REJECT)begin
    if(state==WAIT_MODEL)begin
     canceled<=1;cancel_panic_o<=1;
     // A completed successful core adoption must publish its matching table;
     // the independently issued Panic silences it. Never retain old-table/new-model.
     if(model_completed_generation_i!=header[3])state<=WAIT_MODEL;
    end
    else begin result_o<=1;state<=REJECT;end
   end
  end
 end
endmodule
`default_nettype wire
