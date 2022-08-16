/* -------------------------------------------------------------------------
 *
 * pg_gnuplot.c
 * PostgreSQL extension to let the PostgreSQL server plot the results of
 * a query using gnuplot.
 * The extension uses popen and pclose APIs to open gnuplot as a subprocess
 * The returned handle can then be read or written as a normal file.
 * When read the stdout or the subprocess is read,
 * When written the stdin of the subprocess is written as if the user
 * is typing commands at the gnuplot prompt.
 * This would work only if the server is installed on a linux distro with
 * x11 support.
 * Need to test
 * 1. Does gnuplot get installed on terminal only linux distro
 * 2. If yes how does the extension behave in this case
 * -------------------------------------------------------------------------
 */

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include "postgres.h"
#include "fmgr.h"
#include "lib/stringinfo.h"
#include "executor/spi.h"

#define CODE_VERSION	100
#define GEN_LEN			1024

PG_MODULE_MAGIC;

FILE		*g_fp = NULL;

void		_PG_init(void);
void		_PG_fini(void);

PG_FUNCTION_INFO_V1(pg_plot);
PG_FUNCTION_INFO_V1(pg_gnuplot_version);
PG_FUNCTION_INFO_V1(gnuplot_version);

char read_stdin(FILE *fp);


void
_PG_fini(void)
{
}

void
_PG_init(void)
{
}

/*
 * Returns the version of this library file.
 * At every release the code version is updated
 * to differentiate the old and new libraries installed in the system.
 * Exmaple:
 * SELECT pg_gnuplot_version();
 * would return a 3 digit number
 * The fisrt digit is the major version
 * The last two digits are the minor version
 * The number 100 means version 1.00
 * The number 123 means version 1.23
 * The number 203 means version 2.03
 */
Datum
pg_gnuplot_version(PG_FUNCTION_ARGS)
{
	PG_RETURN_INT32(CODE_VERSION);
}

/*
 * Performs the following tasks
 * 1. Locates the gnuplot binary in the linux system using whereis command
 *    whereis -b gnuplot
 *    If not found reports the error
 *    If found it parses the output of the whereis command to extract
 *    the complete path of the gnuplot binary
 * 2. Constructs the gnuplot command to find its version and runs it
 *    /usr/bin/gnuplot -V
 * 3. Saves the output the command run in step 2 to return to the caller
 * 4. If the file pointer is NULL, or runs the gnuplot
 *    and saves the pointer in the global file pointer
 *    This will be used later by the pg_plot function to issue plot
 *    commands to the invoked gnuplot.
 *    For this reason this function must be called before using pg_plot
 * Eample:
 *	SELECT gnuplot_version();
 *		gnuplot_version      
 *	--------------------------
 *	gnuplot 4.6 patchlevel 2
 *	(1 row)
 */
