/*
 * Copyright (c) 2012 Institute of Geological & Nuclear Sciences Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *		notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *		notice, this list of conditions and the following disclaimer in the
 *		documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 */

/* system includes */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <math.h>

/* libmseed library includes */
#include <libmseed.h>

#define PROGRAM "msscale" /* program name */

#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "xxx"
#endif

/*
 * msscale: scale raw miniseed records
 *
 */

/* program variables */
static char *program_name = PROGRAM;
static char *program_version = PROGRAM " (" PACKAGE_VERSION ") (c) GNS 2014 (m.chadwick@gns.cri.nz)";
static char *program_usage = PROGRAM " [-hv][-A <alpha>][-B <beta>][-O <orient>][<files> ... ]";
static char *program_prefix = "[" PROGRAM "] ";

static int verbose = 0; /* program verbosity */

static char *orient = "T";
static double alpha = 0.0;
static double beta = 1.0;

static void log_print(char *message) {
	if (verbose)
		fprintf(stderr, "%s", message);
}

static void err_print(char *message) {
	fprintf(stderr, "error: %s", message);
}

static void record_handler (char *record, int reclen, void *extra) {
	if (fwrite(record, reclen, 1, stdout) != 1) {
		ms_log (2, "error writing mseed record to stdout\n");
	}
}

static int scale_record(MSRecord *msr) {
    int n;

    MSTraceGroup *mstg;

    long precords = 0;
    long psamples = 0;

    /* need at least a sample */
    if (msr->samplecnt < 1)
        return 0;

    /* we need an integer value */
    if (msr->sampletype != 'i')
        return 0;

    /* problem with rate */
    if (msr->samprate == 0.0)
        return 0;

    /* convert to scaled values ... */
    if (orient != NULL)
        msr->channel[2] = (*orient);

    for (n = 0; n < msr->numsamples; n++) {
        ((int *) msr->datasamples)[n] = (int) rint(alpha + beta * ((double) ((int *) msr->datasamples)[n]));
    }

    if ((mstg = mst_initgroup(NULL)) == NULL)
        return -1;

    mst_addmsrtogroup(mstg, msr, 0, -1, -1);
    mst_printtracelist(mstg, 1, 1, 1);
    precords = mst_packgroup (mstg, record_handler, NULL, 512, DE_STEIM2, 1, &psamples, 1, 0, NULL);

    mst_freegroup (&mstg);

    return psamples;
}

int main(int argc, char **argv) {

	MSRecord *msr = NULL;

	int rc;
	int option_index = 0;
	struct option long_options[] = {
		{"help", 0, 0, 'h'},
		{"verbose", 0, 0, 'v'},
		{"alpha", 1, 0, 'A'},
		{"beta", 1, 0, 'B'},
		{"orient", 1, 0, 'O'},
		{0, 0, 0, 0}
	};

	/* adjust output logging ... -> syslog maybe? */
	ms_loginit (log_print, program_prefix, err_print, program_prefix);

	while ((rc = getopt_long(argc, argv, "hvA:B:O:", long_options, &option_index)) != EOF) {
		switch(rc) {
		case '?':
			(void) fprintf(stderr, "usage: %s\n", program_usage);
			exit(-1); /*NOTREACHED*/
		case 'h':
			(void) fprintf(stderr, "\n[%s] miniseed sample scaling\n\n", program_name);
			(void) fprintf(stderr, "usage:\n\t%s\n", program_usage);
			(void) fprintf(stderr, "version:\n\t%s\n", program_version);
			(void) fprintf(stderr, "options:\n");
			(void) fprintf(stderr, "\t-h --help\tcommand line help (this)\n");
			(void) fprintf(stderr, "\t-v --verbose\trun program in verbose mode\n");
			(void) fprintf(stderr, "\t-A --alpha\tadd offset to scaled samples [%g]\n", alpha);
			(void) fprintf(stderr, "\t-B --beta\tscale raw samples [%g]\n", beta);
			(void) fprintf(stderr, "\t-O --orient\talternative orientation code [%s]\n", orient);
			exit(0); /*NOTREACHED*/
		case 'v':
			verbose++;
			break;
		case 'A':
			alpha = atof(optarg);
			break;
		case 'B':
			beta = atof(optarg);
			break;
		case 'O':
			orient = optarg;
			break;
		}
	}

	/* report the program version */
	if (verbose)
		ms_log (0, "%s\n", program_version);

	if (verbose)
	    ms_log (0, "scale [%s] alpha=%g beta=%g\n", alpha, beta);

    do {
        if (verbose)
		  ms_log (0, "process miniseed data from %s\n", (optind < argc) ? argv[optind] : "<stdin>");

		while ((rc = ms_readmsr (&msr, (optind < argc) ? argv[optind] : "-", 0, NULL, NULL, 1, 1, (verbose > 1) ? 1 : 0)) == MS_NOERROR) {
			if (verbose > 1)
				msr_print(msr, (verbose > 2) ? 1 : 0);
			if (scale_record(msr) < 0)
                break;
		}
		if (rc != MS_ENDOFFILE )
		    ms_log (2, "error reading stdin: %s\n", ms_errorstr(rc));

		/* Cleanup memory and close file */
		ms_readmsr (&msr, NULL, 0, NULL, NULL, 0, 0, (verbose > 1) ? 1 : 0);
    } while((++optind) < argc);

	/* closing down */
	if (verbose)
		ms_log (0, "terminated\n");

	/* done */
	return(0);
}
