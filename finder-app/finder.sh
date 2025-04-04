#!/bin/sh
#Finder script
#Dennis Lee

# 2 runtime arguments:
filestr=$1
searchstr=$2

# if any of the parameters above were not specified
if [ $# -lt 2 ]
then
    echo "Error: Required arguments didn't provide correctly."
    echo "- argument 1 = directory to search\n- argument 2 = string to search."
    echo "Exit."
    exit 1
fi

if [ -d "$1" ]
then
    X=$(grep -l "$searchstr" $(find "$filestr" -type f) | wc -l)
    Y=$(grep "$searchstr" $(find "$filestr" -type f) | wc -l)
    echo "The number of files are $X and the number of matching lines are $Y"
else
    # filesdir does not represent a directory on the filesystem
    echo "Error: Invalid directory: $1."
    echo "Exit."
    exit 1
fi