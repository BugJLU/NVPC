sudo mount -t ext4 /dev/nvme0n1 /mnt/nvpcssd/ ; sudo mount -t spfs -o pmem=/dev/pmem0,format,consistency=meta /mnt/nvpcssd/ /mnt/nvpcssd/
sudo rm /mnt/nvpcssd/db_bench/*
ls /mnt/nvpcssd/db_bench
sudo rm /mnt/nvpcssd/db_bench/*
fillseq/8/16/4096/1048576/spfs/90/512/0
/home/shh/git_clone/rocksdb-main/db_bench  --benchmarks=fillseq --threads=1 --wal_dir=/mnt/nvpcssd/db_bench --db=/mnt/nvpcssd/db_bench --key_size=16 --value_size=4096 --sync --num=1048576 --compression_level=0 --compression_ratio=1.0 --target_file_size_base=536870912 --cache_size=0 -compressed_cache_size=0 
Set seed to 1712046516123328 because --seed was 0
Initializing RocksDB Options from the specified file
Initializing RocksDB Options from command-line flags
Integrated BlobDB: blob cache disabled
Keys:       16 bytes each (+ 0 bytes user-defined timestamp)
Values:     4096 bytes each (4096 bytes after compression)
Entries:    1048576
Prefix:    0 bytes
Keys per prefix:    0
RawSize:    4112.0 MB (estimated)
FileSize:   4112.0 MB (estimated)
Write rate: 0 bytes/second
Read rate: 0 ops/second
Compression: Snappy
Compression sampling rate: 0
Memtablerep: SkipListFactory
Perf Level: 1
WARNING: Assertions are enabled; benchmarks unnecessarily slow
------------------------------------------------
Initializing RocksDB Options from the specified file
Initializing RocksDB Options from command-line flags
Integrated BlobDB: blob cache disabled
DB path: [/mnt/nvpcssd/db_bench]
fillseq      :      15.581 micros/op 64178 ops/sec 16.338 seconds 1048576 operations;  251.7 MB/s
sudo rm /mnt/nvpcssd/db_bench/*
ls /mnt/nvpcssd/db_bench
000030.log
000032.log
MANIFEST-000005
sudo umount /mnt/nvpcssd/ ; sudo umount /mnt/nvpcssd/
sudo mount -t ext4 /dev/nvme0n1 /mnt/nvpcssd/ ; sudo mount -t spfs -o pmem=/dev/pmem0,format,consistency=meta /mnt/nvpcssd/ /mnt/nvpcssd/
sudo rm /mnt/nvpcssd/db_bench/*
ls /mnt/nvpcssd/db_bench
sudo rm /mnt/nvpcssd/db_bench/*
fillseq/8/16/4096/1048576/spfs/90/512/1
/home/shh/git_clone/rocksdb-main/db_bench  --benchmarks=fillseq --threads=1 --wal_dir=/mnt/nvpcssd/db_bench --db=/mnt/nvpcssd/db_bench --key_size=16 --value_size=4096 --sync --num=1048576 --compression_level=0 --compression_ratio=1.0 --target_file_size_base=536870912 --cache_size=0 -compressed_cache_size=0 
Set seed to 1712046556800294 because --seed was 0
Initializing RocksDB Options from the specified file
Initializing RocksDB Options from command-line flags
Integrated BlobDB: blob cache disabled
Keys:       16 bytes each (+ 0 bytes user-defined timestamp)
Values:     4096 bytes each (4096 bytes after compression)
Entries:    1048576
Prefix:    0 bytes
Keys per prefix:    0
RawSize:    4112.0 MB (estimated)
FileSize:   4112.0 MB (estimated)
Write rate: 0 bytes/second
Read rate: 0 ops/second
Compression: Snappy
Compression sampling rate: 0
Memtablerep: SkipListFactory
Perf Level: 1
WARNING: Assertions are enabled; benchmarks unnecessarily slow
------------------------------------------------
Initializing RocksDB Options from the specified file
Initializing RocksDB Options from command-line flags
Integrated BlobDB: blob cache disabled
DB path: [/mnt/nvpcssd/db_bench]
fillseq      :      15.751 micros/op 63485 ops/sec 16.517 seconds 1048576 operations;  249.0 MB/s
sudo rm /mnt/nvpcssd/db_bench/*
ls /mnt/nvpcssd/db_bench
000030.log
000032.log
MANIFEST-000005
sudo umount /mnt/nvpcssd/ ; sudo umount /mnt/nvpcssd/
sudo mount -t ext4 /dev/nvme0n1 /mnt/nvpcssd/ ; sudo mount -t spfs -o pmem=/dev/pmem0,format,consistency=meta /mnt/nvpcssd/ /mnt/nvpcssd/
sudo rm /mnt/nvpcssd/db_bench/*
ls /mnt/nvpcssd/db_bench
sudo rm /mnt/nvpcssd/db_bench/*
fillseq/8/16/4096/1048576/spfs/90/512/2
/home/shh/git_clone/rocksdb-main/db_bench  --benchmarks=fillseq --threads=1 --wal_dir=/mnt/nvpcssd/db_bench --db=/mnt/nvpcssd/db_bench --key_size=16 --value_size=4096 --sync --num=1048576 --compression_level=0 --compression_ratio=1.0 --target_file_size_base=536870912 --cache_size=0 -compressed_cache_size=0 
Set seed to 1712046597597260 because --seed was 0
Initializing RocksDB Options from the specified file
Initializing RocksDB Options from command-line flags
Integrated BlobDB: blob cache disabled
Keys:       16 bytes each (+ 0 bytes user-defined timestamp)
Values:     4096 bytes each (4096 bytes after compression)
Entries:    1048576
Prefix:    0 bytes
Keys per prefix:    0
RawSize:    4112.0 MB (estimated)
FileSize:   4112.0 MB (estimated)
Write rate: 0 bytes/second
Read rate: 0 ops/second
Compression: Snappy
Compression sampling rate: 0
Memtablerep: SkipListFactory
Perf Level: 1
WARNING: Assertions are enabled; benchmarks unnecessarily slow
------------------------------------------------
Initializing RocksDB Options from the specified file
Initializing RocksDB Options from command-line flags
Integrated BlobDB: blob cache disabled
DB path: [/mnt/nvpcssd/db_bench]
fillseq      :      15.647 micros/op 63909 ops/sec 16.407 seconds 1048576 operations;  250.6 MB/s
sudo rm /mnt/nvpcssd/db_bench/*
ls /mnt/nvpcssd/db_bench
000030.log
000032.log
MANIFEST-000005
sudo umount /mnt/nvpcssd/ ; sudo umount /mnt/nvpcssd/