Datum
gnuplot_version(PG_FUNCTION_ARGS)
{
	FILE			*fp;
	char			ch;
	StringInfoData	cmdbuf;
	int				firstSpacePos = -1;
	int				i;
	int				popen_err = 0;
	char			errbuf[GEN_LEN + 1];
	char			tmpbuf[GEN_LEN + 1];

	memset(errbuf, 0, GEN_LEN + 1);

	initStringInfo(&cmdbuf);
	fp = popen("whereis -b gnuplot", "r");
	popen_err = errno;
	if (fp == NULL)
	{
		ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("PG_GNUPLOT : popen could not execute command"),
				 errdetail("%s", "whereis -b gnuplot")));
		PG_RETURN_CSTRING(cmdbuf.data);
	}

	i = 0;
	while (1)
	{
		ch = read_stdin(fp);
		if (ch == EOF || ch == 0)
			break;

		if (firstSpacePos < 0)
		{
			if (ch == ' ')
				firstSpacePos = i;
		}
		else
		{
			if (ch == ' ')
			{
				appendStringInfoString(&cmdbuf, " -V");
				ch = '\0';
				appendStringInfoChar(&cmdbuf, ch);
				break;
			}
		}

		if (firstSpacePos >= 0)
		{
			appendStringInfoChar(&cmdbuf, ch);
		}
		i++;
	}
	pclose(fp);

	if (i == 0 && popen_err > 0)
	{
		memset(errbuf, 0, GEN_LEN + 1);
		snprintf(errbuf, GEN_LEN, "%s", strerror(popen_err));
		ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("PG_GNUPLOT : popen could not execute command [whereis -b gnuplot]"),
				 errdetail("%s", errbuf)));
		PG_RETURN_CSTRING(errbuf);
	}

	fp = popen(cmdbuf.data, "r");
	popen_err = errno;
	if (fp == NULL)
	{
		ereport(ERROR, (errmsg("PG_GNUPLOT : popen could not execute command [%s]"), cmdbuf.data));
		PG_RETURN_CSTRING(cmdbuf.data);
	}

	memset(tmpbuf, 0, GEN_LEN + 1);
	memcpy(tmpbuf, cmdbuf.data, (cmdbuf.len > GEN_LEN) ? GEN_LEN : cmdbuf.len);

	resetStringInfo(&cmdbuf);
	i = 0;
	while (1)
	{
		ch = read_stdin(fp);
		if (ch == EOF || ch == 0)
		{
			break;
		}
		i++;
		if (ch != '\n')
			appendStringInfoChar(&cmdbuf, ch);
	}

	if (ch == 0)
	{
		memset(errbuf, 0, GEN_LEN + 1);
		snprintf(errbuf, GEN_LEN, "%s", "timed out waiting for version string");
		ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("PG_GNUPLOT : error while executing command [%s]", tmpbuf),
				 errdetail("%s", errbuf)));
		PG_RETURN_CSTRING(errbuf);
		/*
		 * We are intentionally leaving the fp open in this case
		 * because pclose will block forever waiting for the process to end
		 * which will never end because its waiting for input from stdin
		 * We cannot write Ctrl+D on a readable fp to end it
		 * But this is essential to make sure we do not get stuck forever
		 * Killing it is an option but that seems going too far trying to
		 * handle an error that is very rare.
		 * Nevertheless this is an error situation that will come only
		 * in exceptional circumstances e.g. where -V option in gnuplot gets
		 * deprecated in a future version and some one uses this extension with
		 * the new gnuplot version.
		 * In this case instead of gnuplot printing the version and exiting
		 * will start as if some one tried running it normally.
		 * It will just say you have used an invalid command line option
		 * while running it.
		 */
	}

	pclose(fp);

	if (i == 0 && popen_err > 0)
	{
		memset(errbuf, 0, GEN_LEN + 1);
		snprintf(errbuf, GEN_LEN, "%s", strerror(popen_err));
		ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("PG_GNUPLOT : popen could not execute command [%s]", tmpbuf),
				 errdetail("%s", errbuf)));
		PG_RETURN_CSTRING(errbuf);
	}

	if (g_fp == NULL)
	{
		i = strlen(tmpbuf);
		tmpbuf[i - 3] = '\0';
		g_fp = popen(tmpbuf, "w");
		if (g_fp == NULL)
		{
			ereport(ERROR, (errmsg("PG_GNUPLOT : popen could not execute command [%s]"), tmpbuf));
			PG_RETURN_CSTRING(tmpbuf);
		}
	}

	PG_RETURN_CSTRING(cmdbuf.data);
}

/*
 * This function takes two arguments
 * 1. A select query that is supposed to return any number of columns.
 *    This parameter is optional. It cane be passed as empty string
 *    but not NULL.
 * 2. A plot command that is supposed to plot the results returned
 *    by the query provided in the first argument.
 * The function first checks if the caller has provided both the query
 * and the plot command or not.
 * If only the plot command is provided then the query is not run
 * just the plot command is passed on to gnuplot.
 * The function checks that if the command is quit, then it closes
 * the global file pointer. Subsequent calls to pg_plot will need 
 * gnuplot_version to be called first.
 * If both the query and the plot command is provided then the function
 * first runs plot command and then runs the query.
 * The plot command must be such that it makes gnuplot to expect the data
 * from stdin. i.e. the plot command must use '-' as file name
 * which instructs gnuplot to expect data from stdin.
 * It then gathers the results of the query row by row and sends them over
 * to gnuplot seperating each column by spaces and each row is terminated
 * by \n. When the rows are finished then the function sends e\n to gnuplot
 * to ask it to do plotting.
 */
