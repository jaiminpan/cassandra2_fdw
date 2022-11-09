#!/bin/bash

# Initialize all Docker images
./setup_img.sh

# Run the Cassandra part, will also run the PostgreSQL parts for each Cassandra container
./cassandra.sh

# Clean up everything
#./cleanup.sh
