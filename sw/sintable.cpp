////////////////////////////////////////////////////////////////////////////////
//
// Filename: 	sintable.cpp
//
// Project:	A series of CORDIC related projects
//
// Purpose:	To define two different table-based sinewave calculators that
//		can be used within an FPGA.  This routine not only creates a
//	table based sinewave calculator, but also creates a hex file defining
//	the values in the table that can be used.
//
// Creator:	Dan Gisselquist, Ph.D.
//		Gisselquist Technology, LLC
//
////////////////////////////////////////////////////////////////////////////////
//
// Copyright (C) 2017, Gisselquist Technology, LLC
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <math.h>
#include <assert.h>
#include "hexfile.h"

#include "legal.h"

void	sintable(FILE *fp, const char *fname, int lgtable, int ow,
		bool with_reset, bool with_aux, bool async_reset) {
	char	*name;
	const	char	PURPOSE[] =
	"This is a very simple sinewave table lookup approach\n"
	"//\t\tapproach to generating a sine wave.  It has the lowest latency\n"
	"//\tamong all sinewave generation alternatives.";

	if (lgtable >= 24) {
		fprintf(stderr, "ERR: Requested table size is greater than 16M\n\n");
		fprintf(stderr, "While this is an arbitrary limit, few FPGA's have this kind of\n");
		fprintf(stderr, "block RAM.  If you know what you are doing, you can change this\n");
		fprintf(stderr, "limit up to perhaps 30 without much hassle.  Beyond that, be\n");
		fprintf(stderr, "aware of integer overflow.\n");
		exit(EXIT_FAILURE);
	}

	legal(fp, fname, PROJECT, PURPOSE);
	fprintf(fp, "`default_nettype\tnone\n//\n");
	name = modulename(fname);

	std::string	resetw = (!with_reset) ? ""
				: (async_reset) ? "i_areset_n, ":"i_reset, ";
	std::string	always_reset;
	if ((with_reset)&&(async_reset))
		always_reset = "\talways @(posedge i_clk, negedge i_areset_n)\n"
			"\tif (!i_areset_n)\n";
	else if (with_reset)
		always_reset = "\talways @(posedge i_clk)\n"
			"\tif (i_reset)\n";
	else
		always_reset = "\talways @(posedge i_clk)\n\t";

	fprintf(fp,
		"module	%s(i_clk, %si_ce, %si_phase, o_val%s);\n"
		"\t//\n"
		"\tparameter\tPW =%2d, // Number of bits in the input phase\n"
		"\t\t\tOW =%2d; // Number of output bits\n"
		"\t//\n"
		"\tinput\twire\t\t\ti_clk, %si_ce;\n"
		"\tinput\twire\t[(PW-1):0]\ti_phase;\n"
		"\toutput\treg\t[(OW-1):0]\to_val;\n",
		name,
		resetw.c_str(),
		(with_aux)   ? "i_aux, "  :"",
		(with_aux)   ? ", o_aux"  :"",
		lgtable, ow,
		resetw.c_str());
	if (with_aux)
		fprintf(fp,
			"\t//\n"
			"\tinput\twire\t\t\ti_aux;\n"
			"\toutput\treg\t\t\to_aux;\n");
	fprintf(fp,
		"\n"
		"\treg\t[(OW-1):0]\t\ttbl\t[0:((1<<PW)-1)];\n"
		"\n"
		"\tinitial\t$readmemh(\"%s.hex\", tbl);\n"
		"\n", name);

	fprintf(fp, "%s", always_reset.c_str());
	if (with_reset) {
		fprintf(fp, 
			"\t\to_val <= 0;\n"
			"\telse ");
	}

	fprintf(fp, "if (i_ce)\n"
		"\t\to_val <= tbl[i_phase];\n\n");

	if (with_aux) {
		fprintf(fp, "%s", always_reset.c_str());
		if (with_reset) {
			fprintf(fp, 
				"\t\to_aux <= 0;\n"
				"\telse ");
		}
		fprintf(fp, "if (i_ce)\n"
			"\t\to_aux <= i_aux;\n");
	}
	fprintf(fp, "endmodule\n");

	long	*tbldata;
	tbldata = new long[(1<<lgtable)];
	int	tbl_entries = (1<<lgtable);
	long	maxv = (1l<<(ow-1))-1l;

	for(int k=0; k<tbl_entries; k++) {
		double	ph;
		ph = 2.0 * M_PI * (double)k / (double)tbl_entries;

		tbldata[k]  = (long)maxv * sin(ph);
	}

	hextable(fname, lgtable, ow, tbldata);

	delete[] tbldata;
}

