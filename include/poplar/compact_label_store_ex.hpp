#ifndef POPLAR_TRIE_COMPACT_LABEL_STORE_EX_HPP
#define POPLAR_TRIE_COMPACT_LABEL_STORE_EX_HPP

#include <iostream>
#include <memory>
#include <vector>

#include "bit_chunk.hpp"
#include "vbyte.hpp"

namespace poplar {

template <typename Value, uint64_t ChunkSize = 16>
class compact_label_store_ex {
 public:
  using this_type = compact_label_store_ex<Value, ChunkSize>;
  using value_type = Value;
  using chunk_type = bit_chunk<ChunkSize>;

  static constexpr bool ex = true;

 public:
  compact_label_store_ex() = default;

  explicit compact_label_store_ex(uint32_t capa_bits)
      : ptrs_((1ULL << capa_bits) / ChunkSize), chunks_(ptrs_.size()) {}

  ~compact_label_store_ex() = default;

  std::pair<const value_type*, uint64_t> compare(uint64_t pos, char_range key) const {
    auto [chunk_id, pos_in_chunk] = decompose_value<ChunkSize>(pos);

    assert(ptrs_[chunk_id]);
    assert(chunks_[chunk_id].get(pos_in_chunk));

    const uint8_t* ptr = ptrs_[chunk_id].get();
    const uint64_t offset = chunks_[chunk_id].popcnt(pos_in_chunk);

    uint64_t alloc = 0;
    for (uint64_t i = 0; i < offset; ++i) {
      ptr += vbyte::decode(ptr, alloc);
      ptr += alloc;
    }
    ptr += vbyte::decode(ptr, alloc);

    if (key.empty()) {
      return {reinterpret_cast<const value_type*>(ptr), 0};
    }

    uint64_t length = alloc - sizeof(value_type);
    for (uint64_t i = 0; i < length; ++i) {
      if (key[i] != ptr[i]) {
        return {nullptr, i};
      }
    }

    if (key[length] != '\0') {
      return {nullptr, length};
    }

    // +1 considers the terminator '\0'
    return {reinterpret_cast<const value_type*>(ptr + length), length + 1};
  };

  value_type* associate(uint64_t pos, char_range key) {
    auto [chunk_id, pos_in_chunk] = decompose_value<ChunkSize>(pos);

    assert(!chunks_[chunk_id].get(pos_in_chunk));
    chunks_[chunk_id].set(pos_in_chunk);

    ++size_;

#ifdef POPLAR_ENABLE_EX_STATS
    max_length_ = std::max<uint64_t>(max_length_, key.length());
    sum_length_ += key.length();
#endif

    if (!ptrs_[chunk_id]) {
      // First association in the group
      uint64_t length = key.empty() ? 0 : key.length() - 1;
      uint64_t new_alloc = vbyte::size(length + sizeof(value_type)) + length + sizeof(value_type);

      ptrs_[chunk_id] = std::make_unique<uint8_t[]>(new_alloc);
      uint8_t* ptr = ptrs_[chunk_id].get();

      ptr += vbyte::encode(ptr, length + sizeof(value_type));
      copy_bytes(ptr, key.begin, length);

      auto ret_ptr = reinterpret_cast<value_type*>(ptr + length);
      *ret_ptr = static_cast<value_type>(0);

      return ret_ptr;
    }

    // Second and subsequent association in the group
    auto fr_alloc = get_allocs_(chunk_id, pos_in_chunk);

    const uint64_t len = key.empty() ? 0 : key.length() - 1;
    const uint64_t new_alloc = vbyte::size(len + sizeof(value_type)) + len + sizeof(value_type);

    auto new_unique = std::make_unique<uint8_t[]>(fr_alloc.first + new_alloc + fr_alloc.second);

    // Get raw pointers
    const uint8_t* orig_ptr = ptrs_[chunk_id].get();
    uint8_t* new_ptr = new_unique.get();

    // Copy the front allocation
    copy_bytes(new_ptr, orig_ptr, fr_alloc.first);
    orig_ptr += fr_alloc.first;
    new_ptr += fr_alloc.first;

    // Set new allocation
    new_ptr += vbyte::encode(new_ptr, len + sizeof(value_type));
    copy_bytes(new_ptr, key.begin, len);
    new_ptr += len;
    *reinterpret_cast<value_type*>(new_ptr) = static_cast<value_type>(0);

    // Copy the back allocation
    copy_bytes(new_ptr + sizeof(value_type), orig_ptr, fr_alloc.second);

    // Overwrite
    ptrs_[chunk_id] = std::move(new_unique);

    return reinterpret_cast<value_type*>(new_ptr);
  }

