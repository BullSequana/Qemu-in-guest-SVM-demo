#! /usr/bin/env sh

HP_1G=1
HP_2M=10

HP_2M_SYSFS='/sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages'
HP_1G_SYSFS='/sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages'

HP_2M_PRE_ALLOCATED=0
HP_1G_PRE_ALLOCATED=0

update_alloc() {
    nr="$1"
    sysfs_file="$2"
    pre_alloc="$3"
    if [ "$nr" -lt "$pre_alloc" ]
    then
        return
    fi
    echo "$nr" > "$sysfs_file"
    if [ $(cat "$sysfs_file") != "$nr" ]
    then
        echo 'Error, cannot allocate huge pages' > /dev/stderr
        free_pages
        exit 1
    fi
}

free_pages() {
    echo "$HP_2M_PRE_ALLOCATED" > "$HP_2M_SYSFS"
    echo "$HP_1G_PRE_ALLOCATED" > "$HP_1G_SYSFS"
}

# $1 sysfs file
detect_pre_alloc() {
    cat "$1"
}

if [ \( ! -f "$HP_2M_SYSFS" \) -o \( ! -f "$HP_1G_SYSFS" \) ]
then
    echo 'Error, cannot find sysfs files to reserve huge pages' > /dev/stderr
    exit 1
fi

HP_2M_PRE_ALLOCATED="$(detect_pre_alloc $HP_2M_SYSFS)"
HP_1G_PRE_ALLOCATED="$(detect_pre_alloc $HP_1G_SYSFS)"

# Reserve a huge page
update_alloc "$HP_2M" "$HP_2M_SYSFS" "$HP_2M_PRE_ALLOCATED"
update_alloc "$HP_1G" "$HP_1G_SYSFS" "$HP_1G_PRE_ALLOCATED"

CUR_DIR=$(dirname "$0")
cd "$CUR_DIR"/user
./userapp
free_pages