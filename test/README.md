# Testing the Foreign Data Wraper

## Source Of The Cassandra Docker Images

The Cassandra Docker images are available at [cassandra|Docker Hub](https://hub.docker.com/_/cassandra/).

## Source Of The PostgreSQL Docker Images

The Cassandra Docker images are available at [PostgreSQL|Docker Hub](https://hub.docker.com/_/postgres/).

## Test Scipts

### build.cfg

Contains the configuration of all parameters used in the tests.

#### Parameters

- POSTGRESQL_VERSIONS: An array with all PostgreSQL version numbers, that should be tested against
- POSTGRESQL_HOST: The host where PostgreSQL connections are opened: _localhost_
- POSTGRESQL_PORT: The port where PostgreSQL is listening: _5432_
- POSTGRESQL_USER: The user, that is used to connect to PostgreSQL: fdw_tester
- POSTGRESQL_PWD: The password, which is used to create the user in **POSTGRESQL_USER**
- POSTGRESQL_DB1: Default database: _postgres_
- POSTGRESQL_DB2: Test database
- PostgreSQL_RESULT_VERSION: The PostgreSQL version of the database, where the query results will be stored: _15_
- POSTGRESQL_RESULT_DB: The PostgreSQL database, where the query results will be stored: _results_
- POSTGRESQL_RESULT_SCHEMA: The schema inside the PostgreSQL database, where the query results will be stored: _result_data_
- CASSANDRA_PORT: The port where Cassandra is listening: _9042_
- CASSANDRA_VERSIONS: An array with all Cassandra version numbers, that should be tested against
- DOCKER_NETWORK: The network, that is used by all test containers

### test.sh

This script executes all tests.

```bash
# Execute the tests
./test.sh
```

### setup_img.sh

The script creates all Docker images as configured in [build.cfg](#buildcfg). Within the PostgreSQL container the packages are updated and all needed packages will be installed, that are needed to compile the foreign data wrapper. It will compile the datastax Cassandra lib, that is needed to compile the extension.<br />
The installation runs in all PostgreSQL containers.

### cassandra.sh

The scripts to initialize the Cassandra data for every Container created in setup_img.sh and configured in [build.cfg](#buildcfg).

Within a loop for every Cassandra container configured, it will call the [postgresql.sh](#postgresqlsh).

### postgresql.sh

The script creates a database in a PostgreSQL container and adds all objects to access the Cassandra container given as IP address as parameter within [cassandra.sh](#cassandrash).

### cleanup.sh

The script stops and deletes all containers configured in [build.cfg](#buildcfg). It can also delete the Docker network, where all containers are running with.

By default the result container is not removed. The shell script interprets **1** given as parameter as to remove the container with the result database, too.<br />
As there are no contianers left when started with **1**, the docker network will be removed, too.

This can be achieved by calling this script manually:

```bash
# Remove all containers used for testing and the network
./cleanup.sh 1
```

## Checking Test Results

The test results are stored in a container with PostgreSQL as database.

For every test the following data is stored:

- created: TIMESTAMP WITH TIME ZONE: Execution timestamp
- postgresql_version: TEXT: Name of the PostgreSQL container
- cassandra_version: TEXT: Name of the Cassandra container
- test_description: TEXT: Name/description of the test
- test_result: TEXT: Data read from the Cassandra database, fields are concatinated, separator is pipe

```sql
-- Count of tests for each version
SELECT postgresql_version
    , cassandra_version
    , count(*) AS count_of_tests
FROM result_data.results
GROUP BY postgresql_version
    , cassandra_version
ORDER BY postgresql_version
    , cassandra_version
;
```
