////////////////////////////////////////////////////////////////////////////////
//
// Filename: 	../rtl/seqcordic.v
//
// Project:	A series of CORDIC related projects
//
// Purpose:	This file executes a vector rotation on the values
//		(i_xval, i_yval).  This vector is rotated left by
//	i_phase.  i_phase is given by the angle, in radians, multiplied by
//	2^32/(2pi).  In that fashion, a two pi value is zero just as a zero
//	angle is zero.
//
//	This particular version of the CORDIC processes one value at a
//	time in a sequential, vs pipelined or parallel, fashion.
//
// Creator:	Dan Gisselquist, Ph.D.
//		Gisselquist Technology, LLC
//
////////////////////////////////////////////////////////////////////////////////
//
// Copyright (C) 2017-2018, Gisselquist Technology, LLC
//
// This program is free software (firmware): you can redistribute it and/or
// modify it under the terms of the GNU General Public License as published
// by the Free Software Foundation, either version 3 of the License, or (at
// your option) any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTIBILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
// for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program.  (It's in the $(ROOT)/doc directory.  Run make with no
// target there if the PDF file isn't present.)  If not, see
// <http://www.gnu.org/licenses/> for a copy.
//
// License:	GPL, v3, as defined and found on www.gnu.org,
//		http://www.gnu.org/licenses/gpl.html
//
//
////////////////////////////////////////////////////////////////////////////////
//
//
`default_nettype	none
//
module	seqcordic(i_clk, i_reset, i_stb, i_xval, i_yval, i_phase, i_aux,
		o_busy, o_done, o_xval, o_yval, o_aux);
	localparam	IW=12,	// The number of bits in our inputs
			OW=12,	// The number of output bits to produce
			NSTAGES=15,
			XTRA= 3,// Extra bits for internal precision
			WW=15,	// Our working bit-width
			PW=19;	// Bits in our phase variables
	input	wire				i_clk, i_reset, i_stb;
	input	wire	signed	[(IW-1):0]	i_xval, i_yval;
	input	wire		[(PW-1):0]	i_phase;
	output	wire				o_busy;
	output	reg				o_done;
	output	reg	signed	[(OW-1):0]	o_xval, o_yval;
	input	wire				i_aux;
	output	reg				o_aux;
	// First step: expand our input to our working width.
	// This is going to involve extending our input by one
	// (or more) bits in addition to adding any xtra bits on
	// bits on the right.  The one bit extra on the left is to
	// allow for any accumulation due to the cordic gain
	// within the algorithm.
	// 
	wire	signed [(WW-1):0]	e_xval, e_yval;
	assign	e_xval = { {i_xval[(IW-1)]}, i_xval, {(WW-IW-1){1'b0}} };
	assign	e_yval = { {i_yval[(IW-1)]}, i_yval, {(WW-IW-1){1'b0}} };

	// Declare variables for all of the separate stages
	reg	signed	[(WW-1):0]	xv, prex, yv, prey;
	reg		[(PW-1):0]	ph, preph;

	//
	// Handle the auxilliary logic.
	//
	// The auxilliary bit is designed so that you can place a valid bit into
	// the CORDIC function, and see when it comes out.  While the bit is
	// allowed to be anything, the requirement of this bit is that it *must*
	// be aligned with the output when done.  That is, if i_xval and i_yval
	// are input together with i_aux, then when o_xval and o_yval are set
	// to this value, o_aux *must* contain the value that was in i_aux.
	//
	reg		aux;

	always @(posedge i_clk)
	if (i_reset)
		aux <= 0;
	else if ((i_stb)&&(!o_busy))
		aux <= i_aux;

	// First step, get rid of all but the last 45 degrees
	//	The resulting phase needs to be between -45 and 45
	//		degrees but in units of normalized phase
	always @(posedge i_clk)
		// Walk through all possible quick phase shifts necessary
		// to constrain the input to within +/- 45 degrees.
		case(i_phase[(PW-1):(PW-3)])
		3'b000: begin	// 0 .. 45, No change
			prex  <=  e_xval;
			prey  <=  e_yval;
			preph <= i_phase;
			end
		3'b001: begin	// 45 .. 90
			prex  <= -e_yval;
			prey  <=  e_xval;
			preph <= i_phase - 19'h20000;
			end
		3'b010: begin	// 90 .. 135
			prex  <= -e_yval;
			prey  <=  e_xval;
			preph <= i_phase - 19'h20000;
			end
		3'b011: begin	// 135 .. 180
			prex  <= -e_xval;
			prey  <= -e_yval;
			preph <= i_phase - 19'h40000;
			end
		3'b100: begin	// 180 .. 225
			prex  <= -e_xval;
			prey  <= -e_yval;
			preph <= i_phase - 19'h40000;
			end
		3'b101: begin	// 225 .. 270
			prex  <=  e_yval;
			prey  <= -e_xval;
			preph <= i_phase - 19'h60000;
			end
		3'b110: begin	// 270 .. 315
			prex  <=  e_yval;
			prey  <= -e_xval;
			preph <= i_phase - 19'h60000;
			end
		3'b111: begin	// 315 .. 360, No change
			prex  <=  e_xval;
			prey  <=  e_yval;
			preph <= i_phase;
			end
		endcase

	//
	// In many ways, the key to this whole algorithm lies in the angles
	// necessary to do this.  These angles are also our basic reason for
	// building this CORDIC in C++: Verilog just can't parameterize this
	// much.  Further, these angle's risk becoming unsupportable magic
	// numbers, hence we define these and set them in C++, based upon
	// the needs of our problem, specifically the number of stages and
	// the number of bits required in our phase accumulator
	//
	reg	[18:0]	cordic_angle [0:15];
	reg	[18:0]	cangle;

	initial	cordic_angle[ 0] = 19'h0_9720; //  26.565051 deg
	initial	cordic_angle[ 1] = 19'h0_4fd9; //  14.036243 deg
	initial	cordic_angle[ 2] = 19'h0_2888; //   7.125016 deg
	initial	cordic_angle[ 3] = 19'h0_1458; //   3.576334 deg
	initial	cordic_angle[ 4] = 19'h0_0a2e; //   1.789911 deg
	initial	cordic_angle[ 5] = 19'h0_0517; //   0.895174 deg
	initial	cordic_angle[ 6] = 19'h0_028b; //   0.447614 deg
	initial	cordic_angle[ 7] = 19'h0_0145; //   0.223811 deg
	initial	cordic_angle[ 8] = 19'h0_00a2; //   0.111906 deg
	initial	cordic_angle[ 9] = 19'h0_0051; //   0.055953 deg
	initial	cordic_angle[10] = 19'h0_0028; //   0.027976 deg
	initial	cordic_angle[11] = 19'h0_0014; //   0.013988 deg
	initial	cordic_angle[12] = 19'h0_000a; //   0.006994 deg
	initial	cordic_angle[13] = 19'h0_0005; //   0.003497 deg
	initial	cordic_angle[14] = 19'h0_0002; //   0.001749 deg
	initial	cordic_angle[15] = 19'h0_0001; //   0.000874 deg
	// Std-Dev    : 0.00 (Units)
	// Phase Quantization: 0.000030 (Radians)
	// Gain is 1.164435
	// You can annihilate this gain by multiplying by 32'hdbd95b16
	// and right shifting by 32 bits.


	reg		idle, pre_valid;
	reg	[3:0]	state;

	initial	idle = 1'b1;
	always @(posedge i_clk)
	if (i_reset)
		idle <= 1'b1;
	else if (i_stb)
		idle <= 1'b0;
	else if (state == 14)
		idle <= 1'b1;

	initial	pre_valid = 1'b0;
	always @(posedge i_clk)
	if (i_reset)
		pre_valid <= 1'b0;
	else
		pre_valid <= (i_stb)&&(idle);

	always @(posedge i_clk)
		cangle <= cordic_angle[state];

	initial	state = 0;
	always @(posedge i_clk)
	if (i_reset)
		state <= 0;
	else if (idle)
		state <= 0;
	else if (state == 14)
		state <= 0;
	else
		state <= state + 1;

	// Here's where we are going to put the actual CORDIC
	// we've been studying and discussing.  Everything up to
	// this point has simply been necessary preliminaries.
	always @(posedge i_clk)
	if (pre_valid)
	begin
		xv <= prex;
		yv <= prey;
		ph <= preph;
	end else if (ph[PW-1])
	begin
		xv <= xv + (yv >>> state);
		yv <= yv - (xv >>> state);
		ph <= ph + (cangle);
	end else begin
		xv <= xv - (yv >>> state);
		yv <= yv + (xv >>> state);
		ph <= ph - (cangle);
	end

	// Round our result towards even
	wire	[(WW-1):0]	final_xv, final_yv;

	assign	final_xv = xv + $signed({{(OW){1'b0}},
				xv[(WW-OW)],
				{(WW-OW-1){!xv[WW-OW]}}});
	assign	final_yv = yv + $signed({{(OW){1'b0}},
				yv[(WW-OW)],
				{(WW-OW-1){!yv[WW-OW]}}});

	initial	o_done = 1'b0;
		always @(posedge i_clk)
	if (i_reset)
		o_done <= 1'b0;
	else
		o_done <= (state >= 14);

	always @(posedge i_clk)
	if (state >= 14)
	begin
		o_xval <= final_xv[WW-1:WW-OW];
		o_yval <= final_yv[WW-1:WW-OW];
		o_aux <= aux;
	end

	assign	o_busy = !idle;

	// Make Verilator happy with pre_.val
	// verilator lint_off UNUSED
	wire	[(2*WW-2*OW-1):0] unused_val;
	assign	unused_val = { final_xv[WW-OW-1:0], final_yv[WW-OW-1:0] };
	// verilator lint_on UNUSED
endmodule
