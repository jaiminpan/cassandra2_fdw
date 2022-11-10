cassandra2_fdw
==============
[![Lang](https://img.shields.io/badge/Language-C%2FC%2B%2B-green.svg)]()
[![MIT](https://img.shields.io/badge/License-MIT-green.svg)]()
[![Extension](https://img.shields.io/badge/Extension-PostgreSQL-green.svg)]()

Foreign Data Wrapper (FDW) that facilitates access to Cassandra 2.x, 3.x, and 4.x from within PostgreSQL.

Only functions for reading data originated in Cassandra have been implemented. Writing data back to Cassandra is not supported.

Cassandra: http://cassandra.apache.org/

__PostgreSQL Support__:  
[![version](https://img.shields.io/badge/PostgreSQL-11-blue.svg)]()
[![version](https://img.shields.io/badge/PostgreSQL-12-blue.svg)]()
[![version](https://img.shields.io/badge/PostgreSQL-13-blue.svg)]()
[![version](https://img.shields.io/badge/PostgreSQL-14-blue.svg)]()
[![version](https://img.shields.io/badge/PostgreSQL-15-blue.svg)]()

__Cassandra Support__:  
[![version](https://img.shields.io/badge/Cassandra-3.0-blue.svg)]()
[![version](https://img.shields.io/badge/Cassandra-3.1-blue.svg)]()
[![version](https://img.shields.io/badge/Cassandra-4.0-blue.svg)]()

## Prepare

### Cassandra CPP driver

Firstly, the latest Cassandra2 cpp driver needs be installed from https://github.com/datastax/cpp-driver.

### PostgreSQL Header

You'll need the following packages to be installed with either, apt, or rpm, depending on your Linux distribution

- postgresql-server
- postgresql-contrib
- postgresql-server-dev-{version number}

## Build

```bash
# Clone the repository from GitHub
git clone https://github.com/jaiminpan/cassandra2_fdw

cd cassandra2_fdw
USE_PGXS=1 make
sudo USE_PGXS=1 make install 
```

## Usage

The following parameters can be set on a Cassandra foreign server object:

  * **`host`**: the address or host name of the Cassandra server, Examples: "127.0.0.1" "127.0.0.1,127.0.0.2", "server1.domain.com"  
  * **`port`**: the port number of the Cassandra server. Defaults is 9042  
  * **`protocol`**: the protocol connect to Cassandra server. Defaults depends on cassandra-cpp-driver. (Default is "2" of v2.1.0 and default is "4" of v2.2.2)   

The following parameters can be set on a Cassandra foreign table object:

  * **`schema_name`**: the name of the Cassandra keyspace to query. Defaults to public  
  * **`table_name`**: the name of the Cassandra table to query. Defaults to the foreign table name used in the relevant CREATE command  

Here is an example
```sql
	-- Create the extension inside a database
	CREATE EXTENSION cassandra2_fdw;

	-- Create the server object
	CREATE SERVER cass_serv FOREIGN DATA WRAPPER cassandra2_fdw 
		OPTIONS(host '127.0.0.1,127.0.0.2', port '9042', protocol '2');

	-- Create a user mapping for the server
	CREATE USER MAPPING FOR public SERVER cass_serv OPTIONS(username 'test', password 'test');


	-- Create a foreign table on the server
	CREATE FOREIGN TABLE test (id int) SERVER cass_serv OPTIONS (schema_name 'example', table_name 'order');

	-- Query the foreign table
	SELECT * FROM test limit 5;
```

## Supported Data Types

| Cassandra | PostgreSQL                                           |
| --------- | :--------------------------------------------------- |
| tynint    | smallint                                             |
| smallint  | smallint                                             |
| int       | integer                                              |
| bigint    | bigint                                               |
| boolean   | boolean                                              |
| float     | float                                                |
| double    | double precision                                     |
| timstamp  | bigint (documented int the cpp-driver documentation) |
| text      | text                                                 |
| ascii     | text                                                 |
| varchar   | text                                                 |
| set       | text                                                 |
| uuid      | uuid                                                 |

## Unsupported Data Types

Unsupported data types will return an info text, that the data type is not supported instead of data.

- list: Not implemented in this FDW
- map: Not implemented in this FDW
- user defined types: Not implemented in the cpp-driver

## Test

With  cpp-driver 2.16.0 against mentioned PostgreSQL versions and against Cassandra 3.2.

## Authors

- [Jaimin Pan](mailto:jaimin.pan@gmail.com)
- [Stefanie Janine Stölting](mailto:stefanie@ProOpenSoucre.eu)
