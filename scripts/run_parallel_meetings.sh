#!/bin/sh

LOGS_DIR=qperf_logs

if [ -z "$1" ]; then
    echo "Using default number of meetings"
    MEETINGS=${1:-100}
elif [ "$1" -eq 0 ]; then
    echo "Num meetings must be greater than 0"
    exit 1
else
    MEETINGS=${1:-$1}
fi

if [ -z "$2" ]; then
    RELAY="moq://localhost:33435"
else
    RELAY="$2"
fi

if [ -z "$3" ]; then
    echo "Config file . required"
    exit 1
else
    CONFIG_PATH="$3"
fi

if [ -z "$4" ]; then
    echo "Using default number of clients"
    INSTANCES=5
elif [ "$4" -eq 0 ]; then
    echo "Num clients must be greater than 0"
    exit 1
else
    INSTANCES=${4:-$4}
fi

echo "Running $MEETINGS meetings with $INSTANCES clients each"

rm -rf $LOGS_DIR
mkdir -p $LOGS_DIR

parallel -j $((MEETINGS * INSTANCES)) "./moqbench --meeting --meeting_id {1} -i {2} -n $INSTANCES -c $CONFIG_PATH -r $RELAY > $LOGS_DIR/t_m{1}_c{2}_logs.txt 2>&1" ::: $(seq ${MEETINGS}) ::: $(seq ${INSTANCES})
