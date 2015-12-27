cassandra2_fdw
==============

Foreign Data Wrapper (FDW) that facilitates access to Cassandra 2.x from within PostgreSQL 9.4.

Cassandra: http://cassandra.apache.org/

## Prepare

Firstly, Cassandra2 cpp driver 2.1.0+ need be installed (https://github.com/datastax/cpp-driver).

## Build

```
git clone https://github.com/jaiminpan/cassandra2_fdw

cd cassandra2_fdw
USE_PGXS=1 make
USE_PGXS=1 make install 
# if got error when doing "USE_PGXS=1 make install"
# try "sudo USE_PGXS=1 make install"
```

## Example

*) Enter psql & Set up cassandra_fdw extension.

	$ psql

	CREATE EXTENSION cassandra2_fdw;

	CREATE SERVER cass_serv FOREIGN DATA WRAPPER cassandra2_fdw 
		OPTIONS(url 'localhost:9160');


*) Create a user mapping for the server.

	CREATE USER MAPPING FOR public SERVER cass_serv OPTIONS(username 'test', password 'test');


*) Create a foreign table on the server.

	CREATE FOREIGN TABLE test (id int) SERVER cass_serv OPTIONS (table 'example.oorder');


*) Query the foreign table.

	SELECT * FROM test limit 5;

## Test

It's test using cpp driver(2.1.0) and Cassandra(2.1.2).

## Author

Jaimin Pan jaimin.pan@gmail.com
