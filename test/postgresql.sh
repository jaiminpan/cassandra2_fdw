#!/bin/bash

# Set variables
source ./build.cfg

# Get the IP address of the Cassandra server
if [ -z "$1" ]; then
    echo "No Cassandra host IP address passed, will exit now"
    exit 1
else
    CASSANDRA_HOST=$1
fi

# Get the container name of the Cassandra server
if [ -z "$2" ]; then
    echo "No Cassandra container name passed, will exit now"
    exit 1
else
    CASSANDRA_CONTAINER=$2
fi


for PG_VERSION in "${POSTGRESQL_VERSIONS[@]}"
do
    # Set container names and PostgreSQL imagest
    POSTGRESQL_CONTAINER="PostgreSQL_${PG_VERSION}_N"
    POSTGRESQL_RESULT_CONTAINER="PostgreSQL_RESULT_${PostgreSQL_RESULT_VERSION}_N"
    POSTGRESQL_RESULT_HOST=$(sudo docker exec $POSTGRESQL_RESULT_CONTAINER hostname -i)


    echo "Starting PostgreSQL tests with container $POSTGRESQL_CONTAINER against Cassandra container with IP address $CASSANDRA_HOST"

    # Show PostgreSQL version
    sudo docker exec -it $POSTGRESQL_CONTAINER psql --host=$POSTGRESQL_HOST --port=$POSTGRESQL_PORT --username=$POSTGRESQL_USER $POSTGRESQL_DB1 --command "SELECT version();"

    # Database creation
    sudo docker exec -it $POSTGRESQL_CONTAINER psql --host=$POSTGRESQL_HOST --port=$POSTGRESQL_PORT --username=$POSTGRESQL_USER $POSTGRESQL_DB1 --command "DROP DATABASE IF EXISTS $POSTGRESQL_DB2;"
    sudo docker exec -it $POSTGRESQL_CONTAINER psql --host=$POSTGRESQL_HOST --port=$POSTGRESQL_PORT --username=$POSTGRESQL_USER $POSTGRESQL_DB1 --command "CREATE DATABASE $POSTGRESQL_DB2;"

    # Foreign data wrapper
    ## PostgreSQL result server
    echo "Creating foreign connection to the PostgreSQL result container $POSTGRESQL_RESULT_CONTAINER with IP address $POSTGRESQL_RESULT_HOST"
    sudo docker exec -it $POSTGRESQL_CONTAINER psql --host=$POSTGRESQL_HOST --port=$POSTGRESQL_PORT --username=$POSTGRESQL_USER $POSTGRESQL_DB2 --command "CREATE EXTENSION postgres_fdw;"

    sudo docker exec -it $POSTGRESQL_CONTAINER psql --host=$POSTGRESQL_HOST --port=$POSTGRESQL_PORT --username=$POSTGRESQL_USER $POSTGRESQL_DB2 --command "CREATE SERVER result_serv FOREIGN DATA WRAPPER postgres_fdw OPTIONS (host '$POSTGRESQL_RESULT_HOST', port '$POSTGRESQL_PORT', dbname '$POSTGRESQL_RESULT_DB');"

    sudo docker exec -it $POSTGRESQL_CONTAINER psql --host=$POSTGRESQL_HOST --port=$POSTGRESQL_PORT --username=$POSTGRESQL_USER $POSTGRESQL_DB2 --command "CREATE USER MAPPING FOR $POSTGRESQL_USER server result_serv options (user '$POSTGRESQL_USER', password '$POSTGRESQL_PWD');"

    ## Create a schema for the foreign tables
    sudo docker exec -it $POSTGRESQL_CONTAINER psql --host=$POSTGRESQL_HOST --port=$POSTGRESQL_PORT --username=$POSTGRESQL_USER $POSTGRESQL_DB2 --command "CREATE SCHEMA $POSTGRESQL_RESULT_SCHEMA;"

    # Create foreign table for results
    sudo docker exec -it $POSTGRESQL_CONTAINER psql --host=$POSTGRESQL_HOST --port=$POSTGRESQL_PORT --username=$POSTGRESQL_USER $POSTGRESQL_DB2 --command "CREATE FOREIGN TABLE $POSTGRESQL_RESULT_SCHEMA.fdw_test_results(created TIMESTAMP WITH TIME ZONE, postgresql_version TEXT, cassandra_version TEXT, test_description TEXT, test_result TEXT) server result_serv options (schema_name '$POSTGRESQL_RESULT_SCHEMA', table_name 'results');"

    ## Current Cassandra server

    sudo docker exec -it $POSTGRESQL_CONTAINER psql --host=$POSTGRESQL_HOST --port=$POSTGRESQL_PORT --username=$POSTGRESQL_USER $POSTGRESQL_DB2 --command "CREATE EXTENSION cassandra2_fdw;"

    sudo docker exec -it $POSTGRESQL_CONTAINER psql --host=$POSTGRESQL_HOST --port=$POSTGRESQL_PORT --username=$POSTGRESQL_USER $POSTGRESQL_DB2 --command "CREATE SERVER cass_serv FOREIGN DATA WRAPPER cassandra2_fdw OPTIONS (host '$CASSANDRA_HOST', port '$CASSANDRA_PORT');"

    sudo docker exec -it $POSTGRESQL_CONTAINER psql --host=$POSTGRESQL_HOST --port=$POSTGRESQL_PORT --username=$POSTGRESQL_USER $POSTGRESQL_DB2 --command "CREATE USER MAPPING FOR $POSTGRESQL_USER server cass_serv options (username '', password '');"

    # Create a schema for the foreign tables
    sudo docker exec -it $POSTGRESQL_CONTAINER psql --host=$POSTGRESQL_HOST --port=$POSTGRESQL_PORT --username=$POSTGRESQL_USER $POSTGRESQL_DB2 --command "CREATE SCHEMA cassandra;"

    # Create foreign tables
    sudo docker exec -it $POSTGRESQL_CONTAINER psql --host=$POSTGRESQL_HOST --port=$POSTGRESQL_PORT --username=$POSTGRESQL_USER $POSTGRESQL_DB2 --command "CREATE FOREIGN TABLE cassandra.fdw_test_bigint(id bigint, some_text text) server cass_serv options (schema_name 'cassandra_fdw', table_name 'fdw_test_bigint');"
    sudo docker exec -it $POSTGRESQL_CONTAINER psql --host=$POSTGRESQL_HOST --port=$POSTGRESQL_PORT --username=$POSTGRESQL_USER $POSTGRESQL_DB2 --command "CREATE FOREIGN TABLE cassandra.fdw_test_int(id int, some_text text) server cass_serv options (schema_name 'cassandra_fdw', table_name 'fdw_test_int');"
    sudo docker exec -it $POSTGRESQL_CONTAINER psql --host=$POSTGRESQL_HOST --port=$POSTGRESQL_PORT --username=$POSTGRESQL_USER $POSTGRESQL_DB2 --command "CREATE FOREIGN TABLE cassandra.fdw_test_smallint(id smallint, some_text text) server cass_serv options (schema_name 'cassandra_fdw', table_name 'fdw_test_smallint');"
    sudo docker exec -it $POSTGRESQL_CONTAINER psql --host=$POSTGRESQL_HOST --port=$POSTGRESQL_PORT --username=$POSTGRESQL_USER $POSTGRESQL_DB2 --command "CREATE FOREIGN TABLE cassandra.fdw_test_tinyint(id smallint, some_text text) server cass_serv options (schema_name 'cassandra_fdw', table_name 'fdw_test_smallint');"
    sudo docker exec -it $POSTGRESQL_CONTAINER psql --host=$POSTGRESQL_HOST --port=$POSTGRESQL_PORT --username=$POSTGRESQL_USER $POSTGRESQL_DB2 --command "CREATE FOREIGN TABLE cassandra.fdw_test_boolean(id boolean, some_text text) server cass_serv options (schema_name 'cassandra_fdw', table_name 'fdw_test_boolean');"
    sudo docker exec -it $POSTGRESQL_CONTAINER psql --host=$POSTGRESQL_HOST --port=$POSTGRESQL_PORT --username=$POSTGRESQL_USER $POSTGRESQL_DB2 --command "CREATE FOREIGN TABLE cassandra.fdw_test_float(id float, some_text text) server cass_serv options (schema_name 'cassandra_fdw', table_name 'fdw_test_float');"
    sudo docker exec -it $POSTGRESQL_CONTAINER psql --host=$POSTGRESQL_HOST --port=$POSTGRESQL_PORT --username=$POSTGRESQL_USER $POSTGRESQL_DB2 --command "CREATE FOREIGN TABLE cassandra.fdw_test_double(id double precision, some_text text) server cass_serv options (schema_name 'cassandra_fdw', table_name 'fdw_test_double');"
    sudo docker exec -it $POSTGRESQL_CONTAINER psql --host=$POSTGRESQL_HOST --port=$POSTGRESQL_PORT --username=$POSTGRESQL_USER $POSTGRESQL_DB2 --command "CREATE FOREIGN TABLE cassandra.fdw_test_uuid(id uuid, some_text text) server cass_serv options (schema_name 'cassandra_fdw', table_name 'fdw_test_uuid');"
    sudo docker exec -it $POSTGRESQL_CONTAINER psql --host=$POSTGRESQL_HOST --port=$POSTGRESQL_PORT --username=$POSTGRESQL_USER $POSTGRESQL_DB2 --command "CREATE FOREIGN TABLE cassandra.fdw_test_ascii(id uuid, some_text text) server cass_serv options (schema_name 'cassandra_fdw', table_name 'fdw_test_ascii');"
    sudo docker exec -it $POSTGRESQL_CONTAINER psql --host=$POSTGRESQL_HOST --port=$POSTGRESQL_PORT --username=$POSTGRESQL_USER $POSTGRESQL_DB2 --command "CREATE FOREIGN TABLE cassandra.fdw_test_varchar(id uuid, some_text text) server cass_serv options (schema_name 'cassandra_fdw', table_name 'fdw_test_varchar');"
    sudo docker exec -it $POSTGRESQL_CONTAINER psql --host=$POSTGRESQL_HOST --port=$POSTGRESQL_PORT --username=$POSTGRESQL_USER $POSTGRESQL_DB2 --command "CREATE FOREIGN TABLE cassandra.fdw_test_set(id uuid, some_text text) server cass_serv options (schema_name 'cassandra_fdw', table_name 'fdw_test_set');"
    sudo docker exec -it $POSTGRESQL_CONTAINER psql --host=$POSTGRESQL_HOST --port=$POSTGRESQL_PORT --username=$POSTGRESQL_USER $POSTGRESQL_DB2 --command "CREATE FOREIGN TABLE cassandra.fdw_test_timestamp(id uuid, some_timestamp bigint) server cass_serv options (schema_name 'cassandra_fdw', table_name 'fdw_test_timestamp');"

    # Test by running a queries
    echo "Testing bigint"
    sudo docker exec -it $POSTGRESQL_CONTAINER psql --host=$POSTGRESQL_HOST --port=$POSTGRESQL_PORT --username=$POSTGRESQL_USER $POSTGRESQL_DB2 --command "INSERT INTO $POSTGRESQL_RESULT_SCHEMA.fdw_test_results(created, postgresql_version, cassandra_version, test_description, test_result) SELECT current_timestamp, '$POSTGRESQL_CONTAINER', '$CASSANDRA_CONTAINER', 'Testing bigint', id::text || '|' || some_text from cassandra.fdw_test_bigint;"

    echo "Testing integer"
    sudo docker exec -it $POSTGRESQL_CONTAINER psql --host=$POSTGRESQL_HOST --port=$POSTGRESQL_PORT --username=$POSTGRESQL_USER $POSTGRESQL_DB2 --command "INSERT INTO $POSTGRESQL_RESULT_SCHEMA.fdw_test_results(created, postgresql_version, cassandra_version, test_description, test_result) SELECT current_timestamp, '$POSTGRESQL_CONTAINER', '$CASSANDRA_CONTAINER', 'Testing integer', id::text || '|' || some_text from cassandra.fdw_test_int;"
    
    echo "Testing smallint"
    sudo docker exec -it $POSTGRESQL_CONTAINER psql --host=$POSTGRESQL_HOST --port=$POSTGRESQL_PORT --username=$POSTGRESQL_USER $POSTGRESQL_DB2 --command "INSERT INTO $POSTGRESQL_RESULT_SCHEMA.fdw_test_results(created, postgresql_version, cassandra_version, test_description, test_result) SELECT current_timestamp, '$POSTGRESQL_CONTAINER', '$CASSANDRA_CONTAINER', 'Testing smallint', id::text || '|' || some_text from cassandra.fdw_test_smallint;"

    echo "Testing tinyint"
    sudo docker exec -it $POSTGRESQL_CONTAINER psql --host=$POSTGRESQL_HOST --port=$POSTGRESQL_PORT --username=$POSTGRESQL_USER $POSTGRESQL_DB2 --command "INSERT INTO $POSTGRESQL_RESULT_SCHEMA.fdw_test_results(created, postgresql_version, cassandra_version, test_description, test_result) SELECT current_timestamp, '$POSTGRESQL_CONTAINER', '$CASSANDRA_CONTAINER', 'Testing tinyint', id::text || '|' || some_text from cassandra.fdw_test_tinyint;"

    echo "Testing boolean"
    sudo docker exec -it $POSTGRESQL_CONTAINER psql --host=$POSTGRESQL_HOST --port=$POSTGRESQL_PORT --username=$POSTGRESQL_USER $POSTGRESQL_DB2 --command "INSERT INTO $POSTGRESQL_RESULT_SCHEMA.fdw_test_results(created, postgresql_version, cassandra_version, test_description, test_result) SELECT current_timestamp, '$POSTGRESQL_CONTAINER', '$CASSANDRA_CONTAINER', 'Testing boolean', id::text || '|' || some_text from cassandra.fdw_test_boolean;"

    echo "Testing float"
    sudo docker exec -it $POSTGRESQL_CONTAINER psql --host=$POSTGRESQL_HOST --port=$POSTGRESQL_PORT --username=$POSTGRESQL_USER $POSTGRESQL_DB2 --command "INSERT INTO $POSTGRESQL_RESULT_SCHEMA.fdw_test_results(created, postgresql_version, cassandra_version, test_description, test_result) SELECT current_timestamp, '$POSTGRESQL_CONTAINER', '$CASSANDRA_CONTAINER', 'Testing float', id::text || '|' || some_text from cassandra.fdw_test_float;"

    echo "Testing double precision"
    sudo docker exec -it $POSTGRESQL_CONTAINER psql --host=$POSTGRESQL_HOST --port=$POSTGRESQL_PORT --username=$POSTGRESQL_USER $POSTGRESQL_DB2 --command "INSERT INTO $POSTGRESQL_RESULT_SCHEMA.fdw_test_results(created, postgresql_version, cassandra_version, test_description, test_result) SELECT current_timestamp, '$POSTGRESQL_CONTAINER', '$CASSANDRA_CONTAINER', 'Testing double precision', id::text || '|' || some_text from cassandra.fdw_test_double;"

    echo "Testing uuid"
    sudo docker exec -it $POSTGRESQL_CONTAINER psql --host=$POSTGRESQL_HOST --port=$POSTGRESQL_PORT --username=$POSTGRESQL_USER $POSTGRESQL_DB2 --command "INSERT INTO $POSTGRESQL_RESULT_SCHEMA.fdw_test_results(created, postgresql_version, cassandra_version, test_description, test_result) SELECT current_timestamp, '$POSTGRESQL_CONTAINER', '$CASSANDRA_CONTAINER', 'Testing uuid', id::text || '|' || some_text from cassandra.fdw_test_uuid;"

    echo "Testing ascii"
    sudo docker exec -it $POSTGRESQL_CONTAINER psql --host=$POSTGRESQL_HOST --port=$POSTGRESQL_PORT --username=$POSTGRESQL_USER $POSTGRESQL_DB2 --command "INSERT INTO $POSTGRESQL_RESULT_SCHEMA.fdw_test_results(created, postgresql_version, cassandra_version, test_description, test_result) SELECT current_timestamp, '$POSTGRESQL_CONTAINER', '$CASSANDRA_CONTAINER', 'Testing ascii', id::text || '|' || some_text from cassandra.fdw_test_ascii;"

    echo "Testing varchar"
    sudo docker exec -it $POSTGRESQL_CONTAINER psql --host=$POSTGRESQL_HOST --port=$POSTGRESQL_PORT --username=$POSTGRESQL_USER $POSTGRESQL_DB2 --command "INSERT INTO $POSTGRESQL_RESULT_SCHEMA.fdw_test_results(created, postgresql_version, cassandra_version, test_description, test_result) SELECT current_timestamp, '$POSTGRESQL_CONTAINER', '$CASSANDRA_CONTAINER', 'Testing varchar', id::text || '|' || some_text from cassandra.fdw_test_varchar;"

    echo "Testing set"
    sudo docker exec -it $POSTGRESQL_CONTAINER psql --host=$POSTGRESQL_HOST --port=$POSTGRESQL_PORT --username=$POSTGRESQL_USER $POSTGRESQL_DB2 --command "INSERT INTO $POSTGRESQL_RESULT_SCHEMA.fdw_test_results(created, postgresql_version, cassandra_version, test_description, test_result) SELECT current_timestamp, '$POSTGRESQL_CONTAINER', '$CASSANDRA_CONTAINER', 'Testing set', id::text || '|' || some_text from cassandra.fdw_test_set;"

    echo "Testing timestamp"
    sudo docker exec -it $POSTGRESQL_CONTAINER psql --host=$POSTGRESQL_HOST --port=$POSTGRESQL_PORT --username=$POSTGRESQL_USER $POSTGRESQL_DB2 --command "INSERT INTO $POSTGRESQL_RESULT_SCHEMA.fdw_test_results(created, postgresql_version, cassandra_version, test_description, test_result) SELECT current_timestamp, '$POSTGRESQL_CONTAINER', '$CASSANDRA_CONTAINER', 'Testing timestamp', id::text || '|' || some_timestamp from cassandra.fdw_test_timestamp;"

    # Clear data and objects from testing
    sudo docker exec -it $POSTGRESQL_CONTAINER psql --host=$POSTGRESQL_HOST --port=$POSTGRESQL_PORT --username=$POSTGRESQL_USER $POSTGRESQL_DB1 --command "DROP DATABASE IF EXISTS $POSTGRESQL_DB2;"

done
