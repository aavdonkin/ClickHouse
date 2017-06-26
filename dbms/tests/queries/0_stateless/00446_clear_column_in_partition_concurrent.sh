#!/bin/bash
set -e

ch="clickhouse-client -q"

$ch "DROP TABLE IF EXISTS test.drop_column1"
$ch "DROP TABLE IF EXISTS test.drop_column2"
$ch "CREATE TABLE test.drop_column1 (d Date, i Int64, s String) ENGINE = ReplicatedMergeTree('/clickhouse/tables/test/drop_column', '1', d, d, 8192)"
$ch "CREATE TABLE test.drop_column2 (d Date, i Int64, s String) ENGINE = ReplicatedMergeTree('/clickhouse/tables/test/drop_column', '2', d, d, 8192)"

$ch "INSERT INTO test.drop_column1 VALUES ('2000-01-01', 1, 'a'), ('2000-02-01', 2, 'b')"
$ch "INSERT INTO test.drop_column1 VALUES ('2000-01-01', 3, 'c'), ('2000-02-01', 4, 'd')"

for i in `seq 3`; do
    $ch "INSERT INTO test.drop_column1 VALUES ('2000-03-01', 3, 'c'), ('2000-03-01', 4, 'd')" &
    $ch "ALTER TABLE test.drop_column1 CLEAR COLUMN i IN PARTITION '200001'" --replication_alter_partitions_sync=2 &
    $ch "ALTER TABLE test.drop_column1 CLEAR COLUMN s IN PARTITION '200001'" --replication_alter_partitions_sync=2 &
    $ch "ALTER TABLE test.drop_column1 CLEAR COLUMN i IN PARTITION '200002'" --replication_alter_partitions_sync=2 &
    $ch "ALTER TABLE test.drop_column1 CLEAR COLUMN s IN PARTITION '200002'" --replication_alter_partitions_sync=2 &
    $ch "INSERT INTO test.drop_column1 VALUES ('2000-03-01', 3, 'c'), ('2000-03-01', 4, 'd')" &
done
wait

$ch "SELECT DISTINCT * FROM test.drop_column2 WHERE d != toDate('2000-03-01') ORDER BY d, i, s"
$ch "SELECT DISTINCT * FROM test.drop_column2 WHERE d != toDate('2000-03-01') ORDER BY d, i, s"

$ch "DROP TABLE IF EXISTS test.drop_column1"
$ch "DROP TABLE IF EXISTS test.drop_column2"