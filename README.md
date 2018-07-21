# Poplar-trie

Poplar-trie is a C++17 library of associative arrays with string keys based on a dynamic path-decomposed trie (DynPDT) described in the paper [*Practical implementation of space-efficient dynamic keyword dictionaries*](https://link.springer.com/chapter/10.1007%2F978-3-319-67428-5_19), published in SPIRE 2017 [[paper](https://sites.google.com/site/shnskknd/SPIRE2017.pdf)] [[slide](https://www.slideshare.net/ShunsukeKanda1/practical-implementation-of-spaceefficient-dynamic-keyword-dictionaries)].
However, the implementation of this library is enhanced from the conference version.

The technical details are now being written.

## Implementation overview

Poplar-trie implements an associative array giving a mapping from key strings to values of any type and supporting dynamic update like `std::map<std::string,V>`.
The underlying data structure is the DynPDT.

A property of the DynPDT is that the edge labels are drawn from an integer set larger than that of normal tries represented in one byte, so it is important that searching a child can be performed in constant time.
Poplar-trie solves the task using hash-based trie implementations of the following classes:

- `poplar::plain_hash_trie` and `poplar::plain_hash_trie_r` are trie representations using plain hash tables.
- `poplar::compact_hash_trie` and `poplar::compact_hash_trie_r` are trie representations using compact hash tables.

The difference between `trie` and `trie_r` is whether the node IDs are defined in insertion order or not.

Actually, 

Another property is that the trie has string labels for each node, so their pointers have to be stored.
This library includes the following two methods:

- `plain_label_store` simply stores all pointers to string labels.
- `compact_label_store` relaxes the pointer overhead of `plain_label_store` by grouping pointers.

Class `poplar::map` implements the associative array and takes `*_hash_trie` and `*_label_store` as the template arguments.
That is, there are implementations of six classes.
But, you can easily get the implementations since `poplar.hpp` provides the following aliases:

- `MapPP` = `Map` + `PlainHashTrie` + `PlainLabelStore` (fastest)
- `MapPE` = `Map` + `PlainHashTrie` + `EmbeddedLabelStore`
- `MapPG` = `Map` + `PlainHashTrie` + `GroupedLabelStore`
- `MapCP` = `Map` + `CompactHashTrie` + `PlainLabelStore`
- `MapCE` = `Map` + `CompactHashTrie` + `EmbeddedLabelStore`
- `MapCG` = `Map` + `CompactHashTrie` + `GroupedLabelStore` (smallest)

These have template argument `Lambda` in common.
This is a parameter depending on lengths of given strings.
From previous experimental results, the value 16 (default) would be good for natural language words.
For long strings such as URLs, the value 32 or 64 would be good.


## Build instructions

You can download and compile Poplar-trie as the following commands.

```
$ git clone https://github.com/kampersanda/poplar-trie.git
$ cd poplar-trie
$ mkdir build
$ cd build
$ cmake ..
$ make
$ make install
```

The library uses C++17, so please install g++ 7.0 (or greater) or clang 4.0 (or greater).
In addition, CMake 3.8 (or greater) has to be installed to compile the library.

On the default setting, the library tries to use `SSE4.2` for popcount operations.
If you do not want to use it, please set `DISABLE_SSE4_2` at build time, e.g., `cmake .. -DDISABLE_SSE4_2=1`.

## Easy example

The following code is an easy example of inserting and searching key-value pairs.

```c++
#include <iostream>
#include <poplar.hpp>

int main() {
  std::vector<std::string> keys = {"Aoba", "Yun",    "Hajime", "Hihumi", "Kou",
                                   "Rin",  "Hazuki", "Umiko",  "Nene"};

  poplar::map_pp<int> map;

  try {
    for (int i = 0; i < keys.size(); ++i) {
      int* ptr = map.update(poplar::make_char_range(keys[i]));
      *ptr = i + 1;
    }
    for (int i = 0; i < keys.size(); ++i) {
      const int* ptr = map.find(poplar::make_char_range(keys[i]));
      if (ptr == nullptr or *ptr != i + 1) {
        return 1;
      }
      std::cout << keys[i] << ": " << *ptr << std::endl;
    }
    {
      const int* ptr = map.find(poplar::make_char_range("Hotaru"));
      if (ptr != nullptr) {
        return 1;
      }
      std::cout << "Hotaru: " << -1 << std::endl;
    }
  } catch (const poplar::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  std::cout << "#keys = " << map.size() << std::endl;

  return 0;
}
```

The output will be

```
Aoba: 1
Yun: 2
Hajime: 3
Hihumi: 4
Kou: 5
Rin: 6
Hazuki: 7
Umiko: 8
Nene: 9
Hotaru: -1
#keys = 9
```

## Benchmarks (of previous version)

The main advantage of Poplar-trie is high space efficiency as can be seen in the following results.

The experiments were carried out on Intel Xeon E5 @3.5 GHz CPU, with 32 GB of RAM, running Mac OS X 10.12.
The codes were compiled using Apple LLVM version 8 (clang-8) with optimization -O3.
The dictionaries were constructed by inserting all page titles from Japanese Wikipedia (32.3 MiB) in random order.
The value type is `int`.
The maximum resident set size during construction was measured using the `/usr/bin/time` command.
The insertion time was also measured using `std::chrono::duration_cast`.
And, search time for the same strings was measured.

| Implementation | Space (MiB) | Insertion (micros / key) | Search (micros / key) |
|----------------|------------:|----------------------------:|-------------------------:|
| `MapPP` | 80.4 | **0.68** | 0.48 |
| `MapPE` | 75.6 | 0.91 | 0.57 |
| `MapPG` | 47.2 | 1.71 | 0.80 |
| `MapCP` | 65.5 | 0.81 | 0.54 |
| `MapCE` | 61.6 | 1.00 | 0.61 |
| `MapCG` | **42.3** | 1.62 | 0.85 |
| [JudySL](http://judy.sourceforge.net) | 72.7 | 0.73 | 0.49 |
| [hat-trie](https://github.com/dcjones/hat-trie) | 74.5 | 0.97 | **0.25** |
| [cedarpp](http://www.tkl.iis.u-tokyo.ac.jp/~ynaga/cedar/) | 94.7 | 0.69 | 0.42 |

## Todo

- Support the deletion operation
- Add comments to the codes
- Create the API document

## Related work

- [compact\_sparse\_hash](https://github.com/tudocomp/compact_sparse_hash) is an efficient implementation of a compact associative array with integer keys.
- [mBonsai](https://github.com/Poyias/mBonsai) is the original implementation of succinct dynamic tries.
- [tudocomp](https://github.com/tudocomp/tudocomp) includes many dynamic trie implementations for LZ factorization.
 
## Special thanks

Thanks to [Dr. Dominik Köppl](https://github.com/koeppl) I was able to create the bijective hash function in `bijective_hash.hpp`.

