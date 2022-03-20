
-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pg_gnuplot" to load this file. \quit

CREATE OR REPLACE FUNCTION pg_plot(db_query pg_catalog.text,
									plot_cmd pg_catalog.text)
    RETURNS pg_catalog.int4 STRICT
	AS 'MODULE_PATHNAME' LANGUAGE C;

CREATE OR REPLACE FUNCTION pg_gnuplot_version()
    RETURNS pg_catalog.int4 STRICT
	AS 'MODULE_PATHNAME' LANGUAGE C;

CREATE OR REPLACE FUNCTION gnuplot_version()
    RETURNS cstring
	AS 'MODULE_PATHNAME' LANGUAGE C;
