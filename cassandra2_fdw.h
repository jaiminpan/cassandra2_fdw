/*-------------------------------------------------------------------------
 *
 * cassandra2_fdw.h
 *
 * Copyright (c) 2015 Jaimin Pan
 *
 * This software is released under the PostgreSQL Licence
 *
 * Author: Jaimin Pan <jaimin.pan@gmail.com>
 *
 * IDENTIFICATION
 *		cassandra2_fdw/cassandra2_fdw.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef CASSANDRA2_FDW_H_
#define CASSANDRA2_FDW_H_

#include <cassandra.h>

#include "foreign/foreign.h"
#include "lib/stringinfo.h"
#include "nodes/pathnodes.h"
#include "utils/rel.h"

/* in cass_connection.c */
extern CassSession *pgcass_GetConnection(ForeignServer *server, UserMapping *user,
			  bool will_prep_stmt);
extern void pgcass_ReleaseConnection(CassSession *session);

extern void pgcass_report_error(int elevel, CassFuture* result_future,
				bool clear, const char *sql);

#endif /* CASSANDRA2_FDW_H_ */