  template <typename T>
  void expand(const T& pos_map) {
    this_type new_ls(bit_tools::get_num_bits(capa_size()));

    for (uint64_t pos = 0; pos < pos_map.size(); ++pos) {
      auto [chunk_id, pos_in_chunk] = decompose_value<ChunkSize>(pos);
      uint64_t new_pos = pos_map[pos];
      if (new_pos != UINT64_MAX) {
        auto orig_slice = get_slice_(chunk_id, pos_in_chunk);
        if (!orig_slice.empty()) {
          auto [new_chunk_id, new_pos_in_chunk] = decompose_value<ChunkSize>(new_pos);
          new_ls.set_slice_(new_chunk_id, new_pos_in_chunk, orig_slice);
        }
      }
      if (pos_in_chunk == ChunkSize - 1) {
        ptrs_[chunk_id].reset();
      }
    }

    new_ls.size_ = size_;
#ifdef POPLAR_ENABLE_EX_STATS
    new_ls.max_length_ = max_length_;
    new_ls.sum_length_ = sum_length_;
#endif

    *this = std::move(new_ls);
  }

  uint64_t size() const {
    return size_;
  }
  uint64_t capa_size() const {
    return ptrs_.size() * ChunkSize;
  }

  boost::property_tree::ptree make_ptree() const {
    boost::property_tree::ptree pt;
    pt.put("name", "compact_label_store_ex");
    pt.put("chunk_size", ChunkSize);
    pt.put("size", size());
    pt.put("capa_size", capa_size());
#ifdef POPLAR_ENABLE_EX_STATS
    pt.put("max_length", max_length_);
    pt.put("ave_length", double(sum_length_) / size());
#endif
    return pt;
  }

  compact_label_store_ex(const compact_label_store_ex&) = delete;
  compact_label_store_ex& operator=(const compact_label_store_ex&) = delete;

  compact_label_store_ex(compact_label_store_ex&&) noexcept = default;
  compact_label_store_ex& operator=(compact_label_store_ex&&) noexcept = default;

 private:
  std::vector<std::unique_ptr<uint8_t[]>> ptrs_;
  std::vector<chunk_type> chunks_;
  uint64_t size_ = 0;
#ifdef POPLAR_ENABLE_EX_STATS
  uint64_t max_length_ = 0;
  uint64_t sum_length_ = 0;
#endif

  std::pair<uint64_t, uint64_t> get_allocs_(uint64_t chunk_id, uint64_t pos_in_chunk) {
    assert(chunks_[chunk_id].get(pos_in_chunk));

    const uint8_t* ptr = ptrs_[chunk_id].get();
    assert(ptr != nullptr);

    // -1 means the difference of the above bit_tools::set_bit
    const uint64_t num = chunks_[chunk_id].popcnt() - 1;
    const uint64_t offset = chunks_[chunk_id].popcnt(pos_in_chunk);

    uint64_t front_alloc = 0, back_alloc = 0;

    for (uint64_t i = 0; i < num; ++i) {
      uint64_t len = 0;
      len += vbyte::decode(ptr, len);
      if (i < offset) {
        front_alloc += len;
      } else {
        back_alloc += len;
      }
      ptr += len;
    }

    return {front_alloc, back_alloc};
  }

  char_range get_slice_(uint64_t chunk_id, uint64_t pos_in_chunk) const {
    if (!chunks_[chunk_id].get(pos_in_chunk)) {
      // pos indicates a step node
      return {nullptr, nullptr};
    }

    const uint8_t* ptr = ptrs_[chunk_id].get();
    assert(ptr != nullptr);

    const uint64_t offset = chunks_[chunk_id].popcnt(pos_in_chunk);

    // Proceeds the target position
    uint64_t len = 0;
    for (uint64_t i = 0; i < offset; ++i) {
      ptr += vbyte::decode(ptr, len);
      ptr += len;
    }

    uint64_t vsize = vbyte::decode(ptr, len);
    return {ptr, ptr + (vsize + len)};
  }

  void set_slice_(uint64_t chunk_id, uint64_t pos_in_chunk, char_range new_slice) {
    assert(!chunks_[chunk_id].get(pos_in_chunk));

    chunks_[chunk_id].set(pos_in_chunk);
    const uint8_t* ptr = ptrs_[chunk_id].get();

    if (ptr == nullptr) {
      // First association in the group
      ptrs_[chunk_id] = std::make_unique<uint8_t[]>(new_slice.length());
      copy_bytes(ptrs_[chunk_id].get(), new_slice.begin, new_slice.length());
      return;
    }

    // Second and subsequent association in the group
    auto fr_alloc = get_allocs_(chunk_id, pos_in_chunk);
    auto new_unique = std::make_unique<uint8_t[]>(fr_alloc.first + new_slice.length() + fr_alloc.second);

    uint8_t* new_ptr = new_unique.get();

    // Copy the front allocation
    copy_bytes(new_ptr, ptr, fr_alloc.first);
    ptr += fr_alloc.first;
    new_ptr += fr_alloc.first;

    // Set new label
    copy_bytes(new_ptr, new_slice.begin, new_slice.length());
    new_ptr += new_slice.length();

    // Copy back
    copy_bytes(new_ptr, ptr, fr_alloc.second);
    ptrs_[chunk_id] = std::move(new_unique);
  }
};

}  // namespace poplar

#endif  // POPLAR_TRIE_COMPACT_LABEL_STORE_EX_HPP