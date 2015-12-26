# contrib/cassandra2_fdw/Makefile

MODULE_big = cassandra2_fdw
OBJS = cassandra2_fdw.o cass_connection.o

SHLIB_LINK = -lcassandra

EXTENSION = cassandra2_fdw
DATA = cassandra2_fdw--1.0.sql

REGRESS = cassandra2_fdw

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/cassandra2_fdw
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif
