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

  logic [DataWidth-1:0] bm;
  always_comb for (int i = 0; i < DataWidth; i++) bm[i] = be_w_i[i/8];

  if (NumWords == 1024 && DataWidth == 32) begin : gen_1024x32
    RM_IHPSG13_2P_1024x32_c2_bm_bist i_sram (
        .A_CLK(clk_i), .A_MEN(req_w_i), .A_WEN(1'b1), .A_REN(1'b0), .A_ADDR(addr_w_i),
        .A_DIN(wdata_w_i), .A_DOUT(), .A_BM(bm), .A_DLY(1'b1),
        .A_BIST_CLK(1'b0), .A_BIST_EN(1'b0), .A_BIST_MEN(1'b0), .A_BIST_WEN(1'b0),
        .A_BIST_REN(1'b0), .A_BIST_ADDR('0), .A_BIST_DIN('0), .A_BIST_BM('0),
        .B_CLK(clk_i), .B_MEN(req_r_i), .B_WEN(1'b0), .B_REN(1'b1), .B_ADDR(addr_r_i),
        .B_DIN('0), .B_DOUT(rdata_r_o), .B_BM('0), .B_DLY(1'b1),
        .B_BIST_CLK(1'b0), .B_BIST_EN(1'b0), .B_BIST_MEN(1'b0), .B_BIST_WEN(1'b0),
        .B_BIST_REN(1'b0), .B_BIST_ADDR('0), .B_BIST_DIN('0), .B_BIST_BM('0));
  end else if (NumWords == 64 && DataWidth == 32) begin : gen_64x32
    // No bit mask on this macro: full-word writes only.
    RM_IHPSG13_2P_64x32_c2 i_sram (
        .A_CLK(clk_i), .A_MEN(req_w_i), .A_WEN(1'b1), .A_REN(1'b0), .A_ADDR(addr_w_i),
        .A_DIN(wdata_w_i), .A_DOUT(), .A_DLY(1'b1),
        .B_CLK(clk_i), .B_MEN(req_r_i), .B_WEN(1'b0), .B_REN(1'b1), .B_ADDR(addr_r_i),
        .B_DIN('0), .B_DOUT(rdata_r_o), .B_DLY(1'b1));
  end else begin : gen_unsupported
    $fatal(1, "sram_wrapper_2p: no IHP SG13G2 macro for %0d x %0d bits", NumWords, DataWidth);
  end

endmodule