void	quarterwav(FILE *fp, const char *fname, int lgtable, int ow,
		bool with_reset, bool with_aux, bool async_reset) {
	char	*name;
	const	char	PURPOSE[] =
	"This is a touch more complicated than the simple sinewave table\n"
	"//\t\tlookup approach to generating a sine wave.  This approach\n"
	"//\texploits the fact that a sinewave table has symmetry within it,\n"
	"//\tenough symmetry so as to cut the necessary size of the table\n"
	"//\tin fourths.  Generating the sinewave value, though, requires\n"
	"//\ta little more logic to make this possible.";

	assert(lgtable>2);
	if (lgtable >= 26) {
		fprintf(stderr, "ERR: Requested table size is greater than 16M\n\n");
		fprintf(stderr, "While this is an arbitrary limit, few FPGA's have this kind of\n");
		fprintf(stderr, "block RAM.  If you know what you are doing, you can change this\n");
		fprintf(stderr, "limit up to perhaps 30 without much hassle.  Beyond that, be\n");
		fprintf(stderr, "aware of integer overflow.\n");
		exit(EXIT_FAILURE);
	}

	legal(fp, fname, PROJECT, PURPOSE);
	name = modulename(fname);

	std::string	resetw = (!with_reset) ? ""
				: (async_reset) ? "i_areset_n":"i_reset";
	std::string	always_reset;
	if ((with_reset)&&(async_reset))
		always_reset = "\talways @(posedge i_clk, negedge i_areset_n)\n"
			"\tif (!i_areset_n)\n";
	else if (with_reset)
		always_reset = "\talways @(posedge i_clk)\n"
			"\tif (i_reset)\n";
	else
		always_reset = "\talways @(posedge i_clk)\n\t";


	fprintf(fp,
		"module	%s(i_clk, %s%si_ce, i_phase, %so_val%s);\n"
		"\t//\n"
		"\tparameter\tPW =%2d, // Number of bits in the input phase\n"
		"\t\t\tOW =%2d; // Number of output bits\n"
		"\t//\n"
		"\tinput\t\t\t\ti_clk, %s%si_ce;\n"
		"\tinput\twire\t[(PW-1):0]\ti_phase;\n"
		"\toutput\treg\t[(OW-1):0]\to_val;\n",
		name,
		resetw.c_str(), (with_reset) ? ", ":"",
		(with_aux)   ? "i_aux, ":"",
		(with_aux)   ? ", o_aux":"",
		lgtable, ow,
		resetw.c_str(), (with_reset) ? ", ":"");

	if (with_aux)
		fprintf(fp, "\t//\n"
			"\tinput\twire\t\t\ti_aux;\n"
			"\toutput\twire\t\t\to_aux;\n");

	fprintf(fp,
		"\n"
		"\treg\t[(OW-1):0]\t\tquartertable\t[0:((1<<(PW-2))-1)];\n"
		"\n"
		"\tinitial\t$readmemh(\"%s.hex\", quartertable);\n"
		"\n"
		"\treg\t[1:0]\tnegate;\n"
		"\treg\t[(PW-3):0]\tindex;\n"
		"\treg\t[(OW-1):0]\ttblvalue;\n"
		"\n", name);

	fprintf(fp, "%s", always_reset.c_str());

	if (with_reset)
		fprintf(fp,
			"\tbegin\n"
			"\t\tnegate  <= 2\'b00;\n"
			"\t\tindex   <= 0;\n"
			"\t\ttblvalue<= 0;\n"
			"\t\to_val   <= 0;\n"
			"\tend else ");

	fprintf(fp,
		"if (i_ce)\n"
		"\tbegin\n"
			"\t\t// Clock #1\n"
			"\t\tnegate[0] <= i_phase[(PW-1)];\n"
			"\t\tif (i_phase[(PW-2)])\n"
			"\t\t\tindex <= ~i_phase[(PW-3):0];\n"
			"\t\telse\n"
			"\t\t\tindex <=  i_phase[(PW-3):0];\n"
			""
			"\t\t// Clock #2\n"
			"\t\ttblvalue <= quartertable[index];\n"
			"\t\tnegate[1] <= negate[0];\n"
			""
			"\t\t// Output Clock\n"
			"\t\tif (negate[1])\n"
			"\t\t\to_val <= -tblvalue;\n"
			"\t\telse\n"
			"\t\t\to_val <=  tblvalue;\n"
		"\tend\n\n");

	if (with_aux) {
		fprintf(fp, "\treg [1:0]\taux;\n");
		fprintf(fp, "%s", always_reset.c_str());
		if(with_reset)
			fprintf(fp, "\t\t{ o_aux, aux } <= 0;\n"
				"\telse ");
		fprintf(fp, "if (i_ce)\n\t\t{ o_aux, aux } <= { aux, i_aux };\n");
	}

	fprintf(fp, "endmodule\n");

	long	*tbldata;
	tbldata = new long[(1<<lgtable)];
	int	tbl_entries = (1<<lgtable);
	long	maxv = (1l<<(ow-1))-1l;

	for(int k=0; k<tbl_entries/4; k++) {
		double	ph;
		ph = 2.0 * M_PI * (double)k / (double)tbl_entries;
		ph+=       M_PI             / (double)tbl_entries;
		tbldata[k] = maxv * sin(ph);
	}

	hextable(fname, lgtable-2, ow, tbldata);

	delete[] tbldata;
}
