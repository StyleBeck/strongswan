/*
 * Copyright (C) 2017 Andreas Steffen
 * HSR Hochschule fuer Technik Rapperswil
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <http://www.fsf.org/copyleft/gpl.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <unistd.h>
#ifdef HAVE_SYSLOG
# include <syslog.h>
#endif

#include "sw_collector_db.h"
#include "sw_collector_history.h"

#include <library.h>
#include <utils/debug.h>
#include <utils/lexparser.h>

/**
 * global debug output variables
 */
static int debug_level = 2;
static bool stderr_quiet = FALSE;
static int count = 0;

typedef enum collector_op_t collector_op_t;

enum collector_op_t {
	COLLECTOR_OP_EXTRACT,
	COLLECTOR_OP_LIST
};

/**
 * sw_collector dbg function
 */
static void sw_collector_dbg(debug_t group, level_t level, char *fmt, ...)
{
	va_list args;

	if (level <= debug_level)
	{
		if (!stderr_quiet)
		{
			va_start(args, fmt);
			vfprintf(stderr, fmt, args);
			fprintf(stderr, "\n");
			va_end(args);
		}

#ifdef HAVE_SYSLOG
		{
			int priority = LOG_INFO;
			char buffer[8192];
			char *current = buffer, *next;

			/* write in memory buffer first */
			va_start(args, fmt);
			vsnprintf(buffer, sizeof(buffer), fmt, args);
			va_end(args);

			/* do a syslog with every line */
			while (current)
			{
				next = strchr(current, '\n');
				if (next)
				{
					*(next++) = '\0';
				}
				syslog(priority, "%s\n", current);
				current = next;
			}
		}
#endif /* HAVE_SYSLOG */
	}
}

/**
 * atexit handler
 */
static void cleanup(void)
{
	library_deinit();
#ifdef HAVE_SYSLOG
	closelog();
#endif
}

/**
 * Display usage of sw-collector command
 */
static void usage(void)
{
	printf("\
Usage:\n\
  sw-collector --help\n\
  sw-collector [--debug <level>] [--quiet] --list\n\
  sw-collector [--debug <level>] [--quiet] [--count <event count>]\n");
}

/**
 * Parse command line options
 */
static collector_op_t do_args(int argc, char *argv[])
{
	collector_op_t op = COLLECTOR_OP_EXTRACT;

	/* reinit getopt state */
	optind = 0;

	while (TRUE)
	{
		int c;

		struct option long_opts[] = {
			{ "help", no_argument, NULL, 'h' },
			{ "count", required_argument, NULL, 'c' },
			{ "debug", required_argument, NULL, 'd' },
			{ "list", no_argument, NULL, 'l' },
			{ "quiet", no_argument, NULL, 'q' },
			{ 0,0,0,0 }
		};

		c = getopt_long(argc, argv, "hc:d:lq", long_opts, NULL);
		switch (c)
		{
			case EOF:
				break;
			case 'h':
				usage();
				exit(SUCCESS);
				break;
			case 'c':
				count = atoi(optarg);
				continue;
			case 'd':
				debug_level = atoi(optarg);
				continue;
			case 'l':
				op = COLLECTOR_OP_LIST;
				continue;
			case 'q':
				stderr_quiet = TRUE;
				continue;
			default:
				usage();
				exit(EXIT_FAILURE);
		}
		break;
	}
	return op;
}

/**
 * Extract software events from apt history log files
 */
