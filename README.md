cassandra2_fdw
==============

Foreign Data Wrapper (FDW) that facilitates access to Cassandra 2.x from within PostgreSQL 9.4.

Before install this extension, Cassandra2 cpp driver need be installed (https://github.com/datastax/cpp-driver).

After install:
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
