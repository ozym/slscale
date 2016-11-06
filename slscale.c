/*
 * Copyright (c) 2014 Institute of Geological & Nuclear Sciences Ltd.
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
 */

/* system includes */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <getopt.h>
#include <string.h>
#include <signal.h>
#include <syslog.h>
#include <errno.h>
#include <time.h>
#include <math.h>

/* libmseed library includes */
#include <libmseed.h>
#include <libslink.h>
#include <libdali.h>

#define PROGRAM "slscale" /* program name */

#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "xxx"
#endif

/*
 * slscale: scale raw seedlink miniseed packets and send to datalink
 *
 */

/* program variables */
static char *program_name = PROGRAM;
static char *program_version = PROGRAM " (" PACKAGE_VERSION ") (c) GNS 2014 (m.chadwick@gns.cri.nz)";
static char *program_usage = PROGRAM " [-hv][-w][-i <id>][-A <alpha>][-B <beta>][-O <orient>][<seedlink_options>] [<server>] [<datalink>]";
static char *program_prefix = "[" PROGRAM "] ";

static int verbose = 0; /* program verbosity */
static char *orient = "T";
static double alpha = 0.0;
static double beta = 10.0;

static char *id = "slscale"; /* client id */
static char *seedlink = ":18000"; /* datalink server to use */
static char *datalink = NULL; /* datalink server to use */
static int writeack = 0; /* request for write acks */

/* possible options */
static int unimode = 0;
static char *multiselect = NULL;
static char *selectors = "?TH";
static char *statefile = NULL;
static char *streamfile = NULL;
static int stateint = 300;

static SLCD *slconn = NULL;
static DLCP *dlconn = NULL;

/* handle any KILL/TERM signals */
static void term_handler(int sig) {
	sl_terminate(slconn); return;
}

static void dummy_handler (int sig) {
	return;
}

static void log_print(char *message) {
	if (verbose)
		fprintf(stderr, "%s", message);
}

static void err_print(char *message) {
	fprintf(stderr, "error: %s", message);
}

