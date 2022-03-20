MODULE_big = pg_gnuplot
PG_CPPFLAGS = -I$(libpq_srcdir) -w

SHLIB_LINK = $(libpq)

OBJS = pg_gnuplot.o

EXTENSION = pg_gnuplot
DATA = pg_gnuplot--1.0.sql

ifdef USE_PGXS
PGXS := $(shell pg_config --pgxs)
include $(PGXS)
else
subdir = contrib/pg_gnuplot
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif
