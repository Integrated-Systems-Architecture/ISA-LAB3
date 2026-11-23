module pad_cell_input #(
    parameter PADATTR = 16,
    parameter PADATTR_RND = PADATTR == 0 ? 1 : PADATTR
) (
    input  logic                   pad_in_i,
    input  logic                   pad_oe_i,
    output logic                   pad_out_o,
    inout  wire                    pad_io,
    input  logic [PADATTR_RND-1:0] pad_attributes_i
);
  sg13g2_IOPadIn i_pad (.pad(pad_io), .p2c(pad_out_o));
endmodule

module pad_cell_output #(
    parameter PADATTR = 16,
    parameter PADATTR_RND = PADATTR == 0 ? 1 : PADATTR
) (
    input  logic                   pad_in_i,
    input  logic                   pad_oe_i,
    output logic                   pad_out_o,
    inout  wire                    pad_io,
    input  logic [PADATTR_RND-1:0] pad_attributes_i
);
  assign pad_out_o = 1'b0;
  sg13g2_IOPadOut16mA i_pad (.pad(pad_io), .c2p(pad_in_i));
endmodule

module pad_cell_inout #(
    parameter PADATTR = 16,
    parameter PADATTR_RND = PADATTR == 0 ? 1 : PADATTR
) (
    input  logic                   pad_in_i,
    input  logic                   pad_oe_i,
    output logic                   pad_out_o,
    inout  wire                    pad_io,
    input  logic [PADATTR_RND-1:0] pad_attributes_i
);
  sg13g2_IOPadInOut16mA i_pad (.pad(pad_io), .c2p(pad_in_i), .p2c(pad_out_o), .c2p_en(pad_oe_i));
endmodule
