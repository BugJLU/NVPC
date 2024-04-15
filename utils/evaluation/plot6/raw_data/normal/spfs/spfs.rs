sudo mount -t ext4 /dev/nvme0n1 /mnt/nvpcssd/ ; sudo mount -t spfs -o pmem=/dev/pmem0,format,consistency=meta /mnt/nvpcssd/ /mnt/nvpcssd/
sudo rm /mnt/nvpcssd/db_bench/*
/home/shh/git_clone/rocksdb-main/db_bench  --benchmarks=fillseq --threads=1 --wal_dir=/mnt/nvpcssd/db_bench --db=/mnt/nvpcssd/db_bench --key_size=16 --value_size=4096 --sync --num=1048576 --compression_level=0 --compression_ratio=1.0 --target_file_size_base=536870912 --cache_size=0 -compressed_cache_size=0 
ls /mnt/nvpcssd/db_bench
000009.sst
000011.sst
000013.sst
000015.sst
000017.sst
000019.sst
000021.sst
000023.sst
000025.sst
000027.sst
000029.sst
000030.log
000031.sst
000032.log
000033.sst
000035.sst
000037.sst
000039.sst
000041.sst
000043.sst
000045.sst
000047.sst
000049.sst
000051.sst
000053.sst
000055.sst
000057.sst
000059.sst
000061.sst
000063.sst
000065.sst
000067.sst
000069.sst
000071.sst
000073.sst
000075.sst
000077.sst
000079.sst
000081.sst
000083.sst
000085.sst
000087.sst
000089.sst
000091.sst
000093.sst
000095.sst
000097.sst
000099.sst
000101.sst
000103.sst
000105.sst
000107.sst
000109.sst
000111.sst
000113.sst
000115.sst
000117.sst
000119.sst
000121.sst
000123.sst
000125.sst
000127.sst
000129.sst
000131.sst
000133.sst
000134.log
000135.sst
000136.log
CURRENT
IDENTITY
LOCK
LOG
MANIFEST-000005
MANIFEST-000005
MANIFEST-000138
OPTIONS-000007
readseq/8/16/4096/1048576/spfs/90/512/0
/home/shh/git_clone/rocksdb-main/db_bench  --benchmarks=readseq --threads=1 --wal_dir=/mnt/nvpcssd/db_bench --db=/mnt/nvpcssd/db_bench --key_size=16 --value_size=4096 --sync --num=1048576 --compression_level=0 --compression_ratio=1.0 --target_file_size_base=536870912 --use_existing_db=true --cache_size=0 -compressed_cache_size=0 
Set seed to 1712046704049906 because --seed was 0
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
DB path: [/mnt/nvpcssd/db_bench]
readseq      :       3.588 micros/op 278704 ops/sec 3.762 seconds 1048576 operations; 1092.9 MB/s
sudo rm /mnt/nvpcssd/db_bench/*
ls /mnt/nvpcssd/db_bench
000030.log
000032.log
MANIFEST-000005
sudo umount /mnt/nvpcssd/ ; sudo umount /mnt/nvpcssd/
sudo mount -t ext4 /dev/nvme0n1 /mnt/nvpcssd/ ; sudo mount -t spfs -o pmem=/dev/pmem0,format,consistency=meta /mnt/nvpcssd/ /mnt/nvpcssd/
sudo rm /mnt/nvpcssd/db_bench/*
/home/shh/git_clone/rocksdb-main/db_bench  --benchmarks=fillseq --threads=1 --wal_dir=/mnt/nvpcssd/db_bench --db=/mnt/nvpcssd/db_bench --key_size=16 --value_size=4096 --sync --num=1048576 --compression_level=0 --compression_ratio=1.0 --target_file_size_base=536870912 --cache_size=0 -compressed_cache_size=0 
ls /mnt/nvpcssd/db_bench
000009.sst
000011.sst
000013.sst
000015.sst
000017.sst
000019.sst
000021.sst
000023.sst
000025.sst
000027.sst
000029.sst
000030.log
000031.sst
000032.log
000033.sst
000035.sst
000037.sst
000039.sst
000041.sst
000043.sst
000045.sst
000047.sst
000049.sst
000051.sst
000053.sst
000055.sst
000057.sst
000059.sst
000061.sst
000063.sst
000065.sst
000067.sst
000069.sst
000071.sst
000073.sst
000075.sst
000077.sst
000079.sst
000081.sst
000083.sst
000085.sst
000087.sst
000089.sst
000091.sst
000093.sst
000095.sst
000097.sst
000099.sst
000101.sst
000103.sst
000105.sst
000107.sst
000109.sst
000111.sst
000113.sst
000115.sst
000117.sst
000119.sst
000121.sst
000123.sst
000125.sst
000127.sst
000129.sst
000131.sst
000133.sst
000134.log
000135.sst
000136.log
CURRENT
IDENTITY
LOCK
LOG
MANIFEST-000005
MANIFEST-000005
MANIFEST-000138
OPTIONS-000007
readseq/8/16/4096/1048576/spfs/90/512/1
/home/shh/git_clone/rocksdb-main/db_bench  --benchmarks=readseq --threads=1 --wal_dir=/mnt/nvpcssd/db_bench --db=/mnt/nvpcssd/db_bench --key_size=16 --value_size=4096 --sync --num=1048576 --compression_level=0 --compression_ratio=1.0 --target_file_size_base=536870912 --use_existing_db=true --cache_size=0 -compressed_cache_size=0 
Set seed to 1712046750813879 because --seed was 0
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
DB path: [/mnt/nvpcssd/db_bench]
readseq      :       3.598 micros/op 277915 ops/sec 3.773 seconds 1048576 operations; 1089.8 MB/s
sudo rm /mnt/nvpcssd/db_bench/*
ls /mnt/nvpcssd/db_bench
000030.log
000032.log
MANIFEST-000005
sudo umount /mnt/nvpcssd/ ; sudo umount /mnt/nvpcssd/
sudo mount -t ext4 /dev/nvme0n1 /mnt/nvpcssd/ ; sudo mount -t spfs -o pmem=/dev/pmem0,format,consistency=meta /mnt/nvpcssd/ /mnt/nvpcssd/
sudo rm /mnt/nvpcssd/db_bench/*
/home/shh/git_clone/rocksdb-main/db_bench  --benchmarks=fillseq --threads=1 --wal_dir=/mnt/nvpcssd/db_bench --db=/mnt/nvpcssd/db_bench --key_size=16 --value_size=4096 --sync --num=1048576 --compression_level=0 --compression_ratio=1.0 --target_file_size_base=536870912 --cache_size=0 -compressed_cache_size=0 
ls /mnt/nvpcssd/db_bench
000009.sst
000011.sst
000013.sst
000015.sst
000017.sst
000019.sst
000021.sst
000023.sst
000025.sst
000027.sst
000029.sst
000030.log
000031.sst
000032.log
000033.sst
000035.sst
000037.sst
000039.sst
000041.sst
000043.sst
000045.sst
000047.sst
000049.sst
000051.sst
000053.sst
000055.sst
000057.sst
000059.sst
000061.sst
000063.sst
000065.sst
000067.sst
000069.sst
000071.sst
000073.sst
000075.sst
000077.sst
000079.sst
000081.sst
000083.sst
000085.sst
000087.sst
000089.sst
000091.sst
000093.sst
000095.sst
000097.sst
000099.sst
000101.sst
000103.sst
000105.sst
000107.sst
000109.sst
000111.sst
000113.sst
000115.sst
000117.sst
000119.sst
000121.sst
000123.sst
000125.sst
000127.sst
000129.sst
000131.sst
000133.sst
000134.log
000135.sst
000136.log
CURRENT
IDENTITY
LOCK
LOG
MANIFEST-000005
MANIFEST-000005
MANIFEST-000138
OPTIONS-000007
readseq/8/16/4096/1048576/spfs/90/512/2
/home/shh/git_clone/rocksdb-main/db_bench  --benchmarks=readseq --threads=1 --wal_dir=/mnt/nvpcssd/db_bench --db=/mnt/nvpcssd/db_bench --key_size=16 --value_size=4096 --sync --num=1048576 --compression_level=0 --compression_ratio=1.0 --target_file_size_base=536870912 --use_existing_db=true --cache_size=0 -compressed_cache_size=0 
Set seed to 1712046797833818 because --seed was 0
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
DB path: [/mnt/nvpcssd/db_bench]
readseq      :       3.527 micros/op 283486 ops/sec 3.699 seconds 1048576 operations; 1111.7 MB/s
sudo rm /mnt/nvpcssd/db_bench/*
ls /mnt/nvpcssd/db_bench
000030.log
000032.log
MANIFEST-000005
sudo umount /mnt/nvpcssd/ ; sudo umount /mnt/nvpcssd/
