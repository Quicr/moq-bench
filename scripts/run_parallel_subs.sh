#!/bin/sh

LOGS_DIR=qperf_logs

if [ -z "$1" ]; then
    echo "Using default number of subscriber clients"
    NUM_SUBS=${1:-100}
elif [ "$1" -eq 0 ]; then
    echo "Num subscribers must be greater than 0"
    exit 1
else
    NUM_SUBS=${1:-$1} # Arg 1
fi

if [ -z "$2" ]; then
    RELAY="moq://localhost:33435"
else
    RELAY="$2"
fi

if [ -z "$3" ]; then
    echo "Config file is required"
    exit 1
else
    CONFIG_PATH="$3"
fi

echo "Running $NUM_SUBS subscriber clients"

mkdir -p $LOGS_DIR
parallel -j ${NUM_SUBS}  "./moqbench_sub -i {} -c $CONFIG_PATH --connect_uri $RELAY > $LOGS_DIR/t_{}logs.txt 2>&1" ::: $(seq ${NUM_SUBS})
