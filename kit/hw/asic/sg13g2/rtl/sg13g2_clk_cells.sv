module tc_clk_and2 (
    input  logic clk0_i,
    input  logic clk1_i,
    output logic clk_o
);
  sg13g2_and2_1 i_cell (.A(clk0_i), .B(clk1_i), .X(clk_o));
endmodule

module tc_clk_buffer (
    input  logic clk_i,
    output logic clk_o
);
  sg13g2_buf_1 i_cell (.A(clk_i), .X(clk_o));
endmodule

module tc_clk_gating #(
    parameter bit IS_FUNCTIONAL = 1'b1
) (
    input  logic clk_i,
    input  logic en_i,
    input  logic test_en_i,
    output logic clk_o
);
  sg13g2_slgcp_1 i_cell (.CLK(clk_i), .GATE(en_i), .SCE(test_en_i), .GCLK(clk_o));
endmodule

module tc_clk_inverter (
    input  logic clk_i,
    output logic clk_o
);
  sg13g2_inv_1 i_cell (.A(clk_i), .Y(clk_o));
endmodule

module tc_clk_mux2 (
    input  logic clk0_i,
    input  logic clk1_i,
    input  logic clk_sel_i,
    output logic clk_o
);
  sg13g2_mux2_1 i_cell (.A0(clk0_i), .A1(clk1_i), .S(clk_sel_i), .X(clk_o));
endmodule

module tc_clk_xor2 (
    input  logic clk0_i,
    input  logic clk1_i,
    output logic clk_o
);
  sg13g2_xor2_1 i_cell (.A(clk0_i), .B(clk1_i), .X(clk_o));
endmodule

module tc_clk_or2 (
    input  logic clk0_i,
    input  logic clk1_i,
    output logic clk_o
);
  sg13g2_or2_1 i_cell (.A(clk0_i), .B(clk1_i), .X(clk_o));
endmodule

module pulp_clock_gating (
    input  logic clk_i,
    input  logic en_i,
    input  logic test_en_i,
    output logic clk_o
);
  sg13g2_slgcp_1 i_cell (.CLK(clk_i), .GATE(en_i), .SCE(test_en_i), .GCLK(clk_o));
endmodule

module cv32e40px_clock_gate (
    input  logic clk_i,
    input  logic en_i,
    input  logic scan_cg_en_i,
    output logic clk_o
);
  sg13g2_slgcp_1 i_cell (.CLK(clk_i), .GATE(en_i), .SCE(scan_cg_en_i), .GCLK(clk_o));
endmodule

module pulp_clock_and2 (
    input  logic clk0_i,
    input  logic clk1_i,
    output logic clk_o
);
  sg13g2_and2_1 i_cell (.A(clk0_i), .B(clk1_i), .X(clk_o));
endmodule

module pulp_clock_buffer (
    input  logic clk_i,
    output logic clk_o
);
  sg13g2_buf_1 i_cell (.A(clk_i), .X(clk_o));
endmodule

module pulp_clock_inverter (
    input  logic clk_i,
    output logic clk_o
);
  sg13g2_inv_1 i_cell (.A(clk_i), .Y(clk_o));
endmodule

module pulp_clock_mux2 (
    input  logic clk0_i,
    input  logic clk1_i,
    input  logic clk_sel_i,
    output logic clk_o
);
  sg13g2_mux2_1 i_cell (.A0(clk0_i), .A1(clk1_i), .S(clk_sel_i), .X(clk_o));
endmodule

module pulp_clock_xor2 (
    input  logic clk0_i,
    input  logic clk1_i,
    output logic clk_o
);
  sg13g2_xor2_1 i_cell (.A(clk0_i), .B(clk1_i), .X(clk_o));
endmodule