Datum
pg_plot(PG_FUNCTION_ARGS)
{
	text			*db_qry = PG_GETARG_TEXT_PP(0);
	int				db_qry_len = 0;
	char			*db_query;
	text			*p_cmd = PG_GETARG_TEXT_PP(1);
	int				plot_cmd_len = 0;
	char			*plot_cmd;
	int				row_count = 0;
	int				i, j, k;
	int				ret, cmds;

	if (g_fp == NULL)
	{
		ereport(ERROR, (errmsg("PG_GNUPLOT : gnuplot_version must be called before issuing pg_plot")));
		PG_RETURN_INT32(0);
	}

	if (p_cmd == NULL)
	{
		ereport(ERROR, (errmsg("PG_GNUPLOT : the plot command cannot be empty")));
		PG_RETURN_INT32(0);
	}

	plot_cmd = text_to_cstring(p_cmd);

	plot_cmd_len = strlen(plot_cmd);
	if (plot_cmd_len < 4)
	{
		ereport(ERROR, (errmsg("PG_GNUPLOT : the plot command is invalid")));
		PG_RETURN_INT32(0);
	}

	if (db_qry != NULL)
	{
		db_query = text_to_cstring(db_qry);
		db_qry_len = strlen(db_query);
	}

	fprintf(g_fp, "%s\n", plot_cmd);
	fflush(g_fp);

	ereport(LOG, (errmsg("PG_GNUPLOT : plot command [%s] sent to gnuplot", plot_cmd)));

	if (db_qry == NULL || db_qry_len < 1)
	{
		if (strcmp(plot_cmd, "quit") == 0)
		{
			pclose(g_fp);
			g_fp = NULL;
		}
		ereport(LOG, (errmsg("PG_GNUPLOT : plot command done")));
		PG_RETURN_INT32(1);
	}

	/*
	 * Connect to the SPI manager.
	 */
	if ((ret = SPI_connect()) != SPI_OK_CONNECT)
	{
		fprintf(g_fp, "e\n");
		fflush(g_fp);

		ereport(ERROR, (errmsg("PG_GNUPLOT : cannot connect to the database server")));
		PG_RETURN_INT32(0);
	}

	/*
	 * Create an active snapshot.
	 */
	PushActiveSnapshot(GetTransactionSnapshot());

	/*
	 * Run the query.
	 */
	ret = SPI_execute(db_query, false, 0);

	/*
	 * Get the value returned by the query
	 */
	row_count = SPI_processed;

	if (ret != SPI_OK_SELECT || SPI_tuptable == NULL)
	{
		fprintf(g_fp, "e\n");
		fflush(g_fp);

		SPI_finish();
		ereport(ERROR, (errmsg("PG_GNUPLOT : invalid query results")));
		PG_RETURN_INT32(0);
	}

	ereport(LOG, (errmsg("PG_GNUPLOT : going to send %d rows to gnuplot", row_count)));

	TupleDesc tupdesc = SPI_tuptable->tupdesc;

	cmds = count_cmds(plot_cmd);

	for (i = 0; i < cmds; i++)
	{
		for (j = 0; j < row_count; j++)
		{
			HeapTuple tuple = SPI_tuptable->vals[j];
			for (k = 0; k < SPI_tuptable->tupdesc->natts - 1; k++)
			{
				fprintf(g_fp, "%s  ", SPI_getvalue(tuple, tupdesc, k + 1));
			}
			fprintf(g_fp, "%s\n", SPI_getvalue(tuple, tupdesc, k + 1));
			fflush(g_fp);

			if ( j > 0 && (j % 10000) == 0)
				ereport(LOG, (errmsg("PG_GNUPLOT : sent 10000 more rows to gnuplot")));
		}

		/*
		 * End of terminal input for gnuplot for one block
		 */
		fprintf(g_fp, "e\n");
		fflush(g_fp);
	}
	/*
	 * Finish the transaction.
	 */
	SPI_finish();
	PopActiveSnapshot();

	ereport(LOG, (errmsg("PG_GNUPLOT : plot command finished")));

	PG_RETURN_INT32(1);
}

char
read_stdin(FILE *fp)
{
	struct timeval timeout = {2, 0};
	fd_set fds;
	char ch;
	int ret;

	FD_ZERO(&fds);
	FD_SET(fileno(fp), &fds);
	ret = select(fileno(fp) + 1, &fds, NULL, NULL, &timeout);
	
	if (ret == 0) {
		return 0;
	} else if (ret == -1) {
		return 0;
	} else {
		ch = fgetc(fp);
	}
	return ch;
}

int
count_cmds(char *plot_cmds)
{
	char *p;
	int count = 0;

	if (plot_cmds == NULL)
		return 0;

	for (p = plot_cmds; ( p = strstr( p, "'-'" ) ) != NULL; ++p )
	{
    	count++;
	}
	return count;
}

