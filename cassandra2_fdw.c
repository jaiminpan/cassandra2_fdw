/*-------------------------------------------------------------------------
 *
 * Cassandra2 Foreign Data Wrapper for PostgreSQL
 *
 * Copyright (c) 2015 Jaimin Pan
 *
 * This software is released under the PostgreSQL Licence
 *
 * Author: Jaimin Pan <jaimin.pan@gmail.com>
 *
 * IDENTIFICATION
 *		cassandra2_fdw/cassandra2_fdw.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

PG_MODULE_MAGIC;

/*
 * SQL functions
 */
extern Datum cassandra2_fdw_handler(PG_FUNCTION_ARGS);
extern Datum cassandra2_fdw_validator(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(cassandra2_fdw_handler);
PG_FUNCTION_INFO_V1(cassandra2_fdw_validator);


Datum
cassandra2_fdw_handler(PG_FUNCTION_ARGS)
{
	FdwRoutine *fdwroutine = makeNode(FdwRoutine);

	PG_RETURN_POINTER(fdwroutine);
}

Datum
cassandra2_fdw_validator(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID();
}
