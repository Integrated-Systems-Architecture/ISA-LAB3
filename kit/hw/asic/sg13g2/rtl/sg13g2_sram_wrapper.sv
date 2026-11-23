`define IHP_SRAM_1P(MACRO) \
  MACRO i_sram ( \
      .A_CLK(clk_i), .A_MEN(req_i), .A_WEN(we_i), .A_REN(~we_i), .A_ADDR(addr_i), \
      .A_DIN(wdata_i), .A_DOUT(rdata_o), .A_BM(bm), .A_DLY(1'b1), \
      .A_BIST_CLK(1'b0), .A_BIST_EN(1'b0), .A_BIST_MEN(1'b0), .A_BIST_WEN(1'b0), \
      .A_BIST_REN(1'b0), .A_BIST_ADDR('0), .A_BIST_DIN('0), .A_BIST_BM('0));

module sram_wrapper #(
    parameter int unsigned NumWords  = 32'd1024,
    parameter int unsigned DataWidth = 32'd32,
    parameter int unsigned AddrWidth = (NumWords > 32'd1) ? $clog2(NumWords) : 32'd1
) (
    input  logic                   clk_i,
    input  logic                   rst_ni,
    input  logic                   req_i,
    input  logic                   we_i,
    input  logic [  AddrWidth-1:0] addr_i,
    input  logic [  DataWidth-1:0] wdata_i,
    input  logic [DataWidth/8-1:0] be_i,
    input  logic                   pwrgate_ni,
    output logic                   pwrgate_ack_no,
    input  logic                   set_retentive_ni,
    output logic [  DataWidth-1:0] rdata_o
);

  assign pwrgate_ack_no = pwrgate_ni;

  logic [DataWidth-1:0] bm;
  always_comb for (int i = 0; i < DataWidth; i++) bm[i] = be_i[i/8];

  if (DataWidth == 32 && NumWords % 4096 == 0) begin : gen_2048x64
    // Two 32-bit words per 64-bit row: the bit mask writes only the addressed half.
    localparam int unsigned NumMacros = NumWords / 4096;
    logic [NumMacros-1:0][63:0] dout;
    logic [AddrWidth-1:0] addr_q;

    for (genvar m = 0; m < NumMacros; m++) begin : gen_macro
      logic men;
      if (NumMacros > 1) assign men = req_i && addr_i[AddrWidth-1:12] == m;
      else assign men = req_i;
      RM_IHPSG13_1P_2048x64_c2_bm_bist i_sram (
          .A_CLK(clk_i), .A_MEN(men), .A_WEN(we_i), .A_REN(~we_i), .A_ADDR(addr_i[11:1]),
          .A_DIN({wdata_i, wdata_i}), .A_DOUT(dout[m]),
          .A_BM(addr_i[0] ? {bm, 32'b0} : {32'b0, bm}), .A_DLY(1'b1),
          .A_BIST_CLK(1'b0), .A_BIST_EN(1'b0), .A_BIST_MEN(1'b0), .A_BIST_WEN(1'b0),
          .A_BIST_REN(1'b0), .A_BIST_ADDR('0), .A_BIST_DIN('0), .A_BIST_BM('0));
    end

    always_ff @(posedge clk_i) if (req_i) addr_q <= addr_i;

    logic [63:0] row;
    if (NumMacros > 1) assign row = dout[addr_q[AddrWidth-1:12]];
    else assign row = dout[0];
    assign rdata_o = addr_q[0] ? row[63:32] : row[31:0];

  end else if (NumWords == 4096 && DataWidth == 16) begin : gen_4096x16
    `IHP_SRAM_1P(RM_IHPSG13_1P_4096x16_c3_bm_bist)
  end else if (NumWords == 4096 && DataWidth == 8) begin : gen_4096x8
    `IHP_SRAM_1P(RM_IHPSG13_1P_4096x8_c3_bm_bist)
  end else if (NumWords == 2048 && DataWidth == 64) begin : gen_2048x64_plain
    `IHP_SRAM_1P(RM_IHPSG13_1P_2048x64_c2_bm_bist)
  end else if (NumWords == 2048 && DataWidth == 32) begin : gen_2048x32
    `IHP_SRAM_1P(RM_IHPSG13_1P_2048x32_c2_bm_bist)
  end else if (NumWords == 1024 && DataWidth == 64) begin : gen_1024x64
    `IHP_SRAM_1P(RM_IHPSG13_1P_1024x64_c2_bm_bist)
  end else if (NumWords == 1024 && DataWidth == 32) begin : gen_1024x32
    `IHP_SRAM_1P(RM_IHPSG13_1P_1024x32_c2_bm_bist)
  end else if (NumWords == 1024 && DataWidth == 16) begin : gen_1024x16
    `IHP_SRAM_1P(RM_IHPSG13_1P_1024x16_c2_bm_bist)
  end else if (NumWords == 1024 && DataWidth == 8) begin : gen_1024x8
    `IHP_SRAM_1P(RM_IHPSG13_1P_1024x8_c2_bm_bist)
  end else if (NumWords == 512 && DataWidth == 64) begin : gen_512x64
    `IHP_SRAM_1P(RM_IHPSG13_1P_512x64_c2_bm_bist)
  end else if (NumWords == 512 && DataWidth == 32) begin : gen_512x32
    `IHP_SRAM_1P(RM_IHPSG13_1P_512x32_c2_bm_bist)
  end else if (NumWords == 512 && DataWidth == 16) begin : gen_512x16
    `IHP_SRAM_1P(RM_IHPSG13_1P_512x16_c2_bm_bist)
  end else if (NumWords == 512 && DataWidth == 8) begin : gen_512x8
    `IHP_SRAM_1P(RM_IHPSG13_1P_512x8_c3_bm_bist)
  end else if (NumWords == 256 && DataWidth == 64) begin : gen_256x64
    `IHP_SRAM_1P(RM_IHPSG13_1P_256x64_c2_bm_bist)
  end else if (NumWords == 256 && DataWidth == 48) begin : gen_256x48
    `IHP_SRAM_1P(RM_IHPSG13_1P_256x48_c2_bm_bist)
  end else if (NumWords == 256 && DataWidth == 32) begin : gen_256x32
    `IHP_SRAM_1P(RM_IHPSG13_1P_256x32_c2_bm_bist)
  end else if (NumWords == 256 && DataWidth == 16) begin : gen_256x16
    `IHP_SRAM_1P(RM_IHPSG13_1P_256x16_c2_bm_bist)
  end else if (NumWords == 256 && DataWidth == 8) begin : gen_256x8
    `IHP_SRAM_1P(RM_IHPSG13_1P_256x8_c3_bm_bist)
  end else if (NumWords == 64 && DataWidth == 64) begin : gen_64x64
    `IHP_SRAM_1P(RM_IHPSG13_1P_64x64_c2_bm_bist)
  end else begin : gen_unsupported
    $fatal(1, "sram_wrapper: no IHP SG13G2 macro for %0d x %0d bits", NumWords, DataWidth);
  end

endmodule

`undef IHP_SRAM_1P
