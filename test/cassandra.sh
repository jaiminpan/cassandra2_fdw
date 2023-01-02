#!/bin/bash

# Set variables
source ./build.cfg

for CASSANDRA_VERSION in "${CASSANDRA_VERSIONS[@]}"
do
    # Set container name and image
    CASSANDRA_CONTAINER="Cassandra_${CASSANDRA_VERSION}_N"
    CASSANDRA_CONTAINER=${CASSANDRA_CONTAINER/./_}

    echo "Creating keyspace and tables in $CASSANDRA_CONTAINER"

    # Drop keyspace if exists to start from scratch
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --execute="drop keyspace if exists cassandra_fdw;"

    # Create the cassandra_fdw keyspace
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --execute="CREATE KEYSPACE cassandra_fdw WITH replication = {'class':'SimpleStrategy', 'replication_factor': '1'} AND durable_writes = true;"

    # Create a tables
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="CREATE TABLE fdw_test_int(id int, some_text text, PRIMARY KEY (id));"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="CREATE TABLE fdw_test_bigint(id bigint, some_text text, PRIMARY KEY (id));"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="CREATE TABLE fdw_test_smallint(id smallint, some_text text, PRIMARY KEY (id));"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="CREATE TABLE fdw_test_tinyint(id tinyint, some_text text, PRIMARY KEY (id));"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="CREATE TABLE fdw_test_boolean(id boolean, some_text text, PRIMARY KEY (id));"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="CREATE TABLE fdw_test_float(id float, some_text text, PRIMARY KEY (id));"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="CREATE TABLE fdw_test_double(id double, some_text text, PRIMARY KEY (id));"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="CREATE TABLE fdw_test_uuid(id uuid, some_text text, PRIMARY KEY (id));"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="CREATE TABLE fdw_test_ascii(id uuid, some_text ascii, PRIMARY KEY (id));"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="CREATE TABLE fdw_test_varchar(id uuid, some_text varchar, PRIMARY KEY (id));"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="CREATE TABLE fdw_test_set(id uuid, some_text set<text> , PRIMARY KEY (id));"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="CREATE TABLE fdw_test_timestamp(id uuid, some_timestamp timestamp, PRIMARY KEY (id));"

    echo "Inserting data into tables in $CASSANDRA_CONTAINER"

    # Insert some records
    # bigint
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_bigint(id, some_text) VALUES (1, 'test 1');"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_bigint(id, some_text) VALUES (2, 'test 2');"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_bigint(id, some_text) VALUES (3, 'test 3');"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_bigint(id, some_text) VALUES (4, 'test 4');"

    # integer
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_int(id, some_text) VALUES (1, 'test 1');"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_int(id, some_text) VALUES (2, 'test 2');"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_int(id, some_text) VALUES (3, 'test 3');"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_int(id, some_text) VALUES (4, 'test 4');"

    # smallint
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_smallint(id, some_text) VALUES (1, 'test 1');"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_smallint(id, some_text) VALUES (2, 'test 2');"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_smallint(id, some_text) VALUES (3, 'test 3');"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_smallint(id, some_text) VALUES (4, 'test 4');"

    # tinyint
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_tinyint(id, some_text) VALUES (1, 'test 1');"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_tinyint(id, some_text) VALUES (2, 'test 2');"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_tinyint(id, some_text) VALUES (3, 'test 3');"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_tinyint(id, some_text) VALUES (4, 'test 4');"

    # boolean
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_boolean(id, some_text) VALUES (true, 'test 1');"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_boolean(id, some_text) VALUES (false, 'test 2');"

    # float
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_float(id, some_text) VALUES (1.1, 'test 1');"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_float(id, some_text) VALUES (2e10, 'test 2');"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_float(id, some_text) VALUES (3, 'test 3');"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_float(id, some_text) VALUES (4, 'test 4');"

    # double
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_double(id, some_text) VALUES (1.1, 'test 1');"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_double(id, some_text) VALUES (2e10, 'test 2');"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_double(id, some_text) VALUES (3, 'test 3');"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_double(id, some_text) VALUES (4, 'test 4');"

    # uuid
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_uuid(id, some_text) VALUES (uuid(), 'test 1');"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_uuid(id, some_text) VALUES (uuid(), 'test 2');"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_uuid(id, some_text) VALUES (uuid(), 'test 3');"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_uuid(id, some_text) VALUES (uuid(), 'test 4');"

    # ascii
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_ascii(id, some_text) VALUES (uuid(), 'test 1');"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_ascii(id, some_text) VALUES (uuid(), 'test 2');"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_ascii(id, some_text) VALUES (uuid(), 'test 3');"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_ascii(id, some_text) VALUES (uuid(), 'test 4');"

    # varchar
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_varchar(id, some_text) VALUES (uuid(), 'test 1');"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_varchar(id, some_text) VALUES (uuid(), 'test 2');"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_varchar(id, some_text) VALUES (uuid(), 'test 3');"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_varchar(id, some_text) VALUES (uuid(), 'test 4');"

    # timestamp
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_timestamp(id, some_timestamp) VALUES (uuid(), toTimestamp(now()));"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_timestamp(id, some_timestamp) VALUES (uuid(), toTimestamp(now()));"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_timestamp(id, some_timestamp) VALUES (uuid(), toTimestamp(now()));"
    sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --keyspace=cassandra_fdw --execute="INSERT INTO fdw_test_timestamp(id, some_timestamp) VALUES (uuid(), toTimestamp(now()));"

    # Getting IP address of the Cassandra server
    CASSANDRA_HOST=$(sudo docker exec $CASSANDRA_CONTAINER hostname -i)

    # Starting tests in PostgreSQL
    echo "Starting tests in PostgreSQL container with Cassandra using $CASSANDRA_HOST"
    ./postgresql.sh $CASSANDRA_HOST $CASSANDRA_CONTAINER

    # Drop keyspace
    #sudo docker exec -it $CASSANDRA_CONTAINER cqlsh --execute="drop keyspace if exists cassandra_fdw;"

done
