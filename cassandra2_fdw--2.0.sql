/*-------------------------------------------------------------------------
 *
 * Copyright (c) 2014, Open Source Consulting Group
 *
 *-------------------------------------------------------------------------
 */

CREATE FUNCTION cassandra2_fdw_handler()
RETURNS fdw_handler
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

CREATE FUNCTION cassandra2_fdw_validator(text[], oid)
RETURNS void
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

CREATE FOREIGN DATA WRAPPER cassandra2_fdw
  HANDLER cassandra2_fdw_handler
  VALIDATOR cassandra2_fdw_validator;
