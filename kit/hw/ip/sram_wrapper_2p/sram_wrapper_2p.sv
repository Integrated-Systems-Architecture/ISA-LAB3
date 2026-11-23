module sram_wrapper_2p #(
    parameter int unsigned NumWords  = 32'd1024,
    parameter int unsigned DataWidth = 32'd32,
    parameter int unsigned AddrWidth = (NumWords > 32'd1) ? $clog2(NumWords) : 32'd1,
    parameter int unsigned BeWidth   = DataWidth / 32'd8
) (
    input  logic                 clk_i,
    input  logic                 rst_ni,
    input  logic                 req_w_i,
    input  logic [AddrWidth-1:0] addr_w_i,
    input  logic [DataWidth-1:0] wdata_w_i,
    input  logic [  BeWidth-1:0] be_w_i,
    input  logic                 req_r_i,
    input  logic [AddrWidth-1:0] addr_r_i,
    output logic [DataWidth-1:0] rdata_r_o
);

  logic [DataWidth-1:0] mem_q[NumWords];

  always_ff @(posedge clk_i) begin
    if (req_w_i)
      for (int unsigned i = 0; i < BeWidth; i++)
        if (be_w_i[i]) mem_q[addr_w_i][i*8+:8] <= wdata_w_i[i*8+:8];
    if (req_r_i) rdata_r_o <= mem_q[addr_r_i];
  end

endmodule