static int extract_history(sw_collector_db_t *db)
{
	sw_collector_history_t *history = NULL;
	uint32_t epoch, last_eid, eid = 0;
	char *history_path, *last_time = NULL, rfc_time[21];
	chunk_t *h, history_chunk, line, cmd;
	int status = EXIT_FAILURE;
	bool skip = TRUE;

	/* open history file for reading */
	history_path= lib->settings->get_str(lib->settings, "sw-collector.history",
										 NULL);
	if (!history_path)
	{
		fprintf(stderr, "sw-collector.history path not set.\n");
		return FALSE;
	}
	h = chunk_map(history_path, FALSE);
	if (!h)
	{
		fprintf(stderr, "opening '%s' failed: %s", history, strerror(errno));
		return FALSE;
	}
	history_chunk = *h;

	/* Instantiate history extractor */
	history = sw_collector_history_create(db, 1);
	if (!history)
	{
		/* OS is not supported */
		goto end;
	}

	/* retrieve last event in database */
	if (!db->get_last_event(db, &last_eid, &epoch, &last_time) || !last_eid)
	{
		goto end;
	}
	DBG0(DBG_IMC, "Last-Event: %s, eid = %u, epoch = %u",
				   last_time, last_eid, epoch);

	/* parse history file */
	while (fetchline(&history_chunk, &line))
	{
		if (line.len == 0)
		{
			continue;
		}
		if (!extract_token(&cmd, ':', &line))
		{
			fprintf(stderr, "terminator symbol ':' not found.\n");
			goto end;
		}
		if (match("Start-Date", &cmd))
		{
			if (!history->extract_timestamp(history, line, rfc_time))
			{
				goto end;
			}

			/* have we reached new history entries? */
			if (skip && strcmp(rfc_time, last_time) > 0)
			{
				skip = FALSE;
			}
			if (skip)
			{
				continue;
			}

			/* insert new event into database */
			eid = db->add_event(db, rfc_time);
			if (!eid)
			{
				goto end;
			}
			DBG1(DBG_IMC, "Start-Date: %s, eid = %u, epoch = %u",
						   rfc_time, eid, epoch);
		}
		else if (skip)
		{
			/* skip old history entries which have already been processed */
			continue;
		}
		else if (match("Install", &cmd))
		{
			DBG1(DBG_IMC, "  Install:");
			if (!history->extract_packages(history, line, eid, SW_OP_INSTALL))
			{
				goto end;
			}
		}
		else if (match("Upgrade", &cmd))
		{
			DBG1(DBG_IMC, "  Upgrade:");
			if (!history->extract_packages(history, line, eid, SW_OP_UPGRADE))
			{
				goto end;
			}
		}
		else if (match("Remove", &cmd))
		{
			DBG1(DBG_IMC, "  Remove:");
			if (!history->extract_packages(history, line, eid, SW_OP_REMOVE))
			{
				goto end;
			}
		}
		else if (match("Purge", &cmd))
		{
			DBG1(DBG_IMC, "  Purge:");
			if (!history->extract_packages(history, line, eid, SW_OP_REMOVE))
			{
				goto end;
			}
		}
		else if (match("End-Date", &cmd))
		{
			/* Process 'count' events at a time */
			if (count > 0 && eid - last_eid == count)
			{
				fprintf(stderr, "added %d events\n", count);
				goto end;
			}
		}
	}

	if (history->merge_installed_packages(history))
	{
		status = EXIT_SUCCESS;
	}

end:
	free(last_time);
	DESTROY_IF(history);
	chunk_unmap(h);

	return status;
}

/**
 * List all software identifiers stored in the collector database
 */
static int list_identifiers(sw_collector_db_t *db)
{
	enumerator_t *e;
	char *name, *package, *version;
	uint32_t count = 0, installed_count = 0, installed;

	e = db->create_sw_enumerator(db, FALSE);
	if (!e)
	{
		return EXIT_FAILURE;
	}
	while (e->enumerate(e, &name, &package, &version, &installed))
	{
		printf("%s,%s,%s,%d\n", name, package, version, installed);
		if (installed)
		{
			installed_count++;
		}
		count++;
	}
	e->destroy(e);
	DBG1(DBG_IMC, "retrieved %u software identities with %u installed and %u "
				  "deleted", count, installed_count, count - installed_count);

	return EXIT_SUCCESS;
}

int main(int argc, char *argv[])
{
	sw_collector_db_t *db = NULL;
	collector_op_t op;
	char *uri;
	int status;

	op = do_args(argc, argv);

	/* enable sw_collector debugging hook */
	dbg = sw_collector_dbg;
#ifdef HAVE_SYSLOG
	openlog("sw-collector", 0, LOG_DEBUG);
#endif

	atexit(cleanup);

	/* initialize library */
	if (!library_init(NULL, "sw-collector"))
	{
		exit(SS_RC_LIBSTRONGSWAN_INTEGRITY);
	}

	/* load sw-collector plugins */
	if (!lib->plugins->load(lib->plugins,
			lib->settings->get_str(lib->settings, "sw-collector.load", PLUGINS)))
	{
		exit(SS_RC_INITIALIZATION_FAILED);
	}

	/* connect to sw-collector database */
	uri = lib->settings->get_str(lib->settings, "sw-collector.database", NULL);
	if (!uri)
	{
		fprintf(stderr, "sw-collector.database URI not set.\n");
		exit(EXIT_FAILURE);
	}
	db = sw_collector_db_create(uri);
	if (!db)
	{
		fprintf(stderr, "connection to sw-collector database failed.\n");
		exit(EXIT_FAILURE);
	}

	switch (op)
	{
		case COLLECTOR_OP_EXTRACT:
			status = extract_history(db);
			break;
		case COLLECTOR_OP_LIST:
			status = list_identifiers(db);
			break;
		default:
			break;
	}
	db->destroy(db);

	exit(status);
}