static void record_handler (char *record, int reclen, void *extra) {
	static MSRecord *msr = NULL;
	hptime_t endtime;
	char streamid[100];
	int rv;

	/* logging */
	if (verbose > 0)
		msr_print(msr, (verbose > 2) ? 1 : 0);

	if ((datalink == NULL) && (fwrite(record, reclen, 1, stdout) != 1)) {
		ms_log (2, "error writing mseed record to stdout\n"); return;
	}

	if (datalink != NULL) {

		/* Parse Mini-SEED header */
		if ((rv = msr_unpack (record, reclen, &msr, 0, 0)) != MS_NOERROR) {
			ms_log (2, "error unpacking mseed record: %s", ms_errorstr(rv)); return;
		}

		msr_srcname (msr, streamid, 0);
		strcat (streamid, "/MSEED");

		/* Determine high precision end time */
		endtime = msr_endtime (msr);

		/* Send record to server */
		while (dl_write (dlconn, record, reclen, streamid, msr->starttime, endtime, writeack) < 0) {
			if (verbose)
				ms_log (1, "re-connecting to datalink server\n");
			if (dlconn->link != -1)
				dl_disconnect(dlconn);
			if (dl_connect(dlconn) < 0) {
				ms_log (2, "error re-connecting to datalink server, sleeping 10 seconds\n"); sleep (10);
			}
			if (slconn->terminate)
				break;
		}
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

	char buf[1024];

	MSRecord *msr = NULL;
	SLpacket *slpack = NULL;
	int packetcnt = 0;

	int rc;
	int option_index = 0;
	struct option long_options[] = {
		{"help", 0, 0, 'h'},
		{"verbose", 0, 0, 'v'},
		{"ack", 0, 0, 'w'},
		{"id", 1, 0, 'i'},
		{"delay", 1, 0, 'd'},
		{"timeout", 1, 0, 't'},
		{"heartbeat", 1, 0, 'k'},
		{"streamlist", 1, 0, 'l'},
		{"streams", 1, 0, 'S'},
		{"selectors", 1, 0, 's'},
		{"statefile", 1, 0, 'x'},
		{"update", 1, 0, 'u'},
		{"alpha", 1, 0, 'A'},
		{"beta", 1, 0, 'B'},
		{0, 0, 0, 0}
	};

	/* posix signal handling */
	struct sigaction sa;

	sa.sa_handler = dummy_handler;
	sa.sa_flags	= SA_RESTART;
	sigemptyset (&sa.sa_mask);
	sigaction (SIGALRM, &sa, NULL);

	sa.sa_handler = term_handler;
	sigaction (SIGINT, &sa, NULL);
	sigaction (SIGQUIT, &sa, NULL);
	sigaction (SIGTERM, &sa, NULL);

	sa.sa_handler = SIG_IGN;
	sigaction (SIGHUP, &sa, NULL);
	sigaction (SIGPIPE, &sa, NULL);

	/* adjust output logging ... -> syslog maybe? */
	ms_loginit (log_print, program_prefix, err_print, program_prefix);

	/* get a new connection description */
	slconn = sl_newslcd();

	while ((rc = getopt_long(argc, argv, "hvwi:d:t:k:l:S:s:x:u:A:B:O:", long_options, &option_index)) != EOF) {
		switch(rc) {
		case '?':
			(void) fprintf(stderr, "usage: %s\n", program_usage);
			exit(-1); /*NOTREACHED*/
		case 'h':
			(void) fprintf(stderr, "\n[%s] seedlink sample scaling\n\n", program_name);
			(void) fprintf(stderr, "usage:\n\t%s\n", program_usage);
			(void) fprintf(stderr, "version:\n\t%s\n", program_version);
			(void) fprintf(stderr, "options:\n");
			(void) fprintf(stderr, "\t-h --help\tcommand line help (this)\n");
			(void) fprintf(stderr, "\t-v --verbose\trun program in verbose mode\n");
			(void) fprintf(stderr, "\t-w --ack\trequest write acks [%s]\n", (writeack) ? "on" : "off");
			(void) fprintf(stderr, "\t-i --id \tprovide a config lookup key [%s]\n", id);
			(void) fprintf(stderr, "\t-d --delay\talternative seedlink delay [%d]\n", slconn->netdly);
			(void) fprintf(stderr, "\t-t --timeout\talternative seedlink timeout [%d]\n", slconn->netto);
			(void) fprintf(stderr, "\t-k --heartbeat\talternative seedlink heartbeat [%d]\n", slconn->keepalive);
			(void) fprintf(stderr, "\t-l --streamlist\tuse a stream list file [%s]\n", (streamfile) ? streamfile : "<null>");
			(void) fprintf(stderr, "\t-S --streams\talternative seedlink streams [%s]\n", (multiselect) ? multiselect : "<null>");
			(void) fprintf(stderr, "\t-s --selectors\talternative seedlink selectors [%s]\n", (selectors) ? selectors : "<null>");
			(void) fprintf(stderr, "\t-x --statefile\tseedlink statefile [%s]\n", (statefile) ? statefile : "<null>");
			(void) fprintf(stderr, "\t-u --update\talternative state flush interval [%d]\n", stateint);
			(void) fprintf(stderr, "\t-A --alpha\tadd offset to scaled data samples [%g]\n", alpha);
			(void) fprintf(stderr, "\t-B --beta\tscale factor for raw miniseed samples [%g]\n", beta);
			(void) fprintf(stderr, "\t-O --orient\talternative orientation code [%s]\n", orient);
			exit(0); /*NOTREACHED*/
		case 'v':
			verbose++;
			break;
		case 'w':
			writeack++;
			break;
		case 'i':
			id = optarg;
			break;
		case 'd':
			slconn->netdly = atoi(optarg);
			break;
		case 't':
			slconn->netto = atoi(optarg);
			break;
		case 'k':
			slconn->keepalive = atoi(optarg);
			break;
		case 'l':
			streamfile = optarg;
			break;
		case 's':
			selectors = optarg;
			break;
		case 'S':
			multiselect = optarg;
			break;
		case 'x':
			statefile = optarg;
			break;
		case 'u':
			stateint = atoi(optarg);
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

	/* who to connect to ... */
	seedlink = ((optind < argc) ? argv[optind++] : seedlink);
	datalink = ((optind < argc) ? argv[optind++] : datalink);

	/* report the program version */
	if (verbose)
		ms_log (0, "%s\n", program_version);

	if (datalink) {
		/* provide user tag */
		(void) sprintf(buf, "%s:%s", argv[0], id);

		/* allocate and initialize datalink connection description */
		if ((dlconn = dl_newdlcp (datalink, buf)) == NULL) {
			ms_log(1, "cannot allocation datalink descriptor\n"); exit (-1);
		}
		/* connect to datalink server */
		if (dl_connect(dlconn) < 0) {
			ms_log(1, "error connecting to datalink server: server\n"); exit(-1);
		}
		if (dlconn->writeperm != 1) {
			ms_log(1, "datalink server is non-writable\n"); exit(-1);
		}
	}

	slconn->sladdr = seedlink;
	if (streamfile) {
		if (sl_read_streamlist (slconn, streamfile, selectors) < 0) {
		 ms_log(1, "unable to read streams [%s]\n", streamfile); exit(-1);
		}
	}
	else if (multiselect) {
		if (sl_parse_streamlist (slconn, multiselect, selectors) < 0) {
			ms_log(1, "unable to load streams [%s]\n", multiselect); exit(-1);
		}
	}
	else {
		if (sl_setuniparams (slconn, selectors, -1, 0) < 0) {
			ms_log(1, "unable to load selectors [%s]\n", selectors); exit(-1);
		}
	}

	/* recover any statefile info ... */
	if ((statefile) && (sl_recoverstate (slconn, statefile) < 0)) {
		ms_log (1, "unable to recover statefile [%s]\n", statefile);
	}

	/* loop with the connection manager */
	while (sl_collect (slconn, &slpack)) {
		
		/* unpack record header and data samples */
		if ((rc = msr_unpack (slpack->msrecord, SLRECSIZE, &msr, 1, 1)) != MS_NOERROR) {
			sl_log(2, 0, "error parsing record\n");
		}

		if (verbose > 1)
			msr_print(msr, (verbose > 2) ? 1 : 0);

		/* simply send it through ... */
		if (sl_packettype(slpack) == SLDATA)
            if (scale_record(msr) < 0) {
		        msr_free(&msr); break;
            }

		/* done with it */
		msr_free(&msr);

		/* Save intermediate state files */
		if (statefile && stateint) {
			if (++packetcnt >= stateint) {
				sl_savestate (slconn, statefile);
				packetcnt = 0;
			}
		}
	}

	/* closing down */
	if (verbose)
		ms_log (0, "stopping\n");

	if (statefile && slconn->terminate)
		(void) sl_savestate (slconn, statefile);

	if (slconn->link != -1)
		(void) sl_disconnect (slconn);

	if ((datalink) && (dlconn->link != -1))
		dl_disconnect (dlconn);

	/* closing down */
	if (verbose)
		ms_log (0, "terminated\n");

	/* done */
	return(0);
}
