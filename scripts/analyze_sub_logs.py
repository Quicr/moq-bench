#!/usr/bin/env python3
# -----------------------------------------------------------------------------------------
# Simple program to analyze the qperf subscriber result log files.
#
# Requirements:
#       pip3 install click
# -----------------------------------------------------------------------------------------

import logging
import click
import time
import traceback
import os

logging.basicConfig(format='%(asctime)s | %(levelname)-8s | %(name)s[%(lineno)s] %(threadName)s | %(message)s',
                    level=logging.INFO)
LOG = logging.getLogger("analyze_sub_logs")

PATH = "./"


def process_sub_logs_path(path):
    directory = os.fsencode(path)

    num_delayed = 0
    num_lost_objects = 0
    num_with_issues = 0

    # Per-file set of types seen in 'OR COMPLETE' lines.
    file_types = {}

    for file in os.listdir(directory):
        filename = os.fsdecode(file)
        if not (filename.startswith("t_") and filename.endswith("logs.txt")):
            continue

        filepath = os.path.join(path, filename)
        LOG.debug(f"Processing file: {filepath}")

        types_in_file = set()
        with open(filepath, "r") as f:
            for line in f.readlines():
                if "OR COMPLETE, " not in line:
                    continue
                csv = line.split("OR COMPLETE, ", maxsplit=1)[1].split(", ")

                if len(csv) < 19:
                    LOG.info(f"Skipping line csv length: {len(csv)}, expected 19")
                    continue

                track_id = csv[0]
                test_name = csv[1].strip()
                delta_objects = int(csv[17])
                over_multiplier = int(csv[18].strip())

                types_in_file.add(test_name)

                if over_multiplier > 1 or delta_objects != 0:
                    num_with_issues += 1
                    if over_multiplier > 1:
                        num_delayed += 1
                    if delta_objects != 0:
                        num_lost_objects += 1
                    LOG.info(f"id: {track_id} track name: '{test_name}' delta_objects: {delta_objects}  over multiplier: {over_multiplier}")
                else:
                    LOG.debug(f"id: {track_id} track name: '{test_name}' delta_objects: {delta_objects}  over multiplier: {over_multiplier}")

        file_types[filename] = types_in_file
    # End of for loop through all files

    num_files = len(file_types)
    if num_files == 0:
        LOG.warning(f"ANALYSIS: No log files matching 't_*logs.txt' found in {path}")
        return

    # Expected types = union of types seen in any file.
    expected_types = set().union(*file_types.values())

    # Special case: no file has any 'OR COMPLETE' at all.
    if not expected_types:
        LOG.warning(f"ANALYSIS: All {num_files} log file(s) are incomplete (no 'OR COMPLETE' lines found)")
        return

    # Per-file per-type missing report.
    fully_incomplete = []
    missing_by_type = {t: [] for t in expected_types}
    for filename, types in file_types.items():
        missing = expected_types - types
        if not types:
            fully_incomplete.append(filename)
            LOG.warning(f"INCOMPLETE: '{filename}' has no 'OR COMPLETE' lines (missing all types: {sorted(expected_types)})")
        elif missing:
            LOG.warning(f"INCOMPLETE: '{filename}' is missing 'OR COMPLETE' for type(s): {sorted(missing)}")
        for t in missing:
            missing_by_type[t].append(filename)

    total_missing_any = sum(1 for _, types in file_types.items() if expected_types - types)

    LOG.info(f"ANALYSIS: processed {num_files} log file(s); expected types: {sorted(expected_types)}")

    any_issue = False
    if total_missing_any:
        any_issue = True
        LOG.warning(f"ANALYSIS: {total_missing_any} file(s) missing 'OR COMPLETE' for one or more types")
        for t in sorted(expected_types):
            missing_files = missing_by_type[t]
            if missing_files:
                LOG.warning(f"ANALYSIS:   type '{t}': {len(missing_files)} of {num_files} file(s) missing")
        if fully_incomplete:
            LOG.warning(f"ANALYSIS: {len(fully_incomplete)} file(s) had zero 'OR COMPLETE' lines")

    if num_with_issues:
        any_issue = True
        LOG.warning(
            f"ANALYSIS: {num_with_issues} completed track(s) had issues: "
            f"delayed={num_delayed}, lost_objects={num_lost_objects}"
        )

    if not any_issue:
        LOG.info("ANALYSIS: No issues found")

@click.command(context_settings=dict(help_option_names=['-h', '--help'], max_content_width=200))
@click.option('-p', '--path', 'path',
              help="Directory/path of where the qperf log files are located", metavar='<path>', default=PATH)
@click.option('-d', '--debug', 'debug',
              help="Enable debug logging",
              is_flag=True, default=False)
def main(path, debug):
    if debug:
        LOG.setLevel(logging.DEBUG)

    LOG.info(f"Reading all qperf subscriber log files in directory {path}")

    process_sub_logs_path(path)

if __name__ == '__main__':
    main()
